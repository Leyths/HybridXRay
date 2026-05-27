#include "stdafx.h"

void ESceneFolderTool::CreateControls()
{
    inherited::CreateDefaultControls(estDefault);
}

void ESceneFolderTool::RemoveControls()
{
    inherited::RemoveControls();
}

CCustomObject* ESceneFolderTool::CreateObject(LPVOID data, LPCSTR name)
{
    CCustomObject* O = xr_new<CFolderObject>(data, name);
    O->FParentTools  = this;
    return O;
}
