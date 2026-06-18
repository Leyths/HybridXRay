#include "stdafx.h"

// chunks
//
// Version history:
//   0x0003  legacy items (no per-item w/h/r — defaults applied on load)
//   0x0004  ITEMS2 chunk: per-item w/h/r
//   0x0005  + per-item name (dynamic wallmark identifier; empty for static)
#define WM_VERSION                0x0005

#define WM_CHUNK_VERSION          0x0001
#define WM_CHUNK_FLAGS            0x0002
#define WM_CHUNK_PARAMS           0x0003
#define WM_CHUNK_ITEMS            0x0004
#define WM_CHUNK_ITEMS2           0x0005
//----------------------------------------------------

#define MAX_WALLMARK_COUNT        500
#define MAX_WALLMARK_VERTEX_COUNT 8192
#define COMPILER_SHADER           "def_shaders\\def_vertex_ghost_no_shadow"

ESceneWallmarkTool::ESceneWallmarkTool(): ESceneToolBase(OBJCLASS_WM)
{
    m_MarkWidth      = 1.f;
    m_MarkHeight     = 1.f;
    m_MarkRotate     = 0.f;
    m_Flags.assign(flDrawWallmark);
    //.    m_ShName		= "effects\\wallmarkblend";
    m_ShName         = "effects\\wallmarkmult";
    m_TxName         = "";
    m_Dynamic        = FALSE;
    m_PendingRebuild = nullptr;
}

ESceneWallmarkTool::~ESceneWallmarkTool() {}

int ESceneWallmarkTool::RaySelect(int flag, float& distance, const Fvector& start, const Fvector& direction, BOOL bDistanceOnly)
{
    if (!m_Flags.is(flDrawWallmark))
        return 0;

    wallmark* W = 0;
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
        {
            if ((*w_it)->flags.is(wallmark::flHidden))
                continue;
            Fvector pt;
            if (Fbox::rpOriginOutside == (*w_it)->bbox.Pick2(start, direction, pt))
            {
                float range = start.distance_to(pt);
                if (range < distance)
                {
                    W        = *w_it;
                    distance = range;
                }
            }
        }
    }
    if (W && !bDistanceOnly)
    {
        if (flag == -1)
            W->flags.invert(wallmark::flSelected);
        W->flags.set(wallmark::flSelected, flag);
        // Selection changed — refresh the Properties Panel so it shows the
        // per-wallmark Width / Height / Rotate of the newly-selected mark.
        // (Wallmarks aren't CCustomObjects so they don't get the dispatch
        // CCustomObject::Select fires automatically.)
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
        return 1;
    }
    return 0;
}

int ESceneWallmarkTool::FrustumSelect(int flag, const CFrustum& frustum)
{
    if (!m_Flags.is(flDrawWallmark))
        return 0;

    int count = 0;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
    {
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
        {
            wallmark* W = *m_it;
            if (W->flags.is(wallmark::flHidden))
                continue;
            u32 mask = 0xffff;
            if (frustum.testSAABB(W->bounds.P, W->bounds.R, W->bbox.data(), mask))
            {
                if (-1 == flag)
                    W->flags.invert(wallmark::flSelected);
                else
                    W->flags.set(wallmark::flSelected, flag);
                count++;
            }
        }
    }
    UI->RedrawScene();
    if (count)
        ExecCommand(COMMAND_UPDATE_PROPERTIES);
    return count;
}

void ESceneWallmarkTool::SelectObjects(bool flag)
{
    if (!m_Flags.is(flDrawWallmark))
        return;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
    {
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
            (*m_it)->flags.set(wallmark::flSelected, flag);
    }
    UI->RedrawScene();
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
}

void ESceneWallmarkTool::InvertSelection()
{
    if (!m_Flags.is(flDrawWallmark))
        return;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
    {
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
            (*m_it)->flags.invert(wallmark::flSelected);
    }
    UI->RedrawScene();
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
}

void ESceneWallmarkTool::RemoveSelection()
{
    if (!m_Flags.is(flDrawWallmark))
        return;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
    {
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end();)
            if ((*m_it)->flags.is(wallmark::flSelected))
            {
                wm_destroy(*m_it);
                *m_it = (*p_it)->items.back();
                (*p_it)->items.pop_back();
            }
            else
            {
                m_it++;
            }
    }
    UI->RedrawScene();
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
}

int ESceneWallmarkTool::SelectionCount(bool testflag)
{
    if (!m_Flags.is(flDrawWallmark))
        return 0;
    int count = 0;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
            if ((*m_it)->flags.is(wallmark::flSelected))
                count++;
    return count;
}

void ESceneWallmarkTool::Clear(bool bOnlyNodes)
{
    inherited::Clear();
    {
        for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
        {
            for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
                wm_destroy(*m_it);
            xr_delete(*p_it);
        }
        marks.clear();
    }
    {
        for (u32 it = 0; it < pool.size(); it++)
            xr_delete(pool[it]);
        pool.clear();
    }
}

bool ESceneWallmarkTool::Valid()
{
    return !marks.empty();
}
bool ESceneWallmarkTool::IsNeedSave()
{
    return marks.size();
}
void ESceneWallmarkTool::OnFrame()
{
    // Deferred Properties-Panel-triggered rebuild. The per-selected-wallmark
    // PropValues hold raw pointers into the wallmark struct; RebuildWallmark
    // frees the old struct and replaces it, so we must run it AFTER the
    // ApplyValue iteration that fired OnSelectedWMChanged has fully unwound.
    // Doing it here in OnFrame guarantees that, and the immediate
    // COMMAND_UPDATE_PROPERTIES dispatch refreshes the panel with PropValues
    // pointing at the new wallmark before the user can touch it again.
    if (m_PendingRebuild)
    {
        wallmark* wm     = m_PendingRebuild;
        m_PendingRebuild = nullptr;
        // Epsilon-guard against the rad↔deg / chooser feedback loop. The
        // angle prop displays in degrees but stores in radians; the panel's
        // rad→deg→2-decimal-display→deg→rad round-trip drifts the float by
        // ~1e-4 rad each time the input commits (focus loss, panel rebuild,
        // etc.), and re-firing ApplyValue on those drifts kept self-priming
        // another rebuild. Only re-project if a value moved by more than the
        // noise floor — 1e-4 rad ≈ 0.006°, well below any user-intended edit.
        const float WHR_EPS = 1.0e-4f;
        const bool  changed = wm &&
            (_abs(wm->w - m_SnapW) > WHR_EPS ||
             _abs(wm->h - m_SnapH) > WHR_EPS ||
             _abs(wm->r - m_SnapR) > WHR_EPS ||
             m_PendingShName != m_SnapShName ||
             m_PendingTxName != m_SnapTxName);
        if (changed)
        {
            RebuildWallmark(wm);
            ExecCommand(COMMAND_UPDATE_PROPERTIES);
        }
    }
}

struct zero_slot_pred
{
    template<class C> bool operator()(const C x)
    {
        return x == 0;
    }
};
void ESceneWallmarkTool::RefiningSlots()
{
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot*& slot = *slot_it;
        if (slot->items.empty())
            xr_delete(slot);
    }
    WMSVecIt new_end = std::remove_if(marks.begin(), marks.end(), zero_slot_pred());
    marks.erase(new_end, marks.end());
}

