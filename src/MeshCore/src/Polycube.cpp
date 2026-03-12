#include "MeshCore/Polycube.h"

#include "MeshMath.h"
#include "PolycubeInternal.h"
#include "tetgen.h"

#include <Eigen/Dense>
#include <Eigen/Eigenvalues>
#include <Eigen/Sparse>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PolycubeInternal {

FaceGeometry ComputeFaceGeometry(const std::array<Eigen::Vector3d, 3>& points) {
    FaceGeometry geometry;
    geometry.UnnormalizedNormal = (points[1] - points[0]).cross(points[2] - points[0]);
    const double normalLength = geometry.UnnormalizedNormal.norm();
    geometry.Area = 0.5 * normalLength;
    if (normalLength > 1e-12) {
        geometry.UnitNormal = geometry.UnnormalizedNormal / normalLength;
    }
    return geometry;
}

double SmoothedAbs(double value, double epsilon) {
    return std::sqrt(value * value + epsilon);
}

Eigen::Matrix3d NearestRotation(const Eigen::Matrix3d& matrix) {
    Eigen::JacobiSVD<Eigen::Matrix3d> svd(matrix, Eigen::ComputeFullU | Eigen::ComputeFullV);
    Eigen::Matrix3d u = svd.matrixU();
    Eigen::Matrix3d v = svd.matrixV();
    Eigen::Matrix3d rotation = u * v.transpose();
    if (rotation.determinant() < 0.0) {
        u.col(2) *= -1.0;
        rotation = u * v.transpose();
    }
    return rotation;
}

double SignedTetVolume(const std::array<Eigen::Vector3d, 4>& tet) {
    return (tet[1] - tet[0]).dot((tet[2] - tet[0]).cross(tet[3] - tet[0])) / 6.0;
}

double TetArapEnergy(
    const std::array<Eigen::Vector3d, 4>& restTet,
    const std::array<Eigen::Vector3d, 4>& deformedTet
) {
    Eigen::Matrix3d rest;
    rest.col(0) = restTet[1] - restTet[0];
    rest.col(1) = restTet[2] - restTet[0];
    rest.col(2) = restTet[3] - restTet[0];

    Eigen::Matrix3d def;
    def.col(0) = deformedTet[1] - deformedTet[0];
    def.col(1) = deformedTet[2] - deformedTet[0];
    def.col(2) = deformedTet[3] - deformedTet[0];

    const Eigen::Matrix3d deformation = def * rest.inverse();
    const Eigen::Matrix3d rotation = NearestRotation(deformation);
    return 0.5 * (deformation - rotation).squaredNorm();
}

int DominantAxisLabel(const Eigen::Vector3d& normal) {
    Eigen::Vector3d axis = normal;
    if (axis.squaredNorm() <= 1e-12) {
        return 0;
    }
    axis.normalize();

    int bestAxis = 0;
    double bestValue = std::abs(axis.x());
    if (std::abs(axis.y()) > bestValue) {
        bestAxis = 1;
        bestValue = std::abs(axis.y());
    }
    if (std::abs(axis.z()) > bestValue) {
        bestAxis = 2;
    }

    if (bestAxis == 0) {
        return axis.x() >= 0.0 ? 0 : 1;
    }
    if (bestAxis == 1) {
        return axis.y() >= 0.0 ? 2 : 3;
    }
    return axis.z() >= 0.0 ? 4 : 5;
}

} // namespace PolycubeInternal

namespace {

using Vec3 = Eigen::Vector3d;
using Mat3 = Eigen::Matrix3d;
using Vec9 = Eigen::Matrix<double, 9, 1>;
using Vec12 = Eigen::Matrix<double, 12, 1>;
using Mat9 = Eigen::Matrix<double, 9, 9>;
using Mat12 = Eigen::Matrix<double, 12, 12>;
using Mat3x9 = Eigen::Matrix<double, 3, 9>;
using Mat9x12 = Eigen::Matrix<double, 9, 12>;
using SparseMatrix = Eigen::SparseMatrix<double>;
using Triplet = Eigen::Triplet<double>;

constexpr double kGeometryEpsilon = 1e-12;
constexpr double kKktDiagonalRegularization = 1e-8;
constexpr double kConstraintMeritWeight = 1000.0;
constexpr double kFoldoverBarrierWeight = 1e-2;
constexpr double kMinLineSearchStep = 1.0 / 1024.0;
constexpr double kSkinnyBoundaryAngleDeg = 5.0;
constexpr double kPi = 3.14159265358979323846;
constexpr int kMinTargetTetCount = 1000;
constexpr int kMaxTargetTetCount = 8000;
constexpr int kMaxTetgenAttempts = 5;
constexpr int kFinalArapIterations = 5;

struct TetCell {
    std::array<int, 4> Vertices = {};
    double RestVolume = 0.0;
    Mat3 RestDmInv = Mat3::Zero();
    Mat9x12 GradF = Mat9x12::Zero();
};

struct BoundaryFace {
    std::array<int, 3> Vertices = {};
    int TetIndex = -1;
    int OppositeVertex = -1;
    std::vector<int> Neighbors;
    int Label = -1;
};

struct VolumeMesh {
    std::vector<Vec3> RestPositions;
    std::vector<Vec3> CurrentPositions;
    std::vector<TetCell> Tets;
    std::vector<BoundaryFace> BoundaryFaces;
    double RestBoundaryArea = 0.0;
    double TotalRestVolume = 0.0;
};

struct StateMetrics {
    double BoundaryL1 = 0.0;
    double Complexity = 0.0;
    double Arap = 0.0;
    double BoundaryBarrier = 0.0;
    double TotalArea = 0.0;
    double ExactError = 0.0;
    double MinTetVolume = std::numeric_limits<double>::max();
    double MinBarrierArgument = std::numeric_limits<double>::max();

    double Merit(double restBoundaryArea, double alpha, double complexityWeight) const {
        return alpha * BoundaryL1 + complexityWeight * Complexity + Arap + kFoldoverBarrierWeight * BoundaryBarrier +
            kConstraintMeritWeight * std::abs(TotalArea - restBoundaryArea);
    }

    double NormalizedError(double restBoundaryArea) const {
        if (restBoundaryArea <= kGeometryEpsilon) {
            return 0.0;
        }
        return ExactError / restBoundaryArea;
    }

    double AreaDrift(double restBoundaryArea) const {
        if (restBoundaryArea <= kGeometryEpsilon) {
            return 0.0;
        }
        return (TotalArea - restBoundaryArea) / restBoundaryArea;
    }
};

struct LinearizedSystem {
    SparseMatrix Hessian;
    Eigen::VectorXd Gradient;
    Eigen::VectorXd ConstraintGradient;
    double ConstraintValue = 0.0;
};

Mat3 Skew(const Vec3& v) {
    Mat3 skew = Mat3::Zero();
    skew(0, 1) = -v.z();
    skew(0, 2) = v.y();
    skew(1, 0) = v.z();
    skew(1, 2) = -v.x();
    skew(2, 0) = -v.y();
    skew(2, 1) = v.x();
    return skew;
}

Vec9 FlattenMatrix(const Mat3& matrix) {
    return Eigen::Map<const Vec9>(matrix.data());
}

template <std::size_t VertexCount>
std::array<int, VertexCount * 3> BuildDofIndices(const std::array<int, VertexCount>& vertices) {
    std::array<int, VertexCount * 3> dofs = {};
    for (std::size_t i = 0; i < VertexCount; ++i) {
        dofs[i * 3 + 0] = vertices[i] * 3 + 0;
        dofs[i * 3 + 1] = vertices[i] * 3 + 1;
        dofs[i * 3 + 2] = vertices[i] * 3 + 2;
    }
    return dofs;
}

template <int LocalDofs>
void AddLocalVector(
    Eigen::VectorXd& global,
    const std::array<int, LocalDofs>& indices,
    const Eigen::Matrix<double, LocalDofs, 1>& local
) {
    for (int i = 0; i < LocalDofs; ++i) {
        global(indices[i]) += local(i);
    }
}

template <int LocalDofs>
void AddLocalMatrix(
    std::vector<Triplet>& triplets,
    const std::array<int, LocalDofs>& indices,
    const Eigen::Matrix<double, LocalDofs, LocalDofs>& local
) {
    for (int row = 0; row < LocalDofs; ++row) {
        for (int col = 0; col < LocalDofs; ++col) {
            if (std::abs(local(row, col)) > 1e-14) {
                triplets.emplace_back(indices[row], indices[col], local(row, col));
            }
        }
    }
}

template <int RowDofs, int ColDofs>
void AddLocalMatrixBlock(
    std::vector<Triplet>& triplets,
    const std::array<int, RowDofs>& rowIndices,
    const std::array<int, ColDofs>& colIndices,
    const Eigen::Matrix<double, RowDofs, ColDofs>& local
) {
    for (int row = 0; row < RowDofs; ++row) {
        for (int col = 0; col < ColDofs; ++col) {
            if (std::abs(local(row, col)) > 1e-14) {
                triplets.emplace_back(rowIndices[row], colIndices[col], local(row, col));
            }
        }
    }
}

std::uint64_t EncodeEdge(std::int32_t a, std::int32_t b) {
    const std::uint32_t lo = static_cast<std::uint32_t>(std::min(a, b));
    const std::uint32_t hi = static_cast<std::uint32_t>(std::max(a, b));
    return (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
}

std::uint64_t EncodeFace(std::array<int, 3> face) {
    std::sort(face.begin(), face.end());
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(face[0])) << 42u) |
           (static_cast<std::uint64_t>(static_cast<std::uint32_t>(face[1])) << 21u) |
           static_cast<std::uint64_t>(static_cast<std::uint32_t>(face[2]));
}

std::vector<Vec3> ExtractSurfacePositions(const HE_MeshData& mesh) {
    std::vector<Vec3> positions;
    positions.reserve(mesh.HE_Vertexes.size());
    for (const HE_Vertex* vertex : mesh.HE_Vertexes) {
        positions.emplace_back(vertex->Pos.x, vertex->Pos.y, vertex->Pos.z);
    }
    return positions;
}

void ReportProgress(const PolycubeOptions& options, const PolycubeProgress& progress) {
    if (options.ProgressCallback) {
        options.ProgressCallback(progress);
    }
}

std::vector<std::array<int, 3>> ExtractSurfaceTriangles(const HE_MeshData& mesh) {
    std::vector<std::array<int, 3>> triangles;
    triangles.reserve(mesh.HE_Triangles.size());
    for (const HE_Triangle* triangle : mesh.HE_Triangles) {
        triangles.push_back({
            triangle->VertexIndex0,
            triangle->VertexIndex1,
            triangle->VertexIndex2
        });
    }
    return triangles;
}

double ComputeAverageEdgeLength(
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles
) {
    std::unordered_set<std::uint64_t> uniqueEdges;
    uniqueEdges.reserve(triangles.size() * 3);

    double totalLength = 0.0;
    std::size_t edgeCount = 0;
    for (const auto& triangle : triangles) {
        const std::array<std::pair<int, int>, 3> edges = {
            std::make_pair(triangle[0], triangle[1]),
            std::make_pair(triangle[1], triangle[2]),
            std::make_pair(triangle[2], triangle[0])
        };
        for (const auto& edge : edges) {
            const std::uint64_t key = EncodeEdge(edge.first, edge.second);
            if (uniqueEdges.insert(key).second) {
                totalLength += (positions[edge.first] - positions[edge.second]).norm();
                ++edgeCount;
            }
        }
    }
    return edgeCount == 0 ? 0.0 : (totalLength / static_cast<double>(edgeCount));
}

double ComputeEnclosedVolume(
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles
) {
    double signedVolume = 0.0;
    for (const auto& triangle : triangles) {
        const Vec3& p0 = positions[triangle[0]];
        const Vec3& p1 = positions[triangle[1]];
        const Vec3& p2 = positions[triangle[2]];
        signedVolume += p0.dot(p1.cross(p2));
    }
    return std::abs(signedVolume) / 6.0;
}

int ComputeTargetTetCount(std::size_t boundaryTriangleCount, float tetSpacingScale) {
    const double scale = std::clamp(static_cast<double>(tetSpacingScale), 0.5, 3.0);
    const double desired = static_cast<double>(boundaryTriangleCount) * 0.75 / scale;
    return std::clamp(static_cast<int>(std::round(desired)), kMinTargetTetCount, kMaxTargetTetCount);
}

