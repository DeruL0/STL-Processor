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
    const std::string& polycubeStatus,
    PanelActions& outActions
) const {
    DrawMeshInfo(meshInfo);
    DrawRepairPanel(meshInfo, repairState, outActions);
    DrawPolycubePanel(meshInfo, polycubeState, polycubeStatus, outActions);
}

void Panels::DrawMeshInfo(const MeshInfoState& meshInfo) const {
    if (!ImGui::Begin("Mesh Info")) {
        ImGui::End();
        return;
    }

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
    const std::string& polycubeStatus,
    PanelActions& outActions
) const {
    if (!ImGui::Begin("Polycube")) {
        ImGui::End();
        return;
    }

    ImGui::InputInt("Iteration Budget", &polycubeState.IterationBudget);
    polycubeState.IterationBudget = std::max(1, polycubeState.IterationBudget);
    ImGui::SliderFloat("Alignment Weight", &polycubeState.AlignmentWeight, 0.0f, 1.0f, "%.3f");
    ImGui::Checkbox("Preserve Sharp Features", &polycubeState.PreserveSharpFeatures);

    ImGui::Separator();
    ImGui::TextWrapped("Status: %s", polycubeStatus.c_str());
    if (meshInfo.Busy) {
        ImGui::TextUnformatted("Running. New request will replace queued request.");
    }

    if (ImGui::Button("Run Polycube")) {
        outActions.RequestPolycube = true;
    }

    ImGui::End();
}
} // namespace GUI