extern ECORE_API float r_ssaDISCARD;
const int              MAX_R_VERTEX = 4096;

void                   ESceneWallmarkTool::OnRender(int priority, bool strictB2F)
{
    if (!m_Flags.is(flDrawWallmark))
        return;
    if (marks.empty())
        return;

    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        VERIFY(slot->shader);

        if ((u32(priority) == slot->shader->E[0]->flags.iPriority) && (strictB2F == !!(slot->shader->E[0]->flags.bStrictB2F)))
        {
            // Projection and xform
            float _43 = EDevice->mProject._43;
            EDevice->mProject._43 -= 0.01f;
            RCache.set_xform_world(Fidentity);
            RCache.set_xform_project(EDevice->mProject);

            float     ssaCLIP  = r_ssaDISCARD / 4;

            u32       w_offset = 0;
            FVF::LIT* w_verts  = (FVF::LIT*)RCache.Vertex.Lock(MAX_R_VERTEX, hGeom->vb_stride, w_offset);
            FVF::LIT* w_start  = w_verts;

            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
            {
                wallmark* W = *w_it;
                VERIFY3(W->verts.size() <= MAX_R_VERTEX, "ERROR: Invalid wallmark.", *slot->tx_name);
                if (W->flags.is(wallmark::flHidden))
                    continue;
                if (RImplementation.ViewBase.testSphere_dirty(W->bounds.P, W->bounds.R))
                {
                    float dst = EDevice->vCameraPosition.distance_to_sqr(W->bounds.P);
                    float ssa = W->bounds.R * W->bounds.R / dst;
                    if (ssa >= ssaCLIP)
                    {
                        // fill wallmark.
                        // The wallmark FF stage chain (B_SCREEN_SET blend ID 6)
                        // uses D3DTOP_BLENDDIFFUSEALPHA on stage 1, which lerps
                        // between DIFFUSE.rgb and the texture sample by
                        // DIFFUSE.alpha. The runtime ships a TTL-based alpha
                        // ramp; the editor wants the texture fully visible, so
                        // alpha=0 (no fade) keeps the sampled texture intact.
                        u32 C     = color_rgba(128, 128, 128, 0);
                        int t_cnt = W->verts.size() / 3;
                        for (int t_idx = 0; t_idx < t_cnt; t_idx++)
                        {
                            u32 w_count = u32(w_verts - w_start);
                            if (w_count + 3 > MAX_R_VERTEX)
                            {
                                // Flush via EDevice->DP so every pass of the
                                // shader element fires (mirrors AIMap and the
                                // other editor render paths).
                                RCache.Vertex.Unlock(w_count, hGeom->vb_stride);
                                EDevice->SetShader(slot->shader);
                                EDevice->DP(D3DPT_TRIANGLELIST, hGeom, w_offset, w_count / 3);
                                // Restart (re-lock/re-calc)
                                w_verts = (FVF::LIT*)RCache.Vertex.Lock(MAX_R_VERTEX, hGeom->vb_stride, w_offset);
                                w_start = w_verts;
                            }
                            // real fill buffer
                            FVF::LIT* S = W->verts.data() + t_idx * 3;
                            for (int k = 0; k < 3; k++, S++, w_verts++)
                            {
                                w_verts->p.set(S->p);
                                w_verts->color = C;
                                w_verts->t.set(S->t);
                            }
                        }
                    }
                }
            }
            // Flush stream
            u32 w_count = u32(w_verts - w_start);
            RCache.Vertex.Unlock(w_count, hGeom->vb_stride);
            if (w_count)
            {
                EDevice->SetShader(slot->shader);
                EDevice->DP(D3DPT_TRIANGLELIST, hGeom, w_offset, w_count / 3);
            }
            // Projection
            EDevice->mProject._43 = _43;
            RCache.set_xform_project(EDevice->mProject);
        }
        if ((1 == priority) && (false == strictB2F))
        {
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
            {
                wallmark* W = *w_it;
                if (W->flags.is(wallmark::flHidden))
                    continue;
                if (W->flags.is(wallmark::flSelected))
                    if (RImplementation.ViewBase.testSphere_dirty(W->bounds.P, W->bounds.R))
                        DU_impl.DrawSelectionBoxB(W->bbox);
            }
        }
    }
}

struct zero_item_pred
{
    template<class C> bool operator()(const C x)
    {
        return x == 0;
    }
};
bool ESceneWallmarkTool::LoadLTX(CInifile& ini)
{
    R_ASSERT(0);
    return true;
}
void ESceneWallmarkTool::SaveLTX(CInifile& ini, int id)
{
    inherited::SaveLTX(ini, id);

    ini.w_u32("main", "version", WM_VERSION);

    ini.w_u32("main", "flags", m_Flags.get());

    ini.w_float("main", "mark_width", m_MarkWidth);
    ini.w_float("main", "mark_height", m_MarkHeight);
    ini.w_float("main", "mark_rotate", m_MarkRotate);
    ini.w_string("main", "sh_name", m_ShName.c_str());
    ini.w_string("main", "tx_name", m_TxName.c_str());

    u32       i = 0;
    string128 buff, buff2;
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); ++slot_it, ++i)
    {
        wm_slot* slot = *slot_it;

        sprintf(buff, "slot_%d", i);
        ini.w_u32(buff, "items_count", slot->items.size());
        if (slot->items.size() == 0)
            continue;

        ini.w_string(buff, "sh_name", slot->sh_name.c_str());
        ini.w_string(buff, "tx_name", slot->tx_name.c_str());

        u32 ii = 0;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); ++w_it, ++ii)
        {
            wallmark* W = *w_it;
            sprintf(buff2, "itm_%d_flags", ii);
            ini.w_u32(buff, buff2, W->flags.get());

            sprintf(buff2, "itm_%d_bb_min", ii);
            ini.w_fvector3(buff, buff2, W->bbox.min);
            sprintf(buff2, "itm_%d_bb_max", ii);
            ini.w_fvector3(buff, buff2, W->bbox.max);

            sprintf(buff2, "itm_%d_bsphere_p", ii);
            ini.w_fvector3(buff, buff2, W->bounds.P);
            sprintf(buff2, "itm_%d_bsphere_r", ii);
            ini.w_float(buff, buff2, W->bounds.R);

            sprintf(buff2, "itm_%d_w", ii);
            ini.w_float(buff, buff2, W->w);
            sprintf(buff2, "itm_%d_h", ii);
            ini.w_float(buff, buff2, W->h);
            sprintf(buff2, "itm_%d_r", ii);
            ini.w_float(buff, buff2, W->r);

            sprintf(buff2, "itm_%d_vert_cnt", ii);
            ini.w_u32(buff, buff2, W->verts.size());

            R_ASSERT2(0, "not_implemented");
            //.            F.w				(&*W->verts.begin(),sizeof(FVF::LIT)*W->verts.size());
        }
    }
}

