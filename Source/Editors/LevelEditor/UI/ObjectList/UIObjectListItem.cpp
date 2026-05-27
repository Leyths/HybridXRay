#include "stdafx.h"

UIObjectListItem::UIObjectListItem(shared_str Name): UITreeItem(Name, {})
{
    bIsSelected = false;
    Object      = nullptr;
}

UIObjectListItem::~UIObjectListItem() {}

void UIObjectListItem::Draw()
{
    if (!Object)
        return;

    if (UIObjectList::Form->m_Filter[0])
    {
        if (strstr(Object->GetName(), UIObjectList::Form->m_Filter) == 0)
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
    if (is_folder && !Items.empty())
    {
        Flags |= ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        CFolderObject* fo = (CFolderObject*)Object;
        ImGui::SetNextItemOpen(!fo->IsCollapsed());
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
    if (is_folder)
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.00f, 0.78f, 0.32f, 1.00f));

    bool node_open = ImGui::TreeNodeEx(Name.c_str(), Flags);

    if (is_folder)
        ImGui::PopStyleColor();

    // Persist collapse state for folders.
    if (is_folder && !Items.empty())
    {
        CFolderObject* fo = (CFolderObject*)Object;
        if (ImGui::IsItemToggledOpen())
            fo->SetCollapsed(!fo->IsCollapsed());
    }

    // Click handling — same semantics as before.
    if (ImGui::IsItemClicked())
    {
        if (ImGui::GetIO().KeyShift)
        {
            bool bStart = UIObjectList::Form->m_LastSelected && UIObjectList::Form->m_LastSelected->Owner == Owner;
            if (UIObjectList::Form->m_Filter[0] && UIObjectList::Form->m_LastSelected && UIObjectList::Form->m_LastSelected->Object)
            {
                if (strstr(UIObjectList::Form->m_LastSelected->Object->GetName(), UIObjectList::Form->m_Filter) == 0)
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
                        if (UIObjectList::Form->m_Filter[0])
                        {
                            if (strstr(RItem->Object->GetName(), UIObjectList::Form->m_Filter) == 0)
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
    if (ImGui::BeginDragDropTarget())
    {
        const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("OBJLIST_ITEM");
        if (payload)
        {
            if (is_folder)
                UIObjectList::ReparentSelectedTo((CFolderObject*)Object);
            else
                UIObjectList::ReorderSelectedBefore(Object);
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
