#include "stdafx.h"
#include "UIEditLibrary.h"
#include "../../xrECore/Editor/Library.h"

UIEditLibrary* UIEditLibrary::Form = nullptr;

UIEditLibrary::UIEditLibrary()
{
    {
        ref_texture texture_null;
        texture_null.create("ed\\ed_nodata");
        texture_null->Load();
        VERIFY(texture_null->surface_get());
        texture_null->surface_get()->AddRef();
        m_NullTexture = texture_null->surface_get();
        m_RealTexture = nullptr;
    }

    m_ObjectList = xr_new<UIItemListForm>();
    InitObjects();
    m_ObjectList->SetOnItemFocusedEvent(TOnILItemFocused(this, &UIEditLibrary::OnItemFocused));
    m_Props           = xr_new<UIPropertiesForm>();
    m_Selected        = nullptr;
    m_Preview         = false;
    m_SelectLods      = false;
    m_CameraSaved     = false;
    m_PendingMakeAll  = false;
    m_PendingClearAll = false;
    m_PendingCount    = 0;
}

void UIEditLibrary::OnItemFocused(ListItem* item)
{
    // TODO: возможно нужно текстуру удалять
    // if (m_RealTexture)m_RemoveTexture = m_RealTexture;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();
    m_Current = nullptr;
    if (item)
    {
        m_Selected  = item;
        m_Current   = item->Key();
        auto* m_Thm = ImageLib.CreateThumbnail(m_Current, EImageThumbnail::ETObject);
        if (m_Thm)
        {
            m_Thm->Update(m_RealTexture);
            PropItemVec Info;
            m_Thm->FillInfo(Info);
            m_Props->AssignItems(Info);
        }

        if (m_Preview)
        {
            ListItemsVec vec;
            vec.push_back(item);
            SelectionToReference(&vec);
            // Match the bulk maker's framing so the user can sanity-check the
            // exact thumbnail before committing to a 80k-object run.
            SaveCameraState();
            if (!m_pEditObjects.empty())
            {
                ApplyThumbnailPose(m_pEditObjects.front());
                FrameForThumbnail(m_pEditObjects.front());
            }
        }
    }

    // UpdateObjectProperties();
    UI->RedrawScene();
}

UIEditLibrary::~UIEditLibrary()
{
    RestoreCameraState();
}

void UIEditLibrary::SaveCameraState()
{
    if (m_CameraSaved)
        return;
    m_SavedCamHPB = EDevice->m_Camera.GetHPB();
    m_SavedCamPos = EDevice->m_Camera.GetPosition();
    m_CameraSaved = true;
}

void UIEditLibrary::RestoreCameraState()
{
    if (!m_CameraSaved)
        return;
    EDevice->m_Camera.Set(m_SavedCamHPB, m_SavedCamPos);
    m_CameraSaved = false;
}

void UIEditLibrary::ApplyThumbnailPose(CSceneObject* SO)
{
    if (!SO)
        return;
    // Yaw only — the object stays upright on its own Y axis (trees don't
    // lean, props don't tip). A previous version baked pitch into the
    // object's rotation and made everything look diagonally skewed. The
    // pitch component is applied to the *camera* in FrameForThumbnail
    // instead so the object stays vertical and we look down at it.
    //
    // Both the CEditableObject t_v* and the SceneObject F* have to be set:
    // OnRender() resyncs SO from O->t_v* on every frame (so the library
    // preview tracks edits made in the Object Editor), which would
    // otherwise instantly undo the pose set on the SceneObject alone.
    // FRotation.x is pitch (around world X), .y is heading (around world Y),
    // .z is bank — confirmed via CCustomObject::OnUpdateTransform's
    // setXYZi(-x, -y, -z) → setHPB(y, x, z) shuffle. We want heading only so
    // the object stays vertical; the previous code put 45° on .x, which
    // tilted everything forward.
    CEditableObject* O = SO->GetReference();
    if (O)
    {
        O->t_vPosition.set(0.f, 0.f, 0.f);
        O->t_vRotate.set(0.f, deg2rad(45.f), 0.f);
        O->t_vScale.set(1.f, 1.f, 1.f);
    }
    SO->FPosition.set(0.f, 0.f, 0.f);
    SO->FRotation.set(0.f, deg2rad(45.f), 0.f);
    SO->FScale.set(1.f, 1.f, 1.f);
    // `true` forces OnUpdateTransform to run inline. Without it, the dirty
    // flag is set but FTransform isn't rebuilt until the next OnFrame /
    // RenderSingle. The single-shot Preview flow always has many frames
    // between Apply and capture, but the bulk maker iterates in one tight
    // call stack — FrameForThumbnail would otherwise read a stale matrix
    // (identity) and compute the camera distance against the un-rotated
    // local bbox, clipping wide objects.
    SO->UpdateTransform(true);
}