bool ESceneWallmarkTool::LoadStream(IReader& F)
{
    inherited::LoadStream(F);

    u16 version = 0;

    R_ASSERT(F.r_chunk(WM_CHUNK_VERSION, &version));

    if (version != 0x0003 && version != 0x0004 && version != WM_VERSION)
    {
        ELog.Msg(mtError, "& Static Wallmark: Unsupported version.");
        return false;
    }

    R_ASSERT(F.find_chunk(WM_CHUNK_FLAGS));
    F.r(&m_Flags, sizeof(m_Flags));
    // Migration: the "Draw Wallmarks" UI toggle is gone (it duplicated the
    // tool's own visibility checkbox in the LeftBar Tools list). Any save
    // where the user had turned it off would otherwise render wallmarks
    // blank with no UI to recover. Force the bit on.
    m_Flags.set(flDrawWallmark, TRUE);

    R_ASSERT(F.find_chunk(WM_CHUNK_PARAMS));
    m_MarkWidth  = F.r_float();
    m_MarkHeight = F.r_float();
    m_MarkRotate = F.r_float();
    F.r_stringZ(m_ShName);

    if (version == 0x0003)
        m_ShName = "effects\\wallmarkmult";

    F.r_stringZ(m_TxName);

    IReader* OBJ = F.open_chunk(WM_CHUNK_ITEMS);
    if (OBJ)
    {
        IReader* O = OBJ->open_chunk(0);
        for (int count = 1; O; count++)
        {
            u32 item_count = O->r_u32();
            if (item_count)
            {
                shared_str tex_name, sh_name;
                O->r_stringZ(sh_name);
                O->r_stringZ(tex_name);
                wm_slot* slot = AppendSlot(sh_name, tex_name);
                if (slot)
                {
                    slot->items.resize(item_count);
                    for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
                    {
                        *w_it       = wm_allocate();
                        wallmark* W = *w_it;
                        O->r(&W->flags, sizeof(W->flags));
                        O->r(&W->bbox, sizeof(W->bbox));
                        O->r(&W->bounds, sizeof(W->bounds));
                        W->parent = slot;
                        W->w      = 1.f;
                        W->h      = 1.f;
                        W->r      = 1.f;
                        W->verts.resize(O->r_u32());
                        O->r(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
                    }
                }
            }
            O->close();
            O = OBJ->open_chunk(count);
        }
        OBJ->close();
    }
    else
    {
        IReader* OBJ = F.open_chunk(WM_CHUNK_ITEMS2);
        if (OBJ)
        {
            IReader* O = OBJ->open_chunk(0);
            for (int count = 1; O; count++)
            {
                u32 item_count = O->r_u32();
                if (item_count)
                {
                    shared_str tex_name, sh_name;
                    O->r_stringZ(sh_name);
                    O->r_stringZ(tex_name);
                    wm_slot* slot = AppendSlot(sh_name, tex_name);
                    if (slot)
                    {
                        slot->items.resize(item_count);
                        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
                        {
                            *w_it       = wm_allocate();
                            wallmark* W = *w_it;
                            O->r(&W->flags, sizeof(W->flags));
                            O->r(&W->bbox, sizeof(W->bbox));
                            O->r(&W->bounds, sizeof(W->bounds));
                            W->parent = slot;
                            W->w      = O->r_float();
                            W->h      = O->r_float();
                            W->r      = O->r_float();
                            // v5+ carries the dynamic-mark name; v4 files load
                            // with empty names (and no flDynamic in the flags
                            // field, so they're treated as static).
                            if (version >= 0x0005)
                                O->r_stringZ(W->name);
                            W->verts.resize(O->r_u32());
                            O->r(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
                        }
                    }
                }
                O->close();
                O = OBJ->open_chunk(count);
            }
            OBJ->close();
        }
    }

    // validate wallmarks
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
        {
            wallmark*& W = *w_it;
            if (W->verts.size() > MAX_WALLMARK_VERTEX_COUNT)
            {
                ELog.Msg(mtError, "! ERROR: Invalid wallmark (Contain more than %d vertices). Removed.", MAX_WALLMARK_VERTEX_COUNT);
                wm_destroy(W);
                W = 0;
            }
        }
        WMVecIt new_end = std::remove_if(slot->items.begin(), slot->items.end(), zero_item_pred());
        slot->items.erase(new_end, slot->items.end());
    }

    return true;
}

void ESceneWallmarkTool::SaveStream(IWriter& F)
{
    inherited::SaveStream(F);

    // SHOC's runtime can't ingest the v5 name field — keep it at v4 there.
    // Other targets (COP and friends) get the full v5 layout. Dynamic-mark
    // support is therefore implicitly COP+ only.
    const u16 disk_version = (xrGameManager::GetGame() == EGame::SHOC) ? u16(0x0004) : u16(WM_VERSION);

    F.open_chunk(WM_CHUNK_VERSION);
    F.w_u16(disk_version);
    F.close_chunk();

    F.open_chunk(WM_CHUNK_FLAGS);
    F.w(&m_Flags, sizeof(m_Flags));
    F.close_chunk();

    F.open_chunk(WM_CHUNK_PARAMS);
    F.w_float(m_MarkWidth);
    F.w_float(m_MarkHeight);
    F.w_float(m_MarkRotate);
    F.w_stringZ(m_ShName);
    F.w_stringZ(m_TxName);
    F.close_chunk();

    F.open_chunk(WM_CHUNK_ITEMS2);
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        F.open_chunk(slot_it - marks.begin());
        wm_slot* slot = *slot_it;
        F.w_u32(slot->items.size());
        if (slot->items.size())
        {
            F.w_stringZ(slot->sh_name);
            F.w_stringZ(slot->tx_name);
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
            {
                wallmark* W = *w_it;
                F.w(&W->flags, sizeof(W->flags));
                F.w(&W->bbox, sizeof(W->bbox));
                F.w(&W->bounds, sizeof(W->bounds));
                F.w_float(W->w);
                F.w_float(W->h);
                F.w_float(W->r);
                if (disk_version >= 0x0005)
                    F.w_stringZ(W->name);
                F.w_u32(W->verts.size());
                F.w(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
            }
        }
        F.close_chunk();
    }
    F.close_chunk();
}

// Selection save: write only the wallmarks that are currently selected,
// without the tool-level state (flags / width / height / shader / texture).
// LoadSelection appends these to whatever the user already has placed and
// promotes them to the selected set, which is the copy/paste behavior the
// rest of the editor expects.
void ESceneWallmarkTool::SaveSelection(IWriter& F)
{
    F.open_chunk(WM_CHUNK_VERSION);
    F.w_u16(WM_VERSION);
    F.close_chunk();

    F.open_chunk(WM_CHUNK_ITEMS2);
    u32 chunk_idx = 0;
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        // Skip slots with no selected items so the chunk stays compact.
        u32 selected_count = 0;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
            if ((*w_it)->flags.is(wallmark::flSelected))
                ++selected_count;
        if (selected_count == 0)
            continue;

        F.open_chunk(chunk_idx++);
        F.w_u32(selected_count);
        F.w_stringZ(slot->sh_name);
        F.w_stringZ(slot->tx_name);
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
        {
            wallmark* W = *w_it;
            if (!W->flags.is(wallmark::flSelected))
                continue;
            F.w(&W->flags, sizeof(W->flags));
            F.w(&W->bbox, sizeof(W->bbox));
            F.w(&W->bounds, sizeof(W->bounds));
            F.w_float(W->w);
            F.w_float(W->h);
            F.w_float(W->r);
            // Clipboard payload always uses the latest WM_VERSION (we
            // control both ends), so the name is unconditionally written.
            F.w_stringZ(W->name);
            F.w_u32(W->verts.size());
            F.w(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
        }
        F.close_chunk();
    }
    F.close_chunk();
}

bool ESceneWallmarkTool::LoadSelection(IReader& F)
{
    // Append-mode: don't Clear, and don't read the tool-level FLAGS/PARAMS
    // chunks (the user's current shader/width/etc. should stay put). Only
    // ingest the per-slot ITEMS2 chunk.
    u16 version = 0;
    if (!F.r_chunk(WM_CHUNK_VERSION, &version))
        return false;
    if (version != 0x0003 && version != 0x0004 && version != WM_VERSION)
    {
        ELog.Msg(mtError, "& Static Wallmark: Unsupported version.");
        return false;
    }

    // Deselect existing wallmarks so the newly-pasted set becomes the active
    // selection — matches how scene-object paste behaves.
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        for (WMVecIt w_it = (*slot_it)->items.begin(); w_it != (*slot_it)->items.end(); w_it++)
            (*w_it)->flags.set(wallmark::flSelected, FALSE);

    IReader* OBJ = F.open_chunk(WM_CHUNK_ITEMS2);
    if (!OBJ)
        OBJ = F.open_chunk(WM_CHUNK_ITEMS);
    if (!OBJ)
        return true;   // no items in clipboard payload — nothing to paste, not an error

    IReader* O = OBJ->open_chunk(0);
    for (int count = 1; O; count++)
    {
        u32 item_count = O->r_u32();
        if (item_count)
        {
            shared_str tex_name, sh_name;
            O->r_stringZ(sh_name);
            O->r_stringZ(tex_name);
            wm_slot* slot = FindSlot(sh_name, tex_name);
            if (!slot)
                slot = AppendSlot(sh_name, tex_name);
            if (slot)
            {
                for (u32 i = 0; i < item_count; ++i)
                {
                    wallmark* W = wm_allocate();
                    O->r(&W->flags, sizeof(W->flags));
                    // Force the pasted wallmark to be selected, regardless of
                    // what its serialized flags said.
                    W->flags.set(wallmark::flSelected, TRUE);
                    O->r(&W->bbox, sizeof(W->bbox));
                    O->r(&W->bounds, sizeof(W->bounds));
                    // SaveSelection always emits the v5 layout (we control
                    // both ends), so v3-only clipboard payloads are not a
                    // real scenario; we read w/h/r unconditionally.
                    W->w = O->r_float();
                    W->h = O->r_float();
                    W->r = O->r_float();
                    if (version >= 0x0005)
                        O->r_stringZ(W->name);
                    // Pasted dynamic marks need a fresh unique name — the
                    // serialized one may already be taken by the source mark
                    // we're duplicating.
                    if (W->flags.is(wallmark::flDynamic))
                        W->name = GenerateDynamicWallmarkName(W);
                    W->parent = slot;
                    W->verts.resize(O->r_u32());
                    O->r(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
                    slot->items.push_back(W);
                }
            }
        }
        O->close();
        O = OBJ->open_chunk(count);
    }
    OBJ->close();
    return true;
}

bool ESceneWallmarkTool::Export(LPCSTR path)
{
    RefiningSlots();

    // level.wallmarks — consumed by xrLC during the next full Build to bake
    // STATIC marks into the level geometry. Dynamic marks are deliberately
    // excluded; they render at runtime from level.dwm instead.
    {
        xr_string fn = xr_string(path) + "level.wallmarks";
        IWriter*  F  = FS.w_open(fn.c_str());
        R_ASSERT(F);

        F->open_chunk(1);
        F->w_u32(marks.size());
        for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        {
            wm_slot* slot = *slot_it;
            // Count non-dynamic items first so the header is correct.
            u32 static_count = 0;
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
                if (!(*w_it)->flags.is(wallmark::flDynamic))
                    ++static_count;
            F->w_u32(static_count);
            if (static_count)
            {
                F->w_stringZ(slot->sh_name);
                F->w_stringZ(slot->tx_name);
                for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
                {
                    wallmark* W = *w_it;
                    if (W->flags.is(wallmark::flDynamic))
                        continue;
                    F->w(&W->bounds, sizeof(W->bounds));
                    F->w_u32(W->verts.size());
                    F->w(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
                }
            }
        }
        F->close_chunk();

        FS.w_close(F);
    }

    // level.dwm — sidecar consumed by the runtime engine. Holds DYNAMIC marks
    // only, with their unique per-mark names. Format:
    //   CHUNK 0x0001 VERSION   u32 version (=1)
    //   CHUNK 0x0002 DATA      u32 slot_count
    //                          repeat: stringZ sh, stringZ tx, u32 items
    //                                  repeat: stringZ name, Fsphere bounds,
    //                                          u32 vc, FVF::LIT verts[vc]
    // See DYNAMIC_WALLMARKS_PLAN.md "level.dwm format" for the full spec.
    {
        xr_string fn = xr_string(path) + "level.dwm";

        // Count slots that have at least one dynamic mark — empty slots are
        // dropped so the file is compact.
        u32 dyn_slot_count = 0;
        for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        {
            for (WMVecIt w_it = (*slot_it)->items.begin(); w_it != (*slot_it)->items.end(); ++w_it)
                if ((*w_it)->flags.is(wallmark::flDynamic))
                {
                    ++dyn_slot_count;
                    break;
                }
        }

        IWriter* F = FS.w_open(fn.c_str());
        R_ASSERT(F);

        F->open_chunk(0x0001);
        F->w_u32(1);   // format version
        F->close_chunk();

        F->open_chunk(0x0002);
        F->w_u32(dyn_slot_count);
        for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        {
            wm_slot* slot = *slot_it;

            u32 dyn_item_count = 0;
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); ++w_it)
                if ((*w_it)->flags.is(wallmark::flDynamic))
                    ++dyn_item_count;
            if (dyn_item_count == 0)
                continue;

            F->w_stringZ(slot->sh_name);
            F->w_stringZ(slot->tx_name);
            F->w_u32(dyn_item_count);
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); ++w_it)
            {
                wallmark* W = *w_it;
                if (!W->flags.is(wallmark::flDynamic))
                    continue;
                F->w_stringZ(W->name);
                F->w(&W->bounds, sizeof(W->bounds));
                F->w_u32(W->verts.size());
                F->w(&*W->verts.begin(), sizeof(FVF::LIT) * W->verts.size());
            }
        }
        F->close_chunk();

        FS.w_close(F);
    }

    return true;
}