void PopulateTetGenInput(
    tetgenio& input,
    const std::vector<Vec3>& positions,
    const std::vector<std::array<int, 3>>& triangles
) {
    input.firstnumber = 0;
    input.numberofpoints = static_cast<int>(positions.size());
    input.pointlist = new REAL[input.numberofpoints * 3];
    for (int pointIndex = 0; pointIndex < input.numberofpoints; ++pointIndex) {
        input.pointlist[pointIndex * 3 + 0] = positions[pointIndex].x();
        input.pointlist[pointIndex * 3 + 1] = positions[pointIndex].y();
        input.pointlist[pointIndex * 3 + 2] = positions[pointIndex].z();
    }

    input.numberoffacets = static_cast<int>(triangles.size());
    input.facetlist = new tetgenio::facet[input.numberoffacets];
    input.facetmarkerlist = new int[input.numberoffacets];
    for (int facetIndex = 0; facetIndex < input.numberoffacets; ++facetIndex) {
        tetgenio::facet& facet = input.facetlist[facetIndex];
        tetgenio::init(&facet);
        facet.numberofpolygons = 1;
        facet.polygonlist = new tetgenio::polygon[1];
        tetgenio::polygon& polygon = facet.polygonlist[0];
        tetgenio::init(&polygon);
        polygon.numberofvertices = 3;
        polygon.vertexlist = new int[3];
        polygon.vertexlist[0] = triangles[facetIndex][0];
        polygon.vertexlist[1] = triangles[facetIndex][1];
        polygon.vertexlist[2] = triangles[facetIndex][2];
        input.facetmarkerlist[facetIndex] = 1;
    }
}

Mat9x12 BuildTetGradient(const Mat3& restDmInv) {
    Mat9x12 gradient = Mat9x12::Zero();

    for (int localVertex = 0; localVertex < 4; ++localVertex) {
        for (int axis = 0; axis < 3; ++axis) {
            std::array<Vec3, 4> delta = {
                Vec3::Zero(),
                Vec3::Zero(),
                Vec3::Zero(),
                Vec3::Zero()
            };
            delta[localVertex][axis] = 1.0;

            Mat3 dDs = Mat3::Zero();
            dDs.col(0) = delta[1] - delta[0];
            dDs.col(1) = delta[2] - delta[0];
            dDs.col(2) = delta[3] - delta[0];

            const Mat3 dF = dDs * restDmInv;
            gradient.col(localVertex * 3 + axis) = FlattenMatrix(dF);
        }
    }

    return gradient;
}

Mat3x9 BuildFaceNormalJacobian(const std::array<Vec3, 3>& triangle) {
    const Vec3 edge01 = triangle[1] - triangle[0];
    const Vec3 edge02 = triangle[2] - triangle[0];

    Mat3x9 jacobian = Mat3x9::Zero();
    jacobian.block<3, 3>(0, 0) = Skew(edge02) - Skew(edge01);
    jacobian.block<3, 3>(0, 3) = -Skew(edge02);
    jacobian.block<3, 3>(0, 6) = Skew(edge01);
    return jacobian;
}

Mat3x9 BuildUnitNormalJacobian(
    const std::array<Vec3, 3>& triangle,
    const PolycubeInternal::FaceGeometry& geometry
) {
    const double normalLength = geometry.UnnormalizedNormal.norm();
    if (normalLength <= kGeometryEpsilon) {
        return Mat3x9::Zero();
    }

    const Mat3 projector = Mat3::Identity() - geometry.UnitNormal * geometry.UnitNormal.transpose();
    return (projector / normalLength) * BuildFaceNormalJacobian(triangle);
}

void ClampToSpd(Mat9& matrix) {
    matrix = 0.5 * (matrix + matrix.transpose());
    Eigen::SelfAdjointEigenSolver<Mat9> solver(matrix);
    Eigen::Matrix<double, 9, 1> eigenvalues = solver.eigenvalues();
    for (int i = 0; i < eigenvalues.size(); ++i) {
        eigenvalues[i] = std::max(eigenvalues[i], 0.0);
    }
    matrix = solver.eigenvectors() * eigenvalues.asDiagonal() * solver.eigenvectors().transpose();
}

std::array<Vec3, 4> GatherTet(const VolumeMesh& mesh, const TetCell& tet, bool useCurrent) {
    const std::vector<Vec3>& positions = useCurrent ? mesh.CurrentPositions : mesh.RestPositions;
    return {
        positions[tet.Vertices[0]],
        positions[tet.Vertices[1]],
        positions[tet.Vertices[2]],
        positions[tet.Vertices[3]]
    };
}

std::array<Vec3, 3> GatherFace(const VolumeMesh& mesh, const BoundaryFace& face, bool useCurrent) {
    const std::vector<Vec3>& positions = useCurrent ? mesh.CurrentPositions : mesh.RestPositions;
    return {
        positions[face.Vertices[0]],
        positions[face.Vertices[1]],
        positions[face.Vertices[2]]
    };
}

std::vector<std::vector<int>> BuildBoundaryAdjacency(const std::vector<std::array<int, 3>>& faces) {
    std::unordered_map<std::uint64_t, std::vector<int>> edgeToFaces;
    edgeToFaces.reserve(faces.size() * 3);

    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        const auto& face = faces[faceIndex];
        edgeToFaces[EncodeEdge(face[0], face[1])].push_back(static_cast<int>(faceIndex));
        edgeToFaces[EncodeEdge(face[1], face[2])].push_back(static_cast<int>(faceIndex));
        edgeToFaces[EncodeEdge(face[2], face[0])].push_back(static_cast<int>(faceIndex));
    }

    std::vector<std::vector<int>> adjacency(faces.size());
    for (const auto& [_, edgeFaces] : edgeToFaces) {
        for (std::size_t i = 0; i < edgeFaces.size(); ++i) {
            for (std::size_t j = i + 1; j < edgeFaces.size(); ++j) {
                adjacency[edgeFaces[i]].push_back(edgeFaces[j]);
                adjacency[edgeFaces[j]].push_back(edgeFaces[i]);
            }
        }
    }

    for (auto& neighbors : adjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }
    return adjacency;
}

double SharedEdgeLength(
    const std::array<int, 3>& faceA,
    const std::array<int, 3>& faceB,
    const std::vector<Vec3>& positions
) {
    std::array<int, 2> shared = { -1, -1 };
    int sharedCount = 0;
    for (int vertexA : faceA) {
        for (int vertexB : faceB) {
            if (vertexA == vertexB) {
                if (sharedCount < 2) {
                    shared[sharedCount] = vertexA;
                }
                ++sharedCount;
            }
        }
    }

    if (sharedCount != 2) {
        return 0.0;
    }
    return (positions[shared[0]] - positions[shared[1]]).norm();
}

std::vector<double> ComputeNeighborAreaSums(
    const std::vector<BoundaryFace>& faces,
    const std::vector<PolycubeInternal::FaceGeometry>& geometries
) {
    std::vector<double> sums(faces.size(), 0.0);
    for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
        double areaSum = 0.0;
        for (const int neighbor : faces[faceIndex].Neighbors) {
            if (neighbor < 0 || neighbor >= static_cast<int>(geometries.size())) {
                continue;
            }
            areaSum += geometries[neighbor].Area;
        }
        sums[faceIndex] = areaSum;
    }
    return sums;
}

double ComputeComplexityEdgeArea(
    int faceIndex,
    int neighbor,
    const std::vector<PolycubeInternal::FaceGeometry>& geometries,
    const std::vector<double>& neighborAreaSums
) {
    const double areaA = geometries[faceIndex].Area;
    const double areaB = geometries[neighbor].Area;
    if (areaA <= kGeometryEpsilon || areaB <= kGeometryEpsilon) {
        return 0.0;
    }

    const double denomA = std::max(neighborAreaSums[faceIndex], kGeometryEpsilon);
    const double denomB = std::max(neighborAreaSums[neighbor], kGeometryEpsilon);
    const double gammaAB = areaB / denomA;
    const double gammaBA = areaA / denomB;
    return gammaAB * areaA + gammaBA * areaB;
}

struct PatchComponents {
    std::vector<int> FaceToComponent;
    std::vector<std::vector<int>> ComponentFaces;
};

struct CleanupDiagnostics {
    int RelabelsAccepted = 0;
    int MergeRounds = 0;
    bool StoppedByBudget = false;
    bool StoppedByNoProgress = false;
};

struct CleanupOutcome {
    std::vector<int> Labels;
    CleanupDiagnostics Diagnostics;
};

using CleanupProgressCallback = std::function<void(const char*, int, const CleanupDiagnostics&)>;

PatchComponents BuildPatchComponents(
    const std::vector<int>& labels,
    const std::vector<std::vector<int>>& adjacency
) {
    PatchComponents components;
    components.FaceToComponent.assign(labels.size(), -1);

    for (std::size_t faceIndex = 0; faceIndex < labels.size(); ++faceIndex) {
        if (components.FaceToComponent[faceIndex] >= 0) {
            continue;
        }

        const int componentId = static_cast<int>(components.ComponentFaces.size());
        std::vector<int> stack = { static_cast<int>(faceIndex) };
        components.ComponentFaces.emplace_back();
        components.FaceToComponent[faceIndex] = componentId;

        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            components.ComponentFaces[componentId].push_back(current);

            for (const int neighbor : adjacency[current]) {
                if (components.FaceToComponent[neighbor] >= 0) {
                    continue;
                }
                if (labels[neighbor] != labels[current]) {
                    continue;
                }
                components.FaceToComponent[neighbor] = componentId;
                stack.push_back(neighbor);
            }
        }
    }

    return components;
}

int CountLocalDisagreements(
    const std::vector<int>& labels,
    const std::vector<std::vector<int>>& adjacency,
    int faceIndex,
    int candidateLabel
) {
    int disagreements = 0;
    for (const int neighbor : adjacency[faceIndex]) {
        if (labels[neighbor] != candidateLabel) {
            ++disagreements;
        }
    }
    return disagreements;
}

bool SelectImprovingPatchLabel(
    const std::vector<int>& labels,
    const std::vector<std::vector<int>>& adjacency,
    int faceIndex,
    int& outLabel
) {
    const int currentLabel = labels[faceIndex];
    const int before = CountLocalDisagreements(labels, adjacency, faceIndex, currentLabel);

    std::map<int, int> neighborCounts;
    for (const int neighbor : adjacency[faceIndex]) {
        neighborCounts[labels[neighbor]] += 1;
    }

    int bestLabel = currentLabel;
    int bestAfter = before;
    int bestSupport = -1;
    for (const auto& [candidateLabel, count] : neighborCounts) {
        if (candidateLabel == currentLabel || count < 2) {
            continue;
        }

        const int after = CountLocalDisagreements(labels, adjacency, faceIndex, candidateLabel);
        if (after >= before) {
            continue;
        }

        if (after < bestAfter ||
            (after == bestAfter && count > bestSupport) ||
            (after == bestAfter && count == bestSupport && candidateLabel < bestLabel)) {
            bestLabel = candidateLabel;
            bestAfter = after;
            bestSupport = count;
        }
    }

    if (bestLabel == currentLabel) {
        return false;
    }

    outLabel = bestLabel;
    return true;
}

void StraightenPatchBordersMonotonic(
    std::vector<int>& labels,
    const std::vector<std::vector<int>>& adjacency,
    CleanupDiagnostics& diagnostics
) {
    if (labels.empty()) {
        return;
    }

    const int relabelBudget = std::max<int>(1, static_cast<int>(labels.size()) * 4);
    std::deque<int> queue;
    std::vector<std::uint8_t> inQueue(labels.size(), 1);
    for (std::size_t faceIndex = 0; faceIndex < labels.size(); ++faceIndex) {
        queue.push_back(static_cast<int>(faceIndex));
    }

    while (!queue.empty()) {
        if (diagnostics.RelabelsAccepted >= relabelBudget) {
            diagnostics.StoppedByBudget = true;
            break;
        }

        const int faceIndex = queue.front();
        queue.pop_front();
        inQueue[faceIndex] = 0;

        int candidateLabel = labels[faceIndex];
        if (!SelectImprovingPatchLabel(labels, adjacency, faceIndex, candidateLabel)) {
            continue;
        }

        labels[faceIndex] = candidateLabel;
        ++diagnostics.RelabelsAccepted;

        if (!inQueue[faceIndex]) {
            queue.push_back(faceIndex);
            inQueue[faceIndex] = 1;
        }
        for (const int neighbor : adjacency[faceIndex]) {
            if (inQueue[neighbor]) {
                continue;
            }
            queue.push_back(neighbor);
            inQueue[neighbor] = 1;
        }
    }
}

