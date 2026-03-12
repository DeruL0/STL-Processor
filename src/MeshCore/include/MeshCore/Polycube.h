#pragma once

#include "MeshCore/HalfEdgeMesh.h"

#include <array>
#include <string>
#include <vector>

enum class BoundaryPatchLabel {
    PosX = 0,
    NegX = 1,
    PosY = 2,
    NegY = 3,
    PosZ = 4,
    NegZ = 5
};

enum class PolycubeStage {
    None = 0,
    Preprocess = 1,
    Tetrahedralize = 2,
    Optimize = 3,
    Cleanup = 4,
    Completed = 5
};

enum class PolycubePreviewSource {
    None = 0,
    TetrahedralBoundary = 1,
    AcceptedOptimizationStep = 2,
    CleanupResult = 3
};

struct TetGradientOperator {
    std::array<Vec3f, 4> ShapeGradients;
};

struct TetVertex {
    Vec3f Pos;
    bool IsBoundary = false;
};

struct TetCell {
    std::array<int, 4> Vertices = { -1, -1, -1, -1 };
    float RestVolume = 0.0f;
    TetGradientOperator Gradient;
};

struct BoundaryFace {
    std::array<int, 3> Vertices = { -1, -1, -1 };
    int CellIndex = -1;
    BoundaryPatchLabel Label = BoundaryPatchLabel::PosX;
};

struct TetMesh {
    std::vector<TetVertex> Vertices;
    std::vector<TetCell> Cells;
    std::vector<BoundaryFace> BoundaryFaces;
};

struct PolycubeOptions {
    float TetSpacingScale = 1.0f;
    float FeatureNormalThresholdDeg = 35.0f;
    int MaxOuterStages = 20;
    int MaxInnerIterations = 25;
    bool EnableSkinnyBoundaryCollapse = true;
};

struct PolycubeStats {
    int TetCount = 0;
    int BoundaryFaceCount = 0;
    int OuterStages = 0;
    int InnerIterations = 0;
    int InitialPatchCount = 0;
    int FinalPatchCount = 0;
    float NormalizedError = 0.0f;
    float AreaDrift = 0.0f;
    float MinTetVolume = 0.0f;
};

struct PolycubeResult {
    bool Ok = false;
    bool PreviewApplied = false;
    PolycubeStage StageReached = PolycubeStage::None;
    PolycubePreviewSource PreviewSource = PolycubePreviewSource::None;
    std::string Error;
    std::string Summary;
    PolycubeStats Stats;
    TetMesh OutTetMesh;
    HE_MeshData BoundaryPreviewMesh;
    std::vector<BoundaryPatchLabel> BoundaryLabels;
};

class Polycube {
public:
    PolycubeResult Generate(const HE_MeshData& mesh, const PolycubeOptions& options = {});
};
