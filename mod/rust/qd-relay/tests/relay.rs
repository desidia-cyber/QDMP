//! Relay behaviour, over real UDP sockets where it matters.
//!
//! The decode-path tests use `Relay::handle_datagram` directly  Ethat *is* the
//! decode path, and it is the same function the socket loop calls  Eso 10 000
//! random buffers exercise exactly what a hostile peer would reach.

use std::net::{SocketAddr, UdpSocket};
use std::time::{Duration, Instant};

use qd_core::protocol::*;
use qd_core::rng::Pcg32;
use qd_relay::{Config, Event, Relay, Role};

fn test_config() -> Config {
    Config { bind: Some("127.0.0.1:0".to_owned()), pin: Vec::new() }
}

fn relay() -> Relay {
    Relay::bind(test_config()).expect("relay must bind")
}

fn hello(name: &str, role_wanted: u8) -> Vec<u8> {
    Envelope::new(
        0,
        1,
        Body::Hello(Hello {
            proto: 1,
            build_hash: 0x1122_3344,
            role_wanted,
            save_level: "/Game/Maps/Coop".to_owned(),
            display_name: name.to_owned(),
        }),
    )
    .encode()
    .expect("hello encodes")
}

fn player_row(seq: u32) -> Vec<u8> {
    Envelope::new(
        0,
        seq,
        Body::PlayerRow(PlayerRow {
            slot: 1,
            sample_ms: 1500,
            x: 1000,
            y: -2000,
            z: 300,
            yaw: 8192,
            pitch: -4096,
            vx: 100,
            vy: -100,
            vz: 0,
            move_mode: 3,
            action: player_action::IDLE,
            flags: player_flags::GROUNDED,
        }),
    )
    .encode()
    .expect("row encodes")
}

fn addr(port: u16) -> SocketAddr {
    SocketAddr::from(([127, 0, 0, 1], port))
}

