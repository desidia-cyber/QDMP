// qdnet.h -- UDP transport, session state, and the game-thread handoff.
//
// SHAPE, AND WHY
// --------------
// The corpus rule that constrains this file: "Only UObject access goes on the game
// thread. File I/O, decoding and maths stay off it."  So the socket lives on its own
// thread and never touches a UObject; decoded datagrams are handed to the game
// thread through a small bounded queue that the tick hook drains.  A socket read
// blocking the render thread would present exactly like "the other player is
// invisible", which is the diagnostic trap DollsMP documented.
//
// Transport is a UDP relay, not Steam P2P.  analysis/07-verification.md Check 2
// measured that `steam_api64.dll` is a delay-load import whose DLL is unreachable in
// this install, so the game runs with Steamworks absent.  A relay is therefore the
// design, not a fallback.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdint.h>

#include "qdlog.h"
#include "qdwire.h"

namespace qd {

static const int kInboundQueue = 64;
static const int kReliableSlots = 16;

struct InboundPacket {
    uint8_t  kind;
    uint32_t seq;
    uint32_t session;
    int      body_len;
    uint8_t  body[wire::kMaxBody];
};

// A reliable, at-most-once *effect* sender.  Retransmits until acked, bounded
// attempts, then gives up loudly -- the corpus rule "every bracket must have a
// deadline, and exceeding it must be an error by name, not a silent extension".
struct ReliableSlot {
    bool     used;
    uint32_t seq;
    uint8_t  kind;
    int      len;
    uint8_t  body[wire::kMaxBody];
    int      attempts;
    uint32_t next_ms;
    uint32_t first_ms;
};

enum NetRole { ROLE_NONE = 0, ROLE_HOST = 1, ROLE_PEER = 2 };

struct NetStats {
    uint32_t tx;
    uint32_t rx;
    uint32_t dropped;      // queue full
    uint32_t malformed;
    uint32_t resent;
    uint32_t abandoned;
};

class Net {
public:
    Net()
        : sock_(INVALID_SOCKET), running_(false), seq_(1), role_(ROLE_NONE),
          session_(0), last_rx_ms_(0), last_hello_ms_(0), hello_sent_(0)
    {
        for (int i = 0; i < kReliableSlots; ++i) rel_[i].used = false;
        for (int i = 0; i < kInboundQueue; ++i) q_[i].body_len = 0;
        qhead_ = qtail_ = 0;
        stats_.tx = stats_.rx = stats_.dropped = stats_.malformed = 0;
        stats_.resent = stats_.abandoned = 0;
        remoteLen_ = 0;
        wsaUp_ = false;
    }

    // ---- lifecycle --------------------------------------------------------
    bool Start(const char* relay_host, uint16_t relay_port, const char* save_level)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
            QDLOG(LOG_ERROR, "WSAStartup failed");
            return false;
        }
        wsaUp_ = true;

        sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock_ == INVALID_SOCKET) {
            QDLOG(LOG_ERROR, "socket() failed err=", dec(WSAGetLastError()));
            return false;
        }

        sockaddr_in local;
        memset(&local, 0, sizeof(local));
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = htonl(INADDR_ANY);
        local.sin_port = 0;
        if (bind(sock_, (sockaddr*)&local, sizeof(local)) == SOCKET_ERROR) {
            QDLOG(LOG_ERROR, "bind() failed err=", dec(WSAGetLastError()));
            return false;
        }

        memset(&remote_, 0, sizeof(remote_));
        remote_.sin_family = AF_INET;
        remote_.sin_port = htons(relay_port);
        if (!Resolve(relay_host, &remote_.sin_addr)) {
            QDLOG(LOG_ERROR, "cannot resolve relay host ", relay_host);
            return false;
        }
        remoteLen_ = (int)sizeof(remote_);

        u_long nb = 1;
        ioctlsocket(sock_, FIONBIO, &nb);

        // A receive timeout means Poll() returns promptly even if Windows decides
        // to hold a datagram; without it a lost wakeup stalls the whole session.
        DWORD tmo = 5;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tmo, sizeof(tmo));

        // 128 KB each way: the enemy stream is the largest term at ~16 KB/s, but a
        // burst after a level load is not.
        int buf = 128 * 1024;
        setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, (const char*)&buf, sizeof(buf));
        setsockopt(sock_, SOL_SOCKET, SO_SNDBUF, (const char*)&buf, sizeof(buf));

        int i = 0;
        for (; save_level && save_level[i] && i < (int)sizeof(level_) - 1; ++i) level_[i] = save_level[i];
        level_[i] = 0;

        running_ = true;
        thread_ = CreateThread(nullptr, 0, &Net::ThreadMain, this, 0, nullptr);
        if (!thread_) {
            QDLOG(LOG_ERROR, "cannot start the network thread");
            running_ = false;
            return false;
        }
        QDLOG(LOG_INFO, "net started: relay=", relay_host, ":", dec(relay_port),
              " level=", level_);
        return true;
    }

    void Stop()
    {
        running_ = false;
        if (thread_) {
            WaitForSingleObject(thread_, 1500);
            CloseHandle(thread_);
            thread_ = nullptr;
        }
        if (sock_ != INVALID_SOCKET) {
            closesocket(sock_);
            sock_ = INVALID_SOCKET;
        }
        if (wsaUp_) { WSACleanup(); wsaUp_ = false; }
        QDLOG(LOG_INFO, "net stopped: tx=", dec(stats_.tx), " rx=", dec(stats_.rx),
              " dropped=", dec(stats_.dropped), " malformed=", dec(stats_.malformed),
              " resent=", dec(stats_.resent), " abandoned=", dec(stats_.abandoned));
    }

    NetRole Role() const { return role_; }
    bool IsHost() const  { return role_ == ROLE_HOST; }
    bool IsPeer() const  { return role_ == ROLE_PEER; }
    bool Connected() const { return role_ != ROLE_NONE; }
    uint32_t Session() const { return session_; }
    const NetStats& Stats() const { return stats_; }
    uint32_t LastRxMs() const { return last_rx_ms_; }

    // ---- sending ----------------------------------------------------------
    // Build an envelope around `body` and send it once, unreliable.
    bool SendUnreliable(uint8_t kind, const uint8_t* body, int bodyLen)
    {
        uint8_t pkt[wire::kMaxPacket];
        wire::Writer w(pkt, sizeof(pkt));
        wire::WriteHeader(w, kind, session_, NextSeq(), (uint16_t)bodyLen);
        if (bodyLen > 0) w.Bytes(body, bodyLen);
        if (!w.ok) {
            QDLOG(LOG_WARN, "packet did not fit: kind=", hex32(kind), " body=", dec(bodyLen));
            return false;
        }
        return SendRaw(pkt, w.len);
    }

    // Queue a reliable body.  The slot keeps its own copy because the caller's
    // buffer is a stack frame that will be gone before the retransmit.
    bool SendReliable(uint8_t kind, const uint8_t* body, int bodyLen)
    {
        if (bodyLen > wire::kMaxBody) return false;
        uint32_t s = NextSeq();
        for (int i = 0; i < kReliableSlots; ++i) {
            if (rel_[i].used) continue;
            rel_[i].used = true;
            rel_[i].seq = s;
            rel_[i].kind = kind;
            rel_[i].len = bodyLen;
            if (bodyLen) memcpy(rel_[i].body, body, (size_t)bodyLen);
            rel_[i].attempts = 0;
            rel_[i].next_ms = Now();
            rel_[i].first_ms = Now();
            TransmitReliable(rel_[i]);
            return true;
        }
        QDLOG(LOG_WARN, "no free reliable slot; dropping kind=", hex32(kind));
        return false;
    }

    // Called when an ACK arrives for `seq`.
    void Ack(uint32_t seq)
    {
        AcquireSRWLockExclusive(&lock_);
        for (int i = 0; i < kReliableSlots; ++i) {
            if (rel_[i].used && rel_[i].seq == seq) { rel_[i].used = false; break; }
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    // ---- receiving --------------------------------------------------------
    // Game-thread side.  Pops one decoded datagram, or returns false.
    bool Pop(InboundPacket* out)
    {
        bool got = false;
        AcquireSRWLockExclusive(&lock_);
        if (qhead_ != qtail_) {
            *out = q_[qhead_];
            qhead_ = (qhead_ + 1) % kInboundQueue;
            got = true;
        }
        ReleaseSRWLockExclusive(&lock_);
        return got;
    }

    // Retransmit pump.  Safe to call from the tick hook.
    void PumpReliable()
    {
        AcquireSRWLockExclusive(&lock_);
        uint32_t now = Now();
        for (int i = 0; i < kReliableSlots; ++i) {
            ReliableSlot& r = rel_[i];
            if (!r.used) continue;
            if (now < r.next_ms) continue;
            // 5 attempts over 6 s matches the previous project's measured tuning
            // (68 section 2: "retried up to 5 times; only the first 6 s hold").
            if (r.attempts >= 5 || now - r.first_ms > 6000) {
                r.used = false;
                ++stats_.abandoned;
                QDLOG(LOG_WARN, "reliable send abandoned: kind=", hex32(r.kind),
                      " seq=", dec(r.seq), " attempts=", dec(r.attempts));
                continue;
            }
            ++r.attempts;
            r.next_ms = now + (uint32_t)(100 * r.attempts);
            ++stats_.resent;
            TransmitReliable(r);
        }
        ReleaseSRWLockExclusive(&lock_);
    }

    static uint32_t Now()
    {
        // timeGetTime would be cheaper but is not necessarily at 1 ms resolution
        // unless timeBeginPeriod has been called in this process -- and the corpus
        // measured sleep(1 ms) as 15.6 ms without it.  GetTickCount64 has no such
        // trap and its 10-16 ms granularity is adequate for 30-60 Hz scheduling.
        return (uint32_t)GetTickCount64();
    }

private:
    static DWORD WINAPI ThreadMain(LPVOID self) { ((Net*)self)->Run(); return 0; }

    void Run()
    {
        // Announce, then keep announcing until the relay answers.  The first peer to
        // be accepted becomes the host (DollSMP's Session rule, kept because it is
        // the reason that mod converges without a lobby).
        SendHello();

        uint8_t buf[wire::kMaxPacket];
        while (running_) {
            // `fromLen` is IN/OUT: it must be reset to the buffer's capacity before
            // every call.  Leaving it at 0 makes recvfrom fail with WSAEFAULT (10014),
            // which the first live run produced ~500 times a second -- and the tight
            // retry with no backoff filled the log at 800 KB in under a second and is
            // the most likely reason the game then died.
            fromLen_ = (int)sizeof(from_);
            int n = recvfrom(sock_, (char*)buf, sizeof(buf), 0,
                             (sockaddr*)&from_, &fromLen_);
            uint32_t now = Now();
            if (n > 0) {
                Ingest(buf, n, now);
                consecutiveErrors_ = 0;
            } else if (n == SOCKET_ERROR) {
                int e = WSAGetLastError();
                // Log the first occurrence and then at most once a second: an error
                // path that can spin must not be able to become the workload.
                ++consecutiveErrors_;
                if (e != WSAEWOULDBLOCK && e != WSAETIMEDOUT && e != WSAECONNRESET) {
                    if (consecutiveErrors_ == 1 || (now - lastErrorMs_) > 1000) {
                        lastErrorMs_ = now;
                        QDLOG(LOG_WARN, "recvfrom err=", dec(e),
                              " (", dec(consecutiveErrors_), " consecutive)");
                    }
                }
                // Real errors back off; a would-block does not.
                Sleep((e == WSAEWOULDBLOCK) ? 2 : 50);
            }
            if (role_ == ROLE_NONE && now - last_hello_ms_ > 1000) SendHello();
            // Keepalive at 1 Hz idle so the relay can tell a silent peer from a
            // disconnected one, and so NAT bindings survive.
            if (role_ != ROLE_NONE && now - last_tx_ms_ > 1000) {
                SendUnreliable(wire::K_KEEPALIVE, nullptr, 0);
            }
        }
    }

    void SendHello()
    {
        last_hello_ms_ = Now();
        ++hello_sent_;
        uint8_t body[320];
        wire::Writer w(body, sizeof(body));
        w.U8(1);                              // proto
        w.U32(0);                             // build hash placeholder, set by caller below
        w.U8(0);                              // role wanted: any
        w.Str(level_);
        w.Str("player");
        if (!w.ok) return;

        uint8_t pkt[wire::kMaxPacket];
        wire::Writer pw(pkt, sizeof(pkt));
        wire::WriteHeader(pw, wire::K_HELLO, 0, NextSeq(), (uint16_t)w.len);
        pw.Bytes(body, w.len);
        if (pw.ok) SendRaw(pkt, pw.len);
    }

    void Ingest(const uint8_t* buf, int n, uint32_t now)
    {
        wire::Reader r(buf, n);
        wire::Envelope e;
        if (!wire::ReadHeader(r, &e)) {
            ++stats_.malformed;
            return;
        }
        ++stats_.rx;
        last_rx_ms_ = now;

        switch (e.kind) {
        case wire::K_WELCOME: {
            // role(1) session(4) host_tick(4) level(str) flags_rev(4)
            uint8_t role = r.U8();
            uint32_t sess = r.U32();
            r.U32();                 // host tick, informational
            char lvl[96];
            r.Str(lvl, sizeof(lvl));
            r.U32();                 // flags revision, applied by the caller
            if (!r.ok) { ++stats_.malformed; return; }
            role_ = (role == ROLE_HOST) ? ROLE_HOST : ROLE_PEER;
            session_ = sess;
            QDLOG(LOG_INFO, "session established: role=",
                  (role_ == ROLE_HOST ? "HOST" : "PEER"),
                  " session=", dec(sess), " level=", lvl);
            break;
        }
        case wire::K_ACK:
            Ack(r.U32());
            break;
        case wire::K_KEEPALIVE:
            if (role_ == ROLE_NONE) SendHello();
            break;
        default:
            // Everything else is for the game thread.  A full queue is a drop, not a
            // block: a stalled consumer must never stall the socket.
            QueuePacket(e, r);
            break;
        }
    }

    void QueuePacket(const wire::Envelope& e, wire::Reader& r)
    {
        int avail = r.len - r.pos;
        if (avail < 0 || avail > wire::kMaxBody) { ++stats_.malformed; return; }
        AcquireSRWLockExclusive(&lock_);
        int next = (qtail_ + 1) % kInboundQueue;
        if (next == qhead_) {
            ++stats_.dropped;
            ReleaseSRWLockExclusive(&lock_);
            return;
        }
        InboundPacket& p = q_[qtail_];
        p.kind = e.kind;
        p.seq = e.seq;
        p.session = e.session;
        p.body_len = avail;
        if (avail) memcpy(p.body, r.p + r.pos, (size_t)avail);
        qtail_ = next;
        ReleaseSRWLockExclusive(&lock_);
    }

    void TransmitReliable(const ReliableSlot& s)
    {
        uint8_t pkt[wire::kMaxPacket];
        wire::Writer w(pkt, sizeof(pkt));
        wire::WriteHeader(w, s.kind, session_, s.seq, (uint16_t)s.len);
        if (s.len) w.Bytes(s.body, s.len);
        if (w.ok) SendRaw(pkt, w.len);
    }

    bool SendRaw(const uint8_t* p, int n)
    {
        if (sock_ == INVALID_SOCKET || remoteLen_ == 0) return false;
        int sent = sendto(sock_, (const char*)p, n, 0, (sockaddr*)&remote_, remoteLen_);
        if (sent == SOCKET_ERROR) {
            int e = WSAGetLastError();
            if (e != WSAEWOULDBLOCK) QDLOG(LOG_DEBUG, "sendto err=", dec(e));
            return false;
        }
        ++stats_.tx;
        last_tx_ms_ = Now();
        return true;
    }

    uint32_t NextSeq() { return (uint32_t)InterlockedIncrement((volatile LONG*)&seq_); }

    bool Resolve(const char* host, in_addr* out)
    {
        if (!host || !*host) return false;
        // A literal address is the common case and must not touch DNS.
        if (inet_pton(AF_INET, host, out) == 1) return true;
        addrinfo hints;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host, nullptr, &hints, &res) != 0 || !res) return false;
        *out = ((sockaddr_in*)res->ai_addr)->sin_addr;
        freeaddrinfo(res);
        return true;
    }

    SOCKET      sock_;
    HANDLE      thread_ = nullptr;
    volatile bool running_;
    volatile LONG seq_;
    volatile NetRole role_;
    uint32_t    session_;
    uint32_t    last_rx_ms_;
    uint32_t    last_tx_ms_ = 0;
    uint32_t    last_hello_ms_;
    int         hello_sent_;
    bool        wsaUp_;
    sockaddr_in remote_;
    int         remoteLen_;
    sockaddr_in from_;
    int         fromLen_ = 0;
    uint32_t    lastErrorMs_ = 0;
    uint32_t    consecutiveErrors_ = 0;
    char        level_[96];

    SRWLOCK     lock_ = SRWLOCK_INIT;
    InboundPacket q_[kInboundQueue];
    int         qhead_, qtail_;
    ReliableSlot rel_[kReliableSlots];
    NetStats    stats_;
};

} // namespace qd

