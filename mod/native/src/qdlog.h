// qdlog.h -- the co-op mod's log sink.
//
// WHY THIS IS NOT printf
// ----------------------
// The DollsMP proxy states the constraint in its own header: "Deliberately no CRT
// printf: this runs early, on a thread we own, and a formatting bug should not be
// able to take the game down."  That reasoning applies here unchanged.  The log is
// called from a DllMain-adjacent context and from a hook on the game thread, so it
// must not:
//   - allocate (a heap failure during load turns a diagnostic into a crash),
//   - pull in CRT varargs (which would also force the CRT startup path early),
//   - hold a lock across a call that can block on disk indefinitely.
//
// So: a fixed 4 KB stack buffer per call, a bounded formatter, one SRWLOCK, one
// append-only handle, and a hard "a log write must never be able to fail the
// caller" rule -- every return value is discarded on purpose.
//
// The log file is opened on the first write and never reopened, with
// FILE_SHARE_READ so a second instance or a log tail can read it mid-session.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

namespace qd {

enum LogLevel {
    LOG_ERROR = 0,
    LOG_WARN  = 1,
    LOG_INFO  = 2,
    LOG_DEBUG = 3,
    LOG_TRACE = 4,
};

namespace detail {

// One writer handle for the process.  Opened lazily by LogInit.
inline HANDLE& LogHandle()      { static HANDLE h = INVALID_HANDLE_VALUE; return h; }
inline SRWLOCK& LogLock()       { static SRWLOCK l = SRWLOCK_INIT; return l; }
inline int&     LogThreshold()  { static int t = LOG_INFO; return t; }
// Lines written since the last FlushFileBuffers.  R83's `no log line` was not a lost
// write -- the dump proved the thread faulted -- but a crash must not also cost the
// tail of the record, so WARN/ERROR and every 64th line are flushed through the OS.
inline unsigned& LogUnflushed() { static unsigned n = 0; return n; }
inline char     g_logPath[MAX_PATH];

// --- bounded formatting -----------------------------------------------------
// Each helper appends to `d` and returns the new cursor.  The caller passes a
// buffer end so a runaway value can never overrun the stack buffer; every helper
// stops at `e`.
inline char* Cat(char* d, const char* e, const char* s)
{
    while (*s && d < e) *d++ = *s++;
    return d;
}

inline char* CatN(char* d, const char* e, const char* s, int n)
{
    for (int i = 0; i < n && s[i] && d < e; ++i) *d++ = s[i];
    return d;
}

inline char* CatHex(char* d, const char* e, uintptr_t v)
{
    static const char* H = "0123456789ABCDEF";
    char tmp[17];
    int n = 0;
    if (v == 0) { if (d < e) *d++ = '0'; return d; }
    while (v && n < 16) { tmp[n++] = H[v & 0xF]; v >>= 4; }
    while (n && d < e) *d++ = tmp[--n];
    return d;
}

inline char* CatHex32(char* d, const char* e, uint32_t v)
{
    static const char* H = "0123456789ABCDEF";
    for (int i = 7; i >= 0; --i) { if (d < e) *d++ = H[(v >> (i * 4)) & 0xF]; }
    return d;
}

inline char* CatDec(char* d, const char* e, int64_t v)
{
    char tmp[24];
    int n = 0;
    bool neg = v < 0;
    // Negate through unsigned so INT64_MIN does not trap.
    uint64_t u = neg ? (~(uint64_t)v + 1) : (uint64_t)v;
    if (u == 0) tmp[n++] = '0';
    while (u) { tmp[n++] = (char)('0' + (u % 10)); u /= 10; }
    if (neg && d < e) *d++ = '-';
    while (n && d < e) *d++ = tmp[--n];
    return d;
}

inline char* CatFloat(char* d, const char* e, float v, int decimals)
{
    if (v != v) return Cat(d, e, "nan");
    if (v > 3.0e38f) return Cat(d, e, "+inf");
    if (v < -3.0e38f) return Cat(d, e, "-inf");
    if (v < 0.0f) { if (d < e) *d++ = '-'; v = -v; }
    uint64_t ip = (uint64_t)v;
    d = CatDec(d, e, (int64_t)ip);
    if (decimals > 0) {
        if (d < e) *d++ = '.';
        float frac = v - (float)ip;
        for (int i = 0; i < decimals; ++i) {
            frac *= 10.0f;
            int digit = (int)frac;
            if (digit > 9) digit = 9;
            if (d < e) *d++ = (char)('0' + digit);
            frac -= (float)digit;
        }
    }
    return d;
}

inline const char* LevelName(int lv)
{
    switch (lv) {
    case LOG_ERROR: return "ERR ";
    case LOG_WARN:  return "WARN";
    case LOG_INFO:  return "INFO";
    case LOG_DEBUG: return "DBG ";
    default:        return "TRC ";
    }
}

// The one path that touches the disk.  Kept out of line so the common case
// (level filtered) is a compare and a return.
inline void Emit(int level, const char* msg, int len)
{
    HANDLE h = LogHandle();
    if (h == INVALID_HANDLE_VALUE) return;
    AcquireSRWLockExclusive(&LogLock());
    DWORD wrote = 0;
    (void)WriteFile(h, msg, (DWORD)len, &wrote, nullptr);
    if (level <= LOG_WARN || ++LogUnflushed() >= 64) {
        LogUnflushed() = 0;
        (void)FlushFileBuffers(h);
    }
    ReleaseSRWLockExclusive(&LogLock());
}

} // namespace detail

// Verdict: -1 fail, 0 ok.  A failure here means the log is silent, which the
// proxy reports once through OutputDebugString so a missing log is diagnosable.
// `tag` (optional) names the file qdcoop-<tag>.log, so two instances launched from one
// game directory (the two-player rig) do not interleave into one file.
inline int LogOpen(const char* directory, const char* tag = nullptr)
{
    if (detail::LogHandle() != INVALID_HANDLE_VALUE) return 0;

    char path[MAX_PATH];
    char* d = path;
    const char* e = path + sizeof(path) - 1;
    d = detail::Cat(d, e, directory);
    if (tag && tag[0]) {
        d = detail::Cat(d, e, "\\qdcoop-");
        d = detail::Cat(d, e, tag);
        d = detail::Cat(d, e, ".log");
    } else {
        d = detail::Cat(d, e, "\\qdcoop.log");
    }
    *d = 0;

    HANDLE h = CreateFileA(path, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        OutputDebugStringA("QDCOOP: cannot open log file\n");
        return -1;
    }
    detail::LogHandle() = h;
    for (int i = 0; i < MAX_PATH; ++i) detail::g_logPath[i] = path[i];

    // A process-wide session separator.  Two instances writing one file is a
    // bug in the launcher, not in the log, and this makes it visible.
    char buf[160];
    char* p = buf;
    const char* pe = buf + sizeof(buf) - 4;
    p = detail::Cat(p, pe, "\n===== qdcoop pid ");
    p = detail::CatDec(p, pe, (int64_t)GetCurrentProcessId());
    p = detail::Cat(p, pe, " =====\n");
    detail::Emit(LOG_INFO, buf, (int)(p - buf));
    return 0;
}

inline void LogClose()
{
    if (detail::LogHandle() != INVALID_HANDLE_VALUE) {
        CloseHandle(detail::LogHandle());
        detail::LogHandle() = INVALID_HANDLE_VALUE;
    }
}

inline void LogSetLevel(int level) { detail::LogThreshold() = level; }
inline int  LogLevelGet()          { return detail::LogThreshold(); }
inline const char* LogPath()       { return detail::g_logPath; }

// qd_logf(LOG_INFO, "rva=", hex(0x1234), " n=", dec(3));
// Chained fragments, not a format string: there is nothing to get wrong, and no
// varargs to mis-promote.
namespace detail {
inline char* Prefix(char* p, const char* pe, int level)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    p = CatDec(p, pe, st.wHour);
    if (p < pe) *p++ = ':';
    if (st.wMinute < 10 && p < pe) *p++ = '0';
    p = CatDec(p, pe, st.wMinute);
    if (p < pe) *p++ = ':';
    if (st.wSecond < 10 && p < pe) *p++ = '0';
    p = CatDec(p, pe, st.wSecond);
    if (p < pe) *p++ = '.';
    if (st.wMilliseconds < 100 && p < pe) *p++ = '0';
    if (st.wMilliseconds < 10 && p < pe) *p++ = '0';
    p = CatDec(p, pe, st.wMilliseconds);
    if (p < pe) *p++ = ' ';
    p = Cat(p, pe, LevelName(level));
    if (p < pe) *p++ = ' ';
    return p;
}
} // namespace detail