fn events_of<'a>(events: &'a [Event], mut f: impl FnMut(&'a Event) -> bool) -> Vec<&'a Event> {
    events.iter().filter(|e| f(e)).collect()
}

/// A loopback send followed by a single `poll_once` is *usually* enough but not
/// guaranteed; retrying briefly makes the test deterministic without hiding a
/// relay that never handles the datagram (it still fails, after the deadline).
fn pump(relay: &mut Relay) -> usize {
    let deadline = Instant::now() + Duration::from_millis(500);
    loop {
        let n = relay.poll_once().len();
        if n > 0 {
            return n;
        }
        if Instant::now() >= deadline {
            return 0;
        }
        std::thread::sleep(Duration::from_micros(200));
    }
}

fn recv_any(socket: &UdpSocket) -> (Vec<u8>, SocketAddr) {
    socket.set_nonblocking(false).expect("blocking");
    // 2 s was measured insufficient on a busy machine: this suite runs beside a full
    // native build in the same session, and a starved receiver thread missed one
    // datagram and failed a test that passes in isolation.  The wait only matters on the
    // failure path, so a longer one costs nothing when the relay is healthy.
    socket.set_read_timeout(Some(Duration::from_secs(10))).expect("timeout");
    let mut buf = [0u8; 2048];
    let (n, from) = socket.recv_from(&mut buf).expect("expected a datagram");
    (buf[..n].to_vec(), from)
}

/// The next FORWARDED datagram. The relay also answers every HELLO with a
/// role-assignment WELCOME addressed to the sender; those are skipped here, the
/// way a client that already has its role ignores repeats.
fn recv(socket: &UdpSocket) -> (Vec<u8>, SocketAddr) {
    loop {
        let (bytes, from) = recv_any(socket);
        match Envelope::decode(&bytes) {
            Ok(e) if matches!(e.body, Body::Welcome(_)) => continue,
            _ => return (bytes, from),
        }
    }
}

#[test]
fn every_hello_is_answered_with_the_assigned_role() {
    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();
    let session = relay.session_id();
    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);
    b.send_to(&hello("Bob", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);

    for (sock, want) in [(&a, roles::HOST), (&b, roles::PEER)] {
        let (bytes, from) = recv_any(sock);
        assert_eq!(from, relay_addr);
        let e = Envelope::decode(&bytes).expect("welcome decodes");
        match e.body {
            Body::Welcome(w) => {
                assert_eq!(w.role_assigned, want);
                assert_eq!(w.session, session);
            }
            other => panic!("expected a welcome, got {:?}", other.kind()),
        }
    }
}

#[test]
fn first_hello_becomes_host_and_the_second_is_a_peer() {
    let mut relay = relay();
    let a = addr(41001);
    let b = addr(41002);

    let events = relay.handle_datagram(&hello("Alice", roles::ANY), a);
    assert_eq!(relay.host().map(|p| p.addr), Some(a));
    assert_eq!(
        events_of(&events, |e| matches!(e, Event::Admitted { .. })).len(),
        1,
        "the first peer is admitted"
    );
    assert!(
        events.iter().any(|e| matches!(e, Event::SessionOpened { .. })),
        "the session opens on the first accepted datagram"
    );

    relay.handle_datagram(&hello("Bob", roles::ANY), b);
    assert_eq!(relay.host().map(|p| p.addr), Some(a), "host must not move");
    assert_eq!(relay.peers().len(), 2);
    assert_eq!(relay.peers()[1].role, Some(Role::Peer));
}

#[test]
fn an_explicit_second_host_request_is_refused() {
    let mut relay = relay();
    relay.handle_datagram(&hello("Alice", roles::HOST), addr(41011));
    let events = relay.handle_datagram(&hello("Mallory", roles::HOST), addr(41012));

    assert!(events.iter().any(|e| matches!(
        e,
        Event::Refused { why, .. } if *why == "a host is already in this session"
    )));
    assert_eq!(relay.peers().len(), 1, "a refused peer must not take a slot");
    assert_eq!(relay.host().map(|p| p.name.as_str()), Some("Alice"));
}

#[test]
fn a_third_endpoint_is_refused() {
    let mut relay = relay();
    relay.handle_datagram(&hello("Alice", roles::ANY), addr(41021));
    relay.handle_datagram(&hello("Bob", roles::ANY), addr(41022));
    let events = relay.handle_datagram(&hello("Carol", roles::ANY), addr(41023));
    assert!(events.iter().any(|e| matches!(
        e,
        Event::Refused { why, .. } if *why == "session is full"
    )));
    assert_eq!(relay.peers().len(), 2);
}

#[test]
fn a_non_hello_from_an_unknown_endpoint_is_dropped() {
    let mut relay = relay();
    let events = relay.handle_datagram(&player_row(1), addr(41031));
    assert!(events.iter().any(|e| matches!(
        e,
        Event::Refused { why, .. } if *why == "no HELLO from this endpoint"
    )));
    assert_eq!(relay.stats().forwarded, 0);
    assert!(relay.peers().is_empty());
}

#[test]
fn hello_is_forwarded_and_the_session_id_is_stamped() {
    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();
    let session = relay.session_id();
    assert_ne!(session, 0, "the session id must be usable as a stamp");

    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();
    a.set_nonblocking(true).unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay); // admitted as host; nothing to forward to yet

    b.send_to(&hello("Bob", roles::PEER), relay_addr).unwrap();
    pump(&mut relay);

    let (bytes, _) = recv(&a);
    let decoded = Envelope::decode(&bytes).expect("forwarded hello must decode");
    assert_eq!(decoded.session, session, "the relay assigns the session id");
    match decoded.body {
        Body::Hello(h) => assert_eq!(h.display_name, "Bob"),
        other => panic!("expected a hello, got {:?}", other.kind()),
    }
}

#[test]
fn state_datagrams_are_forwarded_byte_for_byte_apart_from_the_session_stamp() {
    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();
    let session = relay.session_id();

    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();
    b.set_nonblocking(true).unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);
    b.send_to(&hello("Bob", roles::PEER), relay_addr).unwrap();
    pump(&mut relay);
    // Clear the hello the host received so the next read is the row.
    let _ = recv(&a);

    let row = player_row(0x0102_0304);
    a.send_to(&row, relay_addr).unwrap();
    pump(&mut relay);

    let (bytes, from) = recv(&b);
    assert_eq!(from, relay_addr, "forwarding comes from the relay's socket");

    // Every byte identical except the session field at offset 8..12.
    let mut expected = row.clone();
    expected[8..12].copy_from_slice(&session.to_le_bytes());
    assert_eq!(bytes, expected);

    let decoded = Envelope::decode(&bytes).unwrap();
    assert_eq!(decoded.seq, 0x0102_0304, "seq is the sender's, not the relay's");
    assert!(matches!(decoded.body, Body::PlayerRow(_)));
}

