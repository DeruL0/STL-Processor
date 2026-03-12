#include "MeshCore/MeshCore.h"
#include "PolycubeInternal.h"

#include <Eigen/Geometry>

#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace {

bool NearlyEqual(double lhs, double rhs, double epsilon = 1e-5) {
    return std::abs(lhs - rhs) <= epsilon;
}

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

HE_MeshData BuildMesh(
    const std::vector<Vec3f>& positions,
    const std::vector<std::array<std::int32_t, 3>>& triangles
) {
    HE_MeshData mesh;
    mesh.SetIndexedMesh(positions, triangles);
    return mesh;
}

void BuildFaceRing(
    int faceCount,
    std::vector<std::array<int, 3>>& outFaces,
    std::vector<Eigen::Vector3d>& outPositions
) {
    outFaces.clear();
    outPositions.clear();
    outPositions.reserve(faceCount + 1);
    for (int index = 0; index < faceCount; ++index) {
        const double angle = 2.0 * 3.14159265358979323846 * static_cast<double>(index) /
            static_cast<double>(faceCount);
        outPositions.emplace_back(std::cos(angle), std::sin(angle), 0.0);
    }
    outPositions.emplace_back(0.0, 0.0, 0.0);

    const int center = faceCount;
    outFaces.reserve(faceCount);
    for (int index = 0; index < faceCount; ++index) {
        outFaces.push_back({ index, (index + 1) % faceCount, center });
    }
}

double SurfaceNormalizedError(const HE_MeshData& mesh) {
    double totalArea = 0.0;
    double totalError = 0.0;
    for (const HE_Triangle* triangle : mesh.HE_Triangles) {
        const std::array<Eigen::Vector3d, 3> points = {
            Eigen::Vector3d(
                mesh.HE_Vertexes[triangle->VertexIndex0]->Pos.x,
                mesh.HE_Vertexes[triangle->VertexIndex0]->Pos.y,
                mesh.HE_Vertexes[triangle->VertexIndex0]->Pos.z
            ),
            Eigen::Vector3d(
                mesh.HE_Vertexes[triangle->VertexIndex1]->Pos.x,
                mesh.HE_Vertexes[triangle->VertexIndex1]->Pos.y,
                mesh.HE_Vertexes[triangle->VertexIndex1]->Pos.z
            ),
            Eigen::Vector3d(
                mesh.HE_Vertexes[triangle->VertexIndex2]->Pos.x,
                mesh.HE_Vertexes[triangle->VertexIndex2]->Pos.y,
                mesh.HE_Vertexes[triangle->VertexIndex2]->Pos.z
            )
        };
        const PolycubeInternal::FaceGeometry geometry = PolycubeInternal::ComputeFaceGeometry(points);
        totalArea += geometry.Area;
        totalError += geometry.Area * (geometry.UnitNormal.cwiseAbs().sum() - 1.0);
    }
    return totalArea <= 1e-12 ? 0.0 : totalError / totalArea;
}

void TestClosedManifoldValidation() {
    const std::vector<Vec3f> tetraPositions = {
        Vec3f(0.0f, 0.0f, 0.0f),
        Vec3f(1.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 1.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 1.0f)
    };
    const std::vector<std::array<std::int32_t, 3>> tetraTriangles = {
        { 0, 2, 1 },
        { 0, 1, 3 },
        { 1, 2, 3 },
        { 2, 0, 3 }
    };
    HE_MeshData tetraMesh = BuildMesh(tetraPositions, tetraTriangles);
    std::string error;
    Require(tetraMesh.IsClosedTwoManifold(&error), "Closed tetra mesh should validate.");

    const std::vector<Vec3f> openPositions = {
        Vec3f(0.0f, 0.0f, 0.0f),
        Vec3f(1.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 1.0f, 0.0f)
    };
    const std::vector<std::array<std::int32_t, 3>> openTriangles = {
        { 0, 1, 2 }
    };
    HE_MeshData openMesh = BuildMesh(openPositions, openTriangles);
    Require(!openMesh.IsClosedTwoManifold(&error), "Open mesh should be rejected.");
}

