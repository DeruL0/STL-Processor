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
    int IterationBudget = 20;
    float AlignmentWeight = 0.5f;
    bool PreserveSharpFeatures = true;
};

struct PanelActions {
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
        const std::string& polycubeStatus,
        PanelActions& outActions
    ) const;

private:
    void DrawMeshInfo(const MeshInfoState& meshInfo) const;
    void DrawRepairPanel(const MeshInfoState& meshInfo, RepairPanelState& repairState, PanelActions& outActions) const;
    void DrawPolycubePanel(const MeshInfoState& meshInfo, PolycubePanelState& polycubeState, const std::string& polycubeStatus, PanelActions& outActions) const;
};
} // namespace GUI
