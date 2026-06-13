#include "stdafx.h"

UIObjectListItem::UIObjectListItem(shared_str Name): UITreeItem(Name, {})
{
    bIsSelected = false;
    Object      = nullptr;
    m_Wallmark  = nullptr;
}

UIObjectListItem::~UIObjectListItem() {}

// True if this item's name matches the active filter, or — for folders — if any
// descendant does. Without the recursive lookahead, a folder whose own name
// doesn't contain the filter substring would `return` early in Draw() and take
// every matching child down with it. That made the search box useless for any
// item that lived inside a folder.
//
// Compares the displayed Name field rather than Object->GetName() so wallmark
// items (which have no CCustomObject) participate too — their name is the
// synthetic "<texture> [#N]" set at refresh time.
bool UIObjectListItem::MatchesFilterRecursive(UIObjectListItem* item)
{
    if (!item)
        return false;
    const char* filter = UIObjectList::Form->m_Filter;
    if (filter[0] == 0)
        return true;
    const char* name = item->Name.c_str();
    if (name && strstr(name, filter) != 0)
        return true;
    for (UITreeItem* child: item->Items)
    {
        if (MatchesFilterRecursive(static_cast<UIObjectListItem*>(child)))
            return true;
    }
    return false;
}

void UIObjectListItem::Draw()
{
    if (m_Wallmark)
    {
        DrawWallmarkRow();
        return;
    }
    if (!Object)
        return;

    if (UIObjectList::Form->m_Filter[0])
    {
        // For leaves this collapses to the original name-match test. For folders
        // it lets the row through if any descendant matches, so the parent chain
        // up to a hit stays visible.
        if (!MatchesFilterRecursive(this))
            return;
    }
    switch (UIObjectList::Form->m_Mode)
    {
        case UIObjectList::M_All:
            break;
        case UIObjectList::M_Visible:
            if (!Object->Visible())
                return;
            break;
        case UIObjectList::M_Inbvisible:
            if (Object->Visible())
                return;
            break;
        default:
            break;
    }

    const bool is_folder = (Object->FClassID == OBJCLASS_FOLDER);

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags Flags = 0;
    const bool         filter_active = UIObjectList::Form->m_Filter[0] != 0;
    if (is_folder && !Items.empty())
    {
        Flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        CFolderObject* fo = (CFolderObject*)Object;
        // When a filter is active force the folder open so its matching descendants
        // are actually visible. The persisted collapse state is untouched (we just
        // override the visual open state for this frame) so clearing the filter
        // restores whatever the user had before.
        const bool force_open = filter_active;
        ImGui::SetNextItemOpen(force_open || !fo->IsCollapsed());
    }
    else
    {
        Flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }

    if (Object->Selected())
        Flags |= ImGuiTreeNodeFlags_Bullet;
    if (bIsSelected)
        Flags |= ImGuiTreeNodeFlags_Selected;

    // Folders render in a distinct warm amber so they stand out from regular
    // leaf rows even when empty (an empty folder uses the _Leaf flag and would
    // otherwise be indistinguishable from a normal object).
    const bool dim_hidden = !Object->Visible();
    if (dim_hidden)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

    if (is_folder)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.78f, 0.32f, 1.00f));

    bool node_open = ImGui::TreeNodeEx(Name.c_str(), Flags);

    if (is_folder)
        ImGui::PopStyleColor();

    if (dim_hidden)
        ImGui::PopStyleVar();

    // "Go to next selected" landing: DrawObjects picked this item this frame.
    // Scroll the parent table so the row centres in view, then clear the
    // pointer so subsequent items don't try to claim the scroll.
    if (UIObjectList::Form && UIObjectList::Form->m_ScrollToItem == this)
    {
        ImGui::SetScrollHereY(0.5f);
        UIObjectList::Form->m_ScrollToItem = nullptr;
    }

    // Persist collapse state for folders. A click on the expand/collapse arrow
    // fires both IsItemToggledOpen() *and* IsItemClicked() on the same frame —
    // we capture the toggle flag here and use it to suppress the selection
    // branch below, so toggling a folder's children visibility doesn't blow
    // away any selection the user already had.
    bool toggled_open_this_frame = false;
    if (is_folder && !Items.empty())
    {
        CFolderObject* fo = (CFolderObject*)Object;
        if (ImGui::IsItemToggledOpen())
        {
            fo->SetCollapsed(!fo->IsCollapsed());
            toggled_open_this_frame = true;
        }
    }

    // Click handling — same semantics as before, except the arrow-click case
    // (which also fires IsItemClicked) is now ignored so opening/closing a
    // folder is a pure UI gesture with no side effects on scene selection.
    if (ImGui::IsItemClicked() && !toggled_open_this_frame)
    {
        if (ImGui::GetIO().KeyShift)
        {
            bool bStart = UIObjectList::Form->m_LastSelected && UIObjectList::Form->m_LastSelected->Owner == Owner;
            // Disable shift-range only when the endpoint is genuinely *not visible*
            // under the active filter. Use the recursive match so a folder that's
            // visible because a descendant matches still works as a range endpoint.
            if (UIObjectList::Form->m_Filter[0] && UIObjectList::Form->m_LastSelected && UIObjectList::Form->m_LastSelected->Object)
            {
                if (!MatchesFilterRecursive(UIObjectList::Form->m_LastSelected))
                    bStart = false;
            }
            if (bStart)
            {
                UIObjectList::Form->m_Root.ClearSelcted();
                Scene->SelectObjects(false, OBJCLASS_DUMMY);
                bIsSelected = true;
                {
                    size_t StartIndex = 0;
                    size_t EndIndex   = 0;
                    for (UITreeItem* Item: Owner->Items)
                    {
                        UIObjectListItem* RItem = (UIObjectListItem*)Item;
                        if (RItem == UIObjectList::Form->m_LastSelected)
                        {
                            break;
                        }
                        StartIndex++;
                    }
                    for (UITreeItem* Item: Owner->Items)
                    {
                        EndIndex++;
                        UIObjectListItem* RItem = (UIObjectListItem*)Item;
                        if (RItem == this)
                        {
                            break;
                        }
                    }
                    if (StartIndex >= EndIndex)
                    {
                        std::swap(StartIndex, EndIndex);
                        StartIndex--;
                        EndIndex++;
                    }

                    for (size_t i = StartIndex; i < EndIndex; i++)
                    {
                        UIObjectListItem* RItem = (UIObjectListItem*)Owner->Items[i];
                        // Mirror the Draw-time visibility rule: rows hidden by the
                        // filter aren't selectable; folders visible-by-descendant are.
                        if (UIObjectList::Form->m_Filter[0])
                        {
                            if (!MatchesFilterRecursive(RItem))
                                continue;
                        }
                        RItem->Object->Select(true);
                        RItem->bIsSelected = true;
                    }
                }
                UIObjectList::Form->m_LastSelected = this;
            }
        }
        else if (ImGui::GetIO().KeyCtrl)
        {
            Object->Select(true);
            bIsSelected                        = true;
            UIObjectList::Form->m_LastSelected = this;
        }
        else
        {
            // Plain click. If the item is already part of a multi-selection, preserve
            // the selection (otherwise the click that starts a drag would collapse the
            // selection down to one item). Click on an unselected item still snaps to
            // exclusive selection.
            if (!Object->Selected())
            {
                UIObjectList::Form->m_Root.ClearSelcted();
                Scene->SelectObjects(false, OBJCLASS_DUMMY);
                bIsSelected = true;
                Object->Select(true);
            }
            else
            {
                // Already selected — make sure the panel refreshes regardless, so a
                // re-click on the same folder (e.g. after editing somewhere else)
                // brings its properties back into view.
                ExecCommand(COMMAND_UPDATE_PROPERTIES);
            }
            UIObjectList::Form->m_LastSelected = this;
        }
    }

    // Drag source — every row can start a drag.
    if (ImGui::BeginDragDropSource())
    {
        // If the dragged object isn't selected at all, snap selection to just this
        // item so the drag carries a sensible payload. If it's already selected as
        // part of a multi-selection, leave the selection intact.
        if (!Object->Selected())
        {
            UIObjectList::Form->m_Root.ClearSelcted();
            Scene->SelectObjects(false, OBJCLASS_DUMMY);
            bIsSelected = true;
            Object->Select(true);
            UIObjectList::Form->m_LastSelected = this;
        }
        UIObjectListItem* payload = this;
        ImGui::SetDragDropPayload("OBJLIST_ITEM", &payload, sizeof(payload));
        ImGui::Text("Move: %s", Name.c_str());
        ImGui::EndDragDropSource();
    }

    // Drop target — folder rows reparent into themselves; leaf rows reorder before.
    // We dereference the payload to recover the actual dragged item (`UIObjectListItem*`).
    // The helpers use that to build a "drag intent + selection minus ancestors" move set,
    // which is what stops a residual parent-folder selection from piggybacking on a
    // single-child drag.
    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJLIST_ITEM");
        if (payload && payload->DataSize == sizeof(UIObjectListItem*))
        {
            UIObjectListItem* dragged_item = *(UIObjectListItem**)payload->Data;
            CCustomObject*    dragged_obj  = dragged_item ? dragged_item->Object : nullptr;
            if (is_folder)
                UIObjectList::ReparentSelectedTo((CFolderObject*)Object, dragged_obj);
            else
                UIObjectList::ReorderSelectedBefore(Object, dragged_obj);
        }
        ImGui::EndDragDropTarget();
    }

    // Recurse into children for open folder nodes.
    if (is_folder && !Items.empty() && node_open)
    {
        for (UITreeItem* Item: Items)
            ((UIObjectListItem*)Item)->Draw();
        ImGui::TreePop();
    }
}

