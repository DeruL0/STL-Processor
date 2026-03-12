#include "GUI/Panels.h"

#include "imgui.h"

#include <algorithm>

namespace {
const char* kHoleMethodItems = "AREA\0ANGLE\0";
const char* kRefinementItems = "NO_REFINEMENT\0REFINEMENT\0";
const char* kFairingItems = "NO_FAIRING\0UNIFORM\0SCALE\0TAN\0COT\0";
} // namespace

namespace GUI {
void Panels::Draw(
    const MeshInfoState& meshInfo,
    RepairPanelState& repairState,
    PolycubePanelState& polycubeState,
    const PolycubeInfoState& polycubeInfo,
    PanelActions& outActions
) const {
    DrawMeshInfo(meshInfo, outActions);
    DrawRepairPanel(meshInfo, repairState, outActions);
    DrawPolycubePanel(meshInfo, polycubeState, polycubeInfo, outActions);
}

void Panels::DrawMeshInfo(const MeshInfoState& meshInfo, PanelActions& outActions) const {
    if (!ImGui::Begin("Mesh Info")) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Open STL...")) {
        outActions.RequestOpenModel = true;
    }
    ImGui::TextWrapped("Model: %s", meshInfo.ModelPath.c_str());
    ImGui::Separator();
    ImGui::Text("Vertices: %d", meshInfo.VertexCount);
    ImGui::Text("Triangles: %d", meshInfo.TriangleCount);
    ImGui::Separator();
    ImGui::Text("Busy: %s", meshInfo.Busy ? "Yes" : "No");
    ImGui::Text("Task: %s", meshInfo.ActiveTask.c_str());
    ImGui::TextWrapped("Status: %s", meshInfo.Status.c_str());

    ImGui::End();
}

void Panels::DrawRepairPanel(const MeshInfoState& meshInfo, RepairPanelState& repairState, PanelActions& outActions) const {
    if (!ImGui::Begin("Repair")) {
        ImGui::End();
        return;
    }

    int holeMethod = static_cast<int>(repairState.HoleMethod);
    if (ImGui::Combo("Hole Method", &holeMethod, kHoleMethodItems)) {
        repairState.HoleMethod = static_cast<HoleMethodOption>(holeMethod);
    }

    int holeRefinement = static_cast<int>(repairState.HoleRefinement);
    if (ImGui::Combo("Hole Refinement", &holeRefinement, kRefinementItems)) {
        repairState.HoleRefinement = static_cast<RefinementOption>(holeRefinement);
    }

    int holeFairing = static_cast<int>(repairState.HoleFairing);
    if (ImGui::Combo("Hole Fairing", &holeFairing, kFairingItems)) {
        repairState.HoleFairing = static_cast<FairingOption>(holeFairing);
    }
    ImGui::InputInt("Hole Fairing Iterations", &repairState.HoleFairingIterations);
    repairState.HoleFairingIterations = std::max(0, repairState.HoleFairingIterations);

    ImGui::Separator();

    ImGui::InputInt("Denoise Iterations", &repairState.DenoiseIterations);
    repairState.DenoiseIterations = std::max(0, repairState.DenoiseIterations);
    ImGui::SliderFloat("Denoise T", &repairState.DenoiseT, 0.0f, 1.0f, "%.3f");

    ImGui::Separator();

    ImGui::InputInt("Smoothing Iterations", &repairState.SmoothingIterations);
    repairState.SmoothingIterations = std::max(0, repairState.SmoothingIterations);
    int smoothingFairing = static_cast<int>(repairState.SmoothingFairing);
    if (ImGui::Combo("Smoothing Fairing", &smoothingFairing, kFairingItems)) {
        repairState.SmoothingFairing = static_cast<FairingOption>(smoothingFairing);
    }

    ImGui::Separator();
    if (meshInfo.Busy) {
        ImGui::TextUnformatted("Background task running. New request will replace queued request.");
    }

    if (ImGui::Button("RepairNormal")) { outActions.RequestRepairNormal = true; }
    ImGui::SameLine();
    if (ImGui::Button("HolesFilling")) { outActions.RequestHolesFilling = true; }
    ImGui::SameLine();
    if (ImGui::Button("Denoise")) { outActions.RequestDenoise = true; }
    ImGui::SameLine();
    if (ImGui::Button("Smoothing")) { outActions.RequestSmoothing = true; }

    if (ImGui::Button("Apply All")) { outActions.RequestApplyAll = true; }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) { outActions.RequestReset = true; }

    ImGui::End();
}

