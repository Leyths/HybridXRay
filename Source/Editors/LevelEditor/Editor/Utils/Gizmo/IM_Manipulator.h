// Originally by B.O.R.S.C.H.T. team
// see https://bitbucket.org/stalker/xray-csky_borscht_sdk

#pragma once
class IM_Manipulator
{
public:
    bool m_active;
    // Tracks whether the wallmark gizmo path auto-enabled the snap-list gate
    // for the duration of a drag. AddWallmark_internal needs Scene->GetSnap-
    // List(false) to return non-null, which depends on UILeftBarForm's snap-
    // list toggle. Restored on deactivate so the user's prior setting isn't
    // silently overridden.
    bool m_wm_snap_was_off;

    IM_Manipulator(): m_active(false), m_wm_snap_was_off(false) {}

    void Render(float canvasX, float canvasY, float canvasWidth, float canvasHeight);
};
extern IM_Manipulator imManipulator;
