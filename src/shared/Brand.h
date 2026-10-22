#pragma once
#include "imgui.h"

#ifndef CAIRN_VERSION_STRING
#define CAIRN_VERSION_STRING "0.2.0"
#endif

namespace Brand
{
    inline constexpr const char* kClientName   = "Cairn";
    inline constexpr const char* kInjectorName = "Cairn Injector";
    inline constexpr const char* kTargetGame   = "Minecraft 1.21.11";
    inline constexpr const char* kVersion      = CAIRN_VERSION_STRING;

    // Draws the Cairn badge (rounded square + three stacked stones) centred at `center`.
    // Geometry mirrors tools/gen_assets.py so the in-app logo matches the .ico.
    void drawLogo(ImDrawList* dl, ImVec2 center, float size);

    // Just the three stones, no badge (for use on an already dark surface).
    void drawMark(ImDrawList* dl, ImVec2 center, float size);
}