void ESceneWallmarkTool::OnDeviceCreate()
{
    hGeom.create(FVF::F_LIT, RCache.Vertex.Buffer(), NULL);
}

void ESceneWallmarkTool::OnDeviceDestroy()
{
    hGeom.destroy();
}

void                          ESceneWallmarkTool::OnSynchronize() {}

// allocate
ESceneWallmarkTool::wallmark* ESceneWallmarkTool::wm_allocate()
{
    wallmark* W = 0;
    if (pool.empty())
        W = xr_new<wallmark>();
    else
    {
        W = pool.back();
        pool.pop_back();
    }

    W->verts.clear();
    W->src_obj_name = "";   // reset stale name from a previously pooled wallmark
    return W;
}
// destroy
void ESceneWallmarkTool::wm_destroy(wallmark* W)
{
    pool.push_back(W);
}

struct SWMSlotFindPredicate
{
    shared_str sh_name;
    shared_str tx_name;
    SWMSlotFindPredicate(shared_str sh, shared_str tx): sh_name(sh), tx_name(tx) {}
    bool operator()(const ESceneWallmarkTool::wm_slot* slot) const
    {
        return (slot->tx_name == tx_name) && (slot->sh_name == sh_name);
    }
};
ESceneWallmarkTool::wm_slot* ESceneWallmarkTool::FindSlot(shared_str sh_name, shared_str tx_name)
{
    WMSVecIt it = std::find_if(marks.begin(), marks.end(), SWMSlotFindPredicate(sh_name, tx_name));
    return (it != marks.end()) ? *it : 0;
}
ESceneWallmarkTool::wm_slot* ESceneWallmarkTool::AppendSlot(shared_str sh_name, shared_str tx_name)
{
    wm_slot* slot = xr_new<wm_slot>(sh_name, tx_name);
    if (0 == slot->shader)
        xr_delete(slot);
    else
        marks.push_back(slot);
    return slot;
}