void TestFaceGeometryAndSmoothedAbs() {
    const std::array<Eigen::Vector3d, 3> triangle = {
        Eigen::Vector3d(0.0, 0.0, 0.0),
        Eigen::Vector3d(1.0, 0.0, 0.0),
        Eigen::Vector3d(0.0, 1.0, 0.0)
    };
    const PolycubeInternal::FaceGeometry geometry = PolycubeInternal::ComputeFaceGeometry(triangle);
    Require(NearlyEqual(geometry.Area, 0.5), "Triangle area should be 0.5.");
    Require((geometry.UnitNormal - Eigen::Vector3d(0.0, 0.0, 1.0)).norm() <= 1e-6, "Triangle normal should be +Z.");
    Require(NearlyEqual(PolycubeInternal::SmoothedAbs(3.0, 1.0), std::sqrt(10.0)), "Smoothed abs mismatch.");
}

void TestNearestRotationAndArap() {
    const Eigen::AngleAxisd rotation(0.6, Eigen::Vector3d(0.0, 0.0, 1.0));
    const Eigen::Matrix3d matrix = rotation.toRotationMatrix() * 1.3;
    const Eigen::Matrix3d nearest = PolycubeInternal::NearestRotation(matrix);
    Require(NearlyEqual(nearest.determinant(), 1.0, 1e-6), "Nearest rotation must have determinant 1.");
    Require((nearest.transpose() * nearest - Eigen::Matrix3d::Identity()).norm() <= 1e-6, "Nearest rotation must be orthonormal.");

    const std::array<Eigen::Vector3d, 4> restTet = {
        Eigen::Vector3d(0.0, 0.0, 0.0),
        Eigen::Vector3d(1.0, 0.0, 0.0),
        Eigen::Vector3d(0.0, 1.0, 0.0),
        Eigen::Vector3d(0.0, 0.0, 1.0)
    };
    const Eigen::Vector3d translation(2.0, -1.0, 0.5);
    const std::array<Eigen::Vector3d, 4> rigidTet = {
        rotation * restTet[0] + translation,
        rotation * restTet[1] + translation,
        rotation * restTet[2] + translation,
        rotation * restTet[3] + translation
    };
    const double arap = PolycubeInternal::TetArapEnergy(restTet, rigidTet);
    Require(arap <= 1e-6, "Rigid transform should have near-zero ARAP energy.");
}

void TestPatchCleanup() {
    const std::vector<std::array<int, 3>> faces = {
        { 0, 1, 2 },
        { 1, 3, 2 },
        { 1, 4, 3 }
    };
    const std::vector<Eigen::Vector3d> positions = {
        Eigen::Vector3d(0.0, 0.0, 0.0),
        Eigen::Vector3d(1.0, 0.0, 0.0),
        Eigen::Vector3d(0.0, 1.0, 0.0),
        Eigen::Vector3d(1.0, 1.0, 0.0),
        Eigen::Vector3d(2.0, 1.0, 0.0)
    };
    const std::vector<int> labels = { 0, 0, 2 };
    const std::vector<int> cleaned = PolycubeInternal::CleanupPatchLabels(faces, positions, labels);
    Require(cleaned.size() == labels.size(), "Cleanup must preserve label count.");
    if (cleaned[2] != 0) {
        std::ostringstream oss;
        oss << "Degree-1 patch should merge into its neighbor. cleaned="
            << cleaned[0] << "," << cleaned[1] << "," << cleaned[2];
        throw std::runtime_error(oss.str());
    }
}

void TestPatchCleanupOscillationResolves() {
    std::vector<std::array<int, 3>> faces;
    std::vector<Eigen::Vector3d> positions;
    BuildFaceRing(4, faces, positions);

    const std::vector<int> labels = { 0, 1, 0, 1 };
    const std::vector<int> cleaned = PolycubeInternal::CleanupPatchLabels(faces, positions, labels);
    Require(cleaned.size() == labels.size(), "Oscillation cleanup must preserve label count.");
    for (const int label : cleaned) {
        Require(label == cleaned.front(), "Alternating ring should converge to a single label.");
    }
}

void TestPatchCleanupDegreeTwoUsesLongestBoundary() {
    const std::vector<std::array<int, 3>> faces = {
        { 0, 1, 4 },
        { 1, 2, 4 },
        { 2, 3, 4 },
        { 3, 0, 4 }
    };
    const std::vector<Eigen::Vector3d> positions = {
        Eigen::Vector3d(0.0, 0.0, 0.0),
        Eigen::Vector3d(2.0, 0.0, 0.0),
        Eigen::Vector3d(4.0, 0.0, 0.0),
        Eigen::Vector3d(0.0, 1.0, 0.0),
        Eigen::Vector3d(0.5, 0.5, 0.0)
    };
    const std::vector<int> labels = { 0, 0, 2, 1 };
    const std::vector<int> cleaned = PolycubeInternal::CleanupPatchLabels(faces, positions, labels);
    Require(cleaned.size() == labels.size(), "Degree-2 cleanup must preserve label count.");
    if (cleaned[2] != 0) {
        std::ostringstream oss;
        oss << "Degree-2 patch should merge toward the longest shared boundary. cleaned=";
        for (std::size_t index = 0; index < cleaned.size(); ++index) {
            if (index > 0) {
                oss << ',';
            }
            oss << cleaned[index];
        }
        throw std::runtime_error(oss.str());
    }
}

