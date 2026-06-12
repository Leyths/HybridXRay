#pragma once

class UIEditLibrary: public xrUI
{
public:
    UIEditLibrary();
    virtual ~UIEditLibrary();

    static void Update();
    static void Show();
    static void Close();
    static void OnRender();

    ImTextureID m_NullTexture;
    ImTextureID m_RealTexture;

    void        OnItemFocused(ListItem* item);

private:
    static UIEditLibrary*    Form;

    virtual void             Draw();
    void                     DrawObjects();

    void                     DrawRightBar();
    void                     DrawObject(CCustomObject* obj, const char* name);
    void                     InitObjects();
    void                     OnPropertiesClick();
    void                     OnMakeThmClick();
    void                     OnPreviewClick();

    void                     MakeLOD(bool highQuality);
    void                     GenerateLOD(RStringVec& props, bool bHighQuality);

    void                     RefreshSelected();
    void                     ChangeReference(const RStringVec& items);
    bool                     SelectionToReference(ListItemsVec* props);

    UIItemListForm*          m_ObjectList;
    UIPropertiesForm*        m_Props;
    LPCSTR                   m_Current;
    bool                     m_Preview;
    ListItem*                m_Selected;

    bool                     m_SelectLods;
    bool                     m_HighQualityLod;

    xr_vector<CSceneObject*> m_pEditObjects;

    // Centers + 45/45 rotates the preview SceneObject so the on-screen view
    // matches what the bulk thumbnail maker will capture. Lets the user
    // sanity-check a few thumbs single-shot before committing 80k.
    void                     ApplyThumbnailPose(CSceneObject* SO);
    void                     FrameForThumbnail(CSceneObject* SO);
    // Camera save/restore so toggling Preview doesn't yank the user's view
    // permanently. Saved on first Preview-on, restored on Preview-off /
    // library close / batch completion.
    void                     SaveCameraState();
    void                     RestoreCameraState();
    bool                     m_CameraSaved;
    Fvector                  m_SavedCamHPB;
    Fvector                  m_SavedCamPos;

    // Bulk thumbnail flow.
    u32                      CountMissingThumbnails() const;
    u32                      CountExistingThumbnails() const;
    bool                     ItemThumbExists(ListItem* item) const;
    void                     MakeAllMissingThumbnails();
    void                     ClearAllThumbnails();
    void                     DrawConfirmModals();
    bool                     m_PendingMakeAll;
    bool                     m_PendingClearAll;
    u32                      m_PendingCount;

    /*
    static IC bool IsOpen() { return Form; }
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
    EMode m_Mode;
    CCustomObject* m_SelectedObject;
    string_path m_Filter;*/
};