void ESceneWallmarkTool::RecurseTri(u32 t, Fmatrix& mView, wallmark& W)
{
    CDB::TRI* T = sml_collector.getT() + t;
    if (T->dummy)
        return;
    T->dummy        = 0xffffffff;

    // Some vars
    u32*     v_ids  = T->verts;
    Fvector* v_data = sml_collector.getV();
    sml_poly_src.clear();
    sml_poly_src.push_back(v_data[v_ids[0]]);
    sml_poly_src.push_back(v_data[v_ids[1]]);
    sml_poly_src.push_back(v_data[v_ids[2]]);
    sml_poly_dest.clear();

    sPoly* P = sml_clipper.ClipPoly(sml_poly_src, sml_poly_dest);

    if (P)
    {
        // Create vertices and triangulate poly (tri-fan style triangulation)
        FVF::LIT V0, V1, V2;
        Fvector  UV;

        mView.transform_tiny(UV, (*P)[0]);
        V0.set((*P)[0], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
        mView.transform_tiny(UV, (*P)[1]);
        V1.set((*P)[1], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);

        for (u32 i = 2; i < P->size(); i++)
        {
            mView.transform_tiny(UV, (*P)[i]);
            V2.set((*P)[i], 0, (1 + UV.x) * .5f, (1 - UV.y) * .5f);
            W.verts.push_back(V0);
            W.verts.push_back(V1);
            W.verts.push_back(V2);
            V1 = V2;
        }

        // recurse
        for (u32 i = 0; i < 3; i++)
        {
            u32 adj = sml_adjacency[3 * t + i];
            if (0xffffffff == adj)
                continue;
            CDB::TRI* SML = sml_collector.getT() + adj;
            v_ids         = SML->verts;

            Fvector test_normal;
            test_normal.mknormal(v_data[v_ids[0]], v_data[v_ids[1]], v_data[v_ids[2]]);
            float cosa = test_normal.dotproduct(sml_normal);
            if (cosa < EPS)
                continue;
            RecurseTri(adj, mView, W);
        }
    }
}

void ESceneWallmarkTool::BuildMatrix(Fmatrix& mView, float inv_w, float inv_h, float angle, const Fvector& from)
{
    // build projection
    Fmatrix mScale, mRot;
    Fvector at, up, right, y;
    at.sub(from, sml_normal);
    y.set(EDevice->vCameraTop);

    if (m_Flags.is(flAxisAlign))
    {
        y.set(0, 1, 0);
        if (_abs(sml_normal.y) > 0.99f)
            y.set(1, 0, 0);
    }
    else
    {
        y.set(EDevice->vCameraTop);
        if (fsimilar(y.dotproduct(sml_normal), 1.f, EPS))
            y.set(EDevice->vCameraRight);
    }
    right.crossproduct(y, sml_normal);
    up.crossproduct(sml_normal, right);
    mView.build_camera(from, at, up);
    mRot.rotateZ(angle);
    mView.mulA_43(mRot);
    mScale.scale(inv_w, inv_h, _max(inv_w, inv_h));
    mView.mulA_43(mScale);
}

int ESceneWallmarkTool::ObjectCount()
{
    int count = 0;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
        count += (*p_it)->items.size();
    return count;
}

BOOL ESceneWallmarkTool::AddWallmark_internal(const Fvector& start, const Fvector& dir, shared_str sh, shared_str tx, float width, float height, float rotate, wallmark* exclude_from_similar, bool silent, bool ignore_use, ObjectList* override_list, bool is_dynamic, const shared_str& dyn_name)
{
    /*
    if (ObjectCount()>=MAX_WALLMARK_COUNT){
        ELog.DlgMsg			(mtError, "& Maximum wallmark per level is reached [Max: %d].",MAX_WALLMARK_COUNT);
        return FALSE;
    }
    */

    if (0 == sh.size())
    {
        if (!silent)
            ELog.DlgMsg(mtError, "& Select texture before add wallmark.");
        return FALSE;
    }
    if (0 == tx.size())
    {
        if (!silent)
            ELog.DlgMsg(mtError, "& Select texture before add wallmark.");
        return FALSE;
    }
    // pick contact poly
    Fvector     contact_pt;
    float       dist      = UI->ZFar();
    ObjectList* snap_list = override_list ? override_list : Scene->GetSnapList(ignore_use);
    if (!snap_list)
    {
        if (!silent)
            ELog.DlgMsg(mtError, "Wallmark needs a target surface.\n\n1. Open the Snap List in the right toolbar.\n2. Add the scene objects you want wallmarks to stick to.\n3. Tick the \"Enable/Show Snap List\" checkbox.");
        return FALSE;
    }
    // pick contact poly
    SPickQuery PQ;
    sml_collector.clear();
    if (Scene->RayQuery(PQ, start, dir, dist, CDB::OPT_ONLYNEAREST | CDB::OPT_CULL, snap_list))
    {
        contact_pt.mad(PQ.m_Start, PQ.m_Direction, PQ.r_begin()->range);
        sml_normal.mknormal(PQ.r_begin()->verts[0], PQ.r_begin()->verts[1], PQ.r_begin()->verts[2]);
        sml_collector.add_face_packed_D(PQ.r_begin()->verts[0], PQ.r_begin()->verts[1], PQ.r_begin()->verts[2], 0);
    }
    else
        return FALSE;

    // box pick poly
    Fbox bbox;
    bbox.set(contact_pt, contact_pt);
    bbox.grow(_max(height, width) * 2);
    SPickQuery BQ;
    if (Scene->BoxQuery(BQ, bbox, CDB::OPT_FULL_TEST, snap_list))
    {
        for (u32 k = 0; k < (u32)BQ.r_count(); k++)
        {
            SPickQuery::SResult* R = BQ.r_begin() + k;
            Fvector              test_normal;
            test_normal.mknormal(R->verts[0], R->verts[1], R->verts[2]);
            float cosa = test_normal.dotproduct(sml_normal);
            if (cosa < 0.1)
                continue;
            sml_collector.add_face_packed_D(R->verts[0], R->verts[1], R->verts[2], 0);
        }
    }

    // remove duplicate poly
    sml_collector.remove_duplicate_T();
    // calculate adjacency
    sml_collector.calc_adjacency(sml_adjacency);

    // build 3D ortho-frustum
    Fmatrix mView;
    BuildMatrix(mView, 2 / width, 2 / height, rotate, contact_pt);   // width/2 height/2  (BuildMatrix need radius)
    sml_clipper.CreateFromMatrix(mView, FRUSTUM_P_LRTB);

    // create wallmark
    wallmark* W = wm_allocate();
    W->w        = width;
    W->h        = height;
    W->r        = rotate;

    // Capture the host scene object's name so the Object List can label this
    // wallmark with "<object>/<texture>". Best-effort: if the ray happens to
    // miss the scene-object list (e.g. only terrain/non-CCustomObject geometry
    // is in the snap list), leave it empty and the label falls back to just
    // the texture.
    if (CCustomObject* host = Scene->RayPickObject(dist, start, dir, OBJCLASS_SCENEOBJECT, 0, snap_list))
        W->src_obj_name = host->GetName();

    RecurseTri(0, mView, *W);

    // calc sphere
    if ((W->verts.size() < 3) || (W->verts.size() > MAX_WALLMARK_VERTEX_COUNT))
    {
        if (!silent)
            ELog.DlgMsg(mtError, "! Invalid wallmark vertex count. [Min: %d. Max: %d].", 3, MAX_WALLMARK_VERTEX_COUNT);
        wm_destroy(W);
        return FALSE;
    }
    else
    {
        W->bbox.invalidate();
        FVF::LIT* I = &*W->verts.begin();
        FVF::LIT* E = &*W->verts.end();
        for (; I != E; I++)
            W->bbox.modify(I->p);
        W->bbox.getsphere(W->bounds.P, W->bounds.R);
        // assign() replaces the whole flag bag — preserve flDynamic from the
        // caller (placement: brush state; rebuild: original mark's value).
        u8 new_flags = wallmark::flSelected;
        if (is_dynamic)
            new_flags |= wallmark::flDynamic;
        W->flags.assign(new_flags);
        W->name = dyn_name;
        W->bbox.grow(EPS_L);
    }

    // search if similar wallmark exists
    wm_slot* slot = FindSlot(sh, tx);
    if (slot)
    {
        W->parent   = slot;
        WMVecIt it  = slot->items.begin();
        WMVecIt end = slot->items.end();
        for (; it != end; it++)
        {
            wallmark* wm = *it;
            if (wm == exclude_from_similar)
                continue;   // caller will free this one explicitly
            if (wm->bounds.P.similar(W->bounds.P, 0.02f))
            {   // replace
                wm_destroy(wm);
                *it = W;
                return TRUE;
            }
        }
    }
    else
    {
        slot      = AppendSlot(sh, tx);
        W->parent = slot;
    }

    // no similar - register _new_
    if (slot)
        slot->items.push_back(W);
    return TRUE;
}

BOOL ESceneWallmarkTool::AddWallmark(const Fvector& start, const Fvector& dir)
{
    // Brush state decides whether the new mark gets flDynamic + a unique
    // name; non-dynamic placements still pass through with empty name.
    const bool       is_dyn = !!m_Dynamic;
    const shared_str name   = is_dyn ? GenerateDynamicWallmarkName() : shared_str();
    return AddWallmark_internal(start, dir, m_ShName, m_TxName, m_MarkWidth, m_MarkHeight, m_MarkRotate,
                                 /*exclude=*/nullptr, /*silent=*/false, /*ignore_use=*/false, /*override_list=*/nullptr,
                                 is_dyn, name);
}

ESceneWallmarkTool::wallmark* ESceneWallmarkTool::FindSingleSelectedWallmark()
{
    wallmark* sel = nullptr;
    for (WMSVecIt p_it = marks.begin(); p_it != marks.end(); p_it++)
    {
        for (WMVecIt m_it = (*p_it)->items.begin(); m_it != (*p_it)->items.end(); m_it++)
        {
            if ((*m_it)->flags.is(wallmark::flSelected))
            {
                if (sel)
                    return nullptr;   // more than one selected
                sel = *m_it;
            }
        }
    }
    return sel;
}

bool ESceneWallmarkTool::PickSurfacePoint(const Fvector& start, const Fvector& dir, Fvector& out_world)
{
    // ignore-use because we want the snap list regardless of the LeftBar
    // toggle — the caller (drag control) auto-flips it on for the drag.
    ObjectList* snap_list = Scene->GetSnapList(true);
    if (!snap_list)
        return false;
    SPickQuery PQ;
    if (!Scene->RayQuery(PQ, start, dir, UI->ZFar(), CDB::OPT_ONLYNEAREST | CDB::OPT_CULL, snap_list))
        return false;
    out_world.mad(PQ.m_Start, PQ.m_Direction, PQ.r_begin()->range);
    return true;
}

shared_str ESceneWallmarkTool::GenerateDynamicWallmarkName(wallmark* exclude, const shared_str& preferred)
{
    // Collect taken names across every slot. Linear in mark count, fine for
    // the editor — sub-millisecond at any reasonable level density.
    xr_set<shared_str> taken;
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); ++slot_it)
    {
        for (WMVecIt w_it = (*slot_it)->items.begin(); w_it != (*slot_it)->items.end(); ++w_it)
        {
            wallmark* w = *w_it;
            if (w == exclude)
                continue;
            if (w->flags.is(wallmark::flDynamic) && w->name.size())
                taken.insert(w->name);
        }
    }

    // User-supplied name: honour verbatim if free, otherwise suffix _NN.
    if (preferred.size())
    {
        if (taken.find(preferred) == taken.end())
            return preferred;
        string128 buf;
        for (u32 i = 1; i < 10000; ++i)
        {
            xr_sprintf(buf, sizeof(buf), "%s_%02u", preferred.c_str(), i);
            shared_str candidate(buf);
            if (taken.find(candidate) == taken.end())
                return candidate;
        }
        // Pathological collision storm — fall through to wm_NNN below.
    }

    // Fresh-placement name: wm_001, wm_002, ... 3-digit zero-padded, widening
    // automatically beyond 999 via %u's natural overflow into 4+ digits.
    string128 buf;
    for (u32 i = 1; i < 1000000; ++i)
    {
        xr_sprintf(buf, sizeof(buf), "wm_%03u", i);
        shared_str candidate(buf);
        if (taken.find(candidate) == taken.end())
            return candidate;
    }
    // Should never reach here at any sane level density. Return something
    // stable so callers always get a non-empty name.
    return shared_str("wm_overflow");
}

