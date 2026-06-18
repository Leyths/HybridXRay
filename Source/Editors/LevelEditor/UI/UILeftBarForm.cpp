#include "stdafx.h"

UILeftBarForm::UILeftBarForm()
{
    m_UseSnapList      = false;
    m_SnapListMode     = false;
    m_SnapItem_Current = 0;
    m_LastSeenTarget   = OBJCLASS_DUMMY;
}

UILeftBarForm::~UILeftBarForm() {}

void UILeftBarForm::Draw()
{
    ImGui::Begin("LeftBar", 0);
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    ImGuiStyle& style      = ImGui::GetStyle();
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    // ------------------------------------------------------------------------------------------------------ //
    if (ImGui::CollapsingHeader(("  Tools"_RU >> u8"  Режимы Редактирования"), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_FramePadding | ImGuiTableFlags_NoBordersInBody))
    {
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SameLine(0, 10);
        ImGui::BulletTextColored(ImVec4(0.75, 1.5, 0, 0.85), "");
        ImGui::BeginGroup();
        // The left-bar palette is a fixed list of user-facing scene tools (not every
        // ObjClassID — folders and other organizational classes are intentionally
        // excluded). Hard-coding the count keeps the column-split arithmetic stable
        // when new ObjClassIDs are added.
        static const u32  kToolsCount     = 16;
        static ObjClassID Tools[kToolsCount + 1] =
        {
            OBJCLASS_SCENEOBJECT, OBJCLASS_LIGHT,      OBJCLASS_SOUND_SRC, OBJCLASS_SOUND_ENV, OBJCLASS_GLOW,
            OBJCLASS_SHAPE,       OBJCLASS_SPAWNPOINT, OBJCLASS_WAY,       OBJCLASS_SECTOR,    OBJCLASS_PORTAL,
            OBJCLASS_GROUP,       OBJCLASS_PS,         OBJCLASS_DO,        OBJCLASS_AIMAP,     OBJCLASS_WM,
            OBJCLASS_FOG_VOL,     OBJCLASS_force_dword
        };

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
        ImGui::Columns(2);
        ImGui::Separator();

        for (u32 i = 0; Tools[i] != OBJCLASS_force_dword; i++)
        {
            u32 id = 0;
            if (i % 2)
                id = (kToolsCount / 2) + (i / 2);
            else
                id = (i / 2);
            ESceneToolBase* tool   = Scene->GetTool(Tools[id]);
            bool            visble = tool->IsVisible();
            ImGui::PushID(tool->ClassName());
            if (ImGui::Checkbox("##value", &visble))
            {
                tool->m_EditFlags.set(ESceneToolBase::flVisible, visble);
                UI->RedrawScene();
            };
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SameLine();
            if (ImGui::RadioButton(tool->ClassDesc(), LTools->GetTarget() == Tools[id]))
            {
                ExecCommand(COMMAND_CHANGE_TARGET, Tools[id]);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
            ImGui::NextColumn();
        }
        ImGui::Columns(1);
        ImGui::Separator();
        ImGui::PopStyleVar(2);
        ImGui::EndGroup();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // ------------------------------------------------------------------------------------------------------ //
    // Auto-open the Snap List the moment the user switches into the Wallmark
    // tool — wallmarks can't be placed without a populated snap list, and
    // forgetting to expand the dropdown is the most common stumbling block.
    // We only force-open on the transition (not every frame the wallmark
    // tool is active) so subsequent manual collapses by the user stick.
    const ObjClassID cur_target = LTools->GetTarget();
    if (cur_target != m_LastSeenTarget)
    {
        if (cur_target == OBJCLASS_WM)
            ImGui::SetNextItemOpen(true);
        m_LastSeenTarget = cur_target;
    }
    if (ImGui::CollapsingHeader(("  Snap List"_RU >> u8"  Список привязанных объектов"), ImGuiTreeNodeFlags_FramePadding | ImGuiTableFlags_NoBordersInBody))
    {
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SameLine(0, 10);
        ImGui::BulletTextColored(ImVec4(0.75, 1.5, 0, 0.85), "");
        ImGui::BeginGroup();
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 0));
            ImGui::Separator();
            {
                ImGui::BulletText("Commands"_RU >> u8"Команды", ImGuiDir_Left);
                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                if (ImGui::BeginPopupContextItem("Commands", 1))
                {
                    if (ImGui::MenuItem("Make List From Selected"_RU >> u8"Добавить из выделенного"))
                    {
                        ExecCommand(COMMAND_SET_SNAP_OBJECTS);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::MenuItem("Select Object From List"_RU >> u8"Выбрать объект из списка"))
                    {
                        ExecCommand(COMMAND_SELECT_SNAP_OBJECTS);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::Separator();
                    if (ImGui::MenuItem("Add Selected To List"_RU >> u8"Добавить выделенное в список"))
                    {
                        ExecCommand(COMMAND_ADD_SEL_SNAP_OBJECTS);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    if (ImGui::MenuItem("Remove Selected From List"_RU >> u8"Удалить выделенное из списка"))
                    {
                        ExecCommand(COMMAND_DEL_SEL_SNAP_OBJECTS);
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::EndPopup();
                }
                ImGui::OpenPopupOnItemClick("Commands", 0);
            }
            ImGui::Checkbox("Enable/Show Snap List"_RU >> u8"Показать/Скрыть из списка", &m_UseSnapList);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            ImGui::Separator();
            ImGui::Checkbox("+/- Mode"_RU >> u8"+/- Режим", &m_SnapListMode);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SameLine(0, 10);
            if (ImGui::Button("X"))
            {
                if (ELog.DlgMsg(mtConfirmation, mbYes | mbNo, "Are you sure to clear snap objects?") == mrYes)
                    ExecCommand(COMMAND_CLEAR_SNAP_OBJECTS);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopStyleVar(2);

            ObjectList* lst = Scene->GetSnapList(true);
            ImGui::SetNextItemWidth(-1);
            ImGui::ListBox(
                "##snap_list_box", &m_SnapItem_Current,
                [](void* data, int ind, const char** out) -> bool
                {
                    auto item = reinterpret_cast<ObjectList*>(data)->begin();
                    std::advance(item, ind);
                    *out = (*item)->GetName();
                    return true;
                },
                reinterpret_cast<void*>(lst), lst->size(), 7);
            ImGui::PopStyleVar(2);
        }
        ImGui::Separator();
        ImGui::EndGroup();
    }
    if (LTools->GetToolForm())
        LTools->GetToolForm()->Draw();
    ImGui::End();
}
