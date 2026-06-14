// Originally by B.O.R.S.C.H.T. team
// see https://bitbucket.org/stalker/xray-csky_borscht_sdk

#include "stdafx.h"

#include "IM_Manipulator.h"
#include "../xrEUI/imgui.h"
#include "../xrEUI/ImGuizmo.h"
#include "../../scene/scene.h"
#include "../../UI_LevelTools.h"
#include "../../Entry/CustomObject.h"
#include "../../Tools/Wallmark/ESceneWallmarkTools.h"
#include "../../Tools/ESceneClassList.h"

IM_Manipulator imManipulator;

void           IM_Manipulator::Render(float canvasX, float canvasY, float canvasWidth, float canvasHeight)
{
    ImGuizmo::SetRect(canvasX, canvasY, canvasWidth, canvasHeight);
    ImGuizmo::SetDrawlist();

    // Wallmarks aren't CCustomObjects — the regular tool/GetOTool path below
    // doesn't apply. In Move mode show a combined translate + rotate-around-
    // normal widget (decals have one fewer degree of freedom than full
    // objects: 2D in-plane translate and single-axis rotation around the
    // surface normal). In Scale mode show a 2D scale in the wallmark's
    // tangent plane. The existing drag controller (TUI_ControlWallmarkMove)
    // still handles plain off-gizmo clicks — ImGuizmo only captures input
    // when the user grabs a handle.
    if (LTools->CurrentClassID() == OBJCLASS_WM &&
        (LTools->GetAction() == etaMove || LTools->GetAction() == etaScale))
    {
        ESceneWallmarkTool* wmt = (ESceneWallmarkTool*)Scene->GetTool(OBJCLASS_WM);
        if (wmt)
        {
            ESceneWallmarkTool::wallmark* wm = wmt->FindSingleSelectedWallmark();
            if (wm)
            {
                // AddWallmark_internal needs the snap-list gate on to find a
                // ray hit; auto-enable it for the duration of the drag. Same
                // pattern TUI_ControlWallmarkMove uses for its drag path.
                UILeftBarForm* lb = MainForm->GetLeftBarForm();
                if (lb && !lb->IsUseSnapList() && !m_active)
                {
                    lb->SetUseSnapList(true);
                    m_wm_snap_was_off = true;
                }

                // Build pose matrix in surface tangent plane. World-up serves
                // as the reference for in-plane "right" unless the surface is
                // near-horizontal, in which case use world-X — same logic
                // BuildMatrix uses with the flAxisAlign flag. Apply the
                // wallmark's stored rotation `r` so the gizmo's local frame
                // reflects the current orientation around the normal.
                const Fvector normal = wm->compute_normal();
                Fvector       y_ref;
                if (_abs(normal.y) > 0.99f)
                    y_ref.set(1.f, 0.f, 0.f);
                else
                    y_ref.set(0.f, 1.f, 0.f);

                Fvector right;
                right.crossproduct(y_ref, normal);
                right.normalize_safe();
                Fvector up;
                up.crossproduct(normal, right);

                const float cs = _cos(wm->r);
                const float sn = _sin(wm->r);
                Fvector     iAxis, jAxis;
                iAxis.x = right.x * cs + up.x * sn;
                iAxis.y = right.y * cs + up.y * sn;
                iAxis.z = right.z * cs + up.z * sn;
                jAxis.x = -right.x * sn + up.x * cs;
                jAxis.y = -right.y * sn + up.y * cs;
                jAxis.z = -right.z * sn + up.z * cs;

                // Bake current w/h into i/j magnitudes. This is essential for
                // ImGuizmo's drag math: it captures the matrix at drag-start
                // and writes back an updated matrix each frame. If we always
                // feed a unit-scaled matrix, ImGuizmo's "current scale" stays
                // at 1.0 forever and the cumulative-from-drag-start delta
                // gets re-applied every frame even when the mouse is static
                // — the wallmark scales exponentially out of control.
                Fmatrix Pose;
                Pose.identity();
                Pose.i.set(iAxis);
                Pose.i.mul(wm->w);
                Pose.j.set(jAxis);
                Pose.j.mul(wm->h);
                Pose.k.set(normal);
                Pose.c.set(wm->bounds.P);

                Fmatrix             Delta = Fidentity;
                ImGuizmo::OPERATION op;
                if (LTools->GetAction() == etaMove)
                    op = (ImGuizmo::OPERATION)(ImGuizmo::TRANSLATE | ImGuizmo::ROTATE_Z);
                else   // etaScale
                    op = (ImGuizmo::OPERATION)(ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y);

                const bool manipulated = ImGuizmo::Manipulate(
                    (float*)&Device->mView, (float*)&Device->mProject, op, ImGuizmo::LOCAL,
                    (float*)&Pose, (float*)&Delta, nullptr);

                if (manipulated)
                {
                    // Unified extraction works for both modes — pose mutations
                    // are absolute, not deltas. Translation = Pose.c.
                    // Scale = magnitudes of Pose.i/j (unchanged in move mode).
                    // Rotation = signed angle between original iAxis and the
                    // new (normalized) Pose.i, around the surface normal.
                    // Using world-aware geometry instead of Delta.getXYZ.z
                    // — the latter only gives the right answer when the
                    // wallmark's normal happens to be world-Z.
                    const float new_w = Pose.i.magnitude();
                    const float new_h = Pose.j.magnitude();

                    Fvector new_i_dir = Pose.i;
                    new_i_dir.normalize_safe();
                    Fvector cv;
                    cv.crossproduct(iAxis, new_i_dir);
                    const float sin_a       = cv.dotproduct(normal);
                    const float cos_a       = iAxis.dotproduct(new_i_dir);
                    const float delta_angle = atan2f(sin_a, cos_a);
                    const float new_r       = wm->r + delta_angle;

                    const Fvector new_p = Pose.c;
                    wmt->RebuildSelectedWallmark(new_p, new_r, new_w, new_h);
                }

                if (ImGuizmo::IsUsing() && !m_active)
                    m_active = true;
                if (!ImGuizmo::IsUsing() && m_active)
                {
                    Scene->UndoSave();
                    if (m_wm_snap_was_off)
                    {
                        if (lb)
                            lb->SetUseSnapList(false);
                        m_wm_snap_was_off = false;
                    }
                    m_active = false;
                }
                return;
            }
        }
    }

    ESceneCustomOTool* tool = Scene->GetOTool(LTools->CurrentClassID());
    if (!tool)
        return;

    ObjectList lst;
    tool->GetQueryObjects(lst, TRUE, TRUE, FALSE);
    if (lst.size() < 1)
        return;

    const bool IsCSParent   = Tools->GetSettings(etfCSParent);
    Fmatrix    ObjectMatrix = lst.front()->FTransform;
    Fmatrix    DeltaMatrix  = Fidentity;

    switch (LTools->GetAction())
    {
        case etaMove:
        {
            float  MoveSnap[3];
            float* PtrMoveSnap = LTools->GetSettings(etfMSnap) ? MoveSnap : nullptr;

            if (PtrMoveSnap)
                std::fill_n(MoveSnap, std::size(MoveSnap), Tools->m_MoveSnap);

            const bool IsManipulated = ImGuizmo::Manipulate((float*)&Device->mView, (float*)&Device->mProject, ImGuizmo::TRANSLATE, ImGuizmo::WORLD, (float*)&ObjectMatrix, (float*)&DeltaMatrix, PtrMoveSnap);

            if (IsManipulated)
            {
                for (ObjectIt it = lst.begin(); it != lst.end(); it++)
                    (*it)->Move(DeltaMatrix.c);
            }
        }
        break;
        case etaRotate:
        {
            float  RotateSnap;
            float* PtrRotateSnap = LTools->GetSettings(etfASnap) ? &RotateSnap : nullptr;

            if (PtrRotateSnap)
                RotateSnap = rad2deg(Tools->m_RotateSnapAngle);

            Fvector OriginalRotation;

            ObjectMatrix.getXYZ(OriginalRotation);

            const bool IsManipulated = ImGuizmo::Manipulate((float*)&Device->mView, (float*)&Device->mProject, ImGuizmo::ROTATE, ImGuizmo::WORLD, (float*)&ObjectMatrix, (float*)&DeltaMatrix, PtrRotateSnap);

            if (IsManipulated)
            {
                Fvector DeltaXYZ;

                DeltaMatrix.getXYZ(DeltaXYZ);

                for (ObjectIt it = lst.begin(); it != lst.end(); it++)
                {
                    void (CCustomObject::*Handler)(Fvector&, float);

                    if (IsCSParent)
                        Handler = &CCustomObject::RotateParent;
                    else
                        Handler = &CCustomObject::RotateLocal;

                    (*it->*Handler)(Fvector().set(0, 0, 1), -DeltaXYZ.z);
                    (*it->*Handler)(Fvector().set(1, 0, 0), -DeltaXYZ.x);
                    (*it->*Handler)(Fvector().set(0, 1, 0), -DeltaXYZ.y);
                }
                UI->UpdateScene();
            }
        }
        break;
        case etaScale:
        {
            float  ScaleSnap[3];
            float* PtrScaleSnap = LTools->GetSettings(etfScaleFixed) ? ScaleSnap : nullptr;

            if (PtrScaleSnap)
                std::fill_n(ScaleSnap, std::size(ScaleSnap), Tools->m_ScaleFixed);

            const bool IsManipulated = ImGuizmo::Manipulate((float*)&Device->mView, (float*)&Device->mProject, ImGuizmo::SCALE, ImGuizmo::LOCAL, (float*)&ObjectMatrix, (float*)&DeltaMatrix, PtrScaleSnap);

            if (IsManipulated)
            {
                Fvector Scale;
                Scale.x = DeltaMatrix.i.magnitude();
                Scale.y = DeltaMatrix.j.magnitude();
                Scale.z = DeltaMatrix.k.magnitude();

                for (ObjectIt it = lst.begin(); it != lst.end(); it++)
                {
                    Scale.mul((*it)->GetScale());
                    (*it)->SetScale(Scale);
                }
                UI->UpdateScene();
            }
        }
        break;
    }

    if (ImGuizmo::IsUsing() && !m_active)
    {
        // activate
        m_active = true;
    }

    if (!ImGuizmo::IsUsing() && m_active)
    {
        // deactivate
        Scene->UndoSave();
        m_active = false;
    }
}
