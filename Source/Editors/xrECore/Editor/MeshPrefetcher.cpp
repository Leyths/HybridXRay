#include "stdafx.h"
#include "MeshPrefetcher.h"

ECORE_API CMeshPrefetcher* g_MeshPrefetch = nullptr;

CMeshPrefetcher::CMeshPrefetcher(): m_sem(nullptr), m_worker(nullptr), m_stop(false)
{
    m_sem    = CreateSemaphore(nullptr, 0, LONG_MAX, nullptr);
    m_worker = thread_spawn(&CMeshPrefetcher::WorkerEntry, "mesh_prefetch", 256 * 1024, this);
}

CMeshPrefetcher::~CMeshPrefetcher()
{
    Shutdown();
    if (m_sem)
        CloseHandle(m_sem);
}

void CMeshPrefetcher::Enqueue(CEditableMesh* mesh)
{
    if (!mesh)
        return;
    if (mesh->HasRenderBuffers())
        return;

    Job* job  = xr_new<Job>();
    job->mesh = mesh;

    {
        xrCriticalSection::raii lk(&m_lock);
        m_pending.push_back(job);
    }
    ReleaseSemaphore(m_sem, 1, nullptr);
}

void CMeshPrefetcher::WorkerLoop()
{
    for (;;)
    {
        WaitForSingleObject(m_sem, INFINITE);
        if (m_stop)
            return;

        Job* job = nullptr;
        {
            xrCriticalSection::raii lk(&m_lock);
            if (m_pending.empty())
                continue;
            job = m_pending.front();
            m_pending.pop_front();
        }

        // CPU prep — touches mesh CPU data only, no D3D9. If the mesh has
        // already been uploaded (raced with a sync path) Prepare returns
        // false and we drop the job.
        const bool ok = job->mesh->PrepareCpuRenderBuffers(job->prepared);
        if (!ok)
        {
            xr_delete(job);
            continue;
        }

        {
            xrCriticalSection::raii lk(&m_lock);
            m_ready.push_back(job);
        }
    }
}

void CMeshPrefetcher::WorkerEntry(void* arg)
{
    static_cast<CMeshPrefetcher*>(arg)->WorkerLoop();
}

void CMeshPrefetcher::Drain(int max_count, int max_ms)
{
    CTimer budget;
    budget.Start();
    for (int i = 0; i < max_count; ++i)
    {
        if (i > 0 && (int)budget.GetElapsed_ms() >= max_ms)
            return;

        Job* job = nullptr;
        {
            xrCriticalSection::raii lk(&m_lock);
            if (m_ready.empty())
                return;
            // FIFO drain: the worker prepares in enqueue order, so the FRONT
            // of m_ready is the closest-to-camera mesh. Popping back would
            // invert the priority — exactly what was making near geometry
            // appear last.
            job = m_ready.front();
            m_ready.pop_front();
        }

        // Mesh may have been uploaded between Prepare and now (rare: another
        // path forced a sync GenerateRenderBuffers). UploadPreparedBuffers
        // checks HasRenderBuffers and no-ops if so.
        job->mesh->UploadPreparedBuffers(job->prepared);
        xr_delete(job);
    }
}

void CMeshPrefetcher::Clear()
{
    xrCriticalSection::raii lk(&m_lock);
    for (Job* j: m_pending)
        xr_delete(j);
    m_pending.clear();
    for (Job* j: m_ready)
        xr_delete(j);
    m_ready.clear();
}

void CMeshPrefetcher::Shutdown()
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