/// A chunked absolute container must be FORWARDED, not dropped.
///
/// Measured on the rig (LIVE-TEST-LOG R74): the native host publishes
/// `SavableActorStates` as a whole-container header (`n` = 117) split across several
/// datagrams, each carrying a prefix. The decoder read `n` entries per datagram, ran
/// out of data, reported "unexpected end of data", and the relay dropped every
/// actor-state datagram -- 2 364 of them in one session -- while the flag and
/// inventory lanes (which fit in one datagram) forwarded normally. The peer logged
/// `flags applied=` and `inventory body` and never `actor states applied=`.
#[test]
fn a_chunked_actor_states_body_is_forwarded_not_dropped() {
    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();

    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();
    b.set_nonblocking(true).unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);
    b.send_to(&hello("Bob", roles::PEER), relay_addr).unwrap();
    pump(&mut relay);
    let _ = recv(&a);

    // One chunk of a three-datagram pass: the header says 117 entries, two are here.
    let mut body: Vec<u8> = Vec::new();
    body.extend_from_slice(&41u32.to_le_bytes());
    body.extend_from_slice(&117u16.to_le_bytes());
    for i in 0..2u8 {
        body.extend_from_slice(&[i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, i]);
        body.extend_from_slice(&1i32.to_le_bytes());
    }
    let packet = Envelope::new(0, 0x0A0B, Body::Log(Log { bytes: body.clone() }));
    // Build the envelope by hand: the chunk is not a whole container, so the typed
    // encoder (which writes `n = entries.len()`) cannot produce it. That asymmetry is
    // exactly what the native publisher relies on.
    let mut raw = packet.encode().unwrap();
    raw[5] = 0x12; // kind = ACTOR_STATES (magic 0..4, ver 4, kind 5)

    a.send_to(&raw, relay_addr).unwrap();
    // A single `poll_once` is a race: on a busy machine the datagram can still be in the
    // socket's queue when the call returns (measured: this test failed 1 run in ~10 with
    // nothing wrong, which is a flaky test, not a bug).  Poll until it is forwarded.
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut events = relay.poll_once();
    let mut buf = [0u8; 2048];
    let bytes: Vec<u8>;
    loop {
        if let Ok((n, _)) = b.recv_from(&mut buf) {
            // The relay also forwards the host's HELLO/keepalive to this peer, so take the
            // ACTOR_STATES datagram, not merely the next one.
            if n > 5 && buf[5] == 0x12 {
                bytes = buf[..n].to_vec();
                break;
            }
        }
        if Instant::now() >= deadline {
            panic!("a chunked container never reached the other peer: {events:?}");
        }
        std::thread::sleep(Duration::from_millis(5));
        events.extend(relay.poll_once());
    }
    assert!(
        !events.iter().any(|e| matches!(e, Event::Malformed { .. })),
        "a chunked container must not be malformed: {events:?}"
    );
    assert!(
        !events.iter().any(|e| matches!(e, Event::Dropped { .. })),
        "a chunked container must not be dropped: {events:?}"
    );

    assert_eq!(bytes.len(), raw.len(), "the chunk reaches the other peer whole");
    assert_eq!(bytes[5], 0x12, "still ACTOR_STATES");
    match Envelope::decode(&bytes).unwrap().body {
        Body::ActorStates(s) => {
            assert_eq!(s.rev, 41);
            assert_eq!(s.entries.len(), 2, "the prefix, not the whole container");
        }
        other => panic!("expected actor states, got {other:?}"),
    }
}

