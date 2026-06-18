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
        if (RItem->bIsSelected && (RItem->Object || RItem->m_Wallmark))
            fn(RItem);
        VisitSelectedItems(Item, fn);
    }
}

void UIObjectList::CollectSelectedForGoto(UITreeItem* parent, xr_vector<UIObjectListItem*>& out) const
{
    if (!parent)
        return;
    for (UITreeItem* Item: parent->Items)
    {
        UIObjectListItem* it = static_cast<UIObjectListItem*>(Item);
        // Filter: mirror Draw's MatchesFilterRecursive gate. Folders that match
        // only by descendant still pass — we still descend into them looking
        // for the actual leaves.
        if (m_Filter[0] && !UIObjectListItem::MatchesFilterRecursive(it))
            continue;
        // Visibility-mode filter mirrors UIObjectListItem::Draw's early-return:
        // a mode-hidden folder takes its children off the rendered tree, so we
        // also stop recursing here.
        if (it->Object && !it->m_Wallmark)
        {
            if (m_Mode == M_Visible && !it->Object->Visible())
                continue;
            if (m_Mode == M_Inbvisible && it->Object->Visible())
                continue;
        }
        if (it->m_Wallmark)
        {
            const bool hidden = !!it->m_Wallmark->flags.is(ESceneWallmarkTool::wallmark::flHidden);
            if (m_Mode == M_Visible && hidden)
                continue;
            if (m_Mode == M_Inbvisible && !hidden)
                continue;
        }
        bool sel = false;
        if (it->Object && it->Object->Selected())
            sel = true;
        if (it->m_Wallmark && it->m_Wallmark->flags.is(ESceneWallmarkTool::wallmark::flSelected))
            sel = true;
        if (sel)
            out.push_back(it);
        CollectSelectedForGoto(it, out);
    }
}