CleanupOutcome CleanupPatchLabelsDetailed(
    const std::vector<std::array<int, 3>>& faces,
    const std::vector<Eigen::Vector3d>& positions,
    const std::vector<int>& initialLabels,
    const CleanupProgressCallback& progressCallback = {}
) {
    if (faces.size() != initialLabels.size()) {
        throw std::runtime_error("Patch cleanup input size mismatch.");
    }

    CleanupOutcome outcome;
    outcome.Labels = initialLabels;
    const std::vector<std::vector<int>> adjacency = BuildBoundaryAdjacency(faces);

    auto report = [&](const char* phase) {
        if (!progressCallback) {
            return;
        }
        const int componentCount =
            static_cast<int>(BuildPatchComponents(outcome.Labels, adjacency).ComponentFaces.size());
        progressCallback(phase, componentCount, outcome.Diagnostics);
    };

    report("border straighten");
    StraightenPatchBordersMonotonic(outcome.Labels, adjacency, outcome.Diagnostics);
    report("border straighten");

    int currentComponentCount =
        static_cast<int>(BuildPatchComponents(outcome.Labels, adjacency).ComponentFaces.size());
    const int maxMerges = std::max(0, currentComponentCount - 1);
    for (int mergeIndex = 0; mergeIndex < maxMerges; ++mergeIndex) {
        const PatchComponents components = BuildPatchComponents(outcome.Labels, adjacency);
        currentComponentCount = static_cast<int>(components.ComponentFaces.size());
        report("component merge");
        if (currentComponentCount <= 1) {
            break;
        }

        std::unordered_map<std::uint64_t, double> sharedLengths;
        for (std::size_t faceIndex = 0; faceIndex < faces.size(); ++faceIndex) {
            for (const int neighbor : adjacency[faceIndex]) {
                if (neighbor <= static_cast<int>(faceIndex)) {
                    continue;
                }
                const int componentA = components.FaceToComponent[faceIndex];
                const int componentB = components.FaceToComponent[neighbor];
                if (componentA == componentB) {
                    continue;
                }
                const std::uint32_t lo = static_cast<std::uint32_t>(std::min(componentA, componentB));
                const std::uint32_t hi = static_cast<std::uint32_t>(std::max(componentA, componentB));
                const std::uint64_t key =
                    (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
                sharedLengths[key] += SharedEdgeLength(faces[faceIndex], faces[neighbor], positions);
            }
        }

        std::vector<std::unordered_map<int, double>> componentNeighbors(components.ComponentFaces.size());
        for (const auto& [key, length] : sharedLengths) {
            const int componentA = static_cast<int>(key >> 32u);
            const int componentB = static_cast<int>(key & 0xffffffffu);
            componentNeighbors[componentA][componentB] += length;
            componentNeighbors[componentB][componentA] += length;
        }

        int candidateComponent = -1;
        int candidateDegree = std::numeric_limits<int>::max();
        std::size_t candidateSize = std::numeric_limits<std::size_t>::max();
        for (std::size_t componentIndex = 0; componentIndex < componentNeighbors.size(); ++componentIndex) {
            const std::size_t degree = componentNeighbors[componentIndex].size();
            if (degree != 1 && degree != 2) {
                continue;
            }
            if (static_cast<int>(degree) < candidateDegree ||
                (static_cast<int>(degree) == candidateDegree &&
                 components.ComponentFaces[componentIndex].size() < candidateSize)) {
                candidateComponent = static_cast<int>(componentIndex);
                candidateDegree = static_cast<int>(degree);
                candidateSize = components.ComponentFaces[componentIndex].size();
            }
        }

        if (candidateComponent < 0) {
            outcome.Diagnostics.StoppedByNoProgress = true;
            break;
        }

        int bestNeighbor = -1;
        double bestLength = -1.0;
        for (const auto& [neighbor, length] : componentNeighbors[candidateComponent]) {
            if (length > bestLength) {
                bestLength = length;
                bestNeighbor = neighbor;
            }
        }

        if (bestNeighbor < 0) {
            outcome.Diagnostics.StoppedByNoProgress = true;
            break;
        }

        const int replacementLabel = outcome.Labels[components.ComponentFaces[bestNeighbor].front()];
        for (const int faceIndex : components.ComponentFaces[candidateComponent]) {
            outcome.Labels[faceIndex] = replacementLabel;
        }
        ++outcome.Diagnostics.MergeRounds;

        StraightenPatchBordersMonotonic(outcome.Labels, adjacency, outcome.Diagnostics);
        const int nextComponentCount =
            static_cast<int>(BuildPatchComponents(outcome.Labels, adjacency).ComponentFaces.size());
        report("component merge");
        if (nextComponentCount >= currentComponentCount) {
            outcome.Diagnostics.StoppedByNoProgress = true;
            break;
        }
    }

    return outcome;
}

HE_MeshData BuildPreviewMesh(const VolumeMesh& volumeMesh) {
    std::unordered_map<int, int> vertexRemap;
    vertexRemap.reserve(volumeMesh.BoundaryFaces.size() * 3);

    std::vector<Vec3f> positions;
    std::vector<std::array<std::int32_t, 3>> triangles;
    triangles.reserve(volumeMesh.BoundaryFaces.size());

    auto mapVertex = [&](int vertexIndex) -> int {
        const auto it = vertexRemap.find(vertexIndex);
        if (it != vertexRemap.end()) {
            return it->second;
        }

        const int remapped = static_cast<int>(positions.size());
        const Vec3& position = volumeMesh.CurrentPositions[vertexIndex];
        positions.push_back(Vec3f(
            static_cast<float>(position.x()),
            static_cast<float>(position.y()),
            static_cast<float>(position.z())
        ));
        vertexRemap.emplace(vertexIndex, remapped);
        return remapped;
    };

    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        triangles.push_back({
            mapVertex(face.Vertices[0]),
            mapVertex(face.Vertices[1]),
            mapVertex(face.Vertices[2])
        });
    }

    HE_MeshData preview;
    preview.SetIndexedMesh(positions, triangles);
    return preview;
}

struct IntArray4Hash {
    std::size_t operator()(const std::array<int, 4>& values) const noexcept {
        std::size_t seed = 0;
        for (const int value : values) {
            seed ^= std::hash<int>{}(value) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }
        return seed;
    }
};

bool TetHasDuplicateVertices(const std::array<int, 4>& vertices) {
    return vertices[0] == vertices[1] || vertices[0] == vertices[2] || vertices[0] == vertices[3] ||
           vertices[1] == vertices[2] || vertices[1] == vertices[3] ||
           vertices[2] == vertices[3];
}

bool CompactVolumeMeshVertices(VolumeMesh& volumeMesh) {
    if (volumeMesh.Tets.empty()) {
        return false;
    }

    std::vector<int> remap(volumeMesh.RestPositions.size(), -1);
    int nextIndex = 0;
    for (const TetCell& tet : volumeMesh.Tets) {
        for (const int vertex : tet.Vertices) {
            if (vertex < 0 || vertex >= static_cast<int>(remap.size())) {
                return false;
            }
            if (remap[vertex] < 0) {
                remap[vertex] = nextIndex++;
            }
        }
    }

    if (nextIndex < 4) {
        return false;
    }

    std::vector<Vec3> restPositions(nextIndex);
    std::vector<Vec3> currentPositions(nextIndex);
    for (std::size_t oldIndex = 0; oldIndex < remap.size(); ++oldIndex) {
        const int mapped = remap[oldIndex];
        if (mapped < 0) {
            continue;
        }
        restPositions[mapped] = volumeMesh.RestPositions[oldIndex];
        currentPositions[mapped] = volumeMesh.CurrentPositions[oldIndex];
    }

    for (TetCell& tet : volumeMesh.Tets) {
        for (int& vertex : tet.Vertices) {
            vertex = remap[vertex];
        }
    }

    volumeMesh.RestPositions.swap(restPositions);
    volumeMesh.CurrentPositions.swap(currentPositions);
    return true;
}

bool RebuildVolumeMeshDerivedState(VolumeMesh& volumeMesh) {
    if (!CompactVolumeMeshVertices(volumeMesh)) {
        return false;
    }

    std::unordered_set<std::array<int, 4>, IntArray4Hash> uniqueTets;
    uniqueTets.reserve(volumeMesh.Tets.size() * 2);

    std::vector<TetCell> rebuiltTets;
    rebuiltTets.reserve(volumeMesh.Tets.size());
    volumeMesh.TotalRestVolume = 0.0;

    for (TetCell tet : volumeMesh.Tets) {
        if (TetHasDuplicateVertices(tet.Vertices)) {
            continue;
        }

        std::array<int, 4> key = tet.Vertices;
        std::sort(key.begin(), key.end());
        if (!uniqueTets.insert(key).second) {
            continue;
        }

        std::array<Vec3, 4> tetPoints = {
            volumeMesh.RestPositions[tet.Vertices[0]],
            volumeMesh.RestPositions[tet.Vertices[1]],
            volumeMesh.RestPositions[tet.Vertices[2]],
            volumeMesh.RestPositions[tet.Vertices[3]]
        };

        double volume = PolycubeInternal::SignedTetVolume(tetPoints);
        if (std::abs(volume) <= kGeometryEpsilon) {
            continue;
        }

        if (volume < 0.0) {
            std::swap(tet.Vertices[0], tet.Vertices[1]);
            tetPoints = {
                volumeMesh.RestPositions[tet.Vertices[0]],
                volumeMesh.RestPositions[tet.Vertices[1]],
                volumeMesh.RestPositions[tet.Vertices[2]],
                volumeMesh.RestPositions[tet.Vertices[3]]
            };
            volume = PolycubeInternal::SignedTetVolume(tetPoints);
        }

        if (volume <= kGeometryEpsilon) {
            continue;
        }

        Mat3 restDm = Mat3::Zero();
        restDm.col(0) = tetPoints[1] - tetPoints[0];
        restDm.col(1) = tetPoints[2] - tetPoints[0];
        restDm.col(2) = tetPoints[3] - tetPoints[0];
        if (std::abs(restDm.determinant()) <= kGeometryEpsilon) {
            continue;
        }

        tet.RestVolume = volume;
        tet.RestDmInv = restDm.inverse();
        tet.GradF = BuildTetGradient(tet.RestDmInv);
        volumeMesh.TotalRestVolume += tet.RestVolume;
        rebuiltTets.push_back(tet);
    }

    if (rebuiltTets.empty() || volumeMesh.TotalRestVolume <= kGeometryEpsilon) {
        return false;
    }

    volumeMesh.Tets.swap(rebuiltTets);
    volumeMesh.BoundaryFaces.clear();
    volumeMesh.RestBoundaryArea = 0.0;

    struct FaceRecord {
        BoundaryFace Face;
        int Count = 0;
    };

    std::unordered_map<std::uint64_t, FaceRecord> faces;
    faces.reserve(volumeMesh.Tets.size() * 2);

    const std::array<std::array<int, 3>, 4> localFaces = {
        std::array<int, 3>{ 1, 2, 3 },
        std::array<int, 3>{ 0, 3, 2 },
        std::array<int, 3>{ 0, 1, 3 },
        std::array<int, 3>{ 0, 2, 1 }
    };

    for (std::size_t tetIndex = 0; tetIndex < volumeMesh.Tets.size(); ++tetIndex) {
        const TetCell& tet = volumeMesh.Tets[tetIndex];
        for (int opposite = 0; opposite < 4; ++opposite) {
            BoundaryFace boundaryFace;
            boundaryFace.TetIndex = static_cast<int>(tetIndex);
            boundaryFace.OppositeVertex = tet.Vertices[opposite];
            boundaryFace.Vertices = {
                tet.Vertices[localFaces[opposite][0]],
                tet.Vertices[localFaces[opposite][1]],
                tet.Vertices[localFaces[opposite][2]]
            };

            const Vec3& p0 = volumeMesh.RestPositions[boundaryFace.Vertices[0]];
            const Vec3& p1 = volumeMesh.RestPositions[boundaryFace.Vertices[1]];
            const Vec3& p2 = volumeMesh.RestPositions[boundaryFace.Vertices[2]];
            const Vec3& oppositePoint = volumeMesh.RestPositions[boundaryFace.OppositeVertex];

            const Vec3 normal = (p1 - p0).cross(p2 - p0);
            if (normal.dot(oppositePoint - p0) > 0.0) {
                std::swap(boundaryFace.Vertices[1], boundaryFace.Vertices[2]);
            }

            FaceRecord& record = faces[EncodeFace(boundaryFace.Vertices)];
            if (record.Count == 0) {
                record.Face = boundaryFace;
            }
            record.Count += 1;
        }
    }

    std::vector<std::array<int, 3>> boundaryTriangles;
    boundaryTriangles.reserve(faces.size());
    for (const auto& [_, record] : faces) {
        if (record.Count != 1) {
            continue;
        }
        volumeMesh.BoundaryFaces.push_back(record.Face);
        boundaryTriangles.push_back(record.Face.Vertices);
        const PolycubeInternal::FaceGeometry geometry =
            PolycubeInternal::ComputeFaceGeometry(GatherFace(volumeMesh, record.Face, false));
        volumeMesh.RestBoundaryArea += geometry.Area;
    }

    if (volumeMesh.BoundaryFaces.empty() || volumeMesh.RestBoundaryArea <= kGeometryEpsilon) {
        return false;
    }

    const std::vector<std::vector<int>> adjacency = BuildBoundaryAdjacency(boundaryTriangles);
    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        volumeMesh.BoundaryFaces[faceIndex].Neighbors = adjacency[faceIndex];
    }

    return true;
}