void UIEditLibrary::FrameForThumbnail(CSceneObject* SO)
{
    if (!SO)
        return;
    CEditableObject* obj = SO->GetReference();
    if (!obj)
        return;

    Fbox bb = obj->GetBox();
    bb.xform(SO->_Transform());

    // Camera pitched 30° downward, no heading change — we look at the
    // upright yawed object from above-front.
    Fvector camera_hpb;
    camera_hpb.set(0.f, -deg2rad(30.f), 0.f);

    // ZoomExtents would frame the bounding *sphere*, which for axis-aligned
    // bboxes can leave 40%+ empty margin in the captured square. Compute
    // the exact distance instead: project every bbox corner into the
    // camera's local frame, find the smallest distance such that every
    // corner stays inside the square frustum at the capture aspect (= 1).
    //
    // For a corner offset cd (corner - bb_center, in camera-local coords)
    // and a camera distance D along the camera's -k axis, the world point
    // sits at camera-local (cd.x, cd.y, cd.z + D). It fits inside the
    // square frame when:
    //     |cd.x| / (cd.z + D) <= tan(fFOV/2)
    //     |cd.y| / (cd.z + D) <= tan(fFOV/2)
    // ⇒ D >= max(|cd.x|, |cd.y|) / tan(fFOV/2) - cd.z
    Fmatrix cam_rot;
    cam_rot.setHPB(camera_hpb.x, camera_hpb.y, camera_hpb.z);

    Fvector C;
    bb.getcenter(C);
    Fvector corners[8];
    bb.getpoints(corners);

    const float tan_half_fov = tanf(deg2rad(EDevice->fFOV) * 0.5f);
    float       required_d   = 0.f;
    for (int i = 0; i < 8; ++i)
    {
        Fvector d;
        d.sub(corners[i], C);
        // Camera basis vectors in world coords are i/j/k of cam_rot. Project
        // d onto each via dot to get camera-local coordinates of the corner
        // offset.
        Fvector cd;
        cd.x = d.dotproduct(cam_rot.i);
        cd.y = d.dotproduct(cam_rot.j);
        cd.z = d.dotproduct(cam_rot.k);

        const float dx = _abs(cd.x) / tan_half_fov - cd.z;
        const float dy = _abs(cd.y) / tan_half_fov - cd.z;
        required_d     = _max(required_d, _max(dx, dy));
    }
    // Small margin so tight-fitting corners don't get visually clipped by
    // the very edge of the frame.
    required_d *= 1.05f;

    // Camera position = bbox center - forward * distance.
    Fvector pos;
    pos.mad(C, cam_rot.k, -required_d);

    EDevice->m_Camera.Set(camera_hpb, pos);
    UI->RedrawScene();
}

bool UIEditLibrary::ItemThumbExists(ListItem* item) const
{
    if (!item)
        return false;
    string_path fn;
    FS.update_path(fn, _objects_, EFS.ChangeFileExt(item->Key(), ".thm").c_str());
    return !!FS.exist(fn);
}

u32 UIEditLibrary::CountMissingThumbnails() const
{
    u32 missing = 0;
    for (ListItem* item: m_ObjectList->m_Items)
        if (!ItemThumbExists(item))
            ++missing;
    return missing;
}

u32 UIEditLibrary::CountExistingThumbnails() const
{
    u32 existing = 0;
    for (ListItem* item: m_ObjectList->m_Items)
        if (ItemThumbExists(item))
            ++existing;
    return existing;
}

