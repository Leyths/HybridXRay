#pragma once

// Background mesh prefetcher.
//
// Problem: CEditableMesh::Render used to lazy-create GPU vertex buffers
// the first time each mesh was drawn — that was the dominant source of
// camera-pan hitches (renders of 1-2 seconds, see [FRAME-DIAG] profiling).
// The synchronous prewarm we added moved the cost to scene-open but
// blocked the main thread for seconds.
//
// Fix: one worker thread does the CPU half of building a mesh's vertex
// blobs (GenerateVNormals + FillRenderBuffer into heap memory). The main
// thread drains the ready queue each frame and runs only the cheap D3D9
// half (CreateVertexBuffer + Lock + memcpy + Unlock).
//
// Render path is now skip-on-missing: meshes whose buffers haven't been
// uploaded yet just don't draw. They pop in as the drain catches up.
//
// Mirrors the texture prefetcher pattern.

#include "../../xrCore/xrSyncronize.h"
#include "EditMesh.h"

class ECORE_API CMeshPrefetcher
{
public:
    CMeshPrefetcher();
    ~CMeshPrefetcher();

    // Main-thread-only. Pushes a mesh onto the pending queue. No-ops on null
    // or already-uploaded meshes.
    void Enqueue(CEditableMesh* mesh);

    // Pop ready jobs and upload them to the GPU. Stops when (a) the ready
    // queue is empty, (b) max_count entries done, or (c) max_ms wall time
    // spent. Main-thread only — the D3D9 calls live here.
    void Drain(int max_count, int max_ms);

    // Discard pending and ready work without joining the worker.
    void Clear();

    // Stop the worker and join. Call on app shutdown.
    void Shutdown();

private:
    struct Job
    {
        CEditableMesh*                 mesh;
        CEditableMesh::PreparedBuffers prepared;
    };

    static void               WorkerEntry(void* arg);
    void                      WorkerLoop();

    xr_deque<Job*>            m_pending;
    xr_deque<Job*>            m_ready;
    xrCriticalSection         m_lock;
    HANDLE                    m_sem;
    HANDLE                    m_worker;
    volatile bool             m_stop;
};

extern ECORE_API CMeshPrefetcher* g_MeshPrefetch;
