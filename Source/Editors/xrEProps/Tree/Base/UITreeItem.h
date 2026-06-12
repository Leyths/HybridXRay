#pragma once
class XREPROPS_API UITreeItem
{
public:
    UITreeItem(shared_str _Name, SLocalizedString _HintText);
    virtual ~UITreeItem();

    UITreeItem*            AppendItem(const char* _Path, SLocalizedString _HintText = {}, char _PathChar = '\\');
    UITreeItem*            FindItem(const char* _Path, char _PathChar = '\\');
    xr_vector<UITreeItem*> Items;
    UITreeItem*            Owner;
    shared_str             Name;
    SLocalizedString       HintText;

protected:
    virtual UITreeItem* CreateItem(shared_str _Name, SLocalizedString _HintText);
    void                ShowHintIfHovered();

private:
    // Parallel name → child map kept in sync with Items so FindItem is
    // O(log N) instead of a linear scan. Without this, FillItems on big
    // choosers (e.g. the texture picker with ~26k entries) was quadratic
    // and locked the UI for ~12s every open. Sort() reorders Items but
    // leaves this map untouched — keys are stable shared_str pointers.
    xr_map<shared_str, UITreeItem*> ChildIndex;
};