void TestPatchCleanupNoProgressExits() {
    const std::vector<std::array<int, 3>> faces = {
        { 0, 2, 1 },
        { 0, 1, 3 },
        { 1, 2, 3 },
        { 2, 0, 3 }
    };
    const std::vector<Eigen::Vector3d> positions = {
        Eigen::Vector3d(0.0, 0.0, 0.0),
        Eigen::Vector3d(1.0, 0.0, 0.0),
        Eigen::Vector3d(0.0, 1.0, 0.0),
        Eigen::Vector3d(0.0, 0.0, 1.0)
    };
    const std::vector<int> labels = { 0, 1, 2, 3 };
    const std::vector<int> cleaned = PolycubeInternal::CleanupPatchLabels(faces, positions, labels);
    Require(cleaned == labels, "No-progress cleanup case should exit without relabeling.");
}

void TestPatchCleanupBoundedTermination() {
    std::vector<std::array<int, 3>> faces;
    std::vector<Eigen::Vector3d> positions;
    BuildFaceRing(16, faces, positions);

    std::vector<int> labels(faces.size(), 0);
    for (std::size_t faceIndex = 0; faceIndex < labels.size(); ++faceIndex) {
        labels[faceIndex] = static_cast<int>(faceIndex % 2);
    }

    const std::vector<int> cleaned = PolycubeInternal::CleanupPatchLabels(faces, positions, labels);
    Require(cleaned.size() == labels.size(), "Bounded cleanup must preserve label count.");
    for (const int label : cleaned) {
        Require(label == cleaned.front(), "Bounded cleanup should resolve an alternating ring.");
    }
}

void TestPolycubeGenerate() {
    const std::vector<Vec3f> positions = {
        Vec3f(1.0f, 0.0f, 0.0f),
        Vec3f(-1.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 1.0f, 0.0f),
        Vec3f(0.0f, -1.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 1.0f),
        Vec3f(0.0f, 0.0f, -1.0f)
    };
    const std::vector<std::array<std::int32_t, 3>> triangles = {
        { 0, 4, 2 },
        { 2, 4, 1 },
        { 1, 4, 3 },
        { 3, 4, 0 },
        { 2, 5, 0 },
        { 1, 5, 2 },
        { 3, 5, 1 },
        { 0, 5, 3 }
    };

    HE_MeshData sourceMesh = BuildMesh(positions, triangles);
    sourceMesh.RepairNormal();
    sourceMesh.GetVertexesNormal();
    const double initialError = SurfaceNormalizedError(sourceMesh);

    Polycube polycube;
    PolycubeOptions options;
    options.TetSpacingScale = 1.0f;
    options.MaxOuterStages = 4;

    PolycubeResult result = polycube.Generate(sourceMesh, options);
    Require(result.Ok, "Polycube::Generate should succeed on a closed octahedron.");
    Require(result.StageReached == PolycubeStage::Completed, "Polycube::Generate should leave cleanup and complete.");
    Require(result.Stats.TetCount > 0, "Tet count should be positive.");
    Require(result.Stats.BoundaryFaceCount > 0, "Boundary face count should be positive.");
    Require(result.Stats.MinTetVolume > 0.0f, "Minimum tet volume should stay positive.");
    Require(result.Stats.FinalPatchCount <= result.Stats.InitialPatchCount, "Cleanup should not increase patch count.");
    Require(!result.BoundaryPreviewMesh.HE_Triangles.empty(), "Boundary preview mesh should not be empty.");

    const double finalError = SurfaceNormalizedError(result.BoundaryPreviewMesh);
    Require(finalError <= initialError + 1e-4, "Polycube optimization should not increase normalized error.");
}

