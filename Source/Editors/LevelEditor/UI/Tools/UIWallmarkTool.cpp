#include "stdafx.h"

UIWallmarkTool::UIWallmarkTool(): Tool(nullptr) {}

UIWallmarkTool::~UIWallmarkTool() {}

void UIWallmarkTool::Draw()
{
    if (!Tool)
        return;

    // Lazy populate: m_Defaults is empty on first draw (and after any explicit
    // clear). The PropItem pointers target Tool->m_Mark* fields which live as
    // long as the tool itself, so once assigned they stay valid across frames.
    if (m_Defaults.Empty())
    {
        PropItemVec items;
        Tool->FillToolDefaults(items);
        m_Defaults.AssignItems(items);
    }

    if (ImGui::CollapsingHeader(("  Next Placement"_RU >> u8"  Параметры следующего"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding | ImGuiTableFlags_NoBordersInBody))
    {
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SameLine(0, 10);
        ImGui::BulletTextColored(ImVec4(0.75, 1.5, 0, 0.85), "");
        ImGui::BeginGroup();
        m_Defaults.Draw();
        ImGui::Separator();
        ImGui::EndGroup();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
}
