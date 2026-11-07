//! `qd-relay` — the QDCOOP session authority and datagram router.
//!
//! One UDP socket, one session, at most two peers. The relay owns the *session
//! facts* (who is host, what the session id is); it never simulates, never rewrites
//! a body, and never authors a `WELCOME`.
//!
//! # What it does
//!
//! * **The first `HELLO` becomes host.** DollsMP's proven rule, kept because
//!   promoting a peer mid-session would require transferring ownership of boss
//!   state and progression flags, and getting that wrong corrupts a playthrough.
//!   An explicit `role_wanted = host` request when a host already exists is
//!   refused — two authorities in one key space is the failure rule E1b is about.
//! * **It assigns the session id.** The envelope's `session` field is 0 "until
//!   WELCOME assigns it" (WIRE.md §1); the relay stamps its id into any datagram
//!   that still carries 0, and only that field. Bodies are forwarded byte-for-byte.
//! * **It forwards between the two peers.** A datagram from a peer is delivered to
//!   the other peer, unmodified apart from the session stamp.
//! * **It never panics on malformed input.** Every decode failure is logged and the
//!   datagram dropped; `tests` feeds 10 000 random buffers plus 2 000 mutated valid
//!   frames through the same path.
//!
//! # What it deliberately does not do
//!
//! * It does not send `WELCOME`. WIRE.md §3.2 makes `WELCOME` host → peer, and a
//!   relay-authored `Welcome` would be a second writer of the session fact. A peer
//!   that needs to know its role asks the launcher (out of band) or observes that
//!   the relay admitted it; the relay logs the assignment and exposes it through
//!   [`Relay::peers`].
//! * It does not fragment. One datagram carries one envelope, always (WIRE.md §1;
//!   DollsMP 12 H-6 measured 92.4 % effective loss at 256 fragments).
//! * It has no authentication. v1 is plaintext and LAN/direct-IP only
//!   (WIRE.md §6). The session id is a routing token, not a credential.

use std::fmt;
use std::net::{SocketAddr, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::{Duration, Instant};

use qd_core::protocol::{roles, Envelope, HEADER_LEN};
use qd_core::Error;

/// Default bind address (loopback, matching DollsMP's launcher default).
pub const DEFAULT_BIND: &str = "127.0.0.1:47800";

/// How long a silent peer holds its slot.
///
/// A slot held forever makes the session unrejoinable. 30 s is well past a stall
/// and well short of "the player gave up"; a reap is logged but is *not* evidence
/// the player left, so the session is only closed when no peer remains.
pub const PEER_TIMEOUT: Duration = Duration::from_secs(30);

/// Relay configuration.
#[derive(Debug, Clone, Default)]
pub struct Config {
    /// Address to bind; `None` means [`DEFAULT_BIND`].
    pub bind: Option<String>,
    /// `--two` endpoints. When non-empty these are pre-admitted, so a direct-IP
    /// test does not need discovery: a datagram from a pinned endpoint is always
    /// forwarded, and the first `HELLO` among them still decides the host.
    pub pin: Vec<SocketAddr>,
}

impl Config {
    pub fn bind_addr(&self) -> &str {
        self.bind.as_deref().unwrap_or(DEFAULT_BIND)
    }
}

/// A peer's role. `None` means "admitted but has not said which side it is" —
/// only reachable for a `--two` pinned endpoint.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Role {
    Host,
    Peer,
}

impl Role {
    pub fn as_str(self) -> &'static str {
        match self {
            Role::Host => "host",
            Role::Peer => "peer",
        }
    }

    fn from_wanted(wanted: u8) -> Option<Role> {
        match wanted {
            roles::HOST => Some(Role::Host),
            roles::PEER => Some(Role::Peer),
            _ => None,
        }
    }
}

/// One admitted endpoint.
#[derive(Debug, Clone)]
pub struct Peer {
    pub addr: SocketAddr,
    pub role: Option<Role>,
    pub name: String,
    pub last_seen: Instant,
    pub datagrams: u64,
}

