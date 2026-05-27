#include "stdafx.h"

CCustomObject* EScene::FindObjectByName(LPCSTR name, ObjClassID classfilter)
{
    if (!name)
        return NULL;

    CCustomObject* object = 0;
    if (classfilter == OBJCLASS_DUMMY)
    {
        SceneToolsMapPairIt _I = m_SceneTools.begin();
        SceneToolsMapPairIt _E = m_SceneTools.end();
        for (; _I != _E; ++_I)
        {
            ESceneCustomOTool* mt = dynamic_cast<ESceneCustomOTool*>(_I->second);

            if (mt && (0 != (object = mt->FindObjectByName(name))))
                return object;
        }
    }
    else
    {
        ESceneCustomOTool* mt = GetOTool(classfilter);
        VERIFY(mt);
        if (mt && (0 != (object = mt->FindObjectByName(name))))
            return object;
    }
    return object;
}

CCustomObject* EScene::FindObjectByName(LPCSTR name, CCustomObject* pass_object)
{
    CCustomObject*      object = 0;
    SceneToolsMapPairIt _I     = m_SceneTools.begin();
    SceneToolsMapPairIt _E     = m_SceneTools.end();
    for (; _I != _E; _I++)
    {
        ESceneCustomOTool* mt = dynamic_cast<ESceneCustomOTool*>(_I->second);
        if (mt && (0 != (object = mt->FindObjectByName(name, pass_object))))
            return object;
    }
    return 0;
}

bool EScene::FindDuplicateName()
{
    // find duplicate name
    SceneToolsMapPairIt _I = m_SceneTools.begin();
    SceneToolsMapPairIt _E = m_SceneTools.end();
    for (; _I != _E; _I++)
    {
        ESceneCustomOTool* mt = dynamic_cast<ESceneCustomOTool*>(_I->second);
        if (mt)
        {
            ObjectList& lst = mt->GetObjects();
            for (ObjectIt _F = lst.begin(); _F != lst.end(); _F++)
                if (FindObjectByName((*_F)->GetName(), *_F))
                {
                    ELog.DlgMsg(mtError, "Duplicate object name already exists: '%s'", (*_F)->GetName());
                    return true;
                }
        }
    }
    return false;
}

void EScene::GenObjectName(ObjClassID cls_id, char* buffer, const char* pref)
{
    // Split pref into a base and a trailing "_NN" numeric suffix, if present.
    // With a suffix, paste/clone walks forward from the source number
    // (abc_04 -> abc_05 -> abc_06 ...) instead of appending _00 every time.
    // Without a suffix, fall back to the old behaviour: try the bare name,
    // then append _00, _01, ...
    xr_string base;
    int       start_n    = 0;
    bool      has_suffix = false;

    if (pref && pref[0])
    {
        size_t len = xr_strlen(pref);
        size_t i   = len;
        while (i > 0 && isdigit((unsigned char)pref[i - 1]))
            --i;
        if (i < len && i > 0 && pref[i - 1] == '_')
        {
            base.assign(pref, i - 1);
            start_n    = atoi(pref + i);
            has_suffix = true;
        }
        else
        {
            base = pref;
        }
    }
    else
    {
        ESceneCustomOTool* ot = GetOTool(cls_id);
        VERIFY(ot);
        base = ot->ClassName();
    }

    for (int k = 0; true; ++k)
    {
        xr_string temp;
        if (has_suffix)
        {
            temp.sprintf("%s_%02d", base.c_str(), start_n + k);
        }
        else if (pref && pref[0])
        {
            if (k == 0)
                temp = pref;
            else
                temp.sprintf("%s_%02d", base.c_str(), k - 1);
        }
        else
        {
            temp.sprintf("%s_%02d", base.c_str(), k);
        }

        bool exists;
        FindObjectByNameCB(temp.c_str(), exists);
        if (!exists)
        {
            xr_strcpy(buffer, 256, temp.c_str());
            return;
        }
    }
}
