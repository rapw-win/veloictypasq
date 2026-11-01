// Astral-Remake - made by kdt255 (https://github.com/kdt255/Astral-Remake)
// Free software: MIT + Commons Clause, selling is not permitted. If you paid for this, you got scammed.
// lust: the preview now shows the velocitypasq 3D agent (model3d.cpp) with its chams and ESP preview logic.
#pragma once
#include "astral/config.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace astral {

// Chams materials of the velocitypasq menu (same order as its chams list).
[[nodiscard]] const char* const* ChamsMaterialNames(int* count);

// Chams material picker: a styled list of the materials. Returns true when the selection changed.
bool ChamsMaterialPicker(const char* id, int* current, const PlayerVisuals& v, int target);

// Live ESP preview inside the current (pane) window: the 3D agent rendered with the selected chams / glow, plus
// box, skeleton, head dot, hitboxes, name, bars, weapon, distance and flags placed around the model bounds.
// Drag the model to rotate it, drag an ESP element to move it to another side of the box.
// target: 0 Enemy (CT agent + M4), 1 Team (T agent + AKR), 2 Local.
void DrawEspPreview(ImDrawList* dl, const ImRect& area, const PlayerVisuals& v, int target, double time);

} // namespace astral
