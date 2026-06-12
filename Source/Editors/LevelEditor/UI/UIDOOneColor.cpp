#include "stdafx.h"
#include "UIDOOneColor.h"
#include "UIDOShuffle.h"
UIDOOneColor::UIDOOneColor()
{
    list_index = 0;
}

UIDOOneColor::~UIDOOneColor() {}

void UIDOOneColor::Draw()
{
    const float frame_h   = ImGui::GetFrameHeight();
    const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
    const float padding_y = ImGui::GetStyle().WindowPadding.y * 2.0f;
    // Buttons column = 4 stacked buttons (color, X, >, <) with spacing between.
    const float buttons_h = frame_h * 4.0f + spacing_y * 3.0f;
    // List grows with item count; over-size by one spacing to keep the
    // last row from being clipped by the inner child's bottom border.
    const float list_h    = (float)list.size() * ImGui::GetTextLineHeightWithSpacing() + padding_y;
    // The sub-panel collapses to the buttons' height when fewer items would
    // fit there and grows to fit the list otherwise. Inner child has
    // NoScrollbar so we explicitly own the height.
    const float row_h     = list_h > buttons_h ? list_h : buttons_h;

    ImGui::BeginGroup();
    ImGui::BeginGroup();
    if (ImGui::ColorEdit3("##value", Color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel))
    {
        DOShuffle->bModif = true;
    }
    if (ImGui::Button("X", ImVec2(frame_h, frame_h)))
    {
        DOShuffle->bModif = true;
        bOpen             = false;
    }
    if (ImGui::Button(">", ImVec2(frame_h, frame_h)))
    {
        if (DOShuffle->m_list_selected >= 0 && DOShuffle->m_list_selected < DOShuffle->m_list.size())
        {
            AppendItem(DOShuffle->m_list[DOShuffle->m_list_selected]);
            DOShuffle->bModif = true;
        }
    }
    if (ImGui::Button("<", ImVec2(frame_h, frame_h)))
    {
        if (list_index >= 0 && list_index < list.size())
        {
            list.erase(list.begin() + list_index);
            list_index        = -1;
            DOShuffle->bModif = true;
        }
    }
    ImGui::EndGroup();
    ImGui::SameLine();
    // Rows that match the user's current pick on the left pane are tinted so
    // the user can see at a glance which color indices contain that detail.
    const xr_string* sel_left = nullptr;
    if (DOShuffle && DOShuffle->m_list_selected >= 0 && DOShuffle->m_list_selected < (int)DOShuffle->m_list.size())
        sel_left = &DOShuffle->m_list[DOShuffle->m_list_selected];

    ImGui::BeginChild("##list", ImVec2(0, row_h), true, ImGuiWindowFlags_NoScrollbar);
    for (int i = 0; i < (int)list.size(); ++i)
    {
        const xr_string& nm        = list[i];
        const bool       match_sel = sel_left && (*sel_left == nm);
        if (match_sel)
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 215, 100, 255));
        if (ImGui::Selectable(nm.c_str(), list_index == i))
            list_index = i;
        if (match_sel)
            ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::EndGroup();
    ImGui::Separator();
}

void UIDOOneColor::RemoveObject(const xr_string& str)
{
    for (auto b = list.begin(), e = list.end(); b != e; b++)
    {
        if (*b == str)
        {
            list.erase(b);
            return;
        }
    }
}

void UIDOOneColor::AppendItem(const xr_string& item)
{
    for (xr_string& i: list)
    {
        if (i == item)
            return;
    }
    list.push_back(item);
}