/// The OBJECTS and ENEMY containers are chunked too, and were rejected the same way.
///
/// MEASURED (round 3, `_test/*/out/relay-*.log`): 624 `malformed datagram ... (kind 0x2a):
/// needed 128 bits, 0 available` and 301 `(kind 0x14)` in single runs.  `ACTOR_STATES` had
/// the "stop when the datagram ends" guard and these two did not, so the object lane's
/// host -> peer leg -- every chest, door, lever, secret wall and pot -- never crossed.
#[test]
fn a_chunked_objects_body_is_forwarded_not_dropped() {
    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();

    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();
    b.set_nonblocking(true).unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);
    b.send_to(&hello("Bob", roles::PEER), relay_addr).unwrap();
    pump(&mut relay);
    let _ = recv(&a);

    // One chunk of a larger pass: the header says 66 rows, this datagram carries two.
    let mut body: Vec<u8> = Vec::new();
    body.extend_from_slice(&9u32.to_le_bytes());
    body.extend_from_slice(&66u16.to_le_bytes());
    for i in 0..2u8 {
        body.extend_from_slice(&[i, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, i]);
        body.push(2);   // shape = door
        body.push(1);   // state = open
    }
    let packet = Envelope::new(0, 0x0C0D, Body::Log(Log { bytes: body.clone() }));
    let mut raw = packet.encode().unwrap();
    raw[5] = 0x2A; // kind = OBJECTS

    a.send_to(&raw, relay_addr).unwrap();
    let deadline = Instant::now() + Duration::from_secs(10);
    let mut events = relay.poll_once();
    let mut buf = [0u8; 2048];
    let bytes: Vec<u8>;
    loop {
        if let Ok((n, _)) = b.recv_from(&mut buf) {
            if n > 5 && buf[5] == 0x2A {
                bytes = buf[..n].to_vec();
                break;
            }
        }
        if Instant::now() >= deadline {
            panic!("a chunked object body never reached the other peer: {events:?}");
        }
        std::thread::sleep(Duration::from_millis(5));
        events.extend(relay.poll_once());
    }
    assert!(
        !events.iter().any(|e| matches!(e, Event::Malformed { .. })),
        "a chunked object body must not be malformed: {events:?}"
    );
    match Envelope::decode(&bytes).unwrap().body {
        Body::Objects(o) => {
            assert_eq!(o.rev, 9);
            assert_eq!(o.entries.len(), 2, "the prefix, not the whole set");
        }
        other => panic!("expected objects, got {other:?}"),
    }
}

#[test]
fn an_already_assigned_session_is_not_rewritten() {    let mut relay = relay();
    let relay_addr = relay.local_addr().unwrap();
    let session = relay.session_id();

    let a = UdpSocket::bind("127.0.0.1:0").unwrap();
    let b = UdpSocket::bind("127.0.0.1:0").unwrap();
    b.set_nonblocking(true).unwrap();

    a.send_to(&hello("Alice", roles::ANY), relay_addr).unwrap();
    pump(&mut relay);
    b.send_to(&hello("Bob", roles::PEER), relay_addr).unwrap();
    pump(&mut relay);
    let _ = recv(&a);

    let other_session = session.wrapping_add(1);
    assert_ne!(other_session, 0);
    let frame = Envelope::new(other_session, 9, Body::Keepalive).encode().unwrap();
    a.send_to(&frame, relay_addr).unwrap();
    pump(&mut relay);

    let (bytes, _) = recv(&b);
    assert_eq!(bytes, frame, "a non-zero session must be forwarded untouched");
    assert_eq!(Envelope::decode(&bytes).unwrap().session, other_session);
}

#[test]
fn pinned_endpoints_are_admitted_without_discovery() {
    let one = addr(41041);
    let two = addr(41042);
    let cfg = Config { bind: Some("127.0.0.1:0".to_owned()), pin: vec![one, two] };
    let mut relay = Relay::bind(cfg).unwrap();
    assert_eq!(relay.peers().len(), 2, "pinned endpoints are pre-admitted");
    assert!(relay.host().is_none(), "but no role is assigned until a HELLO");

    // No HELLO: a datagram from one pinned endpoint still reaches the other.
    let events = relay.handle_datagram(&player_row(1), one);
    assert_eq!(relay.stats().forwarded, 1);
    assert!(events.iter().any(|e| matches!(e, Event::SessionOpened { .. })));

    // An unpinned source is refused even though it is a valid frame.
    let events = relay.handle_datagram(&player_row(2), addr(41043));
    assert!(events.iter().any(|e| matches!(e, Event::Refused { .. })));
}