/// A session transition or a dropped datagram. One line of log each.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    SessionOpened { session: u32 },
    Admitted { addr: SocketAddr, role: Role, name: String },
    Refused { addr: SocketAddr, why: &'static str },
    TimedOut { addr: SocketAddr, role: Option<Role> },
    SessionClosed,
    Malformed { addr: SocketAddr, error: Error },
    Dropped { addr: SocketAddr, why: &'static str },
}

impl fmt::Display for Event {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Event::SessionOpened { session } => {
                write!(f, "[qd-relay] session {session:#010x} opened")
            }
            Event::Admitted { addr, role, name } => {
                if name.is_empty() {
                    write!(f, "[qd-relay] {addr} admitted as {}", role.as_str())
                } else {
                    write!(f, "[qd-relay] {addr} admitted as {} ({name:?})", role.as_str())
                }
            }
            Event::Refused { addr, why } => write!(f, "[qd-relay] {addr} refused: {why}"),
            Event::TimedOut { addr, role } => match role {
                Some(r) => write!(f, "[qd-relay] {} {addr} timed out", r.as_str()),
                None => write!(f, "[qd-relay] pinned endpoint {addr} timed out"),
            },
            Event::SessionClosed => write!(f, "[qd-relay] session closed"),
            Event::Malformed { addr, error } => {
                write!(f, "[qd-relay] malformed datagram from {addr}: {error}")
            }
            Event::Dropped { addr, why } => write!(f, "[qd-relay] dropped datagram from {addr}: {why}"),
        }
    }
}

/// Counters, for the status line and for tests.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct Stats {
    pub datagrams_in: u64,
    pub datagrams_out: u64,
    pub malformed: u64,
    pub refused: u64,
    pub forwarded: u64,
    pub dropped: u64,
}

/// A two-player session. QDCOOP is a co-op mod for exactly two players
/// (WIRE.md §0's datagram budget and the 20-row enemy budget are both derived for
/// N=2), so this is a hard cap rather than a setting.
pub const MAX_PEERS: usize = 2;

/// The relay.
#[derive(Debug)]
pub struct Relay {
    socket: UdpSocket,
    cfg: Config,
    session_id: u32,
    peers: Vec<Peer>,
    session_open: bool,
    stats: Stats,
}

impl Relay {
    /// Bind and prepare a relay.
    pub fn bind(cfg: Config) -> std::io::Result<Self> {
        let socket = UdpSocket::bind(cfg.bind_addr())?;
        socket.set_nonblocking(true)?;
        let now = Instant::now();
        let peers = cfg
            .pin
            .iter()
            .map(|addr| Peer {
                addr: *addr,
                role: None,
                name: String::new(),
                last_seen: now,
                datagrams: 0,
            })
            .collect();
        Ok(Self {
            socket,
            cfg,
            session_id: mint_session_id(),
            peers,
            session_open: false,
            stats: Stats::default(),
        })
    }

    /// Bind with an explicit session id (tests, and reproducible LAN sessions).
    pub fn bind_with_session(cfg: Config, session_id: u32) -> std::io::Result<Self> {
        let mut relay = Self::bind(cfg)?;
        relay.session_id = session_id | 1;
        Ok(relay)
    }

    pub fn local_addr(&self) -> std::io::Result<SocketAddr> {
        self.socket.local_addr()
    }

    pub fn session_id(&self) -> u32 {
        self.session_id
    }

    pub fn config(&self) -> &Config {
        &self.cfg
    }

    pub fn peers(&self) -> &[Peer] {
        &self.peers
    }

    pub fn stats(&self) -> Stats {
        self.stats
    }

    pub fn host(&self) -> Option<&Peer> {
        self.peers.iter().find(|p| p.role == Some(Role::Host))
    }