StateMetrics EvaluateState(
    const VolumeMesh& volumeMesh,
    double alpha,
    double epsilon,
    double complexityWeight
);

double MinBoundaryAngleDegrees(const std::array<Vec3, 3>& triangle) {
    const Vec3 edges[3] = {
        triangle[1] - triangle[0],
        triangle[2] - triangle[1],
        triangle[0] - triangle[2]
    };
    const double a0 = AngleBetweenVectors(
        Vec3f(static_cast<float>(-edges[2].x()), static_cast<float>(-edges[2].y()), static_cast<float>(-edges[2].z())),
        Vec3f(static_cast<float>(edges[0].x()), static_cast<float>(edges[0].y()), static_cast<float>(edges[0].z()))
    );
    const double a1 = AngleBetweenVectors(
        Vec3f(static_cast<float>(-edges[0].x()), static_cast<float>(-edges[0].y()), static_cast<float>(-edges[0].z())),
        Vec3f(static_cast<float>(edges[1].x()), static_cast<float>(edges[1].y()), static_cast<float>(edges[1].z()))
    );
    const double a2 = AngleBetweenVectors(
        Vec3f(static_cast<float>(-edges[1].x()), static_cast<float>(-edges[1].y()), static_cast<float>(-edges[1].z())),
        Vec3f(static_cast<float>(edges[2].x()), static_cast<float>(edges[2].y()), static_cast<float>(edges[2].z()))
    );
    return std::min({ a0, a1, a2 }) * 180.0 / kPi;
}

bool CollapseBoundaryEdge(VolumeMesh& volumeMesh, int keepVertex, int removedVertex) {
    if (keepVertex == removedVertex) {
        return false;
    }
    if (keepVertex < 0 || removedVertex < 0 ||
        keepVertex >= static_cast<int>(volumeMesh.RestPositions.size()) ||
        removedVertex >= static_cast<int>(volumeMesh.RestPositions.size())) {
        return false;
    }

    VolumeMesh candidate = volumeMesh;
    const Vec3 restMidpoint =
        0.5 * (candidate.RestPositions[keepVertex] + candidate.RestPositions[removedVertex]);
    const Vec3 currentMidpoint =
        0.5 * (candidate.CurrentPositions[keepVertex] + candidate.CurrentPositions[removedVertex]);
    candidate.RestPositions[keepVertex] = restMidpoint;
    candidate.RestPositions[removedVertex] = restMidpoint;
    candidate.CurrentPositions[keepVertex] = currentMidpoint;
    candidate.CurrentPositions[removedVertex] = currentMidpoint;

    for (TetCell& tet : candidate.Tets) {
        for (int& vertex : tet.Vertices) {
            if (vertex == removedVertex) {
                vertex = keepVertex;
            }
        }
    }

    if (!RebuildVolumeMeshDerivedState(candidate)) {
        return false;
    }

    const StateMetrics metrics = EvaluateState(candidate, 0.0, 1.0, 0.0);
    if (metrics.MinTetVolume <= 1e-10 || metrics.MinBarrierArgument <= 1e-6) {
        return false;
    }

    volumeMesh = std::move(candidate);
    return true;
}

bool CollapseSkinnyBoundaryTriangles(VolumeMesh& volumeMesh) {
    struct EdgeCandidate {
        int Keep = -1;
        int Remove = -1;
        double Length = std::numeric_limits<double>::max();
    };

    struct FaceCandidate {
        int FaceIndex = -1;
        double MinAngleDeg = std::numeric_limits<double>::max();
        std::array<EdgeCandidate, 3> Edges = {};
    };

    std::vector<FaceCandidate> candidates;
    candidates.reserve(volumeMesh.BoundaryFaces.size());

    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        const BoundaryFace& face = volumeMesh.BoundaryFaces[faceIndex];
        const auto triangle = GatherFace(volumeMesh, face, true);
        const double minAngleDeg = MinBoundaryAngleDegrees(triangle);
        if (minAngleDeg >= kSkinnyBoundaryAngleDeg) {
            continue;
        }

        FaceCandidate candidate;
        candidate.FaceIndex = static_cast<int>(faceIndex);
        candidate.MinAngleDeg = minAngleDeg;

        const std::array<std::pair<int, int>, 3> edges = {
            std::make_pair(face.Vertices[0], face.Vertices[1]),
            std::make_pair(face.Vertices[1], face.Vertices[2]),
            std::make_pair(face.Vertices[2], face.Vertices[0])
        };

        for (std::size_t edgeIndex = 0; edgeIndex < edges.size(); ++edgeIndex) {
            const auto [a, b] = edges[edgeIndex];
            const double length = (volumeMesh.CurrentPositions[a] - volumeMesh.CurrentPositions[b]).norm();
            candidate.Edges[edgeIndex] = { a, b, length };
        }

        std::sort(candidate.Edges.begin(), candidate.Edges.end(), [](const EdgeCandidate& lhs, const EdgeCandidate& rhs) {
            return lhs.Length < rhs.Length;
        });
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(), candidates.end(), [](const FaceCandidate& lhs, const FaceCandidate& rhs) {
        if (lhs.MinAngleDeg != rhs.MinAngleDeg) {
            return lhs.MinAngleDeg < rhs.MinAngleDeg;
        }
        return lhs.FaceIndex < rhs.FaceIndex;
    });

    for (const FaceCandidate& candidate : candidates) {
        for (const EdgeCandidate& edge : candidate.Edges) {
            if (CollapseBoundaryEdge(volumeMesh, edge.Keep, edge.Remove)) {
                return true;
            }
            if (CollapseBoundaryEdge(volumeMesh, edge.Remove, edge.Keep)) {
                return true;
            }
        }
    }

    return false;
}

StateMetrics EvaluateState(
    const VolumeMesh& volumeMesh,
    double alpha,
    double epsilon,
    double complexityWeight
) {
    StateMetrics metrics;
    std::vector<PolycubeInternal::FaceGeometry> geometries;
    geometries.reserve(volumeMesh.BoundaryFaces.size());

    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        const auto triangle = GatherFace(volumeMesh, face, true);
        const PolycubeInternal::FaceGeometry geometry = PolycubeInternal::ComputeFaceGeometry(triangle);
        geometries.push_back(geometry);
        metrics.TotalArea += geometry.Area;
        metrics.BoundaryL1 += 0.5 * (
            PolycubeInternal::SmoothedAbs(geometry.UnnormalizedNormal.x(), epsilon) +
            PolycubeInternal::SmoothedAbs(geometry.UnnormalizedNormal.y(), epsilon) +
            PolycubeInternal::SmoothedAbs(geometry.UnnormalizedNormal.z(), epsilon)
        );
        metrics.ExactError += geometry.Area * (geometry.UnitNormal.cwiseAbs().sum() - 1.0);
    }

    const std::vector<double> neighborAreaSums =
        ComputeNeighborAreaSums(volumeMesh.BoundaryFaces, geometries);

    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        for (const int neighbor : volumeMesh.BoundaryFaces[faceIndex].Neighbors) {
            if (neighbor <= static_cast<int>(faceIndex)) {
                continue;
            }

            if (complexityWeight > 0.0 && volumeMesh.RestBoundaryArea > kGeometryEpsilon) {
                const double edgeArea =
                    ComputeComplexityEdgeArea(static_cast<int>(faceIndex), neighbor, geometries, neighborAreaSums);
                if (edgeArea > 0.0) {
                    const Vec3 diff =
                        geometries[faceIndex].UnitNormal - geometries[neighbor].UnitNormal;
                    metrics.Complexity += edgeArea * diff.squaredNorm() / volumeMesh.RestBoundaryArea;
                }
            }

            const double barrierArgument =
                geometries[faceIndex].UnitNormal.dot(geometries[neighbor].UnitNormal) + 1.0;
            metrics.MinBarrierArgument = std::min(metrics.MinBarrierArgument, barrierArgument);
            if (barrierArgument > 1e-8) {
                metrics.BoundaryBarrier += -std::log(barrierArgument);
            } else {
                metrics.BoundaryBarrier += -std::log(1e-8);
            }
        }
    }

    if (volumeMesh.TotalRestVolume > kGeometryEpsilon) {
        for (const TetCell& tet : volumeMesh.Tets) {
            const auto restTet = GatherTet(volumeMesh, tet, false);
            const auto currentTet = GatherTet(volumeMesh, tet, true);
            const double currentVolume = PolycubeInternal::SignedTetVolume(currentTet);
            metrics.MinTetVolume = std::min(metrics.MinTetVolume, currentVolume);
            metrics.Arap += (tet.RestVolume / volumeMesh.TotalRestVolume) *
                PolycubeInternal::TetArapEnergy(restTet, currentTet);
        }
    }

    if (metrics.MinTetVolume == std::numeric_limits<double>::max()) {
        metrics.MinTetVolume = 0.0;
    }
    if (metrics.MinBarrierArgument == std::numeric_limits<double>::max()) {
        metrics.MinBarrierArgument = 2.0;
    }

    (void)alpha;
    return metrics;
}

