#pragma once
class ESceneWallmarkTool;

// Per-tool LeftBar form for the Wallmark tool. Hosts the "Next Placement"
// properties (the defaults used by each new wallmark you place) — these used
// to live in the right-side Properties Panel as tool state, but the panel
// now reflects the actually-selected wallmark, so the next-placement
// settings moved here.
//
// Rendered by UILeftBarForm::Draw() via LTools->GetToolForm()->Draw(), so it
// only appears when the wallmark tool is the active target.
class UIWallmarkTool: public UIToolCustom
{
public:
    ESceneWallmarkTool* Tool;
    UIWallmarkTool();
    ~UIWallmarkTool() override;
    void Draw() override;

private:
    UIPropertiesForm m_Defaults;
};
