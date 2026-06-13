//----------------------------------------------------
// file: StaticMesh.cpp
//----------------------------------------------------

#include "stdafx.h"
#pragma hdrstop

// #include "EditMeshVLight.h"
#include "EditMesh.h"
#include "EditObject.h"
#include "ui_main.h"
#include "d3dutils.h"
#include "render.h"
//----------------------------------------------------
#define F_LIM (10000)
#define V_LIM (F_LIM * 3)
//----------------------------------------------------
// Worker-thread-safe CPU half of GenerateRenderBuffers. Iterates surfaces,
// allocates per-surface heap blobs, fills them via FillRenderBuffer. Touches
// only mesh CPU state — no D3D calls — so this can run on the mesh prefetcher
// worker without main-thread interleaving.
bool CEditableMesh::PrepareCpuRenderBuffers(PreparedBuffers& out)
{
    if (m_RenderBuffers)
        return false;

    GenerateVNormals(true);
    if (!m_VertexNormals)
        return false;

    for (SurfFacesPairIt sp_it = m_SurfFaces.begin(); sp_it != m_SurfFaces.end(); sp_it++)
    {
        IntVec&   face_lst  = sp_it->second;
        CSurface* _S        = sp_it->first;
        const int num_verts = (int)face_lst.size() * 3;
        if (num_verts <= 0)
            continue;

        u32 num_vertex = (u32)num_verts;
        if (_S->m_Flags.is(CSurface::sf2Sided))
            num_vertex *= 2;
        const int num_face = num_verts / 3;
        const u32 buf_size = D3DXGetFVFVertexSize(_S->_FVF()) * num_vertex;
        if (!buf_size)
            continue;

        u8*    bytes    = (u8*)xr_malloc(buf_size);
        LPBYTE data_ptr = bytes;
        FillRenderBuffer(face_lst, 0, num_face, _S, data_ptr);

        PreparedSurface ps;
        ps.surf       = _S;
        ps.bytes      = bytes;
        ps.size       = buf_size;
        ps.num_vertex = num_vertex;
        out.surfaces.push_back(ps);
    }
    UnloadVNormals();
    return !out.surfaces.empty();
}

// Main-thread D3D9 half. Takes the worker's CPU buffers and uploads to GPU.
void CEditableMesh::UploadPreparedBuffers(PreparedBuffers& in)
{
    if (m_RenderBuffers)
        return;   // someone else uploaded first; drop the prepared bytes
    m_RenderBuffers = xr_new<RBMap>();
    for (PreparedSurface& ps: in.surfaces)
    {
        RBVector rb_vec;
        rb_vec.push_back(st_RenderBuffer(0, ps.num_vertex));
        st_RenderBuffer&        rb  = rb_vec.back();

        IDirect3DVertexBuffer9* pVB = 0;
        R_CHK(HW.pDevice->CreateVertexBuffer(ps.size, D3DUSAGE_WRITEONLY, 0, D3DPOOL_MANAGED, &pVB, 0));
        rb.pGeom.create(ps.surf->_FVF(), pVB, 0);

        u8* dst = 0;
        R_CHK(pVB->Lock(0, 0, (LPVOID*)&dst, 0));
        memcpy(dst, ps.bytes, ps.size);
        pVB->Unlock();

        m_RenderBuffers->insert(mk_pair(ps.surf, rb_vec));
    }
    // PreparedBuffers' destructor frees `bytes` for each surface.
}

void CEditableMesh::GenerateRenderBuffers()
{
    // Compatibility entry point used by any remaining synchronous code paths
    // (mesh-edit tools that mutate geometry and need an immediate refresh).
    // Routes through the same prep/upload split as the background prefetcher
    // so the actual GPU code lives in one place.
    if (m_RenderBuffers)
        return;
    PreparedBuffers tmp;
    if (PrepareCpuRenderBuffers(tmp))
        UploadPreparedBuffers(tmp);
}
//----------------------------------------------------

void CEditableMesh::UnloadRenderBuffers()
{
    if (m_RenderBuffers)
    {
        for (RBMapPairIt rbmp_it = m_RenderBuffers->begin(); rbmp_it != m_RenderBuffers->end(); rbmp_it++)
        {
            for (RBVecIt rb_it = rbmp_it->second.begin(); rb_it != rbmp_it->second.end(); rb_it++)
                if (rb_it->pGeom)
                {
                    _RELEASE(rb_it->pGeom->vb);
                    _RELEASE(rb_it->pGeom->ib);
                    rb_it->pGeom.destroy();
                }
        }
        xr_delete(m_RenderBuffers);
    }
}
//----------------------------------------------------