LinearizedSystem AssembleSystem(
    const VolumeMesh& volumeMesh,
    double alpha,
    double epsilon,
    double complexityWeight
) {
    const int dofCount = static_cast<int>(volumeMesh.CurrentPositions.size() * 3);
    LinearizedSystem system;
    system.Gradient = Eigen::VectorXd::Zero(dofCount);
    system.ConstraintGradient = Eigen::VectorXd::Zero(dofCount);
    std::vector<PolycubeInternal::FaceGeometry> faceGeometries(volumeMesh.BoundaryFaces.size());
    std::vector<Mat3x9> faceUnitNormalJacobians(volumeMesh.BoundaryFaces.size(), Mat3x9::Zero());

    std::vector<Triplet> triplets;
    triplets.reserve(
        static_cast<std::size_t>(volumeMesh.BoundaryFaces.size()) * 162u +
        static_cast<std::size_t>(volumeMesh.Tets.size()) * 144u +
        static_cast<std::size_t>(dofCount)
    );

    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        const BoundaryFace& face = volumeMesh.BoundaryFaces[faceIndex];
        const auto triangle = GatherFace(volumeMesh, face, true);
        const PolycubeInternal::FaceGeometry geometry = PolycubeInternal::ComputeFaceGeometry(triangle);
        faceGeometries[faceIndex] = geometry;
        const Mat3x9 jacobian = BuildFaceNormalJacobian(triangle);
        faceUnitNormalJacobians[faceIndex] = BuildUnitNormalJacobian(triangle, geometry);
        const auto dofIndices = BuildDofIndices(face.Vertices);

        const Eigen::Array3d squared = geometry.UnnormalizedNormal.array().square() + epsilon;
        const Eigen::Array3d sqrtSquared = squared.sqrt();
        const Eigen::Vector3d gradientWeights = (geometry.UnnormalizedNormal.array() / sqrtSquared).matrix();
        const Eigen::Array3d hessianWeights = epsilon / squared.pow(1.5);

        const Vec9 localGradient = 0.5 * alpha * jacobian.transpose() * gradientWeights;
        Mat9 localHessian = 0.5 * alpha * jacobian.transpose() *
            hessianWeights.matrix().asDiagonal() * jacobian;
        ClampToSpd(localHessian);

        if (geometry.Area > kGeometryEpsilon) {
            const Vec9 areaGradient = 0.5 * jacobian.transpose() * geometry.UnitNormal;
            system.ConstraintValue += geometry.Area;
            AddLocalVector(system.ConstraintGradient, dofIndices, areaGradient);
        }

        AddLocalVector(system.Gradient, dofIndices, localGradient);
        AddLocalMatrix(triplets, dofIndices, localHessian);
    }

    const std::vector<double> neighborAreaSums =
        ComputeNeighborAreaSums(volumeMesh.BoundaryFaces, faceGeometries);

    if (complexityWeight > 0.0 && volumeMesh.RestBoundaryArea > kGeometryEpsilon) {
        for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
            const auto dofIndicesA = BuildDofIndices(volumeMesh.BoundaryFaces[faceIndex].Vertices);
            for (const int neighbor : volumeMesh.BoundaryFaces[faceIndex].Neighbors) {
                if (neighbor <= static_cast<int>(faceIndex)) {
                    continue;
                }

                const double edgeArea =
                    ComputeComplexityEdgeArea(static_cast<int>(faceIndex), neighbor, faceGeometries, neighborAreaSums);
                if (edgeArea <= 0.0) {
                    continue;
                }

                const auto dofIndicesB = BuildDofIndices(volumeMesh.BoundaryFaces[neighbor].Vertices);
                const Vec3 diff = faceGeometries[faceIndex].UnitNormal - faceGeometries[neighbor].UnitNormal;
                const double weight = complexityWeight * edgeArea / volumeMesh.RestBoundaryArea;
                const Vec9 localGradientA = 2.0 * weight *
                    faceUnitNormalJacobians[faceIndex].transpose() * diff;
                const Vec9 localGradientB = -2.0 * weight *
                    faceUnitNormalJacobians[neighbor].transpose() * diff;
                const Mat9 hessianAA = 2.0 * weight *
                    faceUnitNormalJacobians[faceIndex].transpose() * faceUnitNormalJacobians[faceIndex];
                const Mat9 hessianAB = -2.0 * weight *
                    faceUnitNormalJacobians[faceIndex].transpose() * faceUnitNormalJacobians[neighbor];
                const Mat9 hessianBA = hessianAB.transpose();
                const Mat9 hessianBB = 2.0 * weight *
                    faceUnitNormalJacobians[neighbor].transpose() * faceUnitNormalJacobians[neighbor];

                AddLocalVector(system.Gradient, dofIndicesA, localGradientA);
                AddLocalVector(system.Gradient, dofIndicesB, localGradientB);
                AddLocalMatrix(triplets, dofIndicesA, hessianAA);
                AddLocalMatrix(triplets, dofIndicesB, hessianBB);
                AddLocalMatrixBlock(triplets, dofIndicesA, dofIndicesB, hessianAB);
                AddLocalMatrixBlock(triplets, dofIndicesB, dofIndicesA, hessianBA);
            }
        }
    }

    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        const auto dofIndicesA = BuildDofIndices(volumeMesh.BoundaryFaces[faceIndex].Vertices);
        for (const int neighbor : volumeMesh.BoundaryFaces[faceIndex].Neighbors) {
            if (neighbor <= static_cast<int>(faceIndex)) {
                continue;
            }

            const auto dofIndicesB = BuildDofIndices(volumeMesh.BoundaryFaces[neighbor].Vertices);
            const double rawArgument =
                faceGeometries[faceIndex].UnitNormal.dot(faceGeometries[neighbor].UnitNormal) + 1.0;
            const double barrierArgument = std::max(rawArgument, 1e-6);
            const double invArgument = 1.0 / barrierArgument;
            const double barrierScale = kFoldoverBarrierWeight * invArgument * invArgument;

            const Vec9 argGradientA =
                faceUnitNormalJacobians[faceIndex].transpose() * faceGeometries[neighbor].UnitNormal;
            const Vec9 argGradientB =
                faceUnitNormalJacobians[neighbor].transpose() * faceGeometries[faceIndex].UnitNormal;
            const Vec9 barrierGradientA = -kFoldoverBarrierWeight * invArgument * argGradientA;
            const Vec9 barrierGradientB = -kFoldoverBarrierWeight * invArgument * argGradientB;

            AddLocalVector(system.Gradient, dofIndicesA, barrierGradientA);
            AddLocalVector(system.Gradient, dofIndicesB, barrierGradientB);

            const Mat9 hessianAA = barrierScale * (argGradientA * argGradientA.transpose());
            const Mat9 hessianAB = barrierScale * (argGradientA * argGradientB.transpose());
            const Mat9 hessianBA = hessianAB.transpose();
            const Mat9 hessianBB = barrierScale * (argGradientB * argGradientB.transpose());

            AddLocalMatrix(triplets, dofIndicesA, hessianAA);
            AddLocalMatrix(triplets, dofIndicesB, hessianBB);
            AddLocalMatrixBlock(triplets, dofIndicesA, dofIndicesB, hessianAB);
            AddLocalMatrixBlock(triplets, dofIndicesB, dofIndicesA, hessianBA);
        }
    }
    system.ConstraintValue -= volumeMesh.RestBoundaryArea;

    for (const TetCell& tet : volumeMesh.Tets) {
        const auto currentTet = GatherTet(volumeMesh, tet, true);

        Mat3 currentDs = Mat3::Zero();
        currentDs.col(0) = currentTet[1] - currentTet[0];
        currentDs.col(1) = currentTet[2] - currentTet[0];
        currentDs.col(2) = currentTet[3] - currentTet[0];

        const Mat3 deformation = currentDs * tet.RestDmInv;
        const Mat3 rotation = PolycubeInternal::NearestRotation(deformation);
        const Vec9 residual = FlattenMatrix(deformation - rotation);

        const double weight = tet.RestVolume / std::max(volumeMesh.TotalRestVolume, kGeometryEpsilon);
        const Vec12 localGradient = weight * tet.GradF.transpose() * residual;
        const Mat12 localHessian = weight * tet.GradF.transpose() * tet.GradF;

        AddLocalVector(system.Gradient, BuildDofIndices(tet.Vertices), localGradient);
        AddLocalMatrix(triplets, BuildDofIndices(tet.Vertices), localHessian);
    }

    for (int dof = 0; dof < dofCount; ++dof) {
        triplets.emplace_back(dof, dof, kKktDiagonalRegularization);
    }

    system.Hessian.resize(dofCount, dofCount);
    system.Hessian.setFromTriplets(triplets.begin(), triplets.end());
    return system;
}

Eigen::VectorXd SolveKktStep(const LinearizedSystem& system) {
    SparseMatrix hessian = system.Hessian;
    Eigen::SimplicialLDLT<SparseMatrix> solver;

    double regularization = kKktDiagonalRegularization;
    for (int attempt = 0; attempt < 6; ++attempt) {
        SparseMatrix regularized = hessian;
        for (int dof = 0; dof < regularized.rows(); ++dof) {
            regularized.coeffRef(dof, dof) += regularization;
        }
        regularized.makeCompressed();

        solver.compute(regularized);
        if (solver.info() == Eigen::Success) {
            const Eigen::VectorXd y = solver.solve(system.ConstraintGradient);
            const Eigen::VectorXd z = solver.solve(-system.Gradient);
            if (solver.info() != Eigen::Success) {
                break;
            }

            const double denom = system.ConstraintGradient.dot(y);
            double lambda = 0.0;
            if (std::abs(denom) > 1e-12) {
                lambda = (-system.ConstraintValue - system.ConstraintGradient.dot(z)) / denom;
            }
            return z - y * lambda;
        }
        regularization *= 10.0;
    }

    throw std::runtime_error("Failed to factorize the polycube KKT system.");
}

Vec3 ComputeCentroid(const std::vector<Vec3>& positions) {
    Vec3 centroid = Vec3::Zero();
    if (positions.empty()) {
        return centroid;
    }
    for (const Vec3& position : positions) {
        centroid += position;
    }
    return centroid / static_cast<double>(positions.size());
}

Vec3 AxisVectorFromLabel(int label) {
    switch (label) {
    case 0: return Vec3(1.0, 0.0, 0.0);
    case 1: return Vec3(-1.0, 0.0, 0.0);
    case 2: return Vec3(0.0, 1.0, 0.0);
    case 3: return Vec3(0.0, -1.0, 0.0);
    case 4: return Vec3(0.0, 0.0, 1.0);
    default: return Vec3(0.0, 0.0, -1.0);
    }
}

Mat3 ComputeGlobalRotation(const VolumeMesh& volumeMesh) {
    Mat3 covariance = Mat3::Zero();
    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        const auto triangle = GatherFace(volumeMesh, face, true);
        const PolycubeInternal::FaceGeometry geometry = PolycubeInternal::ComputeFaceGeometry(triangle);
        if (geometry.Area <= kGeometryEpsilon) {
            continue;
        }
        const int label = PolycubeInternal::DominantAxisLabel(geometry.UnitNormal);
        covariance += geometry.Area * AxisVectorFromLabel(label) * geometry.UnitNormal.transpose();
    }
    if (covariance.norm() <= kGeometryEpsilon) {
        return Mat3::Identity();
    }
    return PolycubeInternal::NearestRotation(covariance);
}

void ApplyGlobalRotation(VolumeMesh& volumeMesh) {
    const Mat3 rotation = ComputeGlobalRotation(volumeMesh);
    if ((rotation - Mat3::Identity()).norm() <= 1e-9) {
        return;
    }

    const Vec3 centroid = ComputeCentroid(volumeMesh.CurrentPositions);
    for (Vec3& position : volumeMesh.CurrentPositions) {
        position = centroid + rotation * (position - centroid);
    }
}

VolumeMesh BuildVolumeMesh(const tetgenio& output) {
    VolumeMesh volumeMesh;
    volumeMesh.RestPositions.reserve(output.numberofpoints);
    volumeMesh.CurrentPositions.reserve(output.numberofpoints);
    for (int pointIndex = 0; pointIndex < output.numberofpoints; ++pointIndex) {
        const int offset = pointIndex * 3;
        const Vec3 point(
            output.pointlist[offset + 0],
            output.pointlist[offset + 1],
            output.pointlist[offset + 2]
        );
        volumeMesh.RestPositions.push_back(point);
        volumeMesh.CurrentPositions.push_back(point);
    }

    volumeMesh.Tets.reserve(output.numberoftetrahedra);
    for (int tetIndex = 0; tetIndex < output.numberoftetrahedra; ++tetIndex) {
        TetCell tet;
        for (int local = 0; local < 4; ++local) {
            tet.Vertices[local] = output.tetrahedronlist[tetIndex * output.numberofcorners + local];
        }

        auto tetPoints = std::array<Vec3, 4>{
            volumeMesh.RestPositions[tet.Vertices[0]],
            volumeMesh.RestPositions[tet.Vertices[1]],
            volumeMesh.RestPositions[tet.Vertices[2]],
            volumeMesh.RestPositions[tet.Vertices[3]]
        };

        double volume = PolycubeInternal::SignedTetVolume(tetPoints);
        if (volume < 0.0) {
            std::swap(tet.Vertices[0], tet.Vertices[1]);
            tetPoints = {
                volumeMesh.RestPositions[tet.Vertices[0]],
                volumeMesh.RestPositions[tet.Vertices[1]],
                volumeMesh.RestPositions[tet.Vertices[2]],
                volumeMesh.RestPositions[tet.Vertices[3]]
            };
            volume = PolycubeInternal::SignedTetVolume(tetPoints);
        }
        if (volume <= kGeometryEpsilon) {
            throw std::runtime_error("TetGen produced a degenerate tetrahedron.");
        }

        Mat3 restDm = Mat3::Zero();
        restDm.col(0) = tetPoints[1] - tetPoints[0];
        restDm.col(1) = tetPoints[2] - tetPoints[0];
        restDm.col(2) = tetPoints[3] - tetPoints[0];

        tet.RestVolume = volume;
        tet.RestDmInv = restDm.inverse();
        tet.GradF = BuildTetGradient(tet.RestDmInv);
        volumeMesh.TotalRestVolume += tet.RestVolume;
        volumeMesh.Tets.push_back(tet);
    }

    struct FaceRecord {
        BoundaryFace Face;
        int Count = 0;
    };

    std::unordered_map<std::uint64_t, FaceRecord> faces;
    faces.reserve(volumeMesh.Tets.size() * 2);

    const std::array<std::array<int, 3>, 4> localFaces = {
        std::array<int, 3>{ 1, 2, 3 },
        std::array<int, 3>{ 0, 3, 2 },
        std::array<int, 3>{ 0, 1, 3 },
        std::array<int, 3>{ 0, 2, 1 }
    };

    for (std::size_t tetIndex = 0; tetIndex < volumeMesh.Tets.size(); ++tetIndex) {
        const TetCell& tet = volumeMesh.Tets[tetIndex];
        for (int opposite = 0; opposite < 4; ++opposite) {
            BoundaryFace boundaryFace;
            boundaryFace.TetIndex = static_cast<int>(tetIndex);
            boundaryFace.OppositeVertex = tet.Vertices[opposite];
            boundaryFace.Vertices = {
                tet.Vertices[localFaces[opposite][0]],
                tet.Vertices[localFaces[opposite][1]],
                tet.Vertices[localFaces[opposite][2]]
            };

            const Vec3& p0 = volumeMesh.RestPositions[boundaryFace.Vertices[0]];
            const Vec3& p1 = volumeMesh.RestPositions[boundaryFace.Vertices[1]];
            const Vec3& p2 = volumeMesh.RestPositions[boundaryFace.Vertices[2]];
            const Vec3& oppositePoint = volumeMesh.RestPositions[boundaryFace.OppositeVertex];

            const Vec3 normal = (p1 - p0).cross(p2 - p0);
            if (normal.dot(oppositePoint - p0) > 0.0) {
                std::swap(boundaryFace.Vertices[1], boundaryFace.Vertices[2]);
            }

            FaceRecord& record = faces[EncodeFace(boundaryFace.Vertices)];
            if (record.Count == 0) {
                record.Face = boundaryFace;
            }
            record.Count += 1;
        }
    }

    for (const auto& [_, record] : faces) {
        if (record.Count == 1) {
            volumeMesh.BoundaryFaces.push_back(record.Face);
        }
    }
    if (volumeMesh.BoundaryFaces.empty()) {
        throw std::runtime_error("TetGen produced no boundary faces.");
    }

    std::vector<std::array<int, 3>> boundaryTriangles;
    boundaryTriangles.reserve(volumeMesh.BoundaryFaces.size());
    for (BoundaryFace& face : volumeMesh.BoundaryFaces) {
        boundaryTriangles.push_back(face.Vertices);
        const PolycubeInternal::FaceGeometry geometry =
            PolycubeInternal::ComputeFaceGeometry(GatherFace(volumeMesh, face, false));
        volumeMesh.RestBoundaryArea += geometry.Area;
    }

    const std::vector<std::vector<int>> adjacency = BuildBoundaryAdjacency(boundaryTriangles);
    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        volumeMesh.BoundaryFaces[faceIndex].Neighbors = adjacency[faceIndex];
    }

    return volumeMesh;
}

