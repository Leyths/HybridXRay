#pragma once
class UIObjectListItem: public UITreeItem
{
public:
    UIObjectListItem(shared_str Name);
    virtual ~UIObjectListItem();

    bool           bIsSelected;
    CCustomObject* Object;
    void           Draw();
    void           DrawRoot();
    void           ClearSelcted(UIObjectListItem* Without = nullptr);

    // True if this item's name matches the active Object List filter, or — for
    // folders — if any descendant does. Static so it can be called for items
    // other than `this` (e.g. the LastSelected endpoint during shift-range
    // selection). Lives as a member so it inherits UIObjectList's `friend class`
    // grant and can read the private filter/Form pointers.
    static bool MatchesFilterRecursive(UIObjectListItem* item);

protected:
    UITreeItem* CreateItem(shared_str Name, SLocalizedString _HintText) override;
};
