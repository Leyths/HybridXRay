#include "stdafx.h"

// Node Add
TUI_ControlWallmarkAdd::TUI_ControlWallmarkAdd(int st, int act, ESceneToolBase* parent): TUI_CustomControl(st, act, parent) {}

bool TUI_ControlWallmarkAdd::Start(TShiftState Shift)
{
    ESceneWallmarkTool* S = (ESceneWallmarkTool*)parent_tool;

    S->SelectObjects(false);
    wm_cnt = 0;
    if (S->AddWallmark(UI->m_CurrentRStart, UI->m_CurrentRDir))
    {
        wm_cnt++;
        if (!(Shift & ssAlt))
        {
            Scene->UndoSave();
            ResetActionToSelect();
            return false;
        }
        else
            return true;
    }
    return false;
}
void TUI_ControlWallmarkAdd::Move(TShiftState _Shift) {}
bool TUI_ControlWallmarkAdd::End(TShiftState _Shift)
{
    if (!(_Shift & ssAlt))
        ResetActionToSelect();
    if (wm_cnt)
        Scene->UndoSave();
    return true;
}

// WM Move — matches the standard move-tool conventions:
//   * Ctrl + LMB click  : one-shot teleport to cursor
//   * LMB drag          : continuous offset-preserving slide. The wallmark
//                          is anchored to its initial click offset; cursor
//                          motion translates the wallmark by the same delta,
//                          rather than teleporting it to the cursor.
//   * Plain LMB click   : no-op
//
// The snap-list gate (LeftBar "Use Snap List" checkbox) is auto-enabled for
// the duration of the drag so the user doesn't have to discover and toggle
// it manually. Restored on End.
TUI_ControlWallmarkMove::TUI_ControlWallmarkMove(int st, int act, ESceneToolBase* parent): TUI_CustomControl(st, act, parent), m_Dragging(false), m_DidMove(false), m_HasDragAnchor(false), m_SnapListWasOff(false)
{
    m_StartCursorWorld.set(0, 0, 0);
    m_StartWallmarkP.set(0, 0, 0);
}

bool TUI_ControlWallmarkMove::Start(TShiftState Shift)
{
    if (Shift == ssRBOnly)
    {
        ExecCommand(COMMAND_SHOWCONTEXTMENU, parent_tool->FClassID);
        return false;
    }

    ESceneWallmarkTool* S = (ESceneWallmarkTool*)parent_tool;
    if (S->SelectionCount(true) != 1)
        return false;

    m_Dragging       = true;
    m_DidMove        = false;
    m_HasDragAnchor  = false;
    m_SnapListWasOff = false;

    // Auto-enable snap-list gate so the user doesn't have to discover and
    // flip the LeftBar toggle just to slide a wallmark.
    UILeftBarForm* lb = MainForm->GetLeftBarForm();
    if (lb && !lb->IsUseSnapList())
    {
        lb->SetUseSnapList(true);
        m_SnapListWasOff = true;
    }

    if (Shift & ssCtrl)
    {
        // Ctrl+click: one-shot teleport to cursor.
        if (S->MoveSelectedWallmarkTo(UI->m_CurrentRStart, UI->m_CurrentRDir))
        {
            m_DidMove = true;
            UI->RedrawScene();
        }
        return true;
    }

    // Plain LMB: capture drag anchor for offset-preserving slide. We don't
    // re-project yet — wait for actual cursor motion.
    ESceneWallmarkTool::wallmark* sel = S->FindSingleSelectedWallmark();
    if (!sel)
        return true;

    Fvector cursor_hit;
    if (!S->PickSurfacePoint(UI->m_CurrentRStart, UI->m_CurrentRDir, cursor_hit))
        return true;   // off-surface click; bail without anchor, Move events will no-op

    m_StartCursorWorld = cursor_hit;
    m_StartWallmarkP   = sel->bounds.P;
    m_HasDragAnchor    = true;
    return true;
}

void TUI_ControlWallmarkMove::Move(TShiftState _Shift)
{
    if (!m_Dragging || !m_HasDragAnchor)
        return;
    if (!(_Shift & ssLeft))
        return;

    ESceneWallmarkTool* S = (ESceneWallmarkTool*)parent_tool;

    Fvector cur_cursor_world;
    if (!S->PickSurfacePoint(UI->m_CurrentRStart, UI->m_CurrentRDir, cur_cursor_world))
        return;

    Fvector delta;
    delta.sub(cur_cursor_world, m_StartCursorWorld);

    Fvector target;
    target.add(m_StartWallmarkP, delta);

    // Cast a ray from the camera through the target. This guarantees the
    // wallmark gets re-projected near `target` regardless of surface
    // curvature between the cursor's hit and the target.
    Fvector ray_start = EDevice->vCameraPosition;
    Fvector ray_dir;
    ray_dir.sub(target, ray_start);
    ray_dir.normalize_safe();

    if (S->MoveSelectedWallmarkTo(ray_start, ray_dir))
    {
        m_DidMove = true;
        UI->RedrawScene();
    }
}

bool TUI_ControlWallmarkMove::End(TShiftState _Shift)
{
    const bool was_dragging      = m_Dragging;
    const bool did_move          = m_DidMove;
    const bool snap_list_was_off = m_SnapListWasOff;
    m_Dragging                   = false;
    m_DidMove                    = false;
    m_HasDragAnchor              = false;
    m_SnapListWasOff             = false;

    if (snap_list_was_off)
    {
        UILeftBarForm* lb = MainForm->GetLeftBarForm();
        if (lb)
            lb->SetUseSnapList(false);
    }

    if (was_dragging && did_move)
        Scene->UndoSave();
    return true;
}