VolumeMesh TetrahedralizeSurface(const HE_MeshData& mesh, const PolycubeOptions& options) {
    const std::vector<Vec3> positions = ExtractSurfacePositions(mesh);
    const std::vector<std::array<int, 3>> triangles = ExtractSurfaceTriangles(mesh);
    const double averageEdgeLength = std::max(ComputeAverageEdgeLength(positions, triangles), 1e-3);
    const double spacing = averageEdgeLength * std::max(0.25f, options.TetSpacingScale);
    const double edgeDrivenVolume = std::max(1e-6, spacing * spacing * spacing / 6.0);
    const int targetTetCount = ComputeTargetTetCount(triangles.size(), options.TetSpacingScale);
    const double enclosedVolume = std::max(ComputeEnclosedVolume(positions, triangles), 1e-6);
    double maxTetVolume = std::max(edgeDrivenVolume, enclosedVolume / static_cast<double>(targetTetCount));

    for (int attempt = 0; attempt < kMaxTetgenAttempts; ++attempt) {
        tetgenio input;
        PopulateTetGenInput(input, positions, triangles);

        tetgenio output;
        std::ostringstream switchStream;
        switchStream << "pq1.4a" << maxTetVolume << "Q";
        std::string switches = switchStream.str();
        std::vector<char> mutableSwitches(switches.begin(), switches.end());
        mutableSwitches.push_back('\0');

        try {
            tetrahedralize(mutableSwitches.data(), &input, &output);
        } catch (int errorCode) {
            std::ostringstream oss;
            oss << "TetGen failed with error code " << errorCode << '.';
            throw std::runtime_error(oss.str());
        }

        if (output.numberoftetrahedra <= 0) {
            throw std::runtime_error("TetGen returned an empty tetrahedral mesh.");
        }

        const int acceptableTetCount = targetTetCount + targetTetCount / 4;
        if (output.numberoftetrahedra > acceptableTetCount && attempt + 1 < kMaxTetgenAttempts) {
            maxTetVolume *= 2.0;
            continue;
        }

        return BuildVolumeMesh(output);
    }

    throw std::runtime_error("TetGen could not produce an acceptable tetrahedral mesh.");
}

std::vector<int> LabelBoundaryFaces(const VolumeMesh& volumeMesh) {
    std::vector<int> labels;
    labels.reserve(volumeMesh.BoundaryFaces.size());
    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        const PolycubeInternal::FaceGeometry geometry =
            PolycubeInternal::ComputeFaceGeometry(GatherFace(volumeMesh, face, true));
        labels.push_back(PolycubeInternal::DominantAxisLabel(geometry.UnitNormal));
    }
    return labels;
}

int CountPatchComponents(
    const std::vector<std::array<int, 3>>& faces,
    const std::vector<int>& labels
) {
    if (faces.empty()) {
        return 0;
    }
    const PatchComponents components = BuildPatchComponents(labels, BuildBoundaryAdjacency(faces));
    return static_cast<int>(components.ComponentFaces.size());
}

int AxisIndexFromLabel(int label) {
    return std::clamp(label / 2, 0, 2);
}

Eigen::VectorXd FlattenPositions(const std::vector<Vec3>& positions) {
    Eigen::VectorXd values(static_cast<int>(positions.size() * 3));
    for (std::size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
        values(vertexIndex * 3 + 0) = positions[vertexIndex].x();
        values(vertexIndex * 3 + 1) = positions[vertexIndex].y();
        values(vertexIndex * 3 + 2) = positions[vertexIndex].z();
    }
    return values;
}

void UnflattenPositions(const Eigen::VectorXd& values, std::vector<Vec3>& positions) {
    for (std::size_t vertexIndex = 0; vertexIndex < positions.size(); ++vertexIndex) {
        positions[vertexIndex].x() = values(vertexIndex * 3 + 0);
        positions[vertexIndex].y() = values(vertexIndex * 3 + 1);
        positions[vertexIndex].z() = values(vertexIndex * 3 + 2);
    }
}

std::vector<bool> BuildBoundaryVertexMask(const VolumeMesh& volumeMesh) {
    std::vector<bool> isBoundary(volumeMesh.CurrentPositions.size(), false);
    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        for (const int vertex : face.Vertices) {
            if (vertex >= 0 && vertex < static_cast<int>(isBoundary.size())) {
                isBoundary[vertex] = true;
            }
        }
    }
    return isBoundary;
}

std::vector<std::vector<int>> BuildVolumeVertexAdjacency(const VolumeMesh& volumeMesh) {
    std::vector<std::unordered_set<int>> neighbors(volumeMesh.CurrentPositions.size());
    for (const TetCell& tet : volumeMesh.Tets) {
        for (int i = 0; i < 4; ++i) {
            for (int j = i + 1; j < 4; ++j) {
                neighbors[tet.Vertices[i]].insert(tet.Vertices[j]);
                neighbors[tet.Vertices[j]].insert(tet.Vertices[i]);
            }
        }
    }

    std::vector<std::vector<int>> adjacency(neighbors.size());
    for (std::size_t vertexIndex = 0; vertexIndex < neighbors.size(); ++vertexIndex) {
        adjacency[vertexIndex].assign(neighbors[vertexIndex].begin(), neighbors[vertexIndex].end());
        std::sort(adjacency[vertexIndex].begin(), adjacency[vertexIndex].end());
    }
    return adjacency;
}

std::vector<std::vector<int>> BuildBoundaryVertexAdjacency(const VolumeMesh& volumeMesh) {
    std::vector<std::unordered_set<int>> neighbors(volumeMesh.CurrentPositions.size());
    for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
        const std::array<std::pair<int, int>, 3> edges = {
            std::make_pair(face.Vertices[0], face.Vertices[1]),
            std::make_pair(face.Vertices[1], face.Vertices[2]),
            std::make_pair(face.Vertices[2], face.Vertices[0])
        };
        for (const auto& [a, b] : edges) {
            neighbors[a].insert(b);
            neighbors[b].insert(a);
        }
    }

    std::vector<std::vector<int>> adjacency(neighbors.size());
    for (std::size_t vertexIndex = 0; vertexIndex < neighbors.size(); ++vertexIndex) {
        adjacency[vertexIndex].assign(neighbors[vertexIndex].begin(), neighbors[vertexIndex].end());
        std::sort(adjacency[vertexIndex].begin(), adjacency[vertexIndex].end());
    }
    return adjacency;
}

struct DofConstraints {
    std::vector<bool> Fixed;
    Eigen::VectorXd Values;
};

DofConstraints BuildPatchPlaneConstraints(const VolumeMesh& volumeMesh, const std::vector<int>& labels) {
    DofConstraints constraints;
    const int dofCount = static_cast<int>(volumeMesh.CurrentPositions.size() * 3);
    constraints.Fixed.assign(dofCount, false);
    constraints.Values = Eigen::VectorXd::Zero(dofCount);

    std::vector<std::vector<int>> vertexFaceAdjacency(volumeMesh.BoundaryFaces.size());
    std::vector<std::vector<int>> vertexToFaces(volumeMesh.CurrentPositions.size());
    for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
        for (const int vertex : volumeMesh.BoundaryFaces[faceIndex].Vertices) {
            vertexToFaces[vertex].push_back(static_cast<int>(faceIndex));
        }
    }

    for (const std::vector<int>& incidentFaces : vertexToFaces) {
        for (std::size_t i = 0; i < incidentFaces.size(); ++i) {
            for (std::size_t j = i + 1; j < incidentFaces.size(); ++j) {
                vertexFaceAdjacency[incidentFaces[i]].push_back(incidentFaces[j]);
                vertexFaceAdjacency[incidentFaces[j]].push_back(incidentFaces[i]);
            }
        }
    }
    for (auto& neighbors : vertexFaceAdjacency) {
        std::sort(neighbors.begin(), neighbors.end());
        neighbors.erase(std::unique(neighbors.begin(), neighbors.end()), neighbors.end());
    }

    const PatchComponents components = BuildPatchComponents(labels, vertexFaceAdjacency);
    std::vector<std::array<double, 3>> valueSums(volumeMesh.CurrentPositions.size(), { 0.0, 0.0, 0.0 });
    std::vector<std::array<int, 3>> valueCounts(volumeMesh.CurrentPositions.size(), { 0, 0, 0 });

    for (const std::vector<int>& componentFaces : components.ComponentFaces) {
        if (componentFaces.empty()) {
            continue;
        }

        const int axis = AxisIndexFromLabel(labels[componentFaces.front()]);
        std::unordered_set<int> patchVertices;
        for (const int faceIndex : componentFaces) {
            const BoundaryFace& face = volumeMesh.BoundaryFaces[faceIndex];
            patchVertices.insert(face.Vertices[0]);
            patchVertices.insert(face.Vertices[1]);
            patchVertices.insert(face.Vertices[2]);
        }
        if (patchVertices.empty()) {
            continue;
        }

        double coordinateSum = 0.0;
        for (const int vertex : patchVertices) {
            coordinateSum += volumeMesh.CurrentPositions[vertex][axis];
        }
        const double patchCoordinate = coordinateSum / static_cast<double>(patchVertices.size());

        for (const int vertex : patchVertices) {
            valueSums[vertex][axis] += patchCoordinate;
            valueCounts[vertex][axis] += 1;
        }
    }

    for (std::size_t vertexIndex = 0; vertexIndex < valueSums.size(); ++vertexIndex) {
        for (int axis = 0; axis < 3; ++axis) {
            if (valueCounts[vertexIndex][axis] <= 0) {
                continue;
            }
            const int dof = static_cast<int>(vertexIndex * 3 + axis);
            constraints.Fixed[dof] = true;
            constraints.Values(dof) =
                valueSums[vertexIndex][axis] / static_cast<double>(valueCounts[vertexIndex][axis]);
        }
    }

    return constraints;
}

