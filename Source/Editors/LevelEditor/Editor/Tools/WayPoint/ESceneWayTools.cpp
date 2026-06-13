#include "stdafx.h"

CCustomObject* ESceneWayTool::CreateObject(LPVOID data, LPCSTR name)
{
    CCustomObject* O = xr_new<CWayObject>(data, name);
    O->FParentTools  = this;
    return O;
}

namespace
{
// Number of dash segments along a single per-index walk<->look line. Even
// segments are drawn, odd ones skipped — the on/off ratio is the gap pattern.
constexpr int    PAIR_DASH_SEGMENTS = 40;
constexpr u32    PAIR_COLOR_WALK    = 0xff4080ff;  // matches WAY_COLOR_WALK
constexpr u32    PAIR_COLOR_LOOK    = 0xff00c0c0;  // matches WAY_COLOR_LOOK
constexpr float  PAIR_DASH_LIFT_Y   = 1.275f;      // match WAYPOINT_SIZE * 0.85f

// Strip a known case-insensitive suffix. Returns the base or an empty string
// if the name doesn't end with the suffix.
xr_string strip_suffix(LPCSTR name, LPCSTR suffix)
{
    if (!name || !suffix) return xr_string();
    size_t nl = xr_strlen(name);
    size_t sl = xr_strlen(suffix);
    if (sl == 0 || nl <= sl) return xr_string();
    if (_stricmp(name + nl - sl, suffix) != 0) return xr_string();
    return xr_string(name).substr(0, nl - sl);
}

// Draw a dashed segment from a to b, alternating two colours so the line
// reads as both members of the walk/look pair. Endpoints are lifted to the
// same y-offset waypoint markers use, so the line sits at the cross centre.
void draw_dashed_pair(const Fvector& a, const Fvector& b)
{
    Fvector p0, p1;
    p0.set(a.x, a.y + PAIR_DASH_LIFT_Y, a.z);
    p1.set(b.x, b.y + PAIR_DASH_LIFT_Y, b.z);

    Fvector step;
    step.sub(p1, p0);
    step.div((float)PAIR_DASH_SEGMENTS);

    Fvector cur = p0;
    for (int i = 0; i < PAIR_DASH_SEGMENTS; ++i)
    {
        Fvector next;
        next.add(cur, step);
        if ((i & 1) == 0)
            DU_impl.DrawLine(cur, next, (i & 2) ? PAIR_COLOR_LOOK : PAIR_COLOR_WALK);
        cur = next;
    }
}
}  // anonymous namespace

void ESceneWayTool::OnRender(int priority, bool strictB2F)
{
    inherited::OnRender(priority, strictB2F);
    if (priority != 1 || strictB2F) return;
    if (m_Objects.empty()) return;

    // Group ways by name-minus-suffix. Each pair holds {walk_way, look_way}.
    typedef std::pair<CWayObject*, CWayObject*> WalkLookPair;
    xr_map<xr_string, WalkLookPair> pairs;
    for (ObjectIt it = m_Objects.begin(); it != m_Objects.end(); ++it)
    {
        CWayObject* w = (CWayObject*)*it;
        if (!w->Visible()) continue;
        LPCSTR nm = w->GetName();
        if (!nm) continue;
        xr_string base = strip_suffix(nm, "_walk");
        if (!base.empty())
        {
            pairs[base].first = w;
            continue;
        }
        base = strip_suffix(nm, "_look");
        if (!base.empty())
            pairs[base].second = w;
    }

    if (pairs.empty()) return;

    RCache.set_xform_world(Fidentity);
    EDevice->SetShader(EDevice->m_WireShader);

    for (auto& kv: pairs)
    {
        CWayObject* walk = kv.second.first;
        CWayObject* look = kv.second.second;
        if (!walk || !look) continue;

        const u32 wc = (u32)walk->m_WayPoints.size();
        const u32 lc = (u32)look->m_WayPoints.size();
        if (wc != lc)
        {
            auto cached = m_PairLogCache.find(kv.first);
            const std::pair<u32, u32> cur(wc, lc);
            if (cached == m_PairLogCache.end() || cached->second != cur)
            {
                Msg("! Waypoint pair '%s': _walk has %u point(s), _look has %u point(s) - drawing %u matched pair(s)",
                    kv.first.c_str(), wc, lc, std::min(wc, lc));
                m_PairLogCache[kv.first] = cur;
            }
        }
        else
        {
            // Counts agree — drop any stale mismatch record so the next
            // mismatch transition logs cleanly.
            m_PairLogCache.erase(kv.first);
        }

        const u32 n = std::min(wc, lc);
        for (u32 i = 0; i < n; ++i)
            draw_dashed_pair(walk->m_WayPoints[i]->m_vPosition,
                             look->m_WayPoints[i]->m_vPosition);
    }
}