void ESceneWallmarkTool::EnsureHostObjectName(wallmark* w)
{
    if (!w || w->src_obj_name.size() > 0)
        return;
    if (w->verts.size() < 3)
        return;

    // Recover an outward surface normal from the first triangle. Wallmark
    // verts lie on the host surface, so a face-normal is identical to the
    // surface normal at the contact point.
    Fvector normal;
    normal.mknormal(w->verts[0].p, w->verts[1].p, w->verts[2].p);
    if (normal.square_magnitude() < EPS)
        return;

    // Step out along the normal and ray-pick back into the surface. The
    // out-step gives the ray clearance from the wallmark's own z-bias; the
    // probe distance comfortably covers small geometry displacement.
    Fvector ray_start;
    ray_start.mad(w->bounds.P, normal, 0.25f);
    Fvector ray_dir = normal;
    ray_dir.invert();

    // Search ALL scene objects, not just the snap list — loaded wallmarks
    // may predate the current snap-list contents, and we want the label to
    // resolve regardless.
    CCustomObject* host = Scene->RayPickObject(0.50f, ray_start, ray_dir, OBJCLASS_SCENEOBJECT, 0, nullptr);
    if (host)
        w->src_obj_name = host->GetName();
}

Fvector ESceneWallmarkTool::wallmark::compute_normal() const
{
    if (verts.size() < 3)
        return Fvector().set(0.f, 1.f, 0.f);
    Fvector n;
    n.mknormal(verts[0].p, verts[1].p, verts[2].p);
    return n;
}

