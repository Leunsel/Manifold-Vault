#pragma once

#include "App/BunkerTheme.h"

#include "imgui.h"

namespace Bunker
{
    class Application;
}

namespace Bunker::UI
{
    bool IconLink(const char* id, const char* icon, ImVec2 size, Theme::Token rest,
                  Theme::Token hover, const char* tooltip = nullptr, bool danger = false);

    float WindowButtonsWidth();
    void WindowButtons(Application& app, ImVec2 at);

    void DrawWindowEdge(Application& app, ImDrawList* dl);
}
