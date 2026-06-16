#include "stdafx.h"

#ifdef USE_ARENA_ALLOCATOR
static const u32   s_arena_size = 32 * 1024 * 1024;
char*              s_fake_array = nullptr;
doug_lea_allocator g_render_lua_allocator(s_fake_array, s_arena_size, "render:lua");
#else    // #ifdef USE_ARENA_ALLOCATOR
doug_lea_allocator g_render_lua_allocator(0, 0, "render:lua");
#endif   // #ifdef USE_ARENA_ALLOCATOR

#define RENDER_OBJECT(P, B)                                                                                                                                                                                \
    {                                                                                                                                                                                                      \
        try                                                                                                                                                                                                \
        {                                                                                                                                                                                                  \
            (N->val)->RenderRoot(P, B);                                                                                                                                                                    \
        }                                                                                                                                                                                                  \
        catch (...)                                                                                                                                                                                        \
        {                                                                                                                                                                                                  \
            ELog.DlgMsg(mtError, "Please notify AlexMX!!! Critical error has occured in render routine!!! [Type B] - Tools: '%s' Object: '%s'", (N->val)->FParentTools->ClassName(), (N->val)->GetName()); \
        }                                                                                                                                                                                                  \
    }

void object_Normal_0(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(0, false);
}
void object_Normal_1(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(1, false);
}
void object_Normal_2(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(2, false);
}
void object_Normal_3(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(3, false);
}
//------------------------------------------------------------------------------
void object_StrictB2F_0(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(0, true);
}
void object_StrictB2F_1(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(1, true);
}
void object_StrictB2F_2(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(2, true);
}
void object_StrictB2F_3(EScene::mapObject_Node* N)
{
    RENDER_OBJECT(3, true);
}

#define RENDER_SCENE_TOOLS(P, B)                                                                                                                              \
    {                                                                                                                                                         \
        SceneMToolsIt s_it  = scene_tools.begin();                                                                                                            \
        SceneMToolsIt s_end = scene_tools.end();                                                                                                              \
        for (; s_it != s_end; s_it++)                                                                                                                         \
        {                                                                                                                                                     \
            EDevice->SetShader(B ? EDevice->m_SelectionShader : EDevice->m_WireShader);                                                                       \
            RCache.set_xform_world(Fidentity);                                                                                                                \
            try                                                                                                                                               \
            {                                                                                                                                                 \
                (*s_it)->OnRenderRoot(P, B);                                                                                                                  \
            }                                                                                                                                                 \
            catch (...)                                                                                                                                       \
            {                                                                                                                                                 \
                ELog.DlgMsg(mtError, "Please notify AlexMX!!! Critical error has occured in render routine!!! [Type B] - Tools: '%s'", (*s_it)->ClassName()); \
            }                                                                                                                                                 \
        }                                                                                                                                                     \
    }

void EScene::RenderSky(const Fmatrix& camera)
{
    if (!valid())
        return;

    //	draw sky
    /*
    //.
        if (m_SkyDome&&fraBottomBar->miDrawSky->Checked){
            st_Environment& E = m_LevelOp.m_Envs[m_LevelOp.m_CurEnv];
            m_SkyDome->GetPosition() = camera.c;
            m_SkyDome->UpdateTransform(true);
            EDevice->SetRS(D3DRS_TEXTUREFACTOR, E.m_SkyColor.get());
            m_SkyDome->RenderSingle();
            EDevice->SetRS(D3DRS_TEXTUREFACTOR,	0xffffffff);
        }
    */
}

struct tools_rp_pred
{
    IC bool operator()(ESceneToolBase* x, ESceneToolBase* y) const
    {
        return x->RenderPriority() < y->RenderPriority();
    }
};

#define DEFINE_MSET_PRED(T, N, I, P) \
    typedef xr_multiset<T, P> N;     \
    typedef N::iterator       I;

DEFINE_MSET_PRED(ESceneToolBase*, SceneMToolsSet, SceneMToolsIt, tools_rp_pred);
DEFINE_MSET_PRED(ESceneCustomOTool*, SceneOToolsSet, SceneOToolsIt, tools_rp_pred);