void TestPolycubeScheduleContinuesAcrossOuterStages() {
    const Eigen::AngleAxisf rotation(0.63f, Eigen::Vector3f(0.2f, 0.9f, -0.3f).normalized());
    const std::vector<Vec3f> positions = {
        Vec3f(1.0f, 0.0f, 0.0f),
        Vec3f(-1.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 1.0f, 0.0f),
        Vec3f(0.0f, -1.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 1.0f),
        Vec3f(0.0f, 0.0f, -1.0f)
    };
    std::vector<Vec3f> rotatedPositions;
    rotatedPositions.reserve(positions.size());
    for (const Vec3f& position : positions) {
        const Eigen::Vector3f rotated =
            rotation * Eigen::Vector3f(position.x, position.y, position.z);
        rotatedPositions.emplace_back(rotated.x(), rotated.y(), rotated.z());
    }

    const std::vector<std::array<std::int32_t, 3>> triangles = {
        { 0, 4, 2 },
        { 2, 4, 1 },
        { 1, 4, 3 },
        { 3, 4, 0 },
        { 2, 5, 0 },
        { 1, 5, 2 },
        { 3, 5, 1 },
        { 0, 5, 3 }
    };

    HE_MeshData sourceMesh = BuildMesh(rotatedPositions, triangles);
    sourceMesh.RepairNormal();
    sourceMesh.GetVertexesNormal();

    Polycube polycube;
    PolycubeOptions options;
    options.TetSpacingScale = 1.0f;
    options.MaxOuterStages = 3;
    options.MaxInnerIterations = 1;

    PolycubeResult result = polycube.Generate(sourceMesh, options);
    Require(result.Ok, "Polycube::Generate should succeed on a rotated octahedron.");
    Require(result.Stats.OuterStages > 1, "Polycube schedule should continue across outer stages when inner iteration budget is exhausted.");
}

void TestPolycubeComplexityWeightPath() {
    const Eigen::AngleAxisf rotation(0.41f, Eigen::Vector3f(0.3f, -0.7f, 0.6f).normalized());
    const std::vector<Vec3f> positions = {
        Vec3f(1.0f, 0.0f, 0.0f),
        Vec3f(-1.0f, 0.0f, 0.0f),
        Vec3f(0.0f, 1.0f, 0.0f),
        Vec3f(0.0f, -1.0f, 0.0f),
        Vec3f(0.0f, 0.0f, 1.0f),
        Vec3f(0.0f, 0.0f, -1.0f)
    };
    std::vector<Vec3f> rotatedPositions;
    rotatedPositions.reserve(positions.size());
    for (const Vec3f& position : positions) {
        const Eigen::Vector3f rotated =
            rotation * Eigen::Vector3f(position.x, position.y, position.z);
        rotatedPositions.emplace_back(rotated.x(), rotated.y(), rotated.z());
    }

    const std::vector<std::array<std::int32_t, 3>> triangles = {
        { 0, 4, 2 },
        { 2, 4, 1 },
        { 1, 4, 3 },
        { 3, 4, 0 },
        { 2, 5, 0 },
        { 1, 5, 2 },
        { 3, 5, 1 },
        { 0, 5, 3 }
    };

    HE_MeshData sourceMesh = BuildMesh(rotatedPositions, triangles);
    sourceMesh.RepairNormal();
    sourceMesh.GetVertexesNormal();
    const double initialError = SurfaceNormalizedError(sourceMesh);

    Polycube polycube;
    PolycubeOptions options;
    options.TetSpacingScale = 1.0f;
    options.ComplexityWeight = 1.0f;
    options.MaxOuterStages = 4;

    PolycubeResult result = polycube.Generate(sourceMesh, options);
    Require(result.Ok, "Polycube::Generate should succeed when complexity weighting is enabled.");
    Require(!result.BoundaryPreviewMesh.HE_Triangles.empty(), "Complexity-weighted polycube should still return a preview mesh.");
    const double finalError = SurfaceNormalizedError(result.BoundaryPreviewMesh);
    Require(finalError <= initialError + 1e-4, "Complexity-weighted polycube should not increase normalized error.");
}

} // namespace

int main() {
    try {
        TestClosedManifoldValidation();
        TestFaceGeometryAndSmoothedAbs();
        TestNearestRotationAndArap();
        TestPatchCleanup();
        TestPatchCleanupOscillationResolves();
        TestPatchCleanupDegreeTwoUsesLongestBoundary();
        TestPatchCleanupNoProgressExits();
        TestPatchCleanupBoundedTermination();
        TestPolycubeGenerate();
        TestPolycubeScheduleContinuesAcrossOuterStages();
        TestPolycubeComplexityWeightPath();
        std::cout << "All MeshCore tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Test failure: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