    /// Handle one received datagram. Never panics; never blocks.
    ///
    /// Returns the events to log. Forwarding errors are reported as events rather
    /// than propagated: one unreachable peer must not stop the relay.
    pub fn handle_datagram(&mut self, buf: &[u8], from: SocketAddr) -> Vec<Event> {
        let mut events = Vec::new();
        self.stats.datagrams_in += 1;

        // Validate the whole datagram before trusting any of it. A `Header` alone
        // would let a body-level lie through to the other peer.
        let envelope = match Envelope::decode(buf) {
            Ok(env) => env,
            Err(error) => {
                self.stats.malformed += 1;
                events.push(Event::Malformed { addr: from, error });
                return events;
            }
        };

        if !self.cfg.pin.is_empty() && !self.cfg.pin.contains(&from) {
            self.stats.refused += 1;
            events.push(Event::Refused { addr: from, why: "endpoint is not pinned (--two)" });
            return events;
        }

        // Only a `HELLO` says who the sender claims to be. Everything else is
        // forwarded on the strength of the endpoint already being admitted.
        let hello = match &envelope.body {
            qd_core::protocol::Body::Hello(h) => Some((h.role_wanted, h.display_name.clone())),
            _ => None,
        };

        let index = match self.peers.iter().position(|p| p.addr == from) {
            Some(i) => {
                // A `--two` pinned endpoint has no role until its first HELLO.
                if self.peers[i].role.is_none() {
                    if let Some((wanted, name)) = hello {
                        match self.decide_role(wanted) {
                            Ok(role) => {
                                self.peers[i].role = Some(role);
                                if !name.is_empty() {
                                    self.peers[i].name = name.clone();
                                }
                                events.push(Event::Admitted { addr: from, role, name });
                            }
                            Err(why) => {
                                self.stats.refused += 1;
                                events.push(Event::Refused { addr: from, why });
                                return events;
                            }
                        }
                    }
                }
                i
            }
            None => {
                let Some((wanted, name)) = hello else {
                    self.stats.refused += 1;
                    events.push(Event::Refused { addr: from, why: "no HELLO from this endpoint" });
                    return events;
                };
                match self.admit(from, wanted, name, &mut events) {
                    Some(i) => i,
                    None => return events,
                }
            }
        };

        self.ensure_session_open(&mut events);
        self.peers[index].last_seen = Instant::now();
        self.peers[index].datagrams += 1;

        // A `HELLO` from an already-admitted peer is a retransmission: it still
        // needs to reach the other side, so it falls through to forwarding.
        self.forward(index, buf, envelope.session, &mut events);
        events
    }

    fn ensure_session_open(&mut self, events: &mut Vec<Event>) {
        if !self.session_open {
            self.session_open = true;
            events.push(Event::SessionOpened { session: self.session_id });
        }
    }

