#pragma once

class ESceneWayTool: public ESceneCustomOTool
{
    typedef ESceneCustomOTool inherited;

    // Last logged (walk_count, look_count) per pair-base-name. OnRender
    // re-evaluates every frame and only emits a Msg() when the tuple changes,
    // so a steady-state mismatch produces a single log line.
    xr_map<xr_string, std::pair<u32, u32>> m_PairLogCache;

protected:
    // controls
    virtual void CreateControls();
    virtual void RemoveControls();

public:
    ESceneWayTool(): ESceneCustomOTool(OBJCLASS_WAY)
    {
        ;
    }
    // definition
    IC LPCSTR ClassName()
    {
        return "way";
    }
    IC LPCSTR ClassDesc()
    {
        return "Way"_RU >> u8"Точки Пути";
    }
    IC int RenderPriority()
    {
        return 1;
    }

    virtual void Clear(bool bSpecific = false)
    {
        inherited::Clear(bSpecific);
        m_PairLogCache.clear();
    }

    virtual void OnRender(int priority, bool strictB2F);
    // IO
    virtual bool IsNeedSave()
    {
        return inherited::IsNeedSave();
    }
    virtual bool           LoadStream(IReader&);
    virtual bool           LoadLTX(CInifile&);
    virtual void           SaveStream(IWriter&);
    virtual void           SaveLTX(CInifile&, int id);
    virtual bool           LoadSelection(IReader&);
    virtual void           SaveSelection(IWriter&);

    virtual void           OnActivate();

    virtual CCustomObject* CreateObject(LPVOID data, LPCSTR name);
};
