//! `qd-relay` command line.
//!
//! ```text
//! qd-relay [--bind <addr>] [--two <addr>] [--two <addr>]
//! ```
//!
//! The relay is a router, not a server in the game sense: it holds no gameplay
//! state. See the crate docs for what it does and does not do.
//!
//! Argument parsing is hand-written because this crate has no dependencies (the
//! whole workspace is dependency-free on purpose: the native side has to be able to
//! reproduce every byte, and a third-party parser buys nothing here).

use std::net::SocketAddr;
use std::process::ExitCode;
use std::sync::atomic::AtomicBool;

use qd_relay::{Config, Relay};

const USAGE: &str = "\
qd-relay - QDCOOP session relay

USAGE:
    qd-relay [OPTIONS]

OPTIONS:
    --bind <addr>    Listen address (default: 127.0.0.1:47800)
    --two <addr>     Pin a peer endpoint; repeat for both peers. Pinned endpoints
                     are admitted without discovery, which is what a LAN/direct-IP
                     test wants. The first HELLO among them still decides the host.
    -h, --help       Print this help

The first peer to send HELLO becomes host. Roles and the session id are logged:
one line per transition, on stdout.";

fn main() -> ExitCode {
    let mut cfg = Config::default();
    let mut args = std::env::args().skip(1);
    while let Some(arg) = args.next() {
        match arg.as_str() {
            "--bind" => {
                let Some(value) = args.next() else {
                    eprintln!("qd-relay: --bind needs an address");
                    return ExitCode::from(2);
                };
                cfg.bind = Some(value);
            }
            "--two" => {
                let Some(value) = args.next() else {
                    eprintln!("qd-relay: --two needs an address");
                    return ExitCode::from(2);
                };
                match parse_addr(&value) {
                    Some(addr) => cfg.pin.push(addr),
                    None => {
                        eprintln!("qd-relay: --two {value:?} is not an address (host:port)");
                        return ExitCode::from(2);
                    }
                }
            }
            "-h" | "--help" => {
                println!("{USAGE}");
                return ExitCode::SUCCESS;
            }
            other => {
                eprintln!("qd-relay: unknown argument {other:?}\n\n{USAGE}");
                return ExitCode::from(2);
            }
        }
    }

    if cfg.pin.len() > qd_relay::MAX_PEERS {
        eprintln!(
            "qd-relay: {} pinned endpoints, but a QDCOOP session has {} players",
            cfg.pin.len(),
            qd_relay::MAX_PEERS
        );
        return ExitCode::from(2);
    }

    if cfg.pin.len() == 1 {
        eprintln!(
            "qd-relay: warning: only one endpoint pinned, so there is nobody to forward to; \
             pass --two twice for a two-peer session"
        );
    }

    let bind_desc = cfg.bind_addr().to_owned();
    let mut relay = match Relay::bind(cfg) {
        Ok(relay) => relay,
        Err(e) => {
            eprintln!("qd-relay: cannot bind {bind_desc}: {e}");
            return ExitCode::FAILURE;
        }
    };
    let local = relay.local_addr().ok();
    println!(
        "[qd-relay] listening on {} (session {:#010x})",
        local.map(|a| a.to_string()).unwrap_or_else(|| "<unknown>".to_owned()),
        relay.session_id()
    );
    if !relay.config().pin.is_empty() {
        let pinned: Vec<String> = relay.config().pin.iter().map(|a| a.to_string()).collect();
        println!("[qd-relay] pinned endpoints: {}", pinned.join(", "));
    }

    // Ctrl-C ends the process; there is nothing to flush, so no handler is needed.
    let stop = AtomicBool::new(false);
    match relay.run(&stop) {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("qd-relay: {e}");
            ExitCode::FAILURE
        }
    }
}

/// `host:port`, or `ip:port`. A bare port is refused rather than guessed: binding
/// the wrong interface silently is exactly the kind of default this project avoids.
fn parse_addr(s: &str) -> Option<SocketAddr> {
    if let Ok(addr) = s.parse::<SocketAddr>() {
        return Some(addr);
    }
    // Allow `localhost:port`, which `SocketAddr` cannot represent.
    if let Some((host, port)) = s.rsplit_once(':') {
        if host.eq_ignore_ascii_case("localhost") {
            if let Ok(port) = port.parse::<u16>() {
                return format!("127.0.0.1:{port}").parse().ok();
            }
        }
    }
    None
}