UIObjectList* UIObjectList::Form = nullptr;
UIObjectList::UIObjectList(): m_Root("")
{
    m_Mode                    = M_All;
    m_Filter[0]               = 0;
    m_LastSelected            = nullptr;
    m_PendingScrollToSelected = false;
    m_NextGotoIndex           = 0;
    m_ScrollToItem            = nullptr;
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
                if (RItem->IsWallmark())
                {
                    RItem->m_Wallmark->flags.set(ESceneWallmarkTool::wallmark::flHidden, FALSE);
                    UI->RedrawScene();
                    return;
                }
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
                if (RItem->IsWallmark())
                {
                    RItem->m_Wallmark->flags.set(ESceneWallmarkTool::wallmark::flHidden, TRUE);
                    // Hidden wallmarks can't be ray-picked either, so drop the
                    // selection state — otherwise a later Show would resurrect
                    // them in their previous selected state, which is fine, but
                    // keeping them "selected while hidden" leaks ghost selection.
                    RItem->m_Wallmark->flags.set(ESceneWallmarkTool::wallmark::flSelected, FALSE);
                    RItem->bIsSelected = false;
                    UI->RedrawScene();
                    return;
                }
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
                if (RItem->IsWallmark())
                {
                    // Use the wallmark's bbox directly; it has no Select()
                    // call but the click handler already set the flag.
                    EDevice->m_Camera.ZoomExtents(RItem->m_Wallmark->bbox);
                    return;
                }
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
    else if (Form->m_cur_cls == OBJCLASS_WM)
    {
        // Wallmarks aren't CCustomObjects (the wallmark tool stores them as
        // raw structs in `wm_slot::items`). We emit one list row per
        // wallmark. Dynamic marks come FIRST (sorted to the top) labelled by
        // their unique `name` (the runtime visibility key). Static marks
        // follow, labelled "<host-object>/<texture-basename> [#N]" when the
        // host scene object is known; older saves with no host info fall
        // back to "<texture-basename> [#N]".
        ESceneWallmarkTool* wmt = (ESceneWallmarkTool*)Scene->GetTool(OBJCLASS_WM);
        if (wmt)
        {
            auto emit_static = [&](ESceneWallmarkTool::wm_slot* slot, const char* tx_base) {
                int idx = 0;
                for (ESceneWallmarkTool::wallmark* w: slot->items)
                {
                    if (w->flags.is(ESceneWallmarkTool::wallmark::flDynamic))
                    {
                        ++idx;   // keep [#N] stable across selective passes
                        continue;
                    }
                    // Lazy resolve: loaded-from-disk wallmarks have no
                    // src_obj_name; recover it via a ray-pick from the
                    // wallmark's surface position. Cached on the wallmark
                    // so subsequent refreshes are free.
                    wmt->EnsureHostObjectName(w);

                    string256   buf;
                    const char* host = w->src_obj_name.size() ? w->src_obj_name.c_str() : nullptr;
                    if (host)
                        xr_sprintf(buf, sizeof(buf), "%s/%s [#%d]", host, tx_base, idx);
                    else
                        xr_sprintf(buf, sizeof(buf), "%s [#%d]", tx_base, idx);
                    ++idx;
                    UIObjectListItem* Item = static_cast<UIObjectListItem*>(Form->m_Root.AppendItem(buf, {}, 0));
                    VERIFY(Item);
                    Item->m_Wallmark  = w;
                    Item->bIsSelected = !!w->flags.is(ESceneWallmarkTool::wallmark::flSelected);
                }
            };
            auto emit_dynamic = [&](ESceneWallmarkTool::wm_slot* slot) {
                for (ESceneWallmarkTool::wallmark* w: slot->items)
                {
                    if (!w->flags.is(ESceneWallmarkTool::wallmark::flDynamic))
                        continue;
                    // Dynamic marks carry their own identifier; that's what
                    // the runtime keys visibility on, so it's what the user
                    // needs to see in the list. Fall back to a placeholder
                    // if the name is missing for any reason.
                    const char* label = w->name.size() ? w->name.c_str() : "(unnamed dynamic)";
                    UIObjectListItem* Item = static_cast<UIObjectListItem*>(Form->m_Root.AppendItem(label, {}, 0));
                    VERIFY(Item);
                    Item->m_Wallmark  = w;
                    Item->bIsSelected = !!w->flags.is(ESceneWallmarkTool::wallmark::flSelected);
                }
            };

            // Pass 1: dynamic marks (top of list).
            for (ESceneWallmarkTool::wm_slot* slot: wmt->marks)
            {
                if (!slot)
                    continue;
                emit_dynamic(slot);
            }
            // Pass 2: static marks (below).
            for (ESceneWallmarkTool::wm_slot* slot: wmt->marks)
            {
                if (!slot)
                    continue;
                const char* tx_full = slot->tx_name.c_str() ? slot->tx_name.c_str() : "";
                const char* tx_base = strrchr(tx_full, '\\');
                tx_base             = tx_base ? tx_base + 1 : tx_full;
                if (!*tx_base)
                    tx_base = "wallmark";
                emit_static(slot, tx_base);
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

// True if `maybe_ancestor` appears in obj's m_pOwnerObject chain.
static bool IsAncestor(CCustomObject* maybe_ancestor, CCustomObject* obj)
{
    if (!maybe_ancestor || !obj)
        return false;
    for (CCustomObject* p = obj->m_pOwnerObject; p; p = p->m_pOwnerObject)
        if (p == maybe_ancestor)
            return true;
    return false;
}

// Build the set of scene objects a drag-drop operation should act on.
//
// The user's drag *intent* is the item from the ImGui payload (`dragged`). On top
// of that we layer the current scene multi-selection, but with two exclusions:
//
//   1. Ancestors of the dragged item are dropped. A residual selection on a
//      parent folder (e.g. user clicked the folder earlier to look at its
//      properties, then expanded it and dragged a child out) must not piggyback
//      the parent into the destination. This was the original drag-drop bug.
//   2. Descendants of anything already in the set are dropped. Moving a folder
//      carries its children via m_pOwnerObject implicitly; listing them again
//      would be redundant and could re-order them oddly inside the destination.
static void CollectDragMoveSet(CCustomObject* dragged, xr_vector<CCustomObject*>& out)
{
    out.clear();
    if (dragged)
        out.push_back(dragged);

    for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    {
        ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);
        if (!ot)
            continue;
        for (CCustomObject* Obj: ot->GetObjects())
        {
            if (!Obj->Selected())
                continue;
            if (Obj == dragged)
                continue;
            if (IsAncestor(Obj, dragged))
                continue;
            if (IsAncestor(dragged, Obj))
                continue;
            // Skip if some earlier-added entry is an ancestor — keeps "topmost only".
            bool covered = false;
            for (CCustomObject* existing: out)
            {
                if (IsAncestor(existing, Obj))
                {
                    covered = true;
                    break;
                }
            }
            if (covered)
                continue;
            out.push_back(Obj);
        }
    }
}

void UIObjectList::ReparentSelectedTo(CFolderObject* target, CCustomObject* dragged)
{
    if (Form == nullptr)
        return;

    xr_vector<CCustomObject*> selected_list;
    CollectDragMoveSet(dragged, selected_list);

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

void UIObjectList::ReorderSelectedBefore(CCustomObject* target, CCustomObject* dragged)
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

    // Honour the dragged-item-vs-residual-selection rules. See CollectDragMoveSet
    // for why this is needed — same bug class as ReparentSelectedTo.
    xr_vector<CCustomObject*> sources;
    CollectDragMoveSet(dragged, sources);
    // The target itself can never be a source.
    sources.erase(std::remove(sources.begin(), sources.end(), target), sources.end());
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

    // Resolve a pending "go to next selected" click. Builds the visible
    // selection list (DFS, filter + mode honoured), advances the cycle
    // index, force-expands the target's ancestor folders so the row
    // actually draws this frame, and stashes the target pointer for
    // UIObjectListItem::Draw to SetScrollHereY on.
    if (m_PendingScrollToSelected)
    {
        m_PendingScrollToSelected = false;
        xr_vector<UIObjectListItem*> selected;
        CollectSelectedForGoto(&m_Root, selected);
        if (!selected.empty())
        {
            if (m_NextGotoIndex < 0 || m_NextGotoIndex >= (int)selected.size())
                m_NextGotoIndex = 0;
            UIObjectListItem* target = selected[m_NextGotoIndex];
            m_NextGotoIndex          = (m_NextGotoIndex + 1) % (int)selected.size();
            for (UITreeItem* p = target->Owner; p && p != &m_Root; p = p->Owner)
            {
                UIObjectListItem* pi = static_cast<UIObjectListItem*>(p);
                if (pi->Object && pi->Object->FClassID == OBJCLASS_FOLDER)
                    ((CFolderObject*)pi->Object)->SetCollapsed(false);
            }
            m_ScrollToItem = target;
        }
    }

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
        // Custom header row: column heading on the left + a small target-icon
        // button overlaid on the right of the same cell. Replaces the default
        // TableHeadersRow() so we can co-locate the "go to next selected" action
        // with the "Objects" label without growing the panel by a toolbar row.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("Objects"_RU >> u8"Объекты");
        {
            // Overlay region: right-edge of the header cell, slightly inset so
            // the icon doesn't touch the column-border line.
            ImVec2 hmin     = ImGui::GetItemRectMin();
            ImVec2 hmax     = ImGui::GetItemRectMax();
            float  h        = hmax.y - hmin.y;
            float  btn_size = h - 2.0f;
            if (btn_size < 8.0f)
                btn_size = 8.0f;
            ImGui::SetCursorScreenPos(ImVec2(hmax.x - btn_size - 2.0f, hmin.y + 1.0f));
            ImVec2 cp = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##goto_selected_btn", ImVec2(btn_size, btn_size));
            const bool  hovered = ImGui::IsItemHovered();
            const bool  clicked = ImGui::IsItemClicked();
            ImDrawList* dl      = ImGui::GetWindowDrawList();
            ImVec2      center(cp.x + btn_size * 0.5f, cp.y + btn_size * 0.5f);
            // Hand-drawn bullseye target so the icon works without depending on
            // dingbat / geometric-symbol glyphs (the editor's font atlas covers
            // Basic Latin + Cyrillic only — no ◎ glyph available).
            ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Text);
            dl->AddCircle(center, btn_size * 0.40f, col, 20, 1.5f);
            dl->AddCircle(center, btn_size * 0.22f, col, 16, 1.5f);
            dl->AddCircleFilled(center, btn_size * 0.08f, col, 8);
            if (hovered)
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Jump to next selected"_RU >> u8"Перейти к следующему выделенному");
            }
            if (clicked)
                m_PendingScrollToSelected = true;
        }
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
            if (payload && payload->DataSize == sizeof(UIObjectListItem*))
            {
                UIObjectListItem* dragged_item = *(UIObjectListItem**)payload->Data;
                CCustomObject*    dragged_obj  = dragged_item ? dragged_item->Object : nullptr;
                ReparentSelectedTo(NULL, dragged_obj);
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::PopID();

        ImGui::EndTable();
    }
}
