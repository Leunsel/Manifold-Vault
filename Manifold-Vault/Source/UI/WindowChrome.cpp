#include "UI/WindowChrome.h"

#include "App/Application.h"
#include "UI/IconButton.h"
#include "UI/Tooltip.h"
#include "UI/UiScale.h"
#include "Generated/FaIcons.h"

namespace Bunker::UI
{
    using Theme::Token;

    bool IconLink(const char* id, const char* icon, ImVec2 size, Token rest, Token hover,
                  const char* tooltip, bool danger)
    {
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::InvisibleButton(id, size);
        const bool hovered = ImGui::IsItemHovered();
        const ImVec2 max(min.x + size.x, min.y + size.y);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hovered)
        {
            const ImU32 fill = danger ? Theme::U32(Token::RedCore, ImGui::IsItemActive() ? 0.28f : 0.16f)
                                      : Theme::U32(Token::TextPrimary, ImGui::IsItemActive() ? 0.10f : 0.06f);
            dl->AddRectFilled(min, max, fill, Theme::Round(Px(4.0f)));
        }
        DrawIconCentred(dl, min, max, icon, Theme::U32(hovered ? hover : rest));
        if (tooltip && *tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            Tooltip(tooltip);
        return clicked;
    }

    namespace
    {
        float ButtonSide() { return ImGui::GetFrameHeight(); }
        float ButtonGap() { return Px(2.0f); }
    }

    float WindowButtonsWidth()
    {
        return ButtonSide() * 3.0f + ButtonGap() * 2.0f;
    }

    void WindowButtons(Application& app, ImVec2 at)
    {
        const float side = ButtonSide();
        const ImVec2 size(side, side);
        ImGui::SetCursorScreenPos(at);
        if (IconLink("##window-minimise", ICON_FA_MINUS, size, Token::TextMuted, Token::TextPrimary))
            app.Minimise();
        ImGui::SameLine(0.0f, ButtonGap());
        if (IconLink("##window-maximise",
                     app.Maximised() ? ICON_FA_WINDOW_RESTORE : ICON_FA_WINDOW_MAXIMIZE, size,
                     Token::TextMuted, Token::TextPrimary))
            app.ToggleMaximised();
        ImGui::SameLine(0.0f, ButtonGap());
        if (IconLink("##window-close", ICON_FA_XMARK, size, Token::TextMuted, Token::RedBright,
                     nullptr, true))
            app.RequestClose();
    }

    void DrawWindowEdge(Application& app, ImDrawList* dl)
    {
        if (app.Maximised())
            return;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const float half = Px(1.0f) * 0.5f;
        dl->AddRect(ImVec2(vp->Pos.x + half, vp->Pos.y + half),
                    ImVec2(vp->Pos.x + vp->Size.x - half, vp->Pos.y + vp->Size.y - half),
                    Theme::U32(Token::Line), app.CornerRadius(), 0, Px(1.0f));
    }
}
