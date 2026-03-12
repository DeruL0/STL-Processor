#pragma once

#include <string>

namespace GUI {
enum class HoleMethodOption {
    Area = 0,
    Angle = 1
};

enum class RefinementOption {
    None = 0,
    Refinement = 1
};

enum class FairingOption {
    None = 0,
    Uniform = 1,
    Scale = 2,
    Tan = 3,
    Cot = 4
};

struct MeshInfoState {
    int VertexCount = 0;
    int TriangleCount = 0;
    bool Busy = false;
    std::string ActiveTask = "None";
    std::string Status = "Idle";
    std::string ModelPath = "assets/Models/Bunny.stl";
};

struct RepairPanelState {
    HoleMethodOption HoleMethod = HoleMethodOption::Area;
    RefinementOption HoleRefinement = RefinementOption::Refinement;
    FairingOption HoleFairing = FairingOption::None;
    int HoleFairingIterations = 1;

    int DenoiseIterations = 20;
    float DenoiseT = 0.3f;

    int SmoothingIterations = 1;
    FairingOption SmoothingFairing = FairingOption::Tan;
};

struct PolycubePanelState {
    float TetSpacingScale = 1.0f;
    float InitialAlpha = 0.1f;
    float ComplexityWeight = 0.0f;
    float AlphaMultiplier = 2.0f;
    float InitialEpsilon = 1.0f;
    float EpsilonDecay = 0.5f;
    float MinEpsilon = 0.01f;
    float TargetNormalizedError = 1e-3f;
    int MaxOuterStages = 20;
    int MaxInnerIterations = 25;
};

struct PolycubeInfoState {
    std::string Status = "Idle";
    std::string Summary;
    std::string Stage = "None";
    std::string PreviewSource = "None";
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

struct PanelActions {
    bool RequestOpenModel = false;
    bool RequestRepairNormal = false;
    bool RequestHolesFilling = false;
    bool RequestDenoise = false;
    bool RequestSmoothing = false;
    bool RequestApplyAll = false;
    bool RequestReset = false;
    bool RequestPolycube = false;
};

class Panels {
public:
    void Draw(
        const MeshInfoState& meshInfo,
        RepairPanelState& repairState,
        PolycubePanelState& polycubeState,
        const PolycubeInfoState& polycubeInfo,
        PanelActions& outActions
    ) const;

private:
    void DrawMeshInfo(const MeshInfoState& meshInfo, PanelActions& outActions) const;
    void DrawRepairPanel(const MeshInfoState& meshInfo, RepairPanelState& repairState, PanelActions& outActions) const;
    void DrawPolycubePanel(const MeshInfoState& meshInfo, PolycubePanelState& polycubeState, const PolycubeInfoState& polycubeInfo, PanelActions& outActions) const;
};
} // namespace GUI
