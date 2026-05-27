#include "stdafx.h"
#pragma hdrstop

#include "FolderObject.h"

#define FOLDER_CHUNK_VERSION       0x1000
#define FOLDER_CHUNK_CLASS         0x1001
#define FOLDER_CHUNK_COLLAPSED     0x1002
#define FOLDER_CHUNK_CHILDREN      0x1003

#define FOLDER_CURRENT_VERSION     0x0001

CFolderObject::CFolderObject(LPVOID data, LPCSTR name): CCustomObject(data, name)
{
    Construct(data);
}

void CFolderObject::Construct(LPVOID data)
{
    FClassID      = OBJCLASS_FOLDER;
    m_FolderClass = OBJCLASS_DUMMY;
    m_bCollapsed  = false;
}

CFolderObject::~CFolderObject() {}

u32 CFolderObject::GetChildren(ObjectList& lst) const
{
    lst.clear();
    for (ChildList::const_iterator it = m_Children.begin(); it != m_Children.end(); ++it)
        if (it->pObject)
            lst.push_back(it->pObject);
    return lst.size();
}

bool CFolderObject::AddChild(CCustomObject* obj)
{
    if (!obj)
        return false;

    // Same-class only: the child must match this folder's class, or be another folder
    // of the same affinity.
    if (obj->FClassID == OBJCLASS_FOLDER)
    {
        CFolderObject* sub = (CFolderObject*)obj;
        if (sub->m_FolderClass != m_FolderClass)
            return false;
        if (sub == this)
            return false;
        // Prevent cycles: walk up parents and reject if we hit obj.
        for (CCustomObject* p = m_pOwnerObject; p; p = p->m_pOwnerObject)
            if (p == obj)
                return false;
    }
    else if (obj->FClassID != m_FolderClass)
    {
        return false;
    }

    // Already a child?
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
        if (it->pObject == obj)
            return true;

    // Detach from previous parent if any.
    if (obj->m_pOwnerObject && obj->m_pOwnerObject->FClassID == OBJCLASS_FOLDER)
    {
        ((CFolderObject*)obj->m_pOwnerObject)->RemoveChild(obj);
    }

    obj->m_pOwnerObject = this;
    obj->m_CO_Flags.set(CCustomObject::flObjectInFolder, TRUE);

    m_Children.resize(m_Children.size() + 1);
    m_Children.back().pObject    = obj;
    m_Children.back().ObjectName = obj->GetName();
    return true;
}

bool CFolderObject::RemoveChild(CCustomObject* obj)
{
    if (!obj)
        return false;
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
    {
        if (it->pObject == obj)
        {
            m_Children.erase(it);
            if (obj->m_pOwnerObject == this)
                obj->m_pOwnerObject = NULL;
            obj->m_CO_Flags.set(CCustomObject::flObjectInFolder, FALSE);
            return true;
        }
    }
    return false;
}

void CFolderObject::PropagateShow(bool show)
{
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
    {
        if (!it->pObject)
            continue;
        it->pObject->Show(show ? TRUE : FALSE);
        if (it->pObject->FClassID == OBJCLASS_FOLDER)
            ((CFolderObject*)it->pObject)->PropagateShow(show);
    }
}

void CFolderObject::Select(int flag)
{
    inherited::Select(flag);
}

bool CFolderObject::GetBox(Fbox& box)
{
    box.invalidate();
    bool any = false;
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
    {
        if (!it->pObject)
            continue;
        Fbox cb;
        if (it->pObject->GetBox(cb))
        {
            if (!any)
            {
                box = cb;
                any = true;
            }
            else
                box.merge(cb);
        }
    }
    return any;
}

void CFolderObject::FillProp(LPCSTR pref, PropItemVec& items)
{
    // Replicate CCustomObject::FillProp but skip the transform fields — folders have no
    // position/rotation/scale.
    PropValue* V;
    EName = GetName();
    V     = PHelper().CreateNameCB(items, PrepareKey(pref, "Name"), &EName, NULL, NULL, RTextValue::TOnAfterEditEvent(this, &CCustomObject::OnObjectNameAfterEdit));
    V->OnChangeEvent.bind(this, &CCustomObject::OnNameChange);

    string64 child_count;
    sprintf(child_count, "count: %u", (u32)m_Children.size());
    PHelper().CreateCaption(items, PrepareKey(pref, "Children"), child_count);
}

bool CFolderObject::LoadStream(IReader& F)
{
    u16 version = 0;
    if (F.find_chunk(FOLDER_CHUNK_VERSION))
        version = F.r_u16();
    if (version < FOLDER_CURRENT_VERSION)
    {
        ELog.DlgMsg(mtError, "! CFolderObject: unsupported version. Object can't load.");
        return false;
    }
    CCustomObject::LoadStream(F);

    if (F.find_chunk(FOLDER_CHUNK_CLASS))
        m_FolderClass = (ObjClassID)F.r_u32();

    if (F.find_chunk(FOLDER_CHUNK_COLLAPSED))
        m_bCollapsed = F.r_u8() != 0;

    if (F.find_chunk(FOLDER_CHUNK_CHILDREN))
    {
        u32 cnt = F.r_u32();
        for (u32 k = 0; k < cnt; ++k)
        {
            m_Children.resize(m_Children.size() + 1);
            F.r_stringZ(m_Children.back().ObjectName);
        }
    }
    return true;
}