#[test]
fn a_pinned_endpoint_still_gets_a_role_from_its_hello() {
    let one = addr(41051);
    let two = addr(41052);
    let cfg = Config { bind: Some("127.0.0.1:0".to_owned()), pin: vec![one, two] };
    let mut relay = Relay::bind(cfg).unwrap();

    relay.handle_datagram(&hello("Alice", roles::ANY), one);
    assert_eq!(relay.host().map(|p| p.addr), Some(one));
    relay.handle_datagram(&hello("Bob", roles::ANY), two);
    assert_eq!(relay.host().map(|p| p.addr), Some(one), "host must not move");
    assert_eq!(relay.peers()[1].role, Some(Role::Peer));
}

#[test]
fn silent_peers_are_reaped_and_the_session_closes() {
    let mut relay = relay();
    relay.handle_datagram(&hello("Alice", roles::ANY), addr(41061));
    relay.handle_datagram(&hello("Bob", roles::ANY), addr(41062));

    let later = Instant::now() + qd_relay::PEER_TIMEOUT + Duration::from_millis(1);
    let events = relay.reap(later);
    assert_eq!(events_of(&events, |e| matches!(e, Event::TimedOut { .. })).len(), 2);
    assert!(events.iter().any(|e| matches!(e, Event::SessionClosed)));
    assert!(relay.peers().is_empty());

    // And the slot is reusable afterwards.
    let events = relay.handle_datagram(&hello("Carol", roles::ANY), addr(41063));
    assert!(events.iter().any(|e| matches!(e, Event::SessionOpened { .. })));
    assert_eq!(relay.host().map(|p| p.name.as_str()), Some("Carol"));
}

#[test]
fn a_peer_that_is_still_talking_is_not_reaped() {
    let mut relay = relay();
    let a = addr(41071);
    relay.handle_datagram(&hello("Alice", roles::ANY), a);
    let later = Instant::now() + qd_relay::PEER_TIMEOUT / 2;
    assert!(relay.reap(later).is_empty());
    assert_eq!(relay.peers().len(), 1);
}

#[test]
fn event_lines_are_single_lines() {
    let events = [
        Event::SessionOpened { session: 7 },
        Event::Admitted { addr: addr(1), role: Role::Host, name: "Bob".into() },
        Event::Refused { addr: addr(2), why: "session is full" },
        Event::TimedOut { addr: addr(3), role: Some(Role::Peer) },
        Event::SessionClosed,
        Event::Malformed { addr: addr(4), kind: 0x12, error: qd_core::Error::BadMagic(*b"XXXX") },
        Event::Dropped { addr: addr(5), why: "no other peer yet" },
    ];
    for e in events {
        let line = e.to_string();
        assert!(!line.is_empty());
        assert!(!line.contains('\n'), "a log line must be one line: {line:?}");
        assert!(line.starts_with("[qd-relay]"));
    }
}

/// 10 000 random byte strings through the real decode path. The assertion is the
/// absence of a panic; the counters prove the test is not vacuous (nothing random
/// was accepted, and nothing was forwarded).
#[test]
fn ten_thousand_random_datagrams_never_panic() {
    let mut relay = relay();
    let mut rng = Pcg32::new(0xC0FF_EE01, 1);
    let mut accepted = 0u64;

    for i in 0..10_000u32 {
        let len = (rng.next_bounded(1600)) as usize;
        let bytes: Vec<u8> = (0..len).map(|_| rng.next_u32() as u8).collect();
        // Vary the source address too, so admission bookkeeping is exercised at a
        // different index every time.
        let from = addr(40_000 + (i % 1000) as u16);
        let events = relay.handle_datagram(&bytes, from);
        if events.iter().any(|e| matches!(e, Event::Admitted { .. })) {
            accepted += 1;
        }
    }

    assert_eq!(relay.stats().datagrams_in, 10_000);
    assert_eq!(relay.stats().forwarded, 0, "random bytes must never reach a peer");
    assert_eq!(relay.stats().malformed, 10_000, "every random buffer must be rejected");
    assert_eq!(accepted, 0);
}

