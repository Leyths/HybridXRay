#include "stdafx.h"

// Walk the UI tree depth-first and invoke `fn` on every selected
// UIObjectListItem. Used by the Object List panel buttons so they can affect
// items that live inside folders (which are not in m_Root.Items directly).
template <class Fn>
static void VisitSelectedItems(UITreeItem* parent, Fn&& fn)
{
    if (!parent)
        return;
    for (UITreeItem* Item: parent->Items)
    {
        UIObjectListItem* RItem = static_cast<UIObjectListItem*>(Item);
        if (RItem->bIsSelected && RItem->Object)
            fn(RItem);
        VisitSelectedItems(Item, fn);
    }
}

UIObjectList* UIObjectList::Form = nullptr;
UIObjectList::UIObjectList(): m_Root("")
{
    m_Mode      = M_All;
    m_Filter[0] = 0;
}

UIObjectList::~UIObjectList() {}

void UIObjectList::Draw()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(400, 400));

    if (!ImGui::Begin("Object List"_RU >> u8"Список Объектов", &bOpen))
    {
        ImGui::PopStyleVar(1);
        ImGui::End();
        return;
    }
    {
        ImGui::BeginGroup();
        DrawObjects();

        ImGui::SetNextItemWidth(-130);
        if (ImGui::InputText("##value", m_Filter, sizeof(m_Filter)))
        {
            m_Root.ClearSelcted();
        }
        ImGui::EndGroup();
    }
    ImGui::SameLine();
    if (ImGui::BeginChild("Right", ImVec2(130, 0)))
    {
        if (ImGui::RadioButton("All"_RU >> u8"ВСЕ", m_Mode == M_All))
        {
            m_Mode = M_All;
            m_Root.ClearSelcted();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::RadioButton("Visible Only"_RU >> u8"Только видимые", m_Mode == M_Visible))
        {
            m_Mode = M_Visible;
            m_Root.ClearSelcted();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::RadioButton("Invisible Only"_RU >> u8"Только скрытые", m_Mode == M_Inbvisible))
        {
            m_Mode = M_Inbvisible;
            m_Root.ClearSelcted();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::Separator();
        if (ImGui::Button("Show Selected"_RU >> u8"Показать выбранное", ImVec2(-1, 0)))
        {
            VisitSelectedItems(&m_Root, [](UIObjectListItem* RItem) {
                RItem->Object->Show(TRUE);
                if (RItem->Object->FClassID == OBJCLASS_FOLDER)
                    ((CFolderObject*)RItem->Object)->PropagateShow(true);
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ImGui::Button("Hide Selected"_RU >> u8"Скрыть выбранное", ImVec2(-1, 0)))
        {
            VisitSelectedItems(&m_Root, [](UIObjectListItem* RItem) {
                RItem->Object->Show(FALSE);
                if (RItem->Object->FClassID == OBJCLASS_FOLDER)
                    ((CFolderObject*)RItem->Object)->PropagateShow(false);
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::Separator();
        if (ImGui::Button("Focus on Selected"_RU >> u8"Фокус на выбранном", ImVec2(-1, 0)))
        {
            VisitSelectedItems(&m_Root, [](UIObjectListItem* RItem) {
                RItem->Object->Select(true);
                Fbox bb;
                if (RItem->Object->GetBox(bb))
                    EDevice->m_Camera.ZoomExtents(bb);
            });
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        ImGui::Separator();
        const bool can_add_folder = IsFolderAllowedForClass(m_cur_cls);
        if (!can_add_folder)
            ImGui::BeginDisabled();
        if (ImGui::Button("+ Folder"_RU >> u8"+ Папка", ImVec2(-1, 0)))
        {
            CreateFolderForCurrentClass();
        }
        if (!can_add_folder)
            ImGui::EndDisabled();
        if (ImGui::IsItemHovered() && can_add_folder)
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    ImGui::EndChild();

    ImGui::PopStyleVar(1);
    ImGui::End();
}

void UIObjectList::Update()
{
    if (Form)
    {
        if (!Form->IsClosed())
        {
            Form->Draw();
        }
        else
        {
            xr_delete(Form);
        }
    }
}

void UIObjectList::Show()
{
    if (Form == nullptr)
        Form = xr_new<UIObjectList>();
    Refresh();
}

void UIObjectList::Close()
{
    xr_delete(Form);
}

// Sort each parent's children so that folders appear first (alphabetically by
// name), followed by leaf objects in their original order. Recursive across the
// whole tree.
static void SortFoldersFirst(UITreeItem* parent)
{
    if (!parent)
        return;
    std::stable_sort(parent->Items.begin(), parent->Items.end(), [](UITreeItem* a, UITreeItem* b) {
        UIObjectListItem* ia       = static_cast<UIObjectListItem*>(a);
        UIObjectListItem* ib       = static_cast<UIObjectListItem*>(b);
        bool              a_folder = ia->Object && ia->Object->FClassID == OBJCLASS_FOLDER;
        bool              b_folder = ib->Object && ib->Object->FClassID == OBJCLASS_FOLDER;
        if (a_folder != b_folder)
            return a_folder;   // folders first
        if (a_folder)          // both folders — alphabetical
            return _stricmp(ia->Name.c_str(), ib->Name.c_str()) < 0;
        return false;          // both leaves — preserve insertion order (stable sort)
    });
    for (UITreeItem* child: parent->Items)
        SortFoldersFirst(child);
}

void UIObjectList::Refresh()
{
    if (Form == nullptr)
        return;
    Form->m_Root    = UIObjectListItem("");
    Form->m_cur_cls = LTools->CurrentClassID();

    if (Form->m_cur_cls == OBJCLASS_DUMMY)
    {
        // Flat list — show everything (including folders, but without their hierarchy).
        for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
        {
            ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);
            if (ot && (it->first != (ObjClassID)OBJCLASS_DUMMY))
            {
                ObjectList& lst = ot->GetObjects();
                for (CCustomObject* Obj: lst)
                {
                    if (Obj->GetName() == 0 || Obj->GetName()[0] == 0)
                        continue;
                    UIObjectListItem* Item = static_cast<UIObjectListItem*>(Form->m_Root.AppendItem(Obj->GetName(), {}, 0));
                    VERIFY(Item);
                    Item->Object = Obj;
                }
            }
        }
    }
    else
    {
        // Hierarchical view: build a tree using m_pOwnerObject pointing at folder objects
        // of our class affinity. We use multiple passes so children only appear after
        // their parent item has been created.
        ObjectList all_for_class;

        ESceneCustomOTool* ot = Scene->GetOTool(Form->m_cur_cls);
        if (ot)
        {
            ObjectList& lst = ot->GetObjects();
            for (CCustomObject* Obj: lst)
                all_for_class.push_back(Obj);
        }
        ESceneCustomOTool* fot = Scene->GetOTool(OBJCLASS_FOLDER);
        if (fot)
        {
            ObjectList& flst = fot->GetObjects();
            for (CCustomObject* Obj: flst)
            {
                CFolderObject* fo = (CFolderObject*)Obj;
                if (fo->GetFolderClass() == Form->m_cur_cls)
                    all_for_class.push_back(fo);
            }
        }

        // map: folder object → its UIObjectListItem
        xr_map<CCustomObject*, UIObjectListItem*> item_map;
        bool                                      progressed = true;
        u32                                       placed     = 0;
        // Loop until no more progress can be made (handles dangling parents safely).
        while (progressed)
        {
            progressed = false;
            for (CCustomObject* Obj: all_for_class)
            {
                if (item_map.find(Obj) != item_map.end())
                    continue;
                if (Obj->GetName() == 0 || Obj->GetName()[0] == 0)
                    continue;

                CCustomObject* parent_obj = Obj->m_pOwnerObject;
                if (parent_obj && parent_obj->FClassID != OBJCLASS_FOLDER)
                    parent_obj = NULL;
                // Defensive: if parent exists but is for a different class, treat as root.
                if (parent_obj && ((CFolderObject*)parent_obj)->GetFolderClass() != Form->m_cur_cls)
                    parent_obj = NULL;

                UITreeItem* parent_item = nullptr;
                if (parent_obj)
                {
                    xr_map<CCustomObject*, UIObjectListItem*>::iterator pit = item_map.find(parent_obj);
                    if (pit == item_map.end())
                        continue;   // wait for parent
                    parent_item = pit->second;
                }
                else
                {
                    parent_item = &Form->m_Root;
                }

                UIObjectListItem* Item = static_cast<UIObjectListItem*>(parent_item->AppendItem(Obj->GetName(), {}, 0));
                VERIFY(Item);
                Item->Object  = Obj;
                item_map[Obj] = Item;
                placed++;
                progressed = true;
            }
        }

        // Anything left unplaced (broken parent chain) — emit at root.
        for (CCustomObject* Obj: all_for_class)
        {
            if (item_map.find(Obj) != item_map.end())
                continue;
            if (Obj->GetName() == 0 || Obj->GetName()[0] == 0)
                continue;
            UIObjectListItem* Item = static_cast<UIObjectListItem*>(Form->m_Root.AppendItem(Obj->GetName(), {}, 0));
            VERIFY(Item);
            Item->Object  = Obj;
            item_map[Obj] = Item;
        }
    }

    SortFoldersFirst(&Form->m_Root);
    Form->m_LastSelected = nullptr;
}

bool UIObjectList::IsFolderAllowedForClass(ObjClassID cls)
{
    switch (cls)
    {
        case OBJCLASS_SCENEOBJECT:
        case OBJCLASS_LIGHT:
        case OBJCLASS_SHAPE:
        case OBJCLASS_SPAWNPOINT:
        case OBJCLASS_WAY:
        case OBJCLASS_SECTOR:
        case OBJCLASS_PORTAL:
            return true;
        default:
            return false;
    }
}

void UIObjectList::CreateFolderForCurrentClass()
{
    if (Form == nullptr)
        return;
    ObjClassID cls = LTools->CurrentClassID();
    if (!IsFolderAllowedForClass(cls))
        return;

    string256 name;
    Scene->GenObjectName(OBJCLASS_FOLDER, name, "folder");
    CFolderObject* folder = xr_new<CFolderObject>((LPVOID)0, name);
    folder->SetFolderClass(cls);
    Scene->AppendObject(folder, true);
    Refresh();
}

void UIObjectList::ReparentSelectedTo(CFolderObject* target)
{
    if (Form == nullptr)
        return;

    // Collect all currently-selected scene objects (not just the UI list items —
    // selection lives on the scene objects so this picks up multi-select correctly).
    ObjClassID expected_class = target ? target->GetFolderClass() : (ObjClassID)Form->m_cur_cls;
    ObjectList selected_list;
    for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    {
        ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);
        if (!ot)
            continue;
        for (CCustomObject* Obj: ot->GetObjects())
            if (Obj->Selected())
                selected_list.push_back(Obj);
    }

    if (selected_list.empty())
        return;

    bool modified = false;
    for (CCustomObject* Obj: selected_list)
    {
        if (Obj == (CCustomObject*)target)
            continue;

        // Class compatibility check.
        if (Obj->FClassID == OBJCLASS_FOLDER)
        {
            CFolderObject* fo = (CFolderObject*)Obj;
            if (target && fo->GetFolderClass() != target->GetFolderClass())
                continue;
            if (!target && fo->GetFolderClass() != (ObjClassID)Form->m_cur_cls)
                continue;
        }
        else
        {
            if (target && Obj->FClassID != target->GetFolderClass())
                continue;
            if (!target && Obj->FClassID != (ObjClassID)Form->m_cur_cls)
                continue;
        }

        // Detach from previous folder if applicable.
        if (Obj->m_pOwnerObject && Obj->m_pOwnerObject->FClassID == OBJCLASS_FOLDER)
        {
            ((CFolderObject*)Obj->m_pOwnerObject)->RemoveChild(Obj);
        }

        if (target)
        {
            if (target->AddChild(Obj))
                modified = true;
        }
        else
        {
            Obj->m_pOwnerObject = NULL;
            Obj->m_CO_Flags.set(CCustomObject::flObjectInFolder, FALSE);
            modified = true;
        }
    }

    if (modified)
    {
        Scene->UndoSave();
        Refresh();
    }
}

void UIObjectList::ReorderSelectedBefore(CCustomObject* target)
{
    if (Form == nullptr || !target)
        return;

    // Folder rows are reparent targets, not reorder targets — early out.
    if (target->FClassID == OBJCLASS_FOLDER)
        return;

    // Target's visual parent (a folder, or NULL for root). Sources will get
    // reparented to this if they aren't already siblings.
    CFolderObject* target_parent = nullptr;
    if (target->m_pOwnerObject && target->m_pOwnerObject->FClassID == OBJCLASS_FOLDER)
        target_parent = (CFolderObject*)target->m_pOwnerObject;

    // Snapshot of currently-selected scene objects across all tools (so multi-drag
    // from anywhere works). We iterate per-tool m_Objects so the relative order
    // among same-class sources is preserved in the result.
    xr_vector<CCustomObject*> sources;
    for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    {
        ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);
        if (!ot)
            continue;
        for (CCustomObject* obj: ot->GetObjects())
            if (obj != target && obj->Selected())
                sources.push_back(obj);
    }
    if (sources.empty())
        return;

    // Pass 1: reparent each source whose parent differs from target's. This
    // sets m_pOwnerObject + updates the folder's m_Children tracking.
    bool any_changed = false;
    for (CCustomObject* src: sources)
    {
        CFolderObject* src_parent = nullptr;
        if (src->m_pOwnerObject && src->m_pOwnerObject->FClassID == OBJCLASS_FOLDER)
            src_parent = (CFolderObject*)src->m_pOwnerObject;

        if (src_parent == target_parent)
            continue;

        // Class-compatibility check when reparenting into a folder.
        if (target_parent)
        {
            if (src->FClassID == OBJCLASS_FOLDER)
            {
                CFolderObject* sf = (CFolderObject*)src;
                if (sf->GetFolderClass() != target_parent->GetFolderClass())
                    continue;
            }
            else if (src->FClassID != target_parent->GetFolderClass())
            {
                continue;
            }
        }

        if (src_parent)
            src_parent->RemoveChild(src);

        if (target_parent)
        {
            target_parent->AddChild(src);
        }
        else
        {
            src->m_pOwnerObject = NULL;
            src->m_CO_Flags.set(CCustomObject::flObjectInFolder, FALSE);
        }
        any_changed = true;
    }

    // Pass 2: splice same-class sources to be right before target in the class
    // tool's underlying m_Objects list, so the visual order is "before target".
    ESceneCustomOTool* ot = Scene->GetOTool(target->FClassID);
    if (ot)
    {
        ObjectList& list      = ot->GetObjects();
        ObjectIt    target_it = std::find(list.begin(), list.end(), target);
        if (target_it != list.end())
        {
            for (CCustomObject* src: sources)
            {
                if (src->FClassID != target->FClassID)
                    continue;
                ObjectIt src_it = std::find(list.begin(), list.end(), src);
                if (src_it != list.end() && src_it != target_it)
                {
                    list.splice(target_it, list, src_it);
                    any_changed = true;
                }
            }
        }
    }

    if (any_changed)
    {
        Scene->UndoSave();
        Refresh();
    }
}

void UIObjectList::DrawObjects()
{
    if (LTools->CurrentClassID() != m_cur_cls)
        Refresh();

    static ImGuiTableFlags flags =
        ImGuiTableFlags_BordersV
        | ImGuiTableFlags_BordersOuterH
        | ImGuiTableFlags_Resizable
        | ImGuiTableFlags_RowBg
        | ImGuiTableFlags_NoBordersInBody
        | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("objects", 1, flags, ImVec2(-130, -ImGui::GetFrameHeight() - 4)))
    {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("Objects"_RU >> u8"Объекты", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        m_Root.DrawRoot();

        // Unparent drop zone — a final wide row at the bottom that accepts drops to
        // move items back to the root level. Not disabled (a disabled Selectable
        // doesn't register as a drop target).
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushID("##unparent_zone");
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
        ImGui::Selectable("(drop here to unparent)"_RU >> u8"(перетащите сюда чтобы убрать из папки)", false, 0, ImVec2(-1, ImGui::GetTextLineHeight() * 1.5f));
        ImGui::PopStyleColor();
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJLIST_ITEM");
            if (payload)
                ReparentSelectedTo(NULL);
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();

        ImGui::EndTable();
    }
}