SparseMatrix BuildExtractionInitializationSystem(
    const VolumeMesh& volumeMesh,
    const std::vector<bool>& isBoundary,
    const std::vector<std::vector<int>>& volumeAdjacency,
    const std::vector<std::vector<int>>& boundaryAdjacency
) {
    std::vector<Triplet> triplets;
    triplets.reserve(volumeMesh.CurrentPositions.size() * 16);

    for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
        const std::vector<int>& neighbors = isBoundary[vertexIndex] ? boundaryAdjacency[vertexIndex] : volumeAdjacency[vertexIndex];
        if (neighbors.empty()) {
            triplets.emplace_back(static_cast<int>(vertexIndex), static_cast<int>(vertexIndex), 1.0);
            continue;
        }

        std::vector<int> rowIndices;
        std::vector<double> coefficients;
        rowIndices.reserve(neighbors.size() + 1);
        coefficients.reserve(neighbors.size() + 1);

        rowIndices.push_back(static_cast<int>(vertexIndex));
        coefficients.push_back(1.0);
        const double invDegree = 1.0 / static_cast<double>(neighbors.size());
        for (const int neighbor : neighbors) {
            rowIndices.push_back(neighbor);
            coefficients.push_back(-invDegree);
        }

        for (std::size_t row = 0; row < rowIndices.size(); ++row) {
            for (std::size_t col = 0; col < rowIndices.size(); ++col) {
                triplets.emplace_back(
                    rowIndices[row],
                    rowIndices[col],
                    coefficients[row] * coefficients[col]
                );
            }
        }
    }

    for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
        triplets.emplace_back(static_cast<int>(vertexIndex), static_cast<int>(vertexIndex), kKktDiagonalRegularization);
    }

    SparseMatrix system(static_cast<int>(volumeMesh.CurrentPositions.size()), static_cast<int>(volumeMesh.CurrentPositions.size()));
    system.setFromTriplets(triplets.begin(), triplets.end());
    return system;
}

bool SolveConstrainedSpdSystem(
    const SparseMatrix& systemMatrix,
    const Eigen::VectorXd& rhs,
    const std::vector<bool>& fixedMask,
    const Eigen::VectorXd& fixedValues,
    Eigen::VectorXd& solution
) {
    const int size = systemMatrix.rows();
    if (systemMatrix.cols() != size ||
        rhs.size() != size ||
        fixedValues.size() != size ||
        static_cast<int>(fixedMask.size()) != size) {
        return false;
    }

    std::vector<int> freeToGlobal;
    freeToGlobal.reserve(size);
    std::vector<int> globalToFree(size, -1);
    for (int index = 0; index < size; ++index) {
        if (fixedMask[index]) {
            continue;
        }
        globalToFree[index] = static_cast<int>(freeToGlobal.size());
        freeToGlobal.push_back(index);
    }

    solution = rhs;
    for (int index = 0; index < size; ++index) {
        if (fixedMask[index]) {
            solution(index) = fixedValues(index);
        }
    }
    if (freeToGlobal.empty()) {
        return true;
    }

    std::vector<Triplet> reducedTriplets;
    reducedTriplets.reserve(static_cast<std::size_t>(systemMatrix.nonZeros()));
    Eigen::VectorXd reducedRhs = Eigen::VectorXd::Zero(static_cast<int>(freeToGlobal.size()));

    for (int freeIndex = 0; freeIndex < static_cast<int>(freeToGlobal.size()); ++freeIndex) {
        reducedRhs(freeIndex) = rhs(freeToGlobal[freeIndex]);
    }

    for (int column = 0; column < systemMatrix.outerSize(); ++column) {
        for (SparseMatrix::InnerIterator it(systemMatrix, column); it; ++it) {
            const int row = it.row();
            const int col = it.col();
            const double value = it.value();
            if (fixedMask[row]) {
                continue;
            }

            const int reducedRow = globalToFree[row];
            if (fixedMask[col]) {
                reducedRhs(reducedRow) -= value * fixedValues(col);
                continue;
            }

            reducedTriplets.emplace_back(reducedRow, globalToFree[col], value);
        }
    }

    SparseMatrix reducedMatrix(static_cast<int>(freeToGlobal.size()), static_cast<int>(freeToGlobal.size()));
    reducedMatrix.setFromTriplets(reducedTriplets.begin(), reducedTriplets.end());
    Eigen::SimplicialLDLT<SparseMatrix> solver;
    solver.compute(reducedMatrix);
    if (solver.info() != Eigen::Success) {
        return false;
    }

    const Eigen::VectorXd reducedSolution = solver.solve(reducedRhs);
    if (solver.info() != Eigen::Success) {
        return false;
    }

    for (int freeIndex = 0; freeIndex < static_cast<int>(freeToGlobal.size()); ++freeIndex) {
        solution(freeToGlobal[freeIndex]) = reducedSolution(freeIndex);
    }
    return true;
}

bool RunBoundaryExtractionInitialization(VolumeMesh& volumeMesh, const DofConstraints& constraints) {
    const std::vector<bool> isBoundary = BuildBoundaryVertexMask(volumeMesh);
    const std::vector<std::vector<int>> volumeAdjacency = BuildVolumeVertexAdjacency(volumeMesh);
    const std::vector<std::vector<int>> boundaryAdjacency = BuildBoundaryVertexAdjacency(volumeMesh);
    const SparseMatrix initializationSystem = BuildExtractionInitializationSystem(
        volumeMesh,
        isBoundary,
        volumeAdjacency,
        boundaryAdjacency
    );

    std::array<Eigen::VectorXd, 3> coordinates = {
        Eigen::VectorXd::Zero(static_cast<int>(volumeMesh.CurrentPositions.size())),
        Eigen::VectorXd::Zero(static_cast<int>(volumeMesh.CurrentPositions.size())),
        Eigen::VectorXd::Zero(static_cast<int>(volumeMesh.CurrentPositions.size()))
    };

    for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
        coordinates[0](static_cast<int>(vertexIndex)) = volumeMesh.CurrentPositions[vertexIndex].x();
        coordinates[1](static_cast<int>(vertexIndex)) = volumeMesh.CurrentPositions[vertexIndex].y();
        coordinates[2](static_cast<int>(vertexIndex)) = volumeMesh.CurrentPositions[vertexIndex].z();
    }

    for (int axis = 0; axis < 3; ++axis) {
        std::vector<bool> fixedMask(volumeMesh.CurrentPositions.size(), false);
        Eigen::VectorXd fixedValues = Eigen::VectorXd::Zero(static_cast<int>(volumeMesh.CurrentPositions.size()));
        for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
            const int dof = static_cast<int>(vertexIndex * 3 + axis);
            fixedMask[vertexIndex] = constraints.Fixed[dof];
            fixedValues(static_cast<int>(vertexIndex)) = constraints.Values(dof);
        }

        if (!SolveConstrainedSpdSystem(
                initializationSystem,
                Eigen::VectorXd::Zero(static_cast<int>(volumeMesh.CurrentPositions.size())),
                fixedMask,
                fixedValues,
                coordinates[axis])) {
            return false;
        }
    }

    for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
        volumeMesh.CurrentPositions[vertexIndex] = Vec3(
            coordinates[0](static_cast<int>(vertexIndex)),
            coordinates[1](static_cast<int>(vertexIndex)),
            coordinates[2](static_cast<int>(vertexIndex))
        );
    }
    return true;
}

SparseMatrix BuildFinalArapSystem(const VolumeMesh& volumeMesh, Eigen::VectorXd& rhs) {
    const int dofCount = static_cast<int>(volumeMesh.CurrentPositions.size() * 3);
    rhs = Eigen::VectorXd::Zero(dofCount);
    std::vector<Triplet> triplets;
    triplets.reserve(
        static_cast<std::size_t>(volumeMesh.Tets.size()) * 144u +
        static_cast<std::size_t>(dofCount)
    );

    for (const TetCell& tet : volumeMesh.Tets) {
        const auto currentTet = GatherTet(volumeMesh, tet, true);
        Mat3 currentDs = Mat3::Zero();
        currentDs.col(0) = currentTet[1] - currentTet[0];
        currentDs.col(1) = currentTet[2] - currentTet[0];
        currentDs.col(2) = currentTet[3] - currentTet[0];

        const Mat3 deformation = currentDs * tet.RestDmInv;
        const Mat3 rotation = PolycubeInternal::NearestRotation(deformation);
        const double weight = tet.RestVolume / std::max(volumeMesh.TotalRestVolume, kGeometryEpsilon);
        const auto dofIndices = BuildDofIndices(tet.Vertices);
        const Mat12 localHessian = weight * tet.GradF.transpose() * tet.GradF;
        const Vec12 localRhs = weight * tet.GradF.transpose() * FlattenMatrix(rotation);

        AddLocalMatrix(triplets, dofIndices, localHessian);
        AddLocalVector(rhs, dofIndices, localRhs);
    }

    for (int dof = 0; dof < dofCount; ++dof) {
        triplets.emplace_back(dof, dof, kKktDiagonalRegularization);
    }

    SparseMatrix system(dofCount, dofCount);
    system.setFromTriplets(triplets.begin(), triplets.end());
    return system;
}

bool RunFinalArapRefinement(
    VolumeMesh& volumeMesh,
    const DofConstraints& constraints,
    double complexityWeight
) {
    const Eigen::VectorXd fixedValues = constraints.Values;
    Eigen::VectorXd flattened = FlattenPositions(volumeMesh.CurrentPositions);
    std::vector<Vec3> bestPositions = volumeMesh.CurrentPositions;

    for (int iteration = 0; iteration < kFinalArapIterations; ++iteration) {
        Eigen::VectorXd rhs;
        const SparseMatrix system = BuildFinalArapSystem(volumeMesh, rhs);
        Eigen::VectorXd candidate = flattened;
        if (!SolveConstrainedSpdSystem(system, rhs, constraints.Fixed, fixedValues, candidate)) {
            break;
        }

        std::vector<Vec3> previous = volumeMesh.CurrentPositions;
        UnflattenPositions(candidate, volumeMesh.CurrentPositions);
        const StateMetrics metrics = EvaluateState(volumeMesh, 0.0, 1.0, complexityWeight);
        if (metrics.MinTetVolume <= 1e-10 || metrics.MinBarrierArgument <= 1e-6) {
            volumeMesh.CurrentPositions = previous;
            break;
        }

        bestPositions = volumeMesh.CurrentPositions;
        flattened = candidate;
    }

    volumeMesh.CurrentPositions = bestPositions;
    return true;
}

void ApplyDirectConstraintProjection(VolumeMesh& volumeMesh, const DofConstraints& constraints) {
    for (std::size_t vertexIndex = 0; vertexIndex < volumeMesh.CurrentPositions.size(); ++vertexIndex) {
        for (int axis = 0; axis < 3; ++axis) {
            const int dof = static_cast<int>(vertexIndex * 3 + axis);
            if (!constraints.Fixed[dof]) {
                continue;
            }
            volumeMesh.CurrentPositions[vertexIndex][axis] = constraints.Values(dof);
        }
    }
}

bool IsValidExtractionState(const VolumeMesh& volumeMesh, double complexityWeight) {
    const StateMetrics metrics = EvaluateState(volumeMesh, 0.0, 1.0, complexityWeight);
    return metrics.MinTetVolume > 1e-10 && metrics.MinBarrierArgument > 1e-6;
}

bool ExtractFinalBoundaryPolycube(
    VolumeMesh& volumeMesh,
    const std::vector<int>& labels,
    double complexityWeight
) {
    const DofConstraints constraints = BuildPatchPlaneConstraints(volumeMesh, labels);
    if (constraints.Fixed.empty()) {
        return false;
    }

    ApplyDirectConstraintProjection(volumeMesh, constraints);
    std::vector<Vec3> extractedPositions = volumeMesh.CurrentPositions;

    if (IsValidExtractionState(volumeMesh, complexityWeight)) {
        std::vector<Vec3> candidatePositions = volumeMesh.CurrentPositions;
        if (RunBoundaryExtractionInitialization(volumeMesh, constraints) &&
            IsValidExtractionState(volumeMesh, complexityWeight)) {
            candidatePositions = volumeMesh.CurrentPositions;
        }
        volumeMesh.CurrentPositions = candidatePositions;

        RunFinalArapRefinement(volumeMesh, constraints, complexityWeight);
        if (IsValidExtractionState(volumeMesh, complexityWeight)) {
            extractedPositions = volumeMesh.CurrentPositions;
        }
    }

    volumeMesh.CurrentPositions = extractedPositions;
    return true;
}

std::string BuildSummary(const PolycubeStats& stats) {
    std::ostringstream oss;
    oss.setf(std::ios::fixed);
    oss.precision(5);
    oss << "Polycube completed. tets=" << stats.TetCount
        << ", boundaryFaces=" << stats.BoundaryFaceCount
        << ", patches=" << stats.InitialPatchCount << "->" << stats.FinalPatchCount
        << ", normalizedError=" << stats.NormalizedError
        << ", minTetVolume=" << stats.MinTetVolume;
    return oss.str();
}

} // namespace