struct Hex   { uintptr_t v; };
struct Hex32 { uint32_t v; };
struct Dec   { int64_t v; };
struct Flt   { float v; int d; };
inline Hex   hex(uintptr_t v)    { Hex h;   h.v = v; return h; }
inline Hex32 hex32(uint32_t v)   { Hex32 h; h.v = v; return h; }
inline Dec   dec(int64_t v)      { Dec d;   d.v = v; return d; }
inline Flt   flt(float v, int d = 1) { Flt f; f.v = v; f.d = d; return f; }

inline char* Append(char* d, const char* e, const char* s)   { return detail::Cat(d, e, s); }
inline char* Append(char* d, const char* e, const Hex& x)    { return detail::CatHex(d, e, x.v); }
inline char* Append(char* d, const char* e, const Hex32& x)  { return detail::CatHex32(d, e, x.v); }
inline char* Append(char* d, const char* e, const Dec& x)    { return detail::CatDec(d, e, x.v); }
inline char* Append(char* d, const char* e, const Flt& x)    { return detail::CatFloat(d, e, x.v, x.d); }

// The fold threads the cursor so the final length is exact -- no post-hoc
// strlen, which would be wrong the moment a fragment contains a NUL.
inline char* LogBuild(char* p, const char* e)
{
    if (p < e) *p = 0;
    return p;
}

template <typename... Rest>
inline char* LogBuild(char* p, const char* e, const char* s, Rest... rest)
{
    p = Append(p, e, s);
    return LogBuild(p, e, rest...);
}

template <typename T, typename... Rest>
inline char* LogBuild(char* p, const char* e, const T& v, Rest... rest)
{
    p = Append(p, e, v);
    return LogBuild(p, e, rest...);
}

template <typename... Args>
inline void Log(int level, Args... args)
{
    // Filtered calls must cost a compare, because Log() is reachable from a
    // per-frame hook.
    if (level > detail::LogThreshold()) return;
    if (detail::LogHandle() == INVALID_HANDLE_VALUE) return;

    char buf[4096];
    const char* pe = buf + sizeof(buf) - 2;
    char* p = detail::Prefix(buf, pe, level);
    p = LogBuild(p, pe, args...);
    // The newline.  The first live run produced every log line concatenated with the
    // next ("...D1E618:47:14.691 INFO ...") because this was missing: LogBuild
    // NUL-terminates but does not add a line break.
    if (p < pe) *p++ = '\n';
    detail::Emit(level, buf, (int)(p - buf));
}

} // namespace qd

#define QDLOG(level, ...) ::qd::Log((level), __VA_ARGS__)

