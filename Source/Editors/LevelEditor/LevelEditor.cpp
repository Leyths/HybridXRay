// LevelEditor.cpp : Определяет точку входа для приложения.
//
#include "stdafx.h"
#include "../../xrAPI\xrGameManager.h"
#include "Engine/xrGameManager.h"
#include "../xrEngine/std_classes.h"
#include "../xrEngine/IGame_Persistent.h"
#include "../xrEngine/XR_IOConsole.h"
#include "../xrEngine/IGame_Level.h"
#include "../xrEngine/x_ray.h"
#include "Engine/XRayEditor.h"
#include "resources/splash.h"
#include <shellapi.h>

// Returns the value of the -level <path> CLI argument, or an empty string if
// absent. Allows scripted launches that auto-load a specific .level file, e.g.
//   LevelEditor.exe -nosplash -level "C:\path with spaces\08 - rostock bar.level"
// CommandLineToArgvW handles quoted paths with spaces.
static xr_string parse_level_arg()
{
    int    argc = 0;
    LPWSTR cmdLine = GetCommandLineW();
    LPWSTR* argv = ::CommandLineToArgvW(cmdLine, &argc);
    xr_string out;
    if (!argv)
        return out;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"-level") == 0)
        {
            char buf[MAX_PATH * 2] = {0};
            ::WideCharToMultiByte(CP_ACP, 0, argv[i + 1], -1, buf, sizeof(buf), NULL, NULL);
            out = buf;
            break;
        }
    }
    ::LocalFree(argv);
    return out;
}

// -autoexit N: after auto-load completes, count N more Frame() iterations
// then break out of the main loop. Used for autonomous debug cycles where we
// want the editor to render-and-exit on its own so the log is finalised.
static int parse_autoexit_arg()
{
    int     argc = 0;
    LPWSTR  cmdLine = GetCommandLineW();
    LPWSTR* argv = ::CommandLineToArgvW(cmdLine, &argc);
    int     out = 0;
    if (!argv)
        return out;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (_wcsicmp(argv[i], L"-autoexit") == 0)
        {
            out = _wtoi(argv[i + 1]);
            break;
        }
    }
    ::LocalFree(argv);
    return out;
}

// Generic single-string CLI arg extractor used by the -import-* switches.
static xr_string parse_string_arg(LPCWSTR flag)
{
    int     argc = 0;
    LPWSTR  cmdLine = GetCommandLineW();
    LPWSTR* argv = ::CommandLineToArgvW(cmdLine, &argc);
    xr_string out;
    if (!argv)
        return out;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (_wcsicmp(argv[i], flag) == 0)
        {
            char buf[MAX_PATH * 2] = {0};
            ::WideCharToMultiByte(CP_ACP, 0, argv[i + 1], -1, buf, sizeof(buf), NULL, NULL);
            out = buf;
            break;
        }
    }
    ::LocalFree(argv);
    return out;
}

XREPROPS_API extern bool bIsActorEditor;
ECORE_API extern bool    bIsLevelEditor;
ECORE_API extern bool    bIsParticleEditor;
ECORE_API extern bool    bIsShaderEditor;