void Panels::DrawPolycubePanel(
    const MeshInfoState& meshInfo,
    PolycubePanelState& polycubeState,
    const PolycubeInfoState& polycubeInfo,
    PanelActions& outActions
) const {
    if (!ImGui::Begin("Polycube")) {
        ImGui::End();
        return;
    }

    ImGui::SliderFloat("Tet Spacing Scale", &polycubeState.TetSpacingScale, 0.5f, 3.0f, "%.2f");
    ImGui::InputFloat("Initial Alpha", &polycubeState.InitialAlpha, 0.05f, 0.1f, "%.3f");
    polycubeState.InitialAlpha = std::max(0.001f, polycubeState.InitialAlpha);
    ImGui::InputFloat("Complexity Weight", &polycubeState.ComplexityWeight, 0.1f, 1.0f, "%.3f");
    polycubeState.ComplexityWeight = std::max(0.0f, polycubeState.ComplexityWeight);
    ImGui::InputFloat("Alpha Multiplier", &polycubeState.AlphaMultiplier, 0.1f, 0.5f, "%.3f");
    polycubeState.AlphaMultiplier = std::max(1.0f, polycubeState.AlphaMultiplier);

    ImGui::InputFloat("Initial Epsilon", &polycubeState.InitialEpsilon, 0.1f, 0.5f, "%.3f");
    polycubeState.InitialEpsilon = std::max(0.001f, polycubeState.InitialEpsilon);
    ImGui::InputFloat("Epsilon Decay", &polycubeState.EpsilonDecay, 0.05f, 0.1f, "%.3f");
    polycubeState.EpsilonDecay = std::clamp(polycubeState.EpsilonDecay, 0.1f, 1.0f);
    ImGui::InputFloat("Min Epsilon", &polycubeState.MinEpsilon, 0.005f, 0.01f, "%.3f");
    polycubeState.MinEpsilon = std::clamp(polycubeState.MinEpsilon, 0.0001f, polycubeState.InitialEpsilon);

    ImGui::InputFloat("Target Error", &polycubeState.TargetNormalizedError, 0.0001f, 0.001f, "%.4f");
    polycubeState.TargetNormalizedError = std::max(1e-5f, polycubeState.TargetNormalizedError);
    ImGui::InputInt("Max Outer Stages", &polycubeState.MaxOuterStages);
    polycubeState.MaxOuterStages = std::max(1, polycubeState.MaxOuterStages);
    ImGui::InputInt("Max Inner Iterations", &polycubeState.MaxInnerIterations);
    polycubeState.MaxInnerIterations = std::max(1, polycubeState.MaxInnerIterations);

    ImGui::TextUnformatted("Volumetric L1 PolyCube with vendored TetGen tetrahedralization.");
    ImGui::TextUnformatted("Solver: Eigen sparse LDLT + Eigen SVD/eigensolver.");

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", polycubeInfo.Status.c_str());
    if (!polycubeInfo.Summary.empty()) {
        ImGui::TextWrapped("Summary: %s", polycubeInfo.Summary.c_str());
    }
    ImGui::Text("Stage: %s", polycubeInfo.Stage.c_str());
    ImGui::Text("Preview: %s", polycubeInfo.PreviewSource.c_str());
    ImGui::Text("Tets: %d", polycubeInfo.TetCount);
    ImGui::Text("Boundary Faces: %d", polycubeInfo.BoundaryFaceCount);
    ImGui::Text("Outer Stages: %d", polycubeInfo.OuterStages);
    ImGui::Text("Inner Iterations: %d", polycubeInfo.InnerIterations);
    ImGui::Text("Patches: %d -> %d", polycubeInfo.InitialPatchCount, polycubeInfo.FinalPatchCount);
    ImGui::Text("Normalized Error: %.6f", polycubeInfo.NormalizedError);
    ImGui::Text("Area Drift: %.6f", polycubeInfo.AreaDrift);
    ImGui::Text("Min Tet Volume: %.6f", polycubeInfo.MinTetVolume);
    if (meshInfo.Busy) {
        ImGui::TextUnformatted("Running. New request will replace queued request.");
    }

    if (ImGui::Button("Run Polycube")) {
        outActions.RequestPolycube = true;
    }

    ImGui::End();
}
} // namespace GUI
