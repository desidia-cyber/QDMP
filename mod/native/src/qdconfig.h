// qdconfig.h -- qdcoop.ini.
//
// WHY ONE SWITCH PER SUBSYSTEM
// ----------------------------
// The corpus records this as a testing-discipline lesson, not a nicety: "One switch
// per subsystem, so one broken part does not block testing the rest."  Every
// subsystem below can be turned off independently, and the default for anything that
// writes to the game's state is OFF until it has been proven on a live session.
// That ordering matters: a default-on write path turns a first-run crash into an
// unattributable one.
//
// The file is read once at load and never re-read. A config change requires a
// restart, which is the honest contract for something that installs hooks at
// DllMain time.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

namespace qd {

struct Config {
    // master
    int  enabled;          // 0 = the DLL loads, logs the substrate, does nothing else
    int  log_level;

    // transports
    char relay_host[64];
    uint16_t relay_port;

    // subsystem switches.  0 = observe and log only.
    int  flag_sync;        // NamedSaveData apply/broadcast
    int  inventory_sync;   // host -> peer InventoryContents mirror + grants
    int  actor_state_sync; // SavableActorStates
    int  enemy_sync;       // enemy rows + StoreEnemySaveState
    int  puppet_sync;      // remote body spawn/drive
    int  player_body;      // ... as the game's own player class (B7 / G4 / G5)
    int  dialogue_sync;    // lock + first-answer-decides
    int  stream_union;     // level-streaming union guard
    int  suppress_pause;   // stop a menu pause from freezing the session
    int  object_scan;      // walk GObjects to find UFunctions (OFF: it exits the game)

    // behaviour
    int  host_authoritative;   // 1 = only the host writes world facts
    int  allow_plaintext_internet; // refuses a public relay unless set
    int  spawn_attempts_max;
    int  enemy_rows_per_tick;  // capped at 20 by WIRE.md; lower on a weak link
    int  player_hz;            // 30 or 60
};

inline Config& Cfg() { static Config c; return c; }

inline void ConfigDefaults()
{
    Config& c = Cfg();
    c.enabled = 1;
    c.log_level = 2;                 // LOG_INFO

    const char* h = "127.0.0.1";
    int i = 0;
    for (; h[i] && i < (int)sizeof(c.relay_host) - 1; ++i) c.relay_host[i] = h[i];
    c.relay_host[i] = 0;
    c.relay_port = 47800;

    c.flag_sync        = 1;
    // OFF, like every other write path.  This one writes an item container through a
    // reflective Blueprint call; Enable it only after a live session has shown the
    // `inventory grant applied ... read back` line (qdwrite.h's WrGrantItem oracle).
    c.inventory_sync   = 0;
    c.actor_state_sync = 0;
    c.enemy_sync       = 0;
    c.puppet_sync      = 0;
    // OFF, like every other write path.  This one asks the engine to CONSTRUCT a full
    // gameplay Blueprint (`FS_PlayerChar_C`), whose ReceiveBeginPlay runs; enable it only
    // on a run that is already looking for B7's answer (`API-GAPS` section 5).
    c.player_body      = 0;
    c.dialogue_sync    = 0;
    c.stream_union     = 0;
    c.suppress_pause   = 0;
    c.object_scan      = 0;

    c.host_authoritative = 1;
    c.allow_plaintext_internet = 0;
    c.spawn_attempts_max = 5;
    c.enemy_rows_per_tick = 20;
    c.player_hz = 30;
}

// ---- a very small INI reader ----------------------------------------------
// Hand-rolled because the game thread must not pull in <fstream>/<string>, and
// because the whole format is "section, key=value, # comment".
namespace detail {

inline char* Trim(char* s)
{
    while (*s == ' ' || *s == '\t') ++s;
    int n = 0;
    while (s[n]) ++n;
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = 0;
    }
    return s;
}

inline bool StrEq(const char* a, const char* b)
{
    for (int i = 0; a[i] || b[i]; ++i) if (a[i] != b[i]) return false;
    return true;
}

inline int ParseInt(const char* s, int fallback)
{
    int v = 0;
    bool neg = false;
    if (*s == '-') { neg = true; ++s; }
    if (*s == 0) return fallback;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return fallback;
        v = v * 10 + (*s - '0');
        if (v > 100000000) return fallback;
    }
    return neg ? -v : v;
}

inline void CopyStr(char* dst, int cap, const char* src)
{
    int i = 0;
    for (; src[i] && i < cap - 1; ++i) dst[i] = src[i];
    dst[i] = 0;
}

} // namespace detail

