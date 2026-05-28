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
};
