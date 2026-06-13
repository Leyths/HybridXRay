#pragma once

// Background texture prefetcher.
//
// Problem: the editor defers texture loads until first-bind on the render
// thread. Swinging the camera around a freshly-opened level faults in
// hundreds of DDS textures synchronously in a handful of frames — exactly
// the "first-pan lag" symptom. See [TEXLOAD-DIAG] profiling for the data.
//
// Fix: one worker thread reads DDS files into RAM behind the main thread.
// The main thread drains the ready queue each frame and does the GPU
// upload (which is fast — D3D9 calls stay on the render thread). By the
// time the user pans somewhere new, the texture is already in memory.
//
// This is a pure optimisation. If a texture binds before its prefetched
// bytes arrive, the existing CTexture::apply_load() synchronous path runs
// — same behaviour as before. The prefetcher's late arrival is harmless;
// drain checks flags.bLoaded and discards the bytes.

#include "../../xrCore/xrSyncronize.h"

class ECORE_API CTexturePrefetcher
{
public:
    CTexturePrefetcher();
    ~CTexturePrefetcher();

    // Add a texture name to the pending queue. Main-thread only. Silently
    // ignores special-format names (theora/avi/sequence) and runtime
    // pseudo-textures ($null, $user$...), since those stay on the existing
    // synchronous lazy-load path.
    void Enqueue(LPCSTR name);

    // Like Enqueue but moves the entry to the front of the pending queue if
    // it was already enqueued in some earlier (lower-priority) position. Used
    // by UI_LevelMain's camera-distance walk to promote close-to-camera
    // textures past the bulk that PrewarmRP enqueued in reference-load order.
    // Main-thread only. Already-loaded textures are skipped (no work to do).
    void EnqueuePriority(LPCSTR name);

    // Pop ready entries and apply them to their CTexture, stopping when
    // either (a) the queue is empty, (b) `max_count` entries processed, or
    // (c) `max_ms` of wall time spent. Time-bounding matters more than
    // count-bounding: each D3D9 CreateTexture costs 1-5ms and a big burst
    // of drains in a single frame causes visible stalls. Main-thread only.
    void Drain(int max_count, int max_ms);

    // Discard pending and ready work without joining the worker. Call on
    // level change / device reset.
    void Clear();

    // Signal the worker to exit and wait. Call once on app shutdown.
    void Shutdown();

private:
    struct ReadyEntry
    {
        shared_str name;
        void*      bytes;
        u32        size;
    };

    static void WorkerEntry(void* arg);
    void        WorkerLoop();

    xr_deque<shared_str>      m_pending;
    xr_vector<ReadyEntry>     m_ready;
    // Names currently in pending OR ready (i.e. either waiting for the
    // worker, or waiting for the main-thread Drain to finish). Lets Enqueue
    // and EnqueuePriority dedup so the worker never reads the same DDS
    // twice. Cleared per-name once Drain finishes that name's upload.
    xr_set<shared_str>        m_enqueued;
    xrCriticalSection         m_lock;
    HANDLE                    m_sem;
    HANDLE                    m_worker;
    volatile bool             m_stop;
};

extern ECORE_API CTexturePrefetcher* g_TexPrefetch;