/// Mutations of *valid* frames: the interesting case, because these get past the
/// magic and have to be rejected by the body decoder.
#[test]
fn mutated_valid_frames_never_panic() {
    let mut relay = relay();
    let mut rng = Pcg32::new(0xBEEF_1234, 7);

    let seeds = [
        hello("Alice", roles::ANY),
        player_row(5),
        Envelope::new(0, 3, Body::WorldFlags(WorldFlags {
            rev: 5,
            entries: vec![FlagEntry { name: "DoorA".to_owned(), value: 1 }],
        }))
        .encode()
        .unwrap(),
        Envelope::new(0, 4, Body::EnemyRows(EnemyRows { rows: vec![EnemyRow::default()] }))
            .encode()
            .unwrap(),
        Envelope::new(0, 5, Body::LevelUnion(LevelUnion {
            bits: vec![1, 2, 3],
            names: Some(vec!["L_Main".to_owned()]),
        }))
        .encode()
        .unwrap(),
        Envelope::new(0, 6, Body::Intent(Intent {
            based_on_rev: 5,
            payload: IntentPayload::SetFlag {
                name: "DoorA".to_owned(),
                value: 1,
                mode: 0,
            },
        }))
        .encode()
        .unwrap(),
    ];

    let mut well_formed = 0u64;
    for i in 0..2_000u32 {
        let seed = &seeds[(i as usize) % seeds.len()];
        let mut bytes = seed.clone();
        match i % 4 {
            0 => {
                // Flip one byte.
                let idx = rng.next_bounded(bytes.len() as u32) as usize;
                bytes[idx] ^= 1 << (rng.next_bounded(8) as u8);
            }
            1 => {
                // Truncate.
                let keep = rng.next_bounded(bytes.len() as u32) as usize;
                bytes.truncate(keep);
            }
            2 => {
                // Append junk (must be rejected as trailing bytes).
                let extra = rng.next_bounded(8) as usize + 1;
                bytes.extend((0..extra).map(|_| rng.next_u32() as u8));
            }
            _ => {
                // Claim a different length in the header.
                let claim = rng.next_bounded(1300) as u16;
                if bytes.len() >= HEADER_LEN {
                    bytes[6..8].copy_from_slice(&claim.to_le_bytes());
                }
            }
        }
        // Some mutations leave a perfectly legal frame (a body byte the decoder
        // does not range-check). Those *should* be forwarded; the invariant is that
        // nothing else is.
        if Envelope::decode(&bytes).is_ok() {
            well_formed += 1;
        }
        let from = addr(45_000 + (i % 100) as u16);
        let _ = relay.handle_datagram(&bytes, from);
    }

    assert_eq!(relay.stats().datagrams_in, 2_000);
    assert_eq!(
        relay.stats().malformed,
        2_000 - well_formed,
        "every undecodable mutation must be counted and dropped"
    );
    assert!(
        relay.stats().forwarded <= well_formed,
        "the relay forwarded {} frames but only {well_formed} decoded",
        relay.stats().forwarded
    );
}

/// The relay must survive a datagram larger than any legal one without allocating
/// its declared length.
#[test]
fn an_oversized_datagram_is_rejected_cheaply() {
    let mut relay = relay();
    let mut bytes = vec![0u8; 4096];
    bytes[0..4].copy_from_slice(&MAGIC);
    bytes[4] = VERSION;
    bytes[5] = Kind::Ack.as_u8();
    bytes[6..8].copy_from_slice(&1201u16.to_le_bytes());
    bytes[8..12].copy_from_slice(&1u32.to_le_bytes());
    bytes[12..16].copy_from_slice(&1u32.to_le_bytes());

    let events = relay.handle_datagram(&bytes, addr(41081));
    assert!(events.iter().any(|e| matches!(e, Event::Malformed { .. })));
    assert_eq!(relay.stats().malformed, 1);
}
