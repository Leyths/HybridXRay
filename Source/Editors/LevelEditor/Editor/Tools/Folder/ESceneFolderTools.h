#pragma once

class ESceneFolderTool: public ESceneCustomOTool
{
    typedef ESceneCustomOTool inherited;

protected:
    virtual void CreateControls();
    virtual void RemoveControls();

public:
    ESceneFolderTool(): ESceneCustomOTool(OBJCLASS_FOLDER) {}

    IC LPCSTR ClassName() { return "folder"; }
    IC LPCSTR ClassDesc() { return "Folder"_RU >> u8"Папка"; }
    IC int    RenderPriority() { return 1; }

    virtual void Clear(bool bSpecific = false) { inherited::Clear(bSpecific); }

    virtual bool IsNeedSave() { return inherited::IsNeedSave(); }

    virtual CCustomObject* CreateObject(LPVOID data, LPCSTR name);
};