void CEditableMesh::FillRenderBuffer(IntVec& face_lst, int start_face, int num_face, const CSurface* surf, LPBYTE& src_data)
{
    bool   bNormals = (m_Normals && m_Parent->m_objectFlags.is(CEditableObject::eoNormals));
    LPBYTE data     = src_data;
    u32    dwFVF    = surf->_FVF();
    u32    dwTexCnt = ((dwFVF & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT);
    for (int fl_i = start_face; fl_i < start_face + num_face; fl_i++)
    {
        u32 f_index = face_lst[fl_i];
        VERIFY(f_index < m_FaceCount);
        st_Face& face = m_Faces[f_index];
        for (int k = 0; k < 3; k++)
        {
            st_FaceVert& fv      = face.pv[k];
            u32          norm_id = f_index * 3 + k;   // fv.pindex;
            VERIFY2(norm_id < m_FaceCount * 3, "Normal index out of range.");
            VERIFY2(fv.pindex < (int)m_VertCount, "Point index out of range.");
            Fvector& PN = bNormals ? m_Normals[norm_id] : m_VertexNormals[norm_id];
            Fvector& V  = m_Vertices[fv.pindex];
            int      sz;
            if (dwFVF & D3DFVF_XYZ)
            {
                sz = sizeof(Fvector);
                VERIFY2(fv.pindex < int(m_VertCount), "- Face index out of range.");
                CopyMemory(data, &V, sz);
                data += sz;
            }
            if (dwFVF & D3DFVF_NORMAL)
            {
                sz = sizeof(Fvector);
                CopyMemory(data, &PN, sz);
                data += sz;
            }
            sz       = sizeof(Fvector2);
            int offs = 0;
            for (int t = 0; t < (int)dwTexCnt; t++)
            {
                VERIFY2((t + offs) < (int)m_VMRefs[fv.vmref].count, "- VMap layer index out of range");
                st_VMapPt& vm_pt = m_VMRefs[fv.vmref].pts[t + offs];
                if (m_VMaps[vm_pt.vmap_index]->type != vmtUV)
                {
                    offs++;
                    t--;
                    continue;
                }
                VERIFY2(vm_pt.vmap_index < int(m_VMaps.size()), "- VMap index out of range");
                st_VMap* vmap = m_VMaps[vm_pt.vmap_index];
                VERIFY2(vm_pt.index < vmap->size(), "- VMap point index out of range");
                CopyMemory(data, &vmap->getUV(vm_pt.index), sz);
                data += sz;
                //                Msg("%3.2f, %3.2f",vmap->getUV(vm_pt.index).x,vmap->getUV(vm_pt.index).y);
            }
        }
        if (surf->m_Flags.is(CSurface::sf2Sided))
        {
            for (int k = 2; k >= 0; k--)
            {
                st_FaceVert& fv = face.pv[k];
                Fvector&     PN = bNormals ? m_Normals[f_index * 3 + k] : m_VertexNormals[f_index * 3 + k];
                int          sz;
                if (dwFVF & D3DFVF_XYZ)
                {
                    sz = sizeof(Fvector);
                    VERIFY2(fv.pindex < int(m_VertCount), "- Face index out of range.");
                    CopyMemory(data, &m_Vertices[fv.pindex], sz);
                    data += sz;
                }
                if (dwFVF & D3DFVF_NORMAL)
                {
                    sz = sizeof(Fvector);
                    Fvector N;
                    N.invert(PN);
                    CopyMemory(data, &N, sz);
                    data += sz;
                }
                sz       = sizeof(Fvector2);
                int offs = 0;
                for (int t = 0; t < (int)dwTexCnt; t++)
                {
                    VERIFY2((t + offs) < (int)m_VMRefs[fv.vmref].count, "- VMap layer index out of range");
                    st_VMapPt& vm_pt = m_VMRefs[fv.vmref].pts[t];
                    if (m_VMaps[vm_pt.vmap_index]->type != vmtUV)
                    {
                        offs++;
                        t--;
                        continue;
                    }
                    VERIFY2(vm_pt.vmap_index < int(m_VMaps.size()), "- VMap index out of range");
                    st_VMap* vmap = m_VMaps[vm_pt.vmap_index];
                    VERIFY2(vm_pt.index < vmap->size(), "- VMap point index out of range");
                    CopyMemory(data, &vmap->getUV(vm_pt.index), sz);
                    data += sz;

                    //	                Msg("%3.2f, %3.2f",vmap->getUV(vm_pt.index).x,vmap->getUV(vm_pt.index).y);
                }
            }
        }
    }
}
//----------------------------------------------------
void CEditableMesh::Render(const Fmatrix& parent, CSurface* S)
{
    // Skip the draw when buffers aren't ready yet — the background mesh
    // prefetcher will hand them in on a later frame. The mesh "pops in".
    // We deliberately don't lazy-load here anymore: the synchronous path
    // is the entire source of the camera-pan hitches we've been chasing.
    if (0 == m_RenderBuffers)
        return;
    // visibility test
    if (!m_Flags.is(flVisible))
        return;
    // frustum test
    Fbox bb;
    bb.set(m_Box);
    bb.xform(parent);
    if (!::Render->occ_visible(bb))
        return;
    // render
    RBMapPairIt rb_pair = m_RenderBuffers->find(S);
    if (rb_pair != m_RenderBuffers->end())
    {
        RBVector& rb_vec = rb_pair->second;
        for (RBVecIt rb_it = rb_vec.begin(); rb_it != rb_vec.end(); rb_it++)
            EDevice->DP(D3DPT_TRIANGLELIST, rb_it->pGeom, 0, rb_it->dwNumVertex / 3);
    }
}
//----------------------------------------------------
#define MAX_VERT_COUNT 0xFFFF
static Fvector RB[MAX_VERT_COUNT];
static int     RB_cnt = 0;

void           CEditableMesh::RenderList(const Fmatrix& parent, u32 color, bool bEdge, IntVec& fl)
{
    //	if (!m_Visible) return;
    //	if (!m_LoadState.is(LS_RBUFFERS)) CreateRenderBuffers();

    if (fl.size() == 0)
        return;
    RCache.set_xform_world(parent);
    EDevice->RenderNearer(0.0006);
    RB_cnt = 0;
    if (bEdge)
    {
        EDevice->SetShader(EDevice->m_WireShader);
        EDevice->SetRS(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    }
    else
        EDevice->SetShader(EDevice->m_SelectionShader);
    for (IntIt dw_it = fl.begin(); dw_it != fl.end(); ++dw_it)
    {
        st_Face& face = m_Faces[*dw_it];
        for (int k = 0; k < 3; ++k)
            RB[RB_cnt++].set(m_Vertices[face.pv[k].pindex]);

        if (RB_cnt == MAX_VERT_COUNT)
        {
            DU_impl.DrawPrimitiveL(D3DPT_TRIANGLELIST, RB_cnt / 3, RB, RB_cnt, color, true, false);
            RB_cnt = 0;
        }
    }

    if (RB_cnt)
        DU_impl.DrawPrimitiveL(D3DPT_TRIANGLELIST, RB_cnt / 3, RB, RB_cnt, color, true, false);

    if (bEdge)
        EDevice->SetRS(D3DRS_FILLMODE, EDevice->dwFillMode);

    EDevice->ResetNearer();
}
//----------------------------------------------------

void CEditableMesh::RenderSelection(const Fmatrix& parent, CSurface* s, u32 color)
{
    if (0 == m_RenderBuffers)
        GenerateRenderBuffers();
    //	if (!m_Visible) return;
    Fbox bb;
    bb.set(m_Box);
    bb.xform(parent);
    if (!::Render->occ_visible(bb))
        return;
    // render
    RCache.set_xform_world(parent);
    if (s)
    {
        SurfFacesPairIt sp_it = m_SurfFaces.find(s);
        if (sp_it != m_SurfFaces.end())
            RenderList(parent, color, false, sp_it->second);
    }
    else
    {
        EDevice->SetRS(D3DRS_TEXTUREFACTOR, color);
        for (RBMapPairIt p_it = m_RenderBuffers->begin(); p_it != m_RenderBuffers->end(); p_it++)
        {
            RBVector& rb_vec = p_it->second;
            for (RBVecIt rb_it = rb_vec.begin(); rb_it != rb_vec.end(); rb_it++)
                EDevice->DP(D3DPT_TRIANGLELIST, rb_it->pGeom, 0, rb_it->dwNumVertex / 3);
        }
        EDevice->SetRS(D3DRS_TEXTUREFACTOR, 0xffffffff);
    }
}
//----------------------------------------------------

void CEditableMesh::RenderEdge(const Fmatrix& parent, CSurface* s, u32 color)
{
    if (0 == m_RenderBuffers)
        GenerateRenderBuffers();
    //	if (!m_Visible) return;
    RCache.set_xform_world(parent);
    EDevice->SetShader(EDevice->m_WireShader);
    EDevice->RenderNearer(0.001);

    // render
    EDevice->SetRS(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    if (s)
    {
        SurfFacesPairIt sp_it = m_SurfFaces.find(s);
        if (sp_it != m_SurfFaces.end())
            RenderList(parent, color, true, sp_it->second);
    }
    else
    {
        EDevice->SetRS(D3DRS_TEXTUREFACTOR, color);
        for (RBMapPairIt p_it = m_RenderBuffers->begin(); p_it != m_RenderBuffers->end(); p_it++)
        {
            RBVector& rb_vec = p_it->second;
            for (RBVecIt rb_it = rb_vec.begin(); rb_it != rb_vec.end(); rb_it++)
                EDevice->DP(D3DPT_TRIANGLELIST, rb_it->pGeom, 0, rb_it->dwNumVertex / 3);
        }
        EDevice->SetRS(D3DRS_TEXTUREFACTOR, 0xffffffff);
    }
    EDevice->SetRS(D3DRS_FILLMODE, EDevice->dwFillMode);
    EDevice->ResetNearer();
}
//----------------------------------------------------

#define SKEL_MAX_FACE_COUNT 10000
struct svertRender
{
    Fvector  P;
    Fvector  N;
    Fvector2 uv;
};
void CEditableMesh::RenderSkeleton(const Fmatrix&, CSurface* S)
{
    if (false == IsGeneratedSVertices(RENDER_SKELETON_LINKS))
        GenerateSVertices(RENDER_SKELETON_LINKS);

    R_ASSERT2(m_SVertices, "SVertices empty!");
    SurfFacesPairIt sp_it = m_SurfFaces.find(S);
    R_ASSERT(sp_it != m_SurfFaces.end());
    IntVec&        face_lst = sp_it->second;
    _VertexStream* Stream   = &RCache.Vertex;
    u32            vBase;

    svertRender*   pv = (svertRender*)Stream->Lock(SKEL_MAX_FACE_COUNT * 3, m_Parent->vs_SkeletonGeom->vb_stride, vBase);
    Fvector        P0, N0, P1, N1;

    int            f_cnt = 0;
    for (IntIt i_it = face_lst.begin(); i_it != face_lst.end(); i_it++)
    {
        for (int k = 0; k < 3; k++, pv++)
        {
            st_SVert& SV = m_SVertices[*i_it * 3 + k];
            pv->uv.set(SV.uv);
            float          total = SV.bones[0].w;

            const Fmatrix& M     = m_Parent->m_Bones[SV.bones[0].id]->_RenderTransform();
            M.transform_tiny(pv->P, SV.offs);
            M.transform_dir(pv->N, SV.norm);

            Fvector P, N;

            for (u8 cnt = 1; cnt < (u8)SV.bones.size(); cnt++)
            {
                total += SV.bones[cnt].w;
                const Fmatrix& M = m_Parent->m_Bones[SV.bones[cnt].id]->_RenderTransform();
                M.transform_tiny(P, SV.offs);
                M.transform_dir(N, SV.norm);
                pv->P.lerp(pv->P, P, SV.bones[cnt].w / total);
                pv->N.lerp(pv->N, N, SV.bones[cnt].w / total);
            }
        }
        f_cnt++;
        if (S->m_Flags.is(CSurface::sf2Sided))
        {
            pv->P.set((pv - 1)->P);
            pv->N.invert((pv - 1)->N);
            pv->uv.set((pv - 1)->uv);
            pv++;
            pv->P.set((pv - 3)->P);
            pv->N.invert((pv - 3)->N);
            pv->uv.set((pv - 3)->uv);
            pv++;
            pv->P.set((pv - 5)->P);
            pv->N.invert((pv - 5)->N);
            pv->uv.set((pv - 5)->uv);
            pv++;
            f_cnt++;
        }
        if (f_cnt >= SKEL_MAX_FACE_COUNT - 1)
        {
            Stream->Unlock(f_cnt * 3, m_Parent->vs_SkeletonGeom->vb_stride);
            EDevice->DP(D3DPT_TRIANGLELIST, m_Parent->vs_SkeletonGeom, vBase, f_cnt);
            pv    = (svertRender*)Stream->Lock(SKEL_MAX_FACE_COUNT * 3, m_Parent->vs_SkeletonGeom->vb_stride, vBase);
            f_cnt = 0;
        }
    }
    Stream->Unlock(f_cnt * 3, m_Parent->vs_SkeletonGeom->vb_stride);
    if (f_cnt)
        EDevice->DP(D3DPT_TRIANGLELIST, m_Parent->vs_SkeletonGeom, vBase, f_cnt);
}
//----------------------------------------------------
