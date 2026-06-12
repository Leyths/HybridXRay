#include "stdafx.h"

UITreeItem::UITreeItem(shared_str _Name, SLocalizedString _HintText): Name(_Name), HintText(_HintText)
{
    Owner = nullptr;
}

UITreeItem::~UITreeItem()
{
    for (UITreeItem* Item: Items)
    {
        xr_delete(Item);
    }
}

UITreeItem* UITreeItem::AppendItem(const char* _Path, SLocalizedString _HintText, char _PathChar)
{
    VERIFY(_Path && *_Path);
    if (_PathChar && strchr(_Path, _PathChar))
    {
        string_path Name;
        xr_strcpy(Name, _Path);
        strchr(Name, _PathChar)[0] = 0;
        shared_str  Key            = Name;
        auto        it             = ChildIndex.find(Key);
        UITreeItem* Item           = it == ChildIndex.end() ? nullptr : it->second;
        if (!Item)
        {
            Item        = CreateItem(Key, _HintText);
            Item->Owner = this;
            Items.push_back(Item);
            ChildIndex.insert(std::make_pair(Key, Item));
        }
        return Item->AppendItem(strchr(_Path, _PathChar) + 1, _HintText);
    }
    else
    {
        shared_str  Key  = _Path;
        auto        it   = ChildIndex.find(Key);
        UITreeItem* Item = it == ChildIndex.end() ? nullptr : it->second;
        if (!Item)
        {
            Item        = CreateItem(Key, _HintText);
            Item->Owner = this;
            Items.push_back(Item);
            ChildIndex.insert(std::make_pair(Key, Item));
        }
        return Item;
    }
}

UITreeItem* UITreeItem::FindItem(const char* _Path, char _PathChar)
{
    if (_PathChar && strchr(_Path, _PathChar))
    {
        string_path Name;
        xr_strcpy(Name, _Path);
        strchr(Name, _PathChar)[0] = 0;
        UITreeItem* Item           = FindItem(Name);
        if (Item)
        {
            return Item->FindItem(strchr(_Path, _PathChar) + 1);
        }
        return nullptr;
    }
    shared_str FName = _Path;
    auto       it    = ChildIndex.find(FName);
    return it == ChildIndex.end() ? nullptr : it->second;
}

UITreeItem* UITreeItem::CreateItem(shared_str _Name, SLocalizedString _HintText)
{
    return xr_new<UITreeItem>(_Name, _HintText);
}

void UITreeItem::ShowHintIfHovered()
{
    if (HintText.StringEN && HintText.StringRU && ImGui::IsItemHovered())
    {
        if (EditorLocalization == ELocalization::EN)
            ImGui::SetTooltip(HintText.StringEN);
        else
            ImGui::SetTooltip(HintText.StringRU);
    }
}