int WINAPI               wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
{
    bIsActorEditor    = false;
    bIsLevelEditor    = true;
    bIsParticleEditor = false;
    bIsShaderEditor   = false;

    if (strstr(GetCommandLine(), "-nosplash") == nullptr)
    {
        constexpr bool topmost = false;
        splash::show(topmost);
    }
    splash::update_progress(1);

    if (!IsDebuggerPresent())
        Debug._initialize(false);

    splash::update_progress(5);
    const char* FSName = "fs.ltx";
    {
        if (xrGameManager::GetGame() == EGame::SHOC)
        {
            FSName = "fs_soc.ltx";
            Core._initialize("Level_Editor_ShoC", ELogCallback, 1, FSName, true);
        }
        else if (xrGameManager::GetGame() == EGame::CS)
        {
            FSName = "fs_cs.ltx";
            Core._initialize("Level_Editor_CS", ELogCallback, 1, FSName, true);
        }
        else
            Core._initialize("Level_Editor_CoP", ELogCallback, 1, FSName, true);
    }

    splash::update_progress(24);
    LTools = xr_new<CLevelTool>();
    Tools  = LTools;

    splash::update_progress(5);
    LUI = xr_new<CLevelMain>();
    UI  = LUI;
    UI->RegisterCommands();

    splash::update_progress(15);
    Scene                = xr_new<EScene>();
    EditorScene          = Scene;
    UIMainForm* MainForm = xr_new<UIMainForm>();
    pApp                 = xr_new<XRayEditor>();
    g_xrGameManager      = xr_new<xrGameManagerLE>();
    g_SEFactoryManager   = xr_new<xrSEFactoryManager>();

    splash::update_progress(24);
    g_pGamePersistent = (IGame_Persistent*)g_xrGameManager->Create(CLSID_GAME_PERSISTANT);
    EDevice->seqAppStart.Process(rp_AppStart);
    Console->Execute("default_controls");
    Console->Hide();

    ::MainForm = MainForm;
    UI->Push(MainForm, false);
    splash::update_progress(25);

    splash::update_progress(1);

    // Hide the splash now that loading is finished. Doing this here (rather
    // than at the end of UIMainForm::Draw) avoids a deadlock when the editor
    // launches without input focus: TUI::Idle skips RealRedrawScene while
    // m_bAppActive is false, so Draw never runs and the splash hangs at 100%
    // until the user clicks the window. Hiding inline guarantees forward
    // progress regardless of activation state.
    splash::hide();

    const xr_string auto_load_level   = parse_level_arg();
    bool            auto_load_pending = !auto_load_level.empty();
    const int       auto_exit_frames  = parse_autoexit_arg();
    // -import-spawn / -import-game: fire the same commands the File menu
    // wires up, but scripted so we can smoke-test the import path end-to-end
    // without any UI interaction. Runs one frame after the auto-load so the
    // scene is fully populated before the dedup pass compares against it.
    const xr_string auto_import_spawn = parse_string_arg(L"-import-spawn");
    const xr_string auto_import_game  = parse_string_arg(L"-import-game");
    auto is_all_spawn_basename = [](LPCSTR fn) -> bool {
        // Match any basename that starts with "all" and ends in ".spawn"
        // (all.spawn, all(1).spawn, all_backup.spawn, ...) — mirrors the
        // File menu's picker so scripted CLI runs route the same way.
        if (!fn || !*fn)
            return false;
        LPCSTR b1 = strrchr(fn, '\\');
        LPCSTR b2 = strrchr(fn, '/');
        LPCSTR base = (b2 > b1) ? b2 : b1;
        base = base ? base + 1 : fn;
        LPCSTR ext = strrchr(base, '.');
        if (!ext || 0 != _stricmp(ext, ".spawn"))
            return false;
        return 0 == _strnicmp(base, "all", 3);
    };
    bool            import_spawn_pending = !auto_import_spawn.empty();
    bool            import_game_pending  = !auto_import_game.empty();
    int             frames_since_load = 0;
    while (MainForm->Frame())
    {
        if (auto_load_pending)
        {
            auto_load_pending = false;
            Msg("- LevelEditor: auto-loading level via -level: '%s'", auto_load_level.c_str());
            FlushLog();
            ExecCommand(COMMAND_LOAD, auto_load_level);
            frames_since_load = 0;
        }
        else if (auto_load_level.size())
        {
            ++frames_since_load;
            // Give the scene one frame to settle after load before firing imports.
            if (frames_since_load == 1)
            {
                // Call the Scene method directly rather than via ExecCommand
                // so the command handler's modal summary dialog doesn't block
                // the main loop before -autoexit can fire.
                if (import_spawn_pending)
                {
                    import_spawn_pending = false;
                    Msg("- LevelEditor: auto-import via -import-spawn: '%s'", auto_import_spawn.c_str());
                    FlushLog();
                    EScene::ImportStats stats;
                    if (is_all_spawn_basename(auto_import_spawn.c_str()))
                    {
                        LPCSTR level_name = Scene->m_LevelOp.m_LevelPrefix.c_str();
                        Scene->ImportAllSpawn(auto_import_spawn.c_str(), level_name, stats);
                    }
                    else
                    {
                        Scene->ImportLevelSpawn(auto_import_spawn.c_str(), stats);
                    }
                }
                if (import_game_pending)
                {
                    import_game_pending = false;
                    Msg("- LevelEditor: auto-import via -import-game: '%s'", auto_import_game.c_str());
                    FlushLog();
                    EScene::ImportStats stats;
                    if (is_all_spawn_basename(auto_import_game.c_str()))
                    {
                        LPCSTR level_name = Scene->m_LevelOp.m_LevelPrefix.c_str();
                        Scene->ImportAllSpawnPatrols(auto_import_game.c_str(), level_name, stats);
                    }
                    else
                    {
                        Scene->ImportLevelGame(auto_import_game.c_str(), stats);
                    }
                }
            }
            if (auto_exit_frames > 0 && frames_since_load >= auto_exit_frames)
            {
                Msg("- LevelEditor: auto-exit after %d post-load frames", frames_since_load);
                FlushLog();
                break;
            }
        }
    }

    xr_delete(MainForm);
    xr_delete(pApp);
    xr_delete(g_xrGameManager);
    xr_delete(g_SEFactoryManager);

    Core._destroy();
    splash::hide();
    return 0;
}