BOOL ESceneWallmarkTool::MoveSelectedWallmarkTo(const Fvector& start, const Fvector& dir)
{
    if (!m_Flags.is(flDrawWallmark))
        return FALSE;

    wallmark* wm = FindSingleSelectedWallmark();
    if (!wm)
        return FALSE;

    // Snapshot before mutation: the old wallmark may go away below.
    shared_str sh = wm->parent->sh_name;
    shared_str tx = wm->parent->tx_name;
    float      w  = wm->w;
    float      h  = wm->h;
    float      r  = wm->r;

    // Preserve flDynamic + name across the rebuild — the rebuilt mark IS the
    // same logical entity, just re-projected. Reading them before the
    // AddWallmark_internal call (and before wm_destroy below) keeps the
    // capture safe.
    const bool       was_dynamic = wm->flags.is(wallmark::flDynamic);
    const shared_str wm_name     = wm->name;

    // Tell AddWallmark_internal to skip `wm` in the similar-bounds replace
    // branch, otherwise it could free wm itself and we'd then double-pool it.
    // Silent: drag may scrape off the snap-list surface for a frame; don't
    // surface a dialog mid-drag.
    if (!AddWallmark_internal(start, dir, sh, tx, w, h, r, wm, /*silent=*/true, /*ignore_use=*/false, /*override_list=*/nullptr,
                              was_dynamic, wm_name))
        return FALSE;

    // New wallmark created and inserted. Now remove the old one.
    {
        WMVec& items = wm->parent->items;
        WMVecIt it   = std::find(items.begin(), items.end(), wm);
        if (it != items.end())
        {
            *it = items.back();
            items.pop_back();
        }
    }
    wm_destroy(wm);
    return TRUE;
}

BOOL ESceneWallmarkTool::RebuildWallmark(wallmark* wm)
{
    if (!m_Flags.is(flDrawWallmark))
        return FALSE;
    if (!wm)
        return FALSE;

    // Ray-from-above approach: cast back along the wallmark's surface normal
    // so the re-projection lands at wm->bounds.P regardless of camera angle.
    const Fvector normal    = wm->compute_normal();
    Fvector       ray_start = wm->bounds.P;
    ray_start.mad(normal, 0.25f);
    Fvector ray_dir = normal;
    ray_dir.invert();

    // Pin the re-projection to the wallmark's ORIGINAL host object. Without
    // this, a re-project ray-picks against whatever's in the snap list right
    // now — which is almost never what the user wants when tweaking an
    // existing wallmark's size, rotation, shader, or texture. The host name
    // is cached on the wallmark at placement time (or lazily resolved via
    // EnsureHostObjectName below for wallmarks loaded from disk).
    EnsureHostObjectName(wm);
    ObjectList host_list;
    if (wm->src_obj_name.size())
    {
        if (CCustomObject* host = Scene->FindObjectByName(wm->src_obj_name.c_str(), OBJCLASS_SCENEOBJECT))
            host_list.push_back(host);
    }

    // Shader / Texture come from the scratch fields the panel choosers write
    // to; on rebuild paths where the user didn't change them (e.g. width
    // edit), they still mirror the wallmark's current slot values because
    // FillPropObjects re-copies them on every refresh. Fall back to the
    // wallmark's own slot if a non-panel caller (the gizmo) invoked us
    // before the panel ran.
    shared_str sh           = m_PendingShName.size() ? m_PendingShName : wm->parent->sh_name;
    shared_str tx           = m_PendingTxName.size() ? m_PendingTxName : wm->parent->tx_name;
    ObjectList* override_lst = host_list.empty() ? nullptr : &host_list;
    // Preserve flDynamic + name across the rebuild — captured before the
    // call so we can pass them in even though wm is about to be destroyed.
    const bool       was_dynamic = wm->flags.is(wallmark::flDynamic);
    const shared_str wm_name     = wm->name;
    if (!AddWallmark_internal(ray_start, ray_dir, sh, tx, wm->w, wm->h, wm->r, wm, /*silent=*/true, /*ignore_use=*/true, override_lst,
                              was_dynamic, wm_name))
        return FALSE;

    {
        WMVec&  items = wm->parent->items;
        WMVecIt it    = std::find(items.begin(), items.end(), wm);
        if (it != items.end())
        {
            *it = items.back();
            items.pop_back();
        }
    }
    wm_destroy(wm);
    return TRUE;
}

BOOL ESceneWallmarkTool::RebuildSelectedWallmark(const Fvector& new_world_pos, float new_r, float new_w, float new_h)
{
    // Thin wrapper kept for the gizmo path. Writes the requested pose into
    // the wallmark, then delegates to RebuildWallmark which already knows how
    // to re-project from those fields.
    wallmark* wm = FindSingleSelectedWallmark();
    if (!wm)
        return FALSE;
    wm->bounds.P = new_world_pos;
    wm->w        = new_w;
    wm->h        = new_h;
    wm->r        = new_r;
    return RebuildWallmark(wm);
}

void ESceneWallmarkTool::FillToolDefaults(PropItemVec& items)
{
    // Defaults applied to the next placed wallmark. Rendered in the LeftBar
    // by UIWallmarkTool — these used to live in FillPropObjects, but the
    // Properties Panel now shows the actually-selected wallmark instead.
    PHelper().CreateFlag32(items, "Alignment", &m_Flags, flAxisAlign, "By Camera", "By World Axis");
    PHelper().CreateFloat(items, "Width", &m_MarkWidth, 0.01f, 10.f);
    PHelper().CreateFloat(items, "Height", &m_MarkHeight, 0.01f, 10.f);
    PHelper().CreateAngle(items, "Rotate", &m_MarkRotate);
    PHelper().CreateChoose(items, "Shader", &m_ShName, smEShader);
    PHelper().CreateChoose(items, "Texture", &m_TxName, smTexture);
    // Dynamic toggle — next mark placed is exported to level.dwm and
    // skipped by xrLC. Per-mark Name field appears in the Properties Panel
    // (FillPropObjects) when a dynamic mark is selected.
    PHelper().CreateBOOL(items, "Dynamic wallmark", &m_Dynamic);
}

void ESceneWallmarkTool::OnSelectedWMChanged(PropValue*)
{
    // ApplyValue has already written the new value through the PropValue's
    // raw pointer into wm->w / wm->h / wm->r (or, for Shader / Texture, the
    // tool's scratch shared_strs). We defer the actual re-project to OnFrame
    // so we're outside ApplyValue's iteration when the old wallmark gets
    // freed and replaced.
    m_PendingRebuild = FindSingleSelectedWallmark();
}

