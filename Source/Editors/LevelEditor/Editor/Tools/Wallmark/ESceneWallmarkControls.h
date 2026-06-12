#pragma once
// refs
class ESceneWallmarkTools;

class TUI_ControlWallmarkAdd: public TUI_CustomControl
{
    u32 wm_cnt;

public:
    TUI_ControlWallmarkAdd(int st, int act, ESceneToolBase* parent);
    virtual bool Start(TShiftState _Shift);
    virtual bool End(TShiftState _Shift);
    virtual void Move(TShiftState _Shift);
};

class TUI_ControlWallmarkMove: public TUI_CustomControl
{
    // LMB-drag: re-projects the selected wallmark with an offset preserved
    // from the initial click — drag translates by cursor delta, the wallmark
    // does NOT teleport to the cursor.
    // Ctrl+LMB click: one-shot teleport to cursor.
    // Plain LMB click without drag: no-op.
    bool    m_Dragging;
    bool    m_DidMove;
    bool    m_HasDragAnchor;
    Fvector m_StartCursorWorld;   // surface hit point at drag start
    Fvector m_StartWallmarkP;     // selected wallmark's center at drag start
    // We auto-enable the LeftBar "Use Snap List" gate for the duration of a
    // drag so the user doesn't have to flip it manually; remember if it was
    // off so End can restore it.
    bool    m_SnapListWasOff;

public:
    TUI_ControlWallmarkMove(int st, int act, ESceneToolBase* parent);
    virtual bool Start(TShiftState _Shift);
    virtual bool End(TShiftState _Shift);
    virtual void Move(TShiftState _Shift);
    // Default Move action runs hidden-cursor (so the user can drag past
    // screen edges); but that mode only refreshes m_DeltaCpH, not the
    // m_CurrentRStart/RDir ray. Our drag needs absolute surface hits each
    // frame, so we keep the cursor visible.
    virtual bool HiddenMode() override
    {
        return false;
    }
};