    /// Which role a sender asking for `wanted` gets.
    ///
    /// `role_wanted = any` follows DollsMP's rule: the first peer in becomes host
    /// and a later one never does. An explicit host request is a claim to
    /// authority and is refused when one already exists (rule E1b: never two
    /// writers in one key space).
    fn decide_role(&self, wanted: u8) -> std::result::Result<Role, &'static str> {
        let host_present = self.host().is_some();
        match Role::from_wanted(wanted) {
            Some(Role::Host) if host_present => Err("a host is already in this session"),
            Some(role) => Ok(role),
            None => Ok(if host_present { Role::Peer } else { Role::Host }),
        }
    }

    fn admit(
        &mut self,
        addr: SocketAddr,
        wanted: u8,
        name: String,
        events: &mut Vec<Event>,
    ) -> Option<usize> {
        if self.peers.len() >= MAX_PEERS {
            self.stats.refused += 1;
            events.push(Event::Refused { addr, why: "session is full" });
            return None;
        }
        let role = match self.decide_role(wanted) {
            Ok(role) => role,
            Err(why) => {
                self.stats.refused += 1;
                events.push(Event::Refused { addr, why });
                return None;
            }
        };
        // Open the session before announcing the first peer, so the log reads in
        // the order the events happened.
        if self.peers.is_empty() {
            self.ensure_session_open(events);
        }
        self.peers.push(Peer {
            addr,
            role: Some(role),
            name: name.clone(),
            last_seen: Instant::now(),
            datagrams: 0,
        });
        events.push(Event::Admitted { addr, role, name });
        Some(self.peers.len() - 1)
    }

    fn forward(
        &mut self,
        from_index: usize,
        buf: &[u8],
        session: u32,
        events: &mut Vec<Event>,
    ) {
        let from = self.peers[from_index].addr;
        let Some(to) = self.peers.iter().find(|p| p.addr != from).map(|p| p.addr) else {
            self.stats.dropped += 1;
            events.push(Event::Dropped { addr: from, why: "no other peer yet" });
            return;
        };

        // Stamp the session id only when the sender has not got one yet. This is
        // the one header field WIRE.md §1 declares unassigned before `WELCOME`;
        // every other byte is forwarded untouched.
        let datagram: std::borrow::Cow<'_, [u8]> = if session == 0 {
            let mut owned = buf.to_vec();
            if owned.len() < HEADER_LEN {
                self.stats.malformed += 1;
                events.push(Event::Malformed {
                    addr: from,
                    error: Error::TruncatedPacket {
                        kind: "header",
                        need: HEADER_LEN,
                        got: owned.len(),
                    },
                });
                return;
            }
            owned[8..12].copy_from_slice(&self.session_id.to_le_bytes());
            std::borrow::Cow::Owned(owned)
        } else {
            std::borrow::Cow::Borrowed(buf)
        };

        match self.socket.send_to(&datagram, to) {
            Ok(_) => {
                self.stats.datagrams_out += 1;
                self.stats.forwarded += 1;
            }
            Err(_) => {
                self.stats.dropped += 1;
                events.push(Event::Dropped { addr: from, why: "send failed (peer unreachable)" });
            }
        }
    }

    /// Receive and handle everything currently pending. Non-blocking.
    pub fn poll_once(&mut self) -> Vec<Event> {
        let mut events = Vec::new();
        let mut buf = [0u8; 2048];
        loop {
            match self.socket.recv_from(&mut buf) {
                Ok((len, from)) => {
                    events.extend(self.handle_datagram(&buf[..len], from));
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::WouldBlock => break,
                // A recv error (including the ICMP-port-unreachable reset Windows
                // surfaces here) must not end the relay.
                Err(_) => break,
            }
        }
        events
    }

    /// Drop peers that have gone silent, and close the session when none remain.
    pub fn reap(&mut self, now: Instant) -> Vec<Event> {
        let mut events = Vec::new();
        // Read the occupancy *before* taking the peers: `take` empties the vector,
        // so asking afterwards always answers "empty" and the close would be lost.
        let was_occupied = !self.peers.is_empty();
        let mut kept = Vec::with_capacity(self.peers.len());
        for p in std::mem::take(&mut self.peers) {
            if now.saturating_duration_since(p.last_seen) >= PEER_TIMEOUT {
                events.push(Event::TimedOut { addr: p.addr, role: p.role });
            } else {
                kept.push(p);
            }
        }
        self.peers = kept;
        if was_occupied && self.peers.is_empty() {
            self.session_open = false;
            events.push(Event::SessionClosed);
        }
        events
    }

    /// The main loop. Logs every event to stdout, one line each.
    ///
    /// `stop` is polled between iterations so a test can end the loop; the binary
    /// passes a flag that is never set and exits on Ctrl-C.
    pub fn run(&mut self, stop: &AtomicBool) -> std::io::Result<()> {
        while !stop.load(Ordering::Relaxed) {
            for event in self.poll_once() {
                println!("{event}");
            }
            for event in self.reap(Instant::now()) {
                println!("{event}");
            }
            // 1 ms, not 5: this sleep sits in the middle of every datagram's path in
            // both directions, so it is pure added latency (DollsMP F-24 note).
            std::thread::sleep(Duration::from_millis(1));
        }
        Ok(())
    }
}

/// Session id: nonzero, and different per relay instance so a stale datagram from
/// an earlier session is rejected by the peers rather than applied.
fn mint_session_id() -> u32 {
    let nanos = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_nanos() as u64)
        .unwrap_or(0);
    let pid = u64::from(std::process::id());
    let mut h = nanos ^ (pid << 32);
    h ^= h >> 33;
    h = h.wrapping_mul(0xFF51_AFD7_ED55_8CCD);
    h ^= h >> 29;
    let id = (h ^ (h >> 32)) as u32;
    if id == 0 {
        1
    } else {
        id
    }
}
