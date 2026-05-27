#pragma once

class CFolderObject: public CCustomObject
{
    typedef CCustomObject inherited;

    struct SFolderChild
    {
        CCustomObject* pObject;
        shared_str     ObjectName;
        SFolderChild(): pObject(NULL) {}
        bool operator==(const CCustomObject* obj) const
        {
            return obj == pObject;
        }
    };
    typedef xr_list<SFolderChild> ChildList;
    ChildList                     m_Children;
    ObjClassID                    m_FolderClass;
    bool                          m_bCollapsed;

public:
    CFolderObject(LPVOID data, LPCSTR name);
    void Construct(LPVOID data);
    virtual ~CFolderObject();

    ObjClassID GetFolderClass() const { return m_FolderClass; }
    void       SetFolderClass(ObjClassID cls) { m_FolderClass = cls; }

    bool       IsCollapsed() const { return m_bCollapsed; }
    void       SetCollapsed(bool v) { m_bCollapsed = v; }

    u32        ChildCount() const { return m_Children.size(); }
    u32        GetChildren(ObjectList& lst) const;

    bool       AddChild(CCustomObject* obj);
    bool       RemoveChild(CCustomObject* obj);

    void       PropagateShow(bool show);

    // CCustomObject overrides
    virtual bool CanAttach() { return false; }

    virtual void Select(int flag);
    virtual void Render(int priority, bool strictB2F) {}
    virtual bool GetBox(Fbox& box);

    virtual void MoveTo(const Fvector& pos, const Fvector& up) {}
    virtual void Move(Fvector& amount) {}
    virtual void RotateParent(Fvector& axis, float angle) {}
    virtual void RotateLocal(Fvector& axis, float angle) {}
    virtual void RotatePivot(const Fmatrix& prev_inv, const Fmatrix& current) {}
    virtual void Scale(Fvector& amount) {}
    virtual void ScalePivot(const Fmatrix& prev_inv, const Fmatrix& current, Fvector& amount) {}

    virtual bool RayPick(float& dist, const Fvector& start, const Fvector& dir, SRayPickInfo* pinf = NULL) { return false; }
    virtual bool FrustumPick(const CFrustum& frustum) { return false; }

    virtual bool LoadStream(IReader&);
    virtual bool LoadLTX(CInifile& ini, LPCSTR sect_name);
    virtual void SaveStream(IWriter&);
    virtual void SaveLTX(CInifile& ini, LPCSTR sect_name);

    virtual bool ExportGame(SExportStreams* data) { return true; }

    virtual void FillProp(LPCSTR pref, PropItemVec& items);

    virtual void OnObjectRemove(const CCustomObject* object);
    virtual void OnSceneUpdate();
    virtual bool OnSelectionRemove();

    virtual void OnShowHint(AStringVec& dest);
};
