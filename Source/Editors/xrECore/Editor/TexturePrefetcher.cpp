#include "stdafx.h"
#include "TexturePrefetcher.h"

#include "../../xrRender/Private/ResourceManager.h"
#include "../../xrRender/Private/SH_Texture.h"
#include "render.h"

// Defined in xrRender/Private/Texture.cpp. External linkage with no header
// because there's exactly one caller outside that TU (this file).
extern bool resolve_dds_path(LPCSTR fRName, string_path& out_path);

ECORE_API CTexturePrefetcher* g_TexPrefetch = nullptr;

CTexturePrefetcher::CTexturePrefetcher(): m_sem(nullptr), m_worker(nullptr), m_stop(false)
{
    m_sem    = CreateSemaphore(nullptr, 0, LONG_MAX, nullptr);
    m_worker = thread_spawn(&CTexturePrefetcher::WorkerEntry, "tex_prefetch", 64 * 1024, this);
}

CTexturePrefetcher::~CTexturePrefetcher()
{
    Shutdown();
    if (m_sem)
        CloseHandle(m_sem);
}

void CTexturePrefetcher::Enqueue(LPCSTR name)
{
    if (!name || !name[0])
        return;
    // Runtime pseudo-textures don't live on disk.
    if (name[0] == '$')
        return;
    // Special-format textures (Theora/AVI/animated sequence) have main-thread
    // state machines and stay on the existing synchronous path.
    string_path probe;
    if (FS.exist(probe, "$game_textures$", name, ".ogm"))
        return;
    if (FS.exist(probe, "$game_textures$", name, ".avi"))
        return;
    if (FS.exist(probe, "$game_textures$", name, ".seq"))
        return;

    {
        xrCriticalSection::raii lk(&m_lock);
        m_pending.push_back(shared_str(name));
    }
    ReleaseSemaphore(m_sem, 1, nullptr);
}

void CTexturePrefetcher::WorkerLoop()
{
    for (;;)
    {
        WaitForSingleObject(m_sem, INFINITE);
        if (m_stop)
            return;

        shared_str name;
        {
            xrCriticalSection::raii lk(&m_lock);
            if (m_pending.empty())
                continue;
            name = m_pending.front();
            m_pending.pop_front();
        }

        string_path path;
        if (!resolve_dds_path(name.c_str(), path))
            continue;

        IReader* r = FS.r_open(path);
        if (!r)
            continue;

        const u32 size  = r->length();
        void*     bytes = xr_malloc(size);
        memcpy(bytes, r->pointer(), size);
        FS.r_close(r);

        {
            xrCriticalSection::raii lk(&m_lock);
            ReadyEntry              e;
            e.name  = name;
            e.bytes = bytes;
            e.size  = size;
            m_ready.push_back(e);
        }
    }
}

void CTexturePrefetcher::WorkerEntry(void* arg)
{
    static_cast<CTexturePrefetcher*>(arg)->WorkerLoop();
}

void CTexturePrefetcher::Drain(int max_count, int max_ms)
{
    CTimer budget;
    budget.Start();
    for (int i = 0; i < max_count; ++i)
    {
        if (i > 0 && (int)budget.GetElapsed_ms() >= max_ms)
            return;
        ReadyEntry e;
        {
            xrCriticalSection::raii lk(&m_lock);
            if (m_ready.empty())
                return;
            e = m_ready.back();
            m_ready.pop_back();
        }

        // The CTexture may have been destroyed between enqueue and drain
        // (level closed, undo, etc.). _FindTexture returns NULL safely.
        CTexture* tex = EDevice->Resources->_FindTexture(e.name.c_str());
        if (tex && !tex->flags.bLoaded)
        {
            u32              mem  = 0;
            ID3DBaseTexture* surf = RImplementation.texture_load_from_blob(e.name.c_str(), e.bytes, e.size, mem);
            if (surf)
            {
                tex->surface_set(surf);
                tex->flags.bLoaded     = true;
                tex->flags.MemoryUsage = mem;
                tex->PostLoad();
            }
            // surf == NULL: bytes were corrupt or D3D rejected them. The
            // texture stays in its "not loaded" state and the next bind
            // takes the synchronous file-based path as a fallback.
        }
        xr_free(e.bytes);
    }
}

void CTexturePrefetcher::Clear()
{
    xrCriticalSection::raii lk(&m_lock);
    m_pending.clear();
    for (size_t i = 0; i < m_ready.size(); ++i)
        xr_free(m_ready[i].bytes);
    m_ready.clear();
}

void CTexturePrefetcher::Shutdown()
{
    if (!m_worker)
        return;
    m_stop = true;
    ReleaseSemaphore(m_sem, 1, nullptr);
    WaitForSingleObject(m_worker, INFINITE);
    CloseHandle(m_worker);
    m_worker = nullptr;
    Clear();
}