// The ini lives beside the game executable, not beside our DLL's module directory:
// a proxy DLL's own path is the game's Binaries directory either way, but being
// explicit stops a future relocation from silently reading a stale file.
inline bool ConfigPath(char* out, int cap)
{
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    // Strip the file name.
    int cut = (int)n;
    while (cut > 0 && exe[cut - 1] != '\\' && exe[cut - 1] != '/') --cut;
    int w = 0;
    for (int i = 0; i < cut && w < cap - 1; ++i) out[w++] = exe[i];
    const char* f = "qdcoop.ini";
    for (int i = 0; f[i] && w < cap - 1; ++i) out[w++] = f[i];
    out[w] = 0;
    return true;
}

// Load the ini.  A missing file is normal and means "defaults": the DLL must be
// installable without one.  A malformed line is logged and skipped, never fatal.
inline int ConfigLoad()
{
    ConfigDefaults();

    char path[MAX_PATH];
    if (!ConfigPath(path, MAX_PATH)) return 0;

    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return 0;

    char buf[8192];
    DWORD got = 0;
    (void)ReadFile(h, buf, sizeof(buf) - 1, &got, nullptr);
    CloseHandle(h);
    buf[got] = 0;

    Config& c = Cfg();
    char section[32] = "";
    char* p = buf;
    int line = 0;
    while (*p) {
        // Split one line.
        char* start = p;
        while (*p && *p != '\n') ++p;
        if (*p == '\n') { *p = 0; ++p; }
        ++line;

        char* s = detail::Trim(start);
        if (*s == 0 || *s == '#' || *s == ';') continue;
        if (*s == '[') {
            char* e = s;
            while (*e && *e != ']') ++e;
            int n = (int)(e - s - 1);
            if (n > 0 && n < (int)sizeof(section)) {
                for (int i = 0; i < n; ++i) section[i] = s[1 + i];
                section[n] = 0;
            }
            continue;
        }
        char* eq = s;
        while (*eq && *eq != '=') ++eq;
        if (*eq != '=') continue;
        *eq = 0;
        char* k = detail::Trim(s);
        char* v = detail::Trim(eq + 1);

        if      (detail::StrEq(k, "Enabled"))       c.enabled = detail::ParseInt(v, c.enabled);
        else if (detail::StrEq(k, "LogLevel"))      c.log_level = detail::ParseInt(v, c.log_level);
        else if (detail::StrEq(k, "RelayHost"))     detail::CopyStr(c.relay_host, sizeof(c.relay_host), v);
        else if (detail::StrEq(k, "RelayPort"))     c.relay_port = (uint16_t)detail::ParseInt(v, c.relay_port);
        else if (detail::StrEq(k, "FlagSync"))      c.flag_sync = detail::ParseInt(v, c.flag_sync);
        else if (detail::StrEq(k, "InventorySync")) c.inventory_sync = detail::ParseInt(v, c.inventory_sync);
        else if (detail::StrEq(k, "ActorStateSync")) c.actor_state_sync = detail::ParseInt(v, c.actor_state_sync);
        else if (detail::StrEq(k, "EnemySync"))     c.enemy_sync = detail::ParseInt(v, c.enemy_sync);
        else if (detail::StrEq(k, "PuppetSync"))    c.puppet_sync = detail::ParseInt(v, c.puppet_sync);
        else if (detail::StrEq(k, "PlayerBody"))    c.player_body = detail::ParseInt(v, c.player_body);
        else if (detail::StrEq(k, "DialogueSync"))  c.dialogue_sync = detail::ParseInt(v, c.dialogue_sync);
        else if (detail::StrEq(k, "StreamUnion"))   c.stream_union = detail::ParseInt(v, c.stream_union);
        else if (detail::StrEq(k, "SuppressMenuPause")) c.suppress_pause = detail::ParseInt(v, c.suppress_pause);
        else if (detail::StrEq(k, "ObjectScan"))   c.object_scan = detail::ParseInt(v, c.object_scan);
        else if (detail::StrEq(k, "HostAuthoritative")) c.host_authoritative = detail::ParseInt(v, c.host_authoritative);
        else if (detail::StrEq(k, "AllowPlaintextInternet")) c.allow_plaintext_internet = detail::ParseInt(v, c.allow_plaintext_internet);
        else if (detail::StrEq(k, "SpawnAttemptsMax")) c.spawn_attempts_max = detail::ParseInt(v, c.spawn_attempts_max);
        else if (detail::StrEq(k, "EnemyRowsPerTick")) c.enemy_rows_per_tick = detail::ParseInt(v, c.enemy_rows_per_tick);
        else if (detail::StrEq(k, "PlayerHz"))      c.player_hz = detail::ParseInt(v, c.player_hz);
        // Unknown keys are ignored on purpose: a config from a newer build must
        // still load on an older DLL rather than refuse to start.
    }

    if (c.enemy_rows_per_tick > 20) c.enemy_rows_per_tick = 20;
    if (c.player_hz != 30 && c.player_hz != 60) c.player_hz = 30;
    return line;
}

} // namespace qd