void EScene::Render(const Fmatrix& camera)
{
    if (!valid())
        return;

    //	if( locked() )	return;

    // extract and sort object tools
    SceneOToolsSet object_tools;
    SceneMToolsSet scene_tools;
    {
        SceneToolsMapPairIt t_it  = m_SceneTools.begin();
        SceneToolsMapPairIt t_end = m_SceneTools.end();
        for (; t_it != t_end; t_it++)
            if (t_it->second)
            {
                // before render
                t_it->second->BeforeRender();
                // sort tools
                ESceneCustomOTool* mt = dynamic_cast<ESceneCustomOTool*>(t_it->second);
                if (mt)
                    object_tools.insert(mt);
                scene_tools.insert(t_it->second);
            }
    }

    // insert objects
    {
        SceneOToolsIt t_it  = object_tools.begin();
        SceneOToolsIt t_end = object_tools.end();
        for (; t_it != t_end; t_it++)
        {
            ObjectList& lst   = (*t_it)->GetObjects();
            ObjectIt    o_it  = lst.begin();
            ObjectIt    o_end = lst.end();
            for (; o_it != o_end; o_it++)
            {
                if (!(*o_it)->Visible() || !(*o_it)->IsRender())
                    continue;
                // Frustum + tiny-size cull. Objects whose bounding sphere
                // lies entirely outside the camera frustum, OR whose
                // projected screen area is below ~1.5 pixels, can't draw
                // anything visible; skipping them eliminates their per-
                // priority dispatch, surface walk, and GPU draw calls
                // downstream. Selected objects are exempt from tiny-cull
                // so the user never loses sight of their selection.
                //
                // Objects whose GetBox returns false (lights, way points,
                // etc.) bypass both tests — small visual handles that need
                // to show up regardless of camera direction.
                //
                // Threshold derivation: CalcSSA returns R^2 / dist^2 (FOV-
                // independent ratio). At 90 deg FOV and 1920px wide
                // viewport, ~1 px maps to (R/dist) ~= 1/960 ~= 1e-3, so
                // (R/dist)^2 ~= 1.1e-6. 2e-6 ~= 1.5 px square, conservative.
                static const float SSA_TINY = 2.0e-6f;
                Fbox bb;
                if ((*o_it)->GetBox(bb))
                {
                    Fvector C;
                    float   R;
                    bb.getsphere(C, R);
                    if (!::Render->ViewBase.testSphere_dirty(C, R))
                        continue;
                    const float dsq = EDevice->vCameraPosition.distance_to_sqr(C);
                    if (!(*o_it)->Selected() && R * R < SSA_TINY * dsq)
                        continue;
                }
                float distSQ = EDevice->vCameraPosition.distance_to_sqr((*o_it)->FPosition);
                mapRenderObjects.insertInAnyWay(distSQ, *o_it);
            }
        }
    }

    // Reused across all normal-pass priorities for the current frame; static
    // to keep the heap allocation across frames.
    static xr_vector<EditableObjectDrawItem> s_batch;

    auto drain_batch = [&]() {
        // Sort by shader handle so identical shaders cluster — drain binds
        // each unique shader exactly once. Sub-sort key (mesh, surf) doesn't
        // matter for correctness, just for cache locality, so skip it.
        std::sort(s_batch.begin(), s_batch.end(),
                  [](const EditableObjectDrawItem& a, const EditableObjectDrawItem& b) {
                      return a.shader < b.shader;
                  });
        ref_shader prev_shader;   // default-constructed = null
        for (const EditableObjectDrawItem& it : s_batch)
        {
            if (it.shader != prev_shader)
            {
                EDevice->SetShader(it.shader);
                prev_shader = it.shader;
            }
            RCache.set_xform_world(it.parent);
            if (it.is_skeleton)
                it.mesh->RenderSkeleton(it.parent, it.surf);
            else
                it.mesh->Render(it.parent, it.surf);
        }
    };

    auto run_normal_batched = [&](void (*normal_fn)(EScene::mapObject_Node*)) {
        s_batch.clear();
        g_DrawCollector = &s_batch;
        mapRenderObjects.traverseLR(normal_fn);
        g_DrawCollector = nullptr;
        drain_batch();
    };

    // priority #0 — normal pass batched (sort by shader, collapse SetShader
    // calls), alpha pass direct (back-to-front depth order must be preserved
    // for correct blending).
    run_normal_batched(object_Normal_0);
    RENDER_SCENE_TOOLS(0, false);
    mapRenderObjects.traverseRL(object_StrictB2F_0);
    RENDER_SCENE_TOOLS(0, true);

    // priority #1
    run_normal_batched(object_Normal_1);
    RENDER_SCENE_TOOLS(1, false);
    mapRenderObjects.traverseRL(object_StrictB2F_1);
    RENDER_SCENE_TOOLS(1, true);

    // priority #2
    run_normal_batched(object_Normal_2);
    RENDER_SCENE_TOOLS(2, false);
    mapRenderObjects.traverseRL(object_StrictB2F_2);
    RENDER_SCENE_TOOLS(2, true);

    // priority #3
    run_normal_batched(object_Normal_3);
    RENDER_SCENE_TOOLS(3, false);
    mapRenderObjects.traverseRL(object_StrictB2F_3);
    RENDER_SCENE_TOOLS(3, true);

    // render snap
    RenderSnapList();

    // clear
    mapRenderObjects.clear();

    SceneMToolsIt s_it  = scene_tools.begin();
    SceneMToolsIt s_end = scene_tools.end();
    for (; s_it != s_end; s_it++)
        (*s_it)->AfterRender();
}