void UIEditLibrary::MakeAllMissingThumbnails()
{
    // Make sure we're in a state matching the single-shot path. Re-using
    // m_pEditObjects so OnRender's preview render covers the snap target.
    const bool was_preview = m_Preview;
    m_Preview              = true;
    SaveCameraState();

    // Collect the work-list up-front so we have a stable count for the
    // progress bar even if generated .thm files start appearing on disk
    // mid-run.
    xr_vector<ListItem*> missing;
    missing.reserve(m_ObjectList->m_Items.size());
    for (ListItem* item: m_ObjectList->m_Items)
        if (!ItemThumbExists(item))
            missing.push_back(item);

    if (missing.empty())
    {
        m_Preview = was_preview;
        ELog.Msg(mtInformation, "+ No missing thumbnails.");
        return;
    }

    SPBItem* pb   = UI->ProgressStart((u32)missing.size(), "Making thumbnails");
    u32      made = 0;

    u32 skipped = 0;
    for (ListItem* item: missing)
    {
        pb->Inc(item->Key());

        RStringVec sel;
        sel.push_back(item->Key());
        ChangeReference(sel);

        if (m_pEditObjects.empty())
        {
            Msg("! Skipping '%s' (load failed)", item->Key());
            ++skipped;
            continue;
        }

        CSceneObject*    SO  = m_pEditObjects.front();
        CEditableObject* obj = SO->GetReference();
        // Three defensive gates before we touch the obj — corrupted .object
        // files have surfaced as null references, mesh-less skeletons, and
        // degenerate bboxes (min == max, all-zero). Any of those would push
        // bad input into ApplyThumbnailPose / FrameForThumbnail / the render
        // path and crash mid-loop. Log + continue keeps the batch alive.
        if (!obj)
        {
            Msg("! Skipping '%s' (null reference)", item->Key());
            ++skipped;
            continue;
        }
        if (obj->MeshCount() == 0)
        {
            Msg("! Skipping '%s' (no meshes)", item->Key());
            ++skipped;
            continue;
        }
        const Fbox& obb = obj->GetBox();
        Fvector     size;
        obb.getsize(size);
        const float min_extent = 1e-4f;
        if (!obb.is_valid() ||
            (size.x < min_extent && size.y < min_extent && size.z < min_extent))
        {
            Msg("! Skipping '%s' (degenerate bbox)", item->Key());
            ++skipped;
            continue;
        }

        ApplyThumbnailPose(SO);
        FrameForThumbnail(SO);

        string_path fn;
        FS.update_path(fn, _objects_, EFS.ChangeFileExt(item->Key(), ".thm").c_str());
        if (ImageLib.CreateOBJThumbnail(fn, obj, obj->Version()))
            ++made;

        if (UI->NeedAbort())
            break;
    }
    UI->ProgressEnd(pb);

    // Drop the temp preview objects so the right-pane texture refreshes from
    // disk on the next click.
    ChangeReference(RStringVec());
    m_Selected    = nullptr;
    m_Current     = nullptr;
    m_RealTexture = nullptr;
    m_Props->ClearProperties();

    m_Preview = was_preview;
    RestoreCameraState();

    if (skipped)
        ELog.DlgMsg(mtInformation, "+ Created %u thumbnail%s. Skipped %u object%s (see log for names).", made, made == 1 ? "" : "s", skipped, skipped == 1 ? "" : "s");
    else
        ELog.DlgMsg(mtInformation, "+ Created %u thumbnail%s.", made, made == 1 ? "" : "s");
}

void UIEditLibrary::ClearAllThumbnails()
{
    u32 removed = 0;
    for (ListItem* item: m_ObjectList->m_Items)
    {
        string_path fn;
        FS.update_path(fn, _objects_, EFS.ChangeFileExt(item->Key(), ".thm").c_str());
        if (FS.exist(fn))
        {
            FS.file_delete(fn);
            ++removed;
        }
    }
    // Drop the right-pane preview texture so the next item click re-loads.
    OnItemFocused(nullptr);
    ELog.DlgMsg(mtInformation, "+ Deleted %u thumbnail%s.", removed, removed == 1 ? "" : "s");
}