std::vector<int> PolycubeInternal::CleanupPatchLabels(
    const std::vector<std::array<int, 3>>& faces,
    const std::vector<Eigen::Vector3d>& positions,
    const std::vector<int>& initialLabels
) {
    return CleanupPatchLabelsDetailed(faces, positions, initialLabels).Labels;
}

PolycubeResult Polycube::Generate(HE_MeshData& mesh, const PolycubeOptions& options) {
    PolycubeResult result;

    try {
        const double initialAlpha = std::max(1e-4, static_cast<double>(options.InitialAlpha));
        const double complexityWeight = std::max(0.0, static_cast<double>(options.ComplexityWeight));
        const double alphaMultiplier = std::max(1.0, static_cast<double>(options.AlphaMultiplier));
        const double initialEpsilon = std::max(1e-4, static_cast<double>(options.InitialEpsilon));
        const double epsilonDecay = std::clamp(static_cast<double>(options.EpsilonDecay), 0.1, 1.0);
        const double minEpsilon = std::clamp(static_cast<double>(options.MinEpsilon), 1e-4, initialEpsilon);
        const double targetNormalizedError =
            std::max(1e-5, static_cast<double>(options.TargetNormalizedError));
        const int maxOuterStages = std::max(1, options.MaxOuterStages);
        const int maxInnerIterations = std::max(1, options.MaxInnerIterations);

        result.StageReached = PolycubeStage::Preprocess;
        ReportProgress(options, PolycubeProgress{
            result.StageReached,
            result.PreviewSource,
            result.Stats,
            "Validating manifold input."
        });

        std::string manifoldError;
        if (!mesh.IsClosedTwoManifold(&manifoldError)) {
            result.Error = "Polycube requires a closed two-manifold triangle mesh. " + manifoldError;
            return result;
        }

        mesh.RepairNormal();
        mesh.GetVertexesNormal();

        VolumeMesh volumeMesh = TetrahedralizeSurface(mesh, options);
        result.StageReached = PolycubeStage::Tetrahedralize;
        result.Stats.TetCount = static_cast<int>(volumeMesh.Tets.size());
        result.Stats.BoundaryFaceCount = static_cast<int>(volumeMesh.BoundaryFaces.size());
        result.BoundaryPreviewMesh = BuildPreviewMesh(volumeMesh);
        result.PreviewApplied = true;
        result.PreviewSource = PolycubePreviewSource::TetrahedralBoundary;
        ReportProgress(options, PolycubeProgress{
            result.StageReached,
            result.PreviewSource,
            result.Stats,
            "Tetrahedralization completed."
        });

        double alpha = initialAlpha;
        double epsilon = initialEpsilon;
        bool anyAcceptedOptimizationStep = false;

        StateMetrics currentMetrics = EvaluateState(volumeMesh, alpha, epsilon, complexityWeight);
        const double initialNormalizedError =
            std::max(1e-6, currentMetrics.NormalizedError(volumeMesh.RestBoundaryArea));

        result.StageReached = PolycubeStage::Optimize;
        for (int outerStage = 0; outerStage < maxOuterStages; ++outerStage) {
            result.Stats.OuterStages = outerStage + 1;
            {
                std::ostringstream status;
                status << "Optimizing stage " << (outerStage + 1)
                       << "/" << maxOuterStages
                       << " (alpha=" << alpha << ", epsilon=" << epsilon << ").";
                ReportProgress(options, PolycubeProgress{
                    result.StageReached,
                    result.PreviewSource,
                    result.Stats,
                    status.str()
                });
            }
            ApplyGlobalRotation(volumeMesh);

            std::deque<double> recentErrors;
            for (int innerIteration = 0; innerIteration < maxInnerIterations; ++innerIteration) {
                const LinearizedSystem system = AssembleSystem(volumeMesh, alpha, epsilon, complexityWeight);
                const Eigen::VectorXd step = SolveKktStep(system);

                if (step.norm() <= 1e-8) {
                    break;
                }

                const StateMetrics referenceMetrics = EvaluateState(volumeMesh, alpha, epsilon, complexityWeight);
                const double referenceMerit =
                    referenceMetrics.Merit(volumeMesh.RestBoundaryArea, alpha, complexityWeight);

                bool accepted = false;
                double lineStep = 1.0;
                std::vector<Vec3> candidatePositions = volumeMesh.CurrentPositions;
                StateMetrics candidateMetrics;

                while (lineStep >= kMinLineSearchStep) {
                    candidatePositions = volumeMesh.CurrentPositions;
                    for (std::size_t vertexIndex = 0; vertexIndex < candidatePositions.size(); ++vertexIndex) {
                        candidatePositions[vertexIndex].x() += lineStep * step(vertexIndex * 3 + 0);
                        candidatePositions[vertexIndex].y() += lineStep * step(vertexIndex * 3 + 1);
                        candidatePositions[vertexIndex].z() += lineStep * step(vertexIndex * 3 + 2);
                    }

                    const std::vector<Vec3> previous = volumeMesh.CurrentPositions;
                    volumeMesh.CurrentPositions = candidatePositions;
                    candidateMetrics = EvaluateState(volumeMesh, alpha, epsilon, complexityWeight);
                    const double candidateMerit =
                        candidateMetrics.Merit(volumeMesh.RestBoundaryArea, alpha, complexityWeight);
                    const bool valid =
                        candidateMetrics.MinTetVolume > 1e-10 &&
                        candidateMetrics.MinBarrierArgument > 1e-6;
                    const bool improved = candidateMerit <= referenceMerit;
                    volumeMesh.CurrentPositions = previous;

                    if (valid && improved) {
                        accepted = true;
                        break;
                    }
                    lineStep *= 0.5;
                }

                if (!accepted) {
                    if (!CollapseSkinnyBoundaryTriangles(volumeMesh)) {
                        break;
                    }
                    result.Stats.TetCount = static_cast<int>(volumeMesh.Tets.size());
                    result.Stats.BoundaryFaceCount = static_cast<int>(volumeMesh.BoundaryFaces.size());
                    ReportProgress(options, PolycubeProgress{
                        result.StageReached,
                        result.PreviewSource,
                        result.Stats,
                        "Optimizing: collapsed skinny boundary triangles and retrying."
                    });
                    continue;
                }

                volumeMesh.CurrentPositions = candidatePositions;
                currentMetrics = candidateMetrics;
                anyAcceptedOptimizationStep = true;
                result.BoundaryPreviewMesh = BuildPreviewMesh(volumeMesh);
                result.PreviewApplied = true;
                result.PreviewSource = PolycubePreviewSource::AcceptedOptimizationStep;

                result.Stats.InnerIterations += 1;
                result.Stats.TetCount = static_cast<int>(volumeMesh.Tets.size());
                result.Stats.BoundaryFaceCount = static_cast<int>(volumeMesh.BoundaryFaces.size());
                result.Stats.NormalizedError =
                    static_cast<float>(currentMetrics.NormalizedError(volumeMesh.RestBoundaryArea));
                result.Stats.AreaDrift =
                    static_cast<float>(currentMetrics.AreaDrift(volumeMesh.RestBoundaryArea));
                result.Stats.MinTetVolume = static_cast<float>(currentMetrics.MinTetVolume);
                {
                    std::ostringstream status;
                    status << "Optimizing: outer stage " << (outerStage + 1)
                           << ", accepted step " << result.Stats.InnerIterations
                           << ", normalized error " << result.Stats.NormalizedError << '.';
                    ReportProgress(options, PolycubeProgress{
                        result.StageReached,
                        result.PreviewSource,
                        result.Stats,
                        status.str()
                    });
                }
                recentErrors.push_back(currentMetrics.NormalizedError(volumeMesh.RestBoundaryArea));
                if (recentErrors.size() > 5) {
                    recentErrors.pop_front();
                }

                if (recentErrors.size() == 5) {
                    double variation = 0.0;
                    for (std::size_t i = 1; i < recentErrors.size(); ++i) {
                        variation += std::abs(recentErrors[i] - recentErrors[i - 1]);
                    }
                    if (variation < initialNormalizedError / 100.0) {
                        break;
                    }
                }
            }

            if (currentMetrics.NormalizedError(volumeMesh.RestBoundaryArea) < targetNormalizedError) {
                break;
            }

            alpha *= alphaMultiplier;
            if (epsilon > minEpsilon) {
                epsilon = std::max(minEpsilon, epsilon * epsilonDecay);
            }
        }

        result.StageReached = PolycubeStage::Cleanup;

        std::vector<std::array<int, 3>> boundaryTriangles;
        boundaryTriangles.reserve(volumeMesh.BoundaryFaces.size());
        for (const BoundaryFace& face : volumeMesh.BoundaryFaces) {
            boundaryTriangles.push_back(face.Vertices);
        }

        std::vector<int> labels = LabelBoundaryFaces(volumeMesh);
        result.Stats.InitialPatchCount = CountPatchComponents(boundaryTriangles, labels);
        ReportProgress(options, PolycubeProgress{
            result.StageReached,
            result.PreviewSource,
            result.Stats,
            "Cleaning up boundary patches."
        });

        const CleanupOutcome cleanupOutcome = CleanupPatchLabelsDetailed(
            boundaryTriangles,
            volumeMesh.CurrentPositions,
            labels,
            [&](const char* phase, int componentCount, const CleanupDiagnostics& diagnostics) {
                PolycubeStats progressStats = result.Stats;
                progressStats.FinalPatchCount = componentCount;

                std::ostringstream status;
                status << "Cleanup: " << phase
                       << ", components=" << componentCount
                       << ", relabels=" << diagnostics.RelabelsAccepted
                       << ", merges=" << diagnostics.MergeRounds;
                if (diagnostics.StoppedByBudget) {
                    status << ", bounded by relabel budget";
                }
                if (diagnostics.StoppedByNoProgress) {
                    status << ", no further progress";
                }

                ReportProgress(options, PolycubeProgress{
                    result.StageReached,
                    result.PreviewSource,
                    progressStats,
                    status.str()
                });
            }
        );
        labels = cleanupOutcome.Labels;
        result.Stats.FinalPatchCount = CountPatchComponents(boundaryTriangles, labels);
        for (std::size_t faceIndex = 0; faceIndex < volumeMesh.BoundaryFaces.size(); ++faceIndex) {
            volumeMesh.BoundaryFaces[faceIndex].Label = labels[faceIndex];
        }

        HE_MeshData finalPreviewMesh = BuildPreviewMesh(volumeMesh);
        ReportProgress(options, PolycubeProgress{
            result.StageReached,
            result.PreviewSource,
            result.Stats,
            "Extracting final patch-aligned boundary."
        });
        VolumeMesh extractedMesh = volumeMesh;
        if (ExtractFinalBoundaryPolycube(extractedMesh, labels, complexityWeight)) {
            finalPreviewMesh = BuildPreviewMesh(extractedMesh);
            if (IsValidExtractionState(extractedMesh, complexityWeight)) {
                volumeMesh = std::move(extractedMesh);
            }
        }

        currentMetrics = EvaluateState(volumeMesh, alpha, epsilon, complexityWeight);
        result.Stats.TetCount = static_cast<int>(volumeMesh.Tets.size());
        result.Stats.BoundaryFaceCount = static_cast<int>(volumeMesh.BoundaryFaces.size());
        result.Stats.NormalizedError = static_cast<float>(currentMetrics.NormalizedError(volumeMesh.RestBoundaryArea));
        result.Stats.AreaDrift = static_cast<float>(currentMetrics.AreaDrift(volumeMesh.RestBoundaryArea));
        result.Stats.MinTetVolume = static_cast<float>(currentMetrics.MinTetVolume);

        result.BoundaryPreviewMesh = std::move(finalPreviewMesh);
        result.PreviewApplied = true;
        result.PreviewSource = PolycubePreviewSource::CleanupResult;
        result.StageReached = PolycubeStage::Completed;
        result.Ok = true;
        result.Summary = BuildSummary(result.Stats);
        ReportProgress(options, PolycubeProgress{
            result.StageReached,
            result.PreviewSource,
            result.Stats,
            result.Summary
        });

        if (!anyAcceptedOptimizationStep) {
            result.PreviewSource = PolycubePreviewSource::CleanupResult;
        }
        return result;
    } catch (const std::exception& exception) {
        if (result.Error.empty()) {
            result.Error = exception.what();
        }
        if (!result.PreviewApplied) {
            result.PreviewSource = PolycubePreviewSource::None;
        }
        return result;
    }
}