void UIObjectListItem::DrawRoot()
{
    for (UITreeItem* Item: Items)
    {
        ((UIObjectListItem*)Item)->Draw();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
}

void UIObjectListItem::ClearSelcted(UIObjectListItem* Without)
{
    for (UITreeItem* Item: Items)
    {
        ((UIObjectListItem*)Item)->bIsSelected = false;
        if (Without != (UIObjectListItem*)Item)
            ((UIObjectListItem*)Item)->ClearSelcted();
    }
}

UITreeItem* UIObjectListItem::CreateItem(shared_str Name, SLocalizedString _HintText)
{
    return xr_new<UIObjectListItem>(Name);
}

// Wallmark items get a simplified row: no folder/drag-drop, just a clickable
// label. Click selects (sets flSelected); Show/Hide buttons toggle flHidden
// (per-wallmark visibility). Reparent and folder hierarchy aren't supported.
void UIObjectListItem::DrawWallmarkRow()
{
    if (!m_Wallmark)
        return;

    if (UIObjectList::Form->m_Filter[0])
    {
        if (!MatchesFilterRecursive(this))
            return;
    }

    const bool hidden = !!m_Wallmark->flags.is(ESceneWallmarkTool::wallmark::flHidden);
    switch (UIObjectList::Form->m_Mode)
    {
        case UIObjectList::M_All:
            break;
        case UIObjectList::M_Visible:
            if (hidden)
                return;
            break;
        case UIObjectList::M_Inbvisible:
            if (!hidden)
                return;
            break;
        default:
            break;
    }

    ImGui::TableNextRow();
    ImGui::TableNextColumn();

    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    const bool         wm_selected_in_scene = !!m_Wallmark->flags.is(ESceneWallmarkTool::wallmark::flSelected);
    if (wm_selected_in_scene)
        Flags |= ImGuiTreeNodeFlags_Bullet;
    if (bIsSelected)
        Flags |= ImGuiTreeNodeFlags_Selected;

    if (hidden)
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);

    ImGui::TreeNodeEx(Name.c_str(), Flags);

    if (hidden)
        ImGui::PopStyleVar();

    // "Go to next selected" landing — wallmark variant. See the matching
    // hook in UIObjectListItem::Draw for the same logic.
    if (UIObjectList::Form && UIObjectList::Form->m_ScrollToItem == this)
    {
        ImGui::SetScrollHereY(0.5f);
        UIObjectList::Form->m_ScrollToItem = nullptr;
    }

    if (ImGui::IsItemClicked())
    {
        ESceneWallmarkTool* wmt = (ESceneWallmarkTool*)Scene->GetTool(OBJCLASS_WM);
        if (ImGui::GetIO().KeyCtrl)
        {
            // Ctrl-click: toggle this wallmark's selection, leave others alone.
            m_Wallmark->flags.invert(ESceneWallmarkTool::wallmark::flSelected);
            bIsSelected = m_Wallmark->flags.is(ESceneWallmarkTool::wallmark::flSelected);
        }
        else
        {
            // Plain click: exclusive select.
            if (wmt)
                wmt->SelectObjects(false);
            UIObjectList::Form->m_Root.ClearSelcted();
            m_Wallmark->flags.set(ESceneWallmarkTool::wallmark::flSelected, TRUE);
            bIsSelected = true;
        }
        UIObjectList::Form->m_LastSelected = this;
        UI->RedrawScene();
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    }
}
