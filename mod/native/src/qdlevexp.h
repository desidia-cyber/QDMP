// qdlevexp.h -- [view-fix2] WHAT IS ON SCREEN: every streamed level, loaded and visible.
//
// THE DEFECT THIS EXISTS FOR
// --------------------------
// Both machines in the user's dialogue shots have large, perfectly flat BLACK regions in the
// ground/water area (never a bright "sky through a hole": the pixels are exactly 0,0,0, so
// nothing was drawn there at all and the scene's own background is black).  The regions are
// bounded by straight lines in places, which is the shape a streamed SUBLEVEL leaves when it
// is not visible: a hidden level's actors are not in the world (`analysis/21`), and the
// terrain/water in it is simply gone.
//
// The mod owns a level union (`qdstream.h` + `StreamPump`/`HK_SetShouldBeVisible`) that can veto
// the game's own hide of a level and can force-show a level for the partner.  That is the one
// mechanism in the module that can leave a level loaded-but-not-visible (or visible without
// actors), so the question "is the union the cause" is answered by reading the same table the
// union acts on, at the moment the hole is on screen -- not by reasoning about it.
//
// This dump is read-only: no engine write, no union decision.
#pragma once
#include <stdint.h>

namespace levexp {

inline const char* LevelStateText(bool shouldLoad, bool shouldVisible, bool reallyVisible)
{
    if (!shouldLoad) return "not-wanted";
    if (!shouldVisible) return "loaded-hidden";
    return reallyVisible ? "visible" : "shown-not-drawn";
}

// One line per level, plus a summary.  Every field is the engine's or the union's own value;
// nothing here is inferred.
inline void Dump(const char* why)
{
    QDLOG(LOG_INFO, "rig: levels dump -- ", (why && why[0] ? why : "-"));
    char world[128] = "";
    LocalMapName(world, sizeof(world));
    const uint32_t now = Net::Now();
    QDLOG(LOG_INFO, "  world=", (world[0] ? world : "-"), " levels=", dec(g_levelN),
          " union=", dec((int)g_stream.Wanted(now).n), " forced=", dec(g_force.Count()),
          " vetoes=", dec((int)g_vetoCount),
          " pin=", (g_rigLevelPin[0] ? g_rigLevelPin : "-"),
          " viewCam=", dec(ViewCamEngaged() ? 1 : 0),
          " partnerNear=", dec(PartnerIsNear(now) ? 1 : 0),
          " unionOn=", dec(Cfg().stream_union ? 1 : 0));
    int visible = 0, loadedHidden = 0, notWanted = 0;
    for (int i = 0; i < g_levelN; ++i) {
        LevelRec& r = g_levels[i];
        if (!r.name[0]) continue;
        void* ls = r.ls;
        if (!ls || !ObjectIsLive(ls)) { QDLOG(LOG_WARN, "  level ", r.name, " (stale handle)"); continue; }
        const bool load = LevelLoaded(ls);
        const bool shouldVis = LevelVisible(ls);
        const bool drawn = LevelActuallyVisible(ls);
        const bool peer = g_map.PeerStateApplies(now) && g_stream.PeerWants(r.name, now);
        const bool forced = g_force.IsForced(r.name);
        const bool pinned = g_rigLevelPin[0] && SameNameText(r.name, g_rigLevelPin);
        const char* st = LevelStateText(load, shouldVis, drawn);
        if (!load) ++notWanted;
        else if (!shouldVis) ++loadedHidden;
        else ++visible;
        QDLOG(LOG_INFO, "  level ", r.name, " state=", st, " shouldLoad=", dec(load ? 1 : 0),
              " shouldVisible=", dec(shouldVis ? 1 : 0), " engineVisible=", dec(drawn ? 1 : 0),
              " gameWants=", dec((int)r.gameWants), " peerWants=", dec(peer ? 1 : 0),
              " forced=", dec(forced ? 1 : 0), " pinned=", dec(pinned ? 1 : 0),
              (r.skipNextShow ? " skipNextShow=1" : ""));
    }
    QDLOG(LOG_INFO, "  levels: ", dec(visible), " visible, ", dec(loadedHidden),
          " loaded-but-hidden, ", dec(notWanted), " not-loaded");
}

} // namespace levexp
