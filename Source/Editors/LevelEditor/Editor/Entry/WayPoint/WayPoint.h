#pragma once

class CFrustum;
class CWayPoint;

struct SWPLink
{
    CWayPoint* way_point;
    float      probability;
    //			    SWPLink():way_point(0),probability(0){}
    SWPLink(CWayPoint* wp, float pb): way_point(wp), probability(pb) {}
};
DEFINE_VECTOR(SWPLink*, WPLVec, WPLIt);

class CWayPoint
{
    friend class CPatrolPoint;
    friend class CPatrolPath;
    friend class CWayObject;
    friend class TfrmPropertiesWayPoint;
    friend class ESceneWayTool;  // pair-line render reads m_vPosition directly
    shared_str m_Name;
    Fvector    m_vPosition;
    Flags32    m_Flags;
    BOOL       m_bSelected;
    WPLVec     m_Links;
    void       CreateLink(CWayPoint* P, float pb);
    bool       AppendLink(CWayPoint* P, float pb);
    bool       DeleteLink(CWayPoint* P);

public:
    CWayPoint(LPCSTR name);
    ~CWayPoint();
    // display_color: ARGB tint for this point's cross marker and unselected-link
    // lines. Selection highlights (yellow link, white box) still override.
    void Render(LPCSTR parent_name, bool bParentSelect, u32 display_color);
    bool RayPick(float& distance, const Fvector& S, const Fvector& D);
    bool FrustumPick(const CFrustum& frustum);
    bool FrustumSelect(int flag, const CFrustum& frustum);
    void Select(int flag);
    void MoveTo(const Fvector& pos)
    {
        m_vPosition.set(pos);
    }
    bool  AddSingleLink(CWayPoint* P);
    bool  AddDoubleLink(CWayPoint* P);
    bool  RemoveLink(CWayPoint* P);
    void  InvertLink(CWayPoint* P);
    void  Convert1Link(CWayPoint* P);
    void  Convert2Link(CWayPoint* P);
    WPLIt FindLink(CWayPoint* P);
    void  GetBox(Fbox& bb);
};

DEFINE_VECTOR(CWayPoint*, WPVec, WPIt);

class CWayObject: public CCustomObject
{
protected:
    friend class TfrmPropertiesWayPoint;
    friend class CPatrolPath;
    friend class CPatrolPoint;
    friend class ESceneWayTool;  // pair-line render reaches m_WayPoints directly
    EWayType              m_Type;
    WPVec                 m_WayPoints;
    // Optional per-way display tint. When m_HasColorOverride is FALSE,
    // GetDisplayColor() infers a default from the way's name suffix
    // (_walk -> blue, _look -> teal, otherwise green). Persisted via the
    // optional WAYOBJECT_CHUNK_COLOR chunk; written only when overridden.
    BOOL                  m_HasColorOverride;
    Fcolor                m_ColorOverride;
    typedef CCustomObject inherited;
    CWayPoint*            FindWayPoint(const shared_str& nm);
    void                  FindWPByName(LPCSTR new_name, bool& res)
    {
        res = !!FindWayPoint(new_name);
    }
    bool OnWayPointNameAfterEdit(PropValue* sender, shared_str& edit_val);
    void OnNameChange(PropValue* sender);

public:
    CWayObject(LPVOID data, LPCSTR name);
    void Construct(LPVOID data);
    virtual ~CWayObject();
    void         Clear();
    virtual bool CanAttach()
    {
        return true;
    }

    EWayType GetType()
    {
        return m_Type;
    }

    virtual void Select(int flag);
    virtual bool RaySelect(int flag, const Fvector& start, const Fvector& dir, bool bRayTest = false);   // flag 1,0,-1 (-1 invert)
    virtual bool FrustumSelect(int flag, const CFrustum& frustum);

    CWayPoint*   AppendWayPoint();
    CWayPoint*   GetFirstSelected();
    int          GetSelectedPoints(WPVec& lst);
    void         RemoveSelectedPoints();
    void         RemoveLink();
    void         InvertLink();
    void         Convert1Link();
    void         Convert2Link();
    bool         Add1Link();
    bool         Add2Link();
    // change position/orientation methods
    virtual void MoveTo(const Fvector& pos, const Fvector& up);
    virtual void Move(Fvector& amount);
    virtual void RotateParent(Fvector& axis, float angle)
    {
        ;
    }
    virtual void RotateLocal(Fvector& axis, float angle)
    {
        ;
    }
    virtual void RotatePivot(const Fmatrix& prev_inv, const Fmatrix& current)
    {
        ;
    }
    virtual void Scale(Fvector& amount)
    {
        ;
    }
    virtual void ScalePivot(const Fmatrix& prev_inv, const Fmatrix& current, Fvector& amount)
    {
        ;
    }

    virtual void           OnUpdateTransform();

    virtual bool           GetBox(Fbox& box);
    virtual void           Render(int priority, bool strictB2F);
    virtual bool           RayPick(float& distance, const Fvector& S, const Fvector& D, SRayPickInfo* pinf = NULL);
    virtual bool           FrustumPick(const CFrustum& frustum);

    virtual bool           LoadStream(IReader&);
    virtual bool           LoadLTX(CInifile& ini, LPCSTR sect_name);
    virtual void           SaveStream(IWriter&);
    virtual void           SaveLTX(CInifile& ini, LPCSTR sect_name);

    // Populate this way object from a compiled level.game patrol sub-chunk.
    // Reads WAYOBJECT_CHUNK_POINTS (per point: vec3 pos, u32 flags, stringZ
    // name) and WAYOBJECT_CHUNK_LINKS (per edge: u16 from, u16 to, float
    // probability). The name and version chunks are expected to have already
    // been consumed by the caller. Returns false if either chunk is missing
    // or claims a count that overflows the reader.
    bool                   LoadFromLevelGame(IReader& sub_chunk);

    virtual bool           ExportGame(SExportStreams* data);

    virtual void           FillProp(LPCSTR pref, PropItemVec& items);

    virtual bool           OnSelectionRemove();

    // Effective ARGB tint for this way, applying override if set, otherwise
    // the suffix-based default (_walk -> blue, _look -> teal, else green).
    u32                    GetDisplayColor() const;

    // True when the way's name ends in the given suffix (case-insensitive).
    // Used by ESceneWayTool to find matching _walk/_look pairs.
    bool                   NameEndsWith(LPCSTR suffix) const;

    virtual const Fvector& GetPosition() const
    {
        return m_WayPoints.front()->m_vPosition;
    }
    virtual void SetPosition(const Fvector& pos)
    {
        MoveTo(pos, Fvector().set(0, 1, 0));
        UpdateTransform();
    }
};