void ESceneWallmarkTool::FillPropObjects(LPCSTR pref, PropItemVec& items)
{
    const int sel = SelectionCount(true);
    if (sel == 0)
    {
        PHelper().CreateCaption(items, PrepareKey(pref, "Wallmark"), "No wallmark selected");
        return;
    }
    if (sel > 1)
    {
        string128 buf;
        sprintf(buf, "%d wallmarks selected - pick one to edit", sel);
        PHelper().CreateCaption(items, PrepareKey(pref, "Wallmark"), buf);
        return;
    }

    wallmark* w = FindSingleSelectedWallmark();
    if (!w)
        return;

    // Seed the per-selection scratch fields from the wallmark's current slot.
    // RebuildWallmark reads them on every re-project; for w/h/r edits they
    // pass through unchanged, and for Shader/Texture edits the chooser
    // writes the new value here before OnChange fires.
    m_PendingShName = w->parent->sh_name;
    m_PendingTxName = w->parent->tx_name;
    // Snapshot for the OnFrame change-detection guard.
    m_SnapW         = w->w;
    m_SnapH         = w->h;
    m_SnapR         = w->r;
    m_SnapShName    = m_PendingShName;
    m_SnapTxName    = m_PendingTxName;

    // Per-instance editable properties. OnChange triggers a deferred rebuild
    // (see OnFrame) which re-projects the wallmark to match the new size,
    // rotation, shader, or texture. The PropValue pointers target wm-> /
    // m_Pending* fields which stay valid until the next ApplyValue
    // iteration finishes — OnSelectedWMChanged is careful to only stash
    // the wallmark, not free it here.
    PropValue* V;
    V = PHelper().CreateFloat(items, PrepareKey(pref, "Width"), &w->w, 0.01f, 10.f);
    V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMChanged);
    V = PHelper().CreateFloat(items, PrepareKey(pref, "Height"), &w->h, 0.01f, 10.f);
    V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMChanged);
    V = PHelper().CreateAngle(items, PrepareKey(pref, "Rotate"), &w->r);
    V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMChanged);
    V = PHelper().CreateChoose(items, PrepareKey(pref, "Shader"), &m_PendingShName, smEShader);
    V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMChanged);
    V = PHelper().CreateChoose(items, PrepareKey(pref, "Texture"), &m_PendingTxName, smTexture);
    V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMChanged);

    // Per-mark identity — only meaningful for dynamic marks, hidden otherwise.
    // The runtime engine keys visibility on this string (level.dwm carries
    // it; see plan B4). Renames resolve collisions via the same
    // disambiguator the placement path uses.
    if (w->flags.is(wallmark::flDynamic))
    {
        V = PHelper().CreateRText(items, PrepareKey(pref, "Name"), &w->name);
        V->OnChangeEvent.bind(this, &ESceneWallmarkTool::OnSelectedWMNameChanged);
    }
}

void ESceneWallmarkTool::OnSelectedWMNameChanged(PropValue*)
{
    // The chooser/RText path has already written the user's typed string into
    // w->name. Verify uniqueness against the rest of the dynamic-mark name
    // set; on collision, suffix _NN. If the resolver changes the value, the
    // PROPERTIES_UPDATE dispatch redraws the field with the disambiguated
    // form so the user sees what was actually stored.
    wallmark* w = FindSingleSelectedWallmark();
    if (!w)
        return;
    if (!w->flags.is(wallmark::flDynamic))
        return;
    // Empty name -> treat as request for a fresh auto-generated one.
    shared_str requested = w->name;
    shared_str resolved  = GenerateDynamicWallmarkName(w, requested);
    if (resolved != w->name)
        w->name = resolved;
    ExecCommand(COMMAND_UPDATE_PROPERTIES);
}

bool ESceneWallmarkTool::Validate(bool)
{
    bool bRes = true;

    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        if (slot->items.size())
        {
            IBlender* B = EDevice->Resources->_FindBlender(*slot->sh_name);
            if (!B || B->canBeLMAPped())
            {
                ELog.Msg(mtError, "& Wallmarks: Invalid or missing shader '%s'.", *slot->sh_name);
                bRes = false;
            }
        }
    }

    return bRes;
}

void ESceneWallmarkTool::GetStaticDesc(int& v_cnt, int& f_cnt, bool b_selected_only, bool b_cform)
{
    if (b_cform)
        return;
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
        {
            wallmark* W = *w_it;

            if (b_selected_only && !W->flags.test(wallmark::flSelected))
                continue;
            // Dynamic marks bypass xrLC — they ship to the engine via
            // level.dwm and render at runtime. Excluding them here keeps
            // the buffers ExportStatic targets correctly sized.
            if (W->flags.is(wallmark::flDynamic))
                continue;

            v_cnt += W->verts.size();
            f_cnt += W->verts.size() / 3;
        }
    }
}

bool ESceneWallmarkTool::ExportStatic(SceneBuilder* B, bool b_selected_only)
{
    for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
    {
        wm_slot* slot = *slot_it;
        for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
        {
            wallmark* W = *w_it;
            // Skip dynamic marks — they're emitted to level.dwm, not baked.
            // GetStaticDesc above also skips them so B's buffers are sized
            // for the static-only subset.
            if (W->flags.is(wallmark::flDynamic))
                continue;
            int       sect_num = B->CalculateSector(W->bounds.P, W->bounds.R);
            int       m_id     = B->BuildMaterial(*slot->sh_name, COMPILER_SHADER, *slot->tx_name, 1, sect_num, false);
            u32       f_cnt    = W->verts.size() / 3;
            for (u32 f_it = 0; f_it < f_cnt; f_it++, B->l_face_it++)
            {
                R_ASSERT(B->l_face_it < B->l_face_cnt);
                b_face& dst_f = B->l_faces[B->l_face_it];
                for (u32 k = 0; k < 3; k++, B->l_vert_it++)
                {
                    R_ASSERT(B->l_vert_it < B->l_vert_cnt);
                    FVF::LIT& src   = W->verts[f_it * 3 + k];
                    Fvector&  dst_v = B->l_verts[B->l_vert_it];
                    dst_v.set(src.p);
                    dst_f.v[k] = B->l_vert_it;
                    dst_f.t[k].set(src.t);
                    dst_f.dwMaterial = (u16)m_id;
                }
            }
        }
    }
    return true;
}

void ESceneWallmarkTool::CreateControls()
{
    inherited::CreateDefaultControls(estDefault);
    // node tools
    AddControl(xr_new<TUI_ControlWallmarkAdd>(0, etaAdd, this));
    AddControl(xr_new<TUI_ControlWallmarkMove>(0, etaMove, this));
    // LeftBar form: hosts the "Next Placement" defaults.
    pForm                          = xr_new<UIWallmarkTool>();
    ((UIWallmarkTool*)pForm)->Tool = this;
}

void ESceneWallmarkTool::RemoveControls()
{
    inherited::RemoveControls();
}

void ESceneWallmarkTool::GetBBox(Fbox& bb, bool bSelOnly)
{
    if (bSelOnly)
    {
        for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        {
            wm_slot* slot = *slot_it;
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
            {
                wallmark* W = *w_it;
                if (W->flags.is(wallmark::flSelected))
                    bb.merge(W->bbox);
            }
        }
    }
    else
    {
        for (WMSVecIt slot_it = marks.begin(); slot_it != marks.end(); slot_it++)
        {
            wm_slot* slot = *slot_it;
            for (WMVecIt w_it = slot->items.begin(); w_it != slot->items.end(); w_it++)
                bb.merge((*w_it)->bbox);
        }
    }
}
