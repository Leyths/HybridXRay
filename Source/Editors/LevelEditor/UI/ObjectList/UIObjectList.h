#pragma once
class CCustomObject;
class CFolderObject;
class UIObjectList: public xrUI
{
    friend class UIObjectListItem;

public:
    UIObjectList();
    virtual ~UIObjectList();
    virtual void   Draw();
    static void    Update();
    static void    Show();
    static void    Close();
    static IC bool IsOpen()
    {
        return Form;
    }
    static void Refresh();

    // Folder operations
    static bool IsFolderAllowedForClass(ObjClassID cls);
    static void CreateFolderForCurrentClass();
    // `dragged` is the actual item the user grabbed (from the ImGui payload). The helpers
    // build the move set as { dragged } + { other selected items that are neither ancestors
    // nor descendants of items already in the set }, which is the single source of truth
    // for what these operations act on. Passing the dragged item explicitly fixes the
    // residual-selection bug where e.g. a previously-selected parent folder would piggyback
    // when the user dragged one of its children — see ReparentSelectedTo's comment.
    static void ReparentSelectedTo(CFolderObject* target, CCustomObject* dragged);   // target == NULL → unparent (root)
    static void ReorderSelectedBefore(CCustomObject* target, CCustomObject* dragged);

private:
    static UIObjectList* Form;

private:
    void DrawObjects();
    void DrawObject(CCustomObject* obj, const char* name);
    // Walks every list row depth-first and accumulates the ones that
    // (a) pass the active filter + visibility mode and (b) are selected.
    // Used by the "go to next selected" target button — index into the
    // returned vector cycles to give the user N presses to walk all
    // selected items in list order.
    void CollectSelectedForGoto(UITreeItem* parent, xr_vector<UIObjectListItem*>& out) const;

private:
    ObjClassID m_cur_cls;
    enum EMode
    {
        M_All,
        M_Visible,
        M_Inbvisible
    };
    EMode             m_Mode;
    string_path       m_Filter;
    UIObjectListItem  m_Root;
    UIObjectListItem* m_LastSelected;
    // "Go to next selected" state. m_PendingScrollToSelected is set by the
    // target button and cleared the same frame in DrawObjects. m_NextGotoIndex
    // cycles through the selected-item list so repeated clicks walk through
    // every selection before wrapping. m_ScrollToItem is the row picked this
    // frame — UIObjectListItem::Draw calls SetScrollHereY when it draws it
    // and clears the pointer.
    bool              m_PendingScrollToSelected;
    int               m_NextGotoIndex;
    UIObjectListItem* m_ScrollToItem;
};