void CFolderObject::SaveStream(IWriter& F)
{
    CCustomObject::SaveStream(F);

    F.open_chunk(FOLDER_CHUNK_VERSION);
    F.w_u16(FOLDER_CURRENT_VERSION);
    F.close_chunk();

    F.open_chunk(FOLDER_CHUNK_CLASS);
    F.w_u32((u32)m_FolderClass);
    F.close_chunk();

    F.open_chunk(FOLDER_CHUNK_COLLAPSED);
    F.w_u8(m_bCollapsed ? 1 : 0);
    F.close_chunk();

    F.open_chunk(FOLDER_CHUNK_CHILDREN);
    F.w_u32(m_Children.size());
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
    {
        const char* nm = it->pObject ? it->pObject->GetName() : it->ObjectName.c_str();
        F.w_stringZ(nm ? nm : "");
    }
    F.close_chunk();
}

bool CFolderObject::LoadLTX(CInifile& ini, LPCSTR sect_name)
{
    u32 version = ini.r_u32(sect_name, "folder_version");
    if (version < FOLDER_CURRENT_VERSION)
    {
        ELog.DlgMsg(mtError, "! CFolderObject: unsupported version. Object can't load.");
        return false;
    }
    CCustomObject::LoadLTX(ini, sect_name);

    m_FolderClass = (ObjClassID)ini.r_u32(sect_name, "folder_class");
    m_bCollapsed  = !!ini.r_bool(sect_name, "folder_collapsed");

    u32 cnt = ini.r_u32(sect_name, "folder_child_count");
    for (u32 k = 0; k < cnt; ++k)
    {
        string128 key;
        sprintf(key, "folder_child_%u", k);
        m_Children.resize(m_Children.size() + 1);
        m_Children.back().ObjectName = ini.r_string(sect_name, key);
    }
    return true;
}

void CFolderObject::SaveLTX(CInifile& ini, LPCSTR sect_name)
{
    CCustomObject::SaveLTX(ini, sect_name);
    ini.w_u32(sect_name, "folder_version", FOLDER_CURRENT_VERSION);
    ini.w_u32(sect_name, "folder_class", (u32)m_FolderClass);
    ini.w_bool(sect_name, "folder_collapsed", m_bCollapsed ? TRUE : FALSE);
    ini.w_u32(sect_name, "folder_child_count", m_Children.size());
    u32 k = 0;
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it, ++k)
    {
        string128 key;
        sprintf(key, "folder_child_%u", k);
        const char* nm = it->pObject ? it->pObject->GetName() : it->ObjectName.c_str();
        ini.w_string(sect_name, key, nm ? nm : "");
    }
}

void CFolderObject::OnObjectRemove(const CCustomObject* object)
{
    ChildList::iterator it = std::find(m_Children.begin(), m_Children.end(), object);
    if (it != m_Children.end())
        m_Children.erase(it);
}

void CFolderObject::OnSceneUpdate()
{
    inherited::OnSceneUpdate();

    // Resolve any child name → pointer references left over from LoadStream.
    bool resolved_any = false;
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end();)
    {
        if (it->pObject == NULL && it->ObjectName.size())
        {
            CCustomObject* CO = Scene->FindObjectByName(it->ObjectName.c_str(), (CCustomObject*)0);
            if (CO)
            {
                it->pObject        = CO;
                CO->m_pOwnerObject = this;
                CO->m_CO_Flags.set(flObjectInFolder, TRUE);
                resolved_any = true;
                ++it;
            }
            else
            {
                ELog.Msg(mtError, "Folder '%s' has invalid reference to object '%s'.", GetName(), it->ObjectName.c_str());
                it = m_Children.erase(it);
            }
        }
        else
        {
            ++it;
        }
    }

    // If any child went missing without being properly removed via OnObjectRemove,
    // clean it up here.
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end();)
    {
        if (it->pObject && it->pObject->IsDeleted())
            it = m_Children.erase(it);
        else
            ++it;
    }
}

bool CFolderObject::OnSelectionRemove()
{
    // Cascade delete to all children.
    // We build a transient list because deleting children mutates m_Children.
    xr_vector<CCustomObject*> to_remove;
    to_remove.reserve(m_Children.size());
    for (ChildList::iterator it = m_Children.begin(); it != m_Children.end(); ++it)
        if (it->pObject)
            to_remove.push_back(it->pObject);

    for (CCustomObject* child: to_remove)
    {
        // Sub-folders cascade recursively via the same OnSelectionRemove path.
        if (child->FClassID == OBJCLASS_FOLDER)
        {
            ((CFolderObject*)child)->OnSelectionRemove();
        }
        Scene->RemoveObject(child, false, true);
        xr_delete(child);
    }
    m_Children.clear();
    return true;
}

void CFolderObject::OnShowHint(AStringVec& dest)
{
    inherited::OnShowHint(dest);
    string64 buf;
    sprintf(buf, "Folder children: %u", (u32)m_Children.size());
    dest.push_back(xr_string(buf));
}