void UIEditLibrary::DrawConfirmModals()
{
    if (m_PendingMakeAll)
    {
        ImGui::OpenPopup("Make thumbnails?");
        m_PendingMakeAll = false;
    }
    if (m_PendingClearAll)
    {
        ImGui::OpenPopup("Clear thumbnails?");
        m_PendingClearAll = false;
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Make thumbnails?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("You are about to make thumbnails for %u items. This may take a while.\n\nBest run with no level loaded so scene geometry doesn't appear in the captures.", m_PendingCount);
        ImGui::Separator();
        if (ImGui::Button("Continue", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            MakeAllMissingThumbnails();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Clear thumbnails?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("You are about to delete thumbnails for %u items. This cannot be undone.", m_PendingCount);
        ImGui::Separator();
        if (ImGui::Button("Continue", ImVec2(120, 0)))
        {
            ImGui::CloseCurrentPopup();
            ClearAllThumbnails();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void UIEditLibrary::InitObjects()
{
    ListItemsVec items;
    FS_FileSet   lst;

    if (Lib.GetObjects(lst))
    {
        FS_FileSetIt it = lst.begin();
        FS_FileSetIt _E = lst.end();
        for (; it != _E; it++)
        {
            xr_string fn;
            ListItem* I = LHelper().CreateItem(items, it->name.c_str(), 0, ListItem::flDrawThumbnail, 0);
        }
    }
    // if (m_RealTexture)m_RemoveTexture = m_RealTexture;
    // m_RealTexture = nullptr;
    // m_Props->ClearProperties();
    m_ObjectList->AssignItems(items);

    //   m_Items.clear();
    //   ListItemsVec items;
    //   FS_FileSet lst;
    //   Lib.GetObjects(lst);
    //   FS_FileSetIt it = lst.begin();
    //   FS_FileSetIt _E = lst.end();
    //   for (; it != _E; it++)
    //       LHelper().CreateItem(m_Items, it->name.c_str(), 0, 0, 0);
    //   m_Items->AssignItems(items, false, true);
}

void UIEditLibrary::Update()
{
    if (!Form)
        return;

    if (!Form->IsClosed())
        Form->Draw();
    else
        Close();
}

void UIEditLibrary::Show()
{
    UI->BeginEState(esEditLibrary);

    if (!Form)
        Form = xr_new<UIEditLibrary>();
}

void UIEditLibrary::Close()
{
    UI->EndEState(esEditLibrary);
    // TODO: возможно еще кого то надо грохнуть
    xr_delete(Form);
}

void UIEditLibrary::DrawObjects()
{
    ImGui::BeginChild("Object List"_RU >> u8"Список Объектов");
    ImGui::Separator();
    // m_ObjectList->m_Flags.set(m_ObjectList->fMultiSelect, true);
    m_ObjectList->Draw();
    ImGui::Separator();
    ImGui::EndChild();
    if (ImGui::IsItemHovered())
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    /*for (ListItem *item : m_Items)
    {
        item->m_Object
    }


        if (it->first == OBJCLASS_DUMMY)
            continue;
        ObjectList& lst = ot->GetObjects();
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        if (ImGui::TreeNode("folder", ("%ss", it->second->ClassDesc())))
        {
            if (OBJCLASS_GROUP == it->first)
            {
                for (ObjectIt _F = lst.begin(); _F != lst.end(); ++_F)
                {
                    switch (m_Mode)
                    {
                    case UIObjectList::M_All:
                        break;
                    case UIObjectList::M_Visible:
                        if (!(*_F)->Visible())continue;
                        break;
                    case UIObjectList::M_Inbvisible:
                        if ((*_F)->Visible())continue;
                        break;
                    default:
                        break;
                    }
                    {
                        strcpy(str_name, ((CGroupObject*)(*_F))->GetName());
                    }
                    DrawObject(*_F, str_name);
                    ImGui::PushID(str_name);
                    if (ImGui::TreeNode(str_name))
                    {
                        ObjectList 					grp_lst;

                        ((CGroupObject*)(*_F))->GetObjects(grp_lst);

                        for (ObjectIt _G = grp_lst.begin(); _G != grp_lst.end(); _G++)
                        {
                            DrawObject(*_G, 0);
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }
            else
            {
                bool FindSelectedObj = false;
                for (ObjectIt _F = lst.begin(); _F != lst.end(); ++_F)
                {
                    switch (m_Mode)
                    {
                    case UIObjectList::M_All:
                        break;
                    case UIObjectList::M_Visible:
                        if (!(*_F)->Visible())continue;
                        break;
                    case UIObjectList::M_Inbvisible:
                        if ((*_F)->Visible())continue;
                        break;
                    default:
                        break;
                    }
                    DrawObject(*_F, 0);
                    FindSelectedObj = FindSelectedObj | (*_F) == m_SelectedObject;
                }
                if (!FindSelectedObj)m_SelectedObject = nullptr;

            }
            ImGui::TreePop();
        }
    }*/

    // m_cur_cls = LTools->CurrentClassID();
    // string1024 str_name;

    // for (SceneToolsMapPairIt it = Scene->FirstTool(); it != Scene->LastTool(); ++it)
    // {
    //	ESceneCustomOTool* ot = dynamic_cast<ESceneCustomOTool*>(it->second);
    //	if (ot && ((m_cur_cls == OBJCLASS_DUMMY) || (it->first == m_cur_cls)))
    //	{
    //		if (it->first == OBJCLASS_DUMMY)
    //			continue;
    //		ObjectList& lst = ot->GetObjects();
    //		ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
    //		if (ImGui::TreeNode("folder", ("%ss", it->second->ClassDesc())))
    //		{
    //			if (OBJCLASS_GROUP == it->first)
    //			{
    //				for (ObjectIt _F = lst.begin(); _F != lst.end(); ++_F)
    //				{
    //					switch (m_Mode)
    //					{
    //					case UIObjectList::M_All:
    //						break;
    //					case UIObjectList::M_Visible:
    //						if (!(*_F)->Visible())continue;
    //						break;
    //					case UIObjectList::M_Inbvisible:
    //						if ((*_F)->Visible())continue;
    //						break;
    //					default:
    //						break;
    //					}
    //					{
    //						strcpy(str_name, ((CGroupObject*)(*_F))->GetName());
    //					}
    //					DrawObject(*_F, str_name);
    //					ImGui::PushID(str_name);
    //					if (ImGui::TreeNode(str_name))
    //					{
    //						ObjectList 					grp_lst;

    //						((CGroupObject*)(*_F))->GetObjects(grp_lst);

    //						for (ObjectIt _G = grp_lst.begin(); _G != grp_lst.end(); _G++)
    //						{
    //							DrawObject(*_G, 0);
    //						}
    //						ImGui::TreePop();
    //					}
    //					ImGui::PopID();
    //				}
    //			}
    //			else
    //			{
    //				bool FindSelectedObj = false;
    //				for (ObjectIt _F = lst.begin(); _F != lst.end(); ++_F)
    //				{
    //					switch (m_Mode)
    //					{
    //					case UIObjectList::M_All:
    //						break;
    //					case UIObjectList::M_Visible:
    //						if (!(*_F)->Visible())continue;
    //						break;
    //					case UIObjectList::M_Inbvisible:
    //						if ((*_F)->Visible())continue;
    //						break;
    //					default:
    //						break;
    //					}
    //					DrawObject(*_F, 0);
    //					FindSelectedObj = FindSelectedObj | (*_F) == m_SelectedObject;
    //				}
    //				if (!FindSelectedObj)m_SelectedObject = nullptr;

    //			}
    //			ImGui::TreePop();
    //		}
    //	}
    //}
}

void UIEditLibrary::DrawObject(CCustomObject* obj, const char* name)
{
    /*if (m_Filter[0])
    {
        if (name)
        {
            if (strstr(name, m_Filter) == 0)return;
        }
        else
        {
            if (strstr(obj->GetName(), m_Filter) == 0)return;
        }
    }
    ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

    if (obj->Selected())
    {
        Flags |= ImGuiTreeNodeFlags_Bullet;
    }
    if (m_SelectedObject == obj)
    {
        Flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (name)
        ImGui::TreeNodeEx(name, Flags);
    else
        ImGui::TreeNodeEx(obj->GetName(), Flags);
    if (ImGui::IsItemClicked())
    {
        if (!ImGui::GetIO().KeyCtrl)
            Scene->SelectObjects(false, OBJCLASS_DUMMY);
        obj->Select(true);
        m_SelectedObject = obj;
    }*/
}

void UIEditLibrary::GenerateLOD(RStringVec& props, bool bHighQuality)
{
    u32      lodsCnt = 0;
    SPBItem* pb      = UI->ProgressStart(props.size(), "Making LOD");

    for (const shared_str& str: props)
    {
        RStringVec reference;
        reference.push_back(str);
        ChangeReference(reference);   // select item

        R_ASSERT(m_pEditObjects.size() == 1);
        CSceneObject*    SO = m_pEditObjects[0];
        CEditableObject* O  = SO->GetReference();

        if (O && O->IsMUStatic())
        {
            pb->Inc(O->GetName());
            BOOL bLod = O->m_objectFlags.is(CEditableObject::eoUsingLOD);
            O->m_objectFlags.set(CEditableObject::eoUsingLOD, FALSE);
            xr_string tex_name;
            tex_name = EFS.ChangeFileExt(O->GetName(), "");

            string_path tmp;
            strcpy(tmp, tex_name.c_str());
            _ChangeSymbol(tmp, '\\', '_');
            tex_name = xr_string("lod_") + tmp;
            tex_name = ImageLib.UpdateFileName(tex_name);
            ImageLib.CreateLODTexture(O, tex_name.c_str(), LOD_IMAGE_SIZE, LOD_IMAGE_SIZE, LOD_SAMPLE_COUNT, O->Version(), bHighQuality ? 4 /*7*/ : 1);
            O->OnDeviceDestroy();
            O->m_objectFlags.set(CEditableObject::eoUsingLOD, bLod);
            ELog.Msg(mtInformation, "+ LOD for object '%s' successfully created.", O->GetName());
            lodsCnt++;
        }
        else
            ELog.Msg(mtError, "! Can't create LOD texture from non 'Multiple Usage' object.", SO->RefName());

        if (UI->NeedAbort())
            break;
    }

    UI->ProgressEnd(pb);

    if (lodsCnt)
        ELog.DlgMsg(mtInformation, "+ '%u' LOD's succesfully created.", lodsCnt);
}

void UIEditLibrary::MakeLOD(bool bHighQuality)
{
    // if (ebSave->Enabled)
    // {
    //     ELog.DlgMsg(mtError, "& Save library changes before generating LOD.");
    //         return;
    // }

    int res = ELog.DlgMsg(mtConfirmation, TMsgDlgButtons() | mbYes | mbNo | mbCancel, "Do you want to select multiple objects?");

    if (res == mrCancel)
        return;

    if (res == mrNo)
    {
        RStringVec sel_items;
        sel_items.push_back(m_Selected->Key());
        GenerateLOD(sel_items, bHighQuality);
        return;
    }

    R_ASSERT(res == mrYes);
    UIChooseForm::SelectItem(smObject, 512, 0);
    m_SelectLods     = true;
    m_HighQualityLod = true;
    // answer is handled in DrawRightBar
}

void UIEditLibrary::OnMakeThmClick()
{
    U32Vec       pixels;
    ListItemsVec sel_items;

    if (!m_Selected)
        return;

    sel_items.push_back(m_Selected);
    // m_Items->GetSelected(NULL, sel_items, false);
    ListItemsIt it   = sel_items.begin();
    ListItemsIt it_e = sel_items.end();

    for (; it != it_e; ++it)
    {
        ListItem*        item = *it;
        CEditableObject* obj  = Lib.CreateEditObject(item->Key());

        if (obj && m_Preview)
        {
            string_path fn;
            FS.update_path(fn, _objects_, ChangeFileExt(obj->GetName(), ".thm").c_str());

            // m_Items->SelectItem(item->Key(), true, false, true);
            if (ImageLib.CreateOBJThumbnail(fn, obj, obj->Version()))
                ELog.Msg(mtInformation, "+ Thumbnail successfully created.");
        }
        else
            ELog.DlgMsg(mtError, "& Can't create thumbnail. Set preview mode.");

        Lib.RemoveEditObject(obj);
    }

    ELog.DlgMsg(mtInformation, "+ Done.");
}

void UIEditLibrary::OnPropertiesClick()
{
    MessageBoxA(0, 0, 0, 0);
}

void UIEditLibrary::DrawRightBar()
{
    if (ImGui::BeginChild("Right", ImVec2(0, 0)))
    {
        if (m_NullTexture || m_RealTexture)
        {
            if (m_RealTexture)
                ImGui::Image(m_RealTexture, ImVec2(200, 200));
            else
                ImGui::Image(m_NullTexture, ImVec2(200, 200));
        }
        else
            ImGui::InvisibleButton("Image", ImVec2(200, 200));

        m_Props->Draw();

        // if (disabled)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        if (ImGui::Button("Properties"_RU >> u8"Свойства", ImVec2(-1, 0)))
            OnPropertiesClick();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // if (disabled)
        {
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }

        // Make Thumbnail & Lod
        {
            bool enableMakeThumbnailAndLod = m_Selected && m_Preview;

            if (!enableMakeThumbnailAndLod)
            {
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
            }

            if (ImGui::Button("Make Thumbnail"_RU >> u8"Создать иконку", ImVec2(-1, 0)))
                OnMakeThmClick();
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (ImGui::Button("Make LOD (High Quality)"_RU >> u8"Создать LOD(Высокое качество)", ImVec2(-1, 0)))
                MakeLOD(true);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (ImGui::Button("Make LOD (Low Quality)"_RU >> u8"Создать LOD(Среднее качество)", ImVec2(-1, 0)))
                MakeLOD(false);
            if (ImGui::IsItemHovered())
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

            if (!enableMakeThumbnailAndLod)
            {
                ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }
        }

        if (ImGui::Checkbox("Preview"_RU >> u8"Предпросмотр", &m_Preview))
            OnPreviewClick();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // if (disabled)
        {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
        }

        if (ImGui::Button("Rename Object"_RU >> u8"Переименовать объект", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::Button("Remove Object"_RU >> u8"Удалить объект", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ImGui::Button("Import Object"_RU >> u8"Импортировать объект", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::Button("Export LWO"_RU >> u8"Экспорт LWO", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::Button("Export OBJ"_RU >> u8"Экспорт OBJ", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        if (ImGui::Button("Save"_RU >> u8"Сохранить", ImVec2(-1, 0)))
        {}
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // if (disabled)
        {
            ImGui::PopItemFlag();
            ImGui::PopStyleVar();
        }

        if (ImGui::Button("Close"_RU >> u8"Закрыть", ImVec2(-1, 0)))
            Close();
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::EndChild();
    }

    if (m_SelectLods)
    {
        bool       change = false;
        SStringVec lst;

        if (UIChooseForm::GetResult(change, lst))
        {
            if (change)
            {
                RStringVec selItems;

                for (const xr_string& xrStr: lst)
                    selItems.push_back(xrStr.c_str());

                GenerateLOD(selItems, m_HighQualityLod);
            }

            m_SelectLods = false;
        }

        UIChooseForm::Update();
    }
}

void UIEditLibrary::OnPreviewClick()
{
    if (m_Preview)
    {
        SaveCameraState();
        RefreshSelected();
        if (!m_pEditObjects.empty())
        {
            ApplyThumbnailPose(m_pEditObjects.front());
            FrameForThumbnail(m_pEditObjects.front());
        }
    }
    else
    {
        RefreshSelected();
        RestoreCameraState();
    }
}

void UIEditLibrary::RefreshSelected()
{
    bool mt = false;

    if (m_Preview)
    {
        if (m_Selected)
        {
            ListItemsVec vec;
            vec.push_back(m_Selected);
            mt = SelectionToReference(&vec);
        }
        else
            mt = SelectionToReference(nullptr);
    }

    // ebMakeThm->Enabled = !bReadOnly && mt;
    // ebMakeLOD_high->Enabled = !bReadOnly && cbPreview->Checked;
    // ebMakeLOD_low->Enabled = !bReadOnly && cbPreview->Checked;
    UI->RedrawScene();
}

/// ---------------------------------------------------------------------------
bool UIEditLibrary::SelectionToReference(ListItemsVec* props)
{
    RStringVec   sel_strings;
    ListItemsVec sel_items;

    if (props)
        sel_items = *props;
    // else
    // m_Items->GetSelected(NULL, sel_items, false /*true*/);

    ListItemsIt it   = sel_items.begin();
    ListItemsIt it_e = sel_items.end();

    for (; it != it_e; ++it)
    {
        ListItem* item = *it;
        sel_strings.push_back(item->Key());
    }
    ChangeReference(sel_strings);
    return sel_strings.size() > 0;
}

void UIEditLibrary::ChangeReference(const RStringVec& items)
{
    xr_vector<CSceneObject*>::iterator it   = m_pEditObjects.begin();
    xr_vector<CSceneObject*>::iterator it_e = m_pEditObjects.end();
    for (; it != it_e; ++it)
    {
        CSceneObject* SO = *it;
        xr_delete(SO);
    }

    m_pEditObjects.clear();

    RStringVec::const_iterator sit   = items.begin();
    RStringVec::const_iterator sit_e = items.end();

    for (; sit != sit_e; ++sit)
    {
        CSceneObject* SO = xr_new<CSceneObject>((LPVOID)0, (LPSTR)0);
        m_pEditObjects.push_back(SO);
        SO->SetReference((*sit).c_str());

        CEditableObject* NE = SO->GetReference();
        if (NE)
        {
            SO->FPosition = NE->t_vPosition;
            SO->FScale    = NE->t_vScale;
            SO->FRotation = NE->t_vRotate;
        }
        // update transformation
        SO->UpdateTransform();

        /*
            // save new position
            CEditableObject* E				= m_pEditObject->GetReference();

            if (E && new_name && (stricmp(E->GetName(),new_name))==0 ) return;

            if (E)
            {
                E->t_vPosition.set			(m_pEditObject->PPosition);
                E->t_vScale.set				(m_pEditObject->PScale);
                E->t_vRotate.set			(m_pEditObject->PRotation);
            }
            m_pEditObject->SetReference		(new_name);
            // get old position
            E								= m_pEditObject->GetReference();
            if (E)
            {
                m_pEditObject->PPosition 	= E->t_vPosition;
                m_pEditObject->PScale 		= E->t_vScale;
                m_pEditObject->PRotation	= E->t_vRotate;
            }
            // update transformation
            m_pEditObject->UpdateTransform	();
        */
    }

    ExecCommand(COMMAND_EVICT_OBJECTS);
    ExecCommand(COMMAND_EVICT_TEXTURES);
}

void UIEditLibrary::OnRender()
{
    if (!Form)
        return;

    if (!Form->m_Preview)
        return;

    for (auto& it: Form->m_pEditObjects)
    {
        CSceneObject*    SO = it;
        CSceneObject*    S  = SO;

        CEditableObject* O  = SO->GetReference();
        if (O)
        {
            S->m_RT_Flags.set(S->flRT_Visible, true);

            if (!S->FPosition.similar(O->t_vPosition))
                S->FPosition = O->t_vPosition;

            if (!S->FRotation.similar(O->t_vRotate))
                S->FRotation = O->t_vRotate;

            if (!S->FScale.similar(O->t_vScale))
                S->FScale = O->t_vScale;

            SO->OnFrame();
            SO->RenderSingle();
            // static bool strict = true;
            // SO->Render(0, strict);
        }
    }
}

void UIEditLibrary::Draw()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(750, 650));

    if (!ImGui::Begin("Object Library"_RU >> u8"Библиотека Объектов", &bOpen))
    {
        ImGui::PopStyleVar(1);
        ImGui::End();
        return;
    }

    {
        ImGui::BeginGroup();

        if (ImGui::BeginChild("Left", ImVec2(-200, -ImGui::GetFrameHeight() - 4), true))
            DrawObjects();

        ImGui::EndChild();
        ImGui::Text(" Items count: %u"_RU >> u8" Количество объектов: %u", m_ObjectList->m_Items.size());
        ImGui::SameLine();
        if (ImGui::Button("Make all missing thumbnails"))
        {
            m_PendingCount   = CountMissingThumbnails();
            m_PendingMakeAll = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SameLine();
        if (ImGui::Button("Clear all thumbnails"))
        {
            m_PendingCount    = CountExistingThumbnails();
            m_PendingClearAll = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::EndGroup();
    }

    ImGui::SameLine();
    DrawRightBar();

    DrawConfirmModals();

    ImGui::PopStyleVar(1);
    ImGui::End();
}
