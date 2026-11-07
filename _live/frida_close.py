"""Persistent close-out driver. Reads one command per stdin line.

Commands:
  sleep N
  info | levels | ulevels | lists | events [N] | clearevents | clock
  actors <substr>
  addlist <listName> <sel> <prio>
  showlevel <uLevelName> <load 0/1> <vis 0/1>
  hidelevel <uLevelName>
  tpplayer x y z
  tpa <addr> x y z
  tickoff <addr>
  hurt <addr> [dmg]
  read <addr> <off> <size>
  dumpclass <addr>
"""
import sys, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"


def main():
    pid = None
    for p in frida.get_local_device().enumerate_processes():
        if "QueensDomain" in p.name:
            pid = p.pid
            break
    if pid is None:
        raise SystemExit("process not found")
    print("ATTACH", pid, flush=True)
    s = frida.attach(pid)
    sc = s.create_script(open(LIVE + r"\frida_close.js", encoding="utf-8").read())
    msgs = []

    def on_message(msg, data):
        if msg.get("type") == "send":
            msgs.append(msg.get("payload"))
        elif msg.get("type") == "error":
            print("JSERR", json.dumps(msg)[:2000], flush=True)

    sc.on("message", on_message)
    sc.load()
    for _ in range(50):
        if any(isinstance(m, dict) and m.get("ready") for m in msgs):
            break
        time.sleep(0.1)
    print("READY", json.dumps([m for m in msgs if isinstance(m, dict) and m.get("ready")][:1]), flush=True)

    def out(v):
        try:
            print(json.dumps(v, indent=1), flush=True)
        except Exception:
            print(str(v), flush=True)

    for raw in sys.stdin:
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        cmd = parts[0]
        print(">>> " + line, flush=True)
        try:
            if cmd == "sleep":
                time.sleep(float(parts[1]))
                print("... slept", parts[1], flush=True)
                continue
            if cmd == "info":
                out(sc.exports_sync.info())
            elif cmd == "levels":
                out(sc.exports_sync.levels())
            elif cmd == "ulevels":
                out(sc.exports_sync.ulevels())
            elif cmd == "streaming":
                out(sc.exports_sync.streaming())
            elif cmd == "lists":
                out(sc.exports_sync.lists())
            elif cmd == "events":
                out(sc.exports_sync.events(int(parts[1]) if len(parts) > 1 else 100))
            elif cmd == "clearevents":
                out(sc.exports_sync.clearevents())
            elif cmd == "clock":
                out(sc.exports_sync.worldclock())
            elif cmd == "actors":
                out(sc.exports_sync.actors(parts[1] if len(parts) > 1 else ""))
            elif cmd == "addlist":
                out(sc.exports_sync.addlist(parts[1], int(parts[2]) if len(parts) > 2 else 0,
                                            int(parts[3]) if len(parts) > 3 else 100))
            elif cmd == "showlevel":
                out(sc.exports_sync.showlevel(parts[1], int(parts[2]), int(parts[3])))
            elif cmd == "hidelevel":
                out(sc.exports_sync.hidelevel(parts[1]))
            elif cmd == "tpplayer":
                out(sc.exports_sync.tpplayer(float(parts[1]), float(parts[2]), float(parts[3])))
            elif cmd == "tpa":
                out(sc.exports_sync.tpa(parts[1], float(parts[2]), float(parts[3]), float(parts[4])))
            elif cmd == "tickoff":
                out(sc.exports_sync.tickoff(parts[1]))
            elif cmd == "tickon":
                out(sc.exports_sync.tickon(parts[1]))
            elif cmd == "hurt":
                out(sc.exports_sync.hurt(parts[1], float(parts[2]) if len(parts) > 2 else 99999.0))
            elif cmd == "findobj":
                out(sc.exports_sync.findobj(parts[1]))
            elif cmd == "funcs":
                out(sc.exports_sync.funcs(parts[1]))
            elif cmd == "fnameidx":
                out(sc.exports_sync.fnameidx(int(parts[1])))
            elif cmd == "arrnames":
                out(sc.exports_sync.arrnames(parts[1], int(parts[2], 0)))
            elif cmd == "arrints":
                out(sc.exports_sync.arrints(parts[1], int(parts[2], 0)))
            elif cmd == "objat":
                out(sc.exports_sync.objat(parts[1], int(parts[2], 0)))
            elif cmd == "spawn":
                out(sc.exports_sync.spawn(parts[1], float(parts[2]), float(parts[3]), float(parts[4])))
            elif cmd == "maxdist":
                out(sc.exports_sync.maxdist(parts[1], float(parts[2])))
            elif cmd == "callfunc":
                out(sc.exports_sync.callfunc(parts[1], parts[2]))
            elif cmd == "spawner":
                out(sc.exports_sync.spawner(parts[1]))
            elif cmd == "read":
                out(sc.exports_sync.read(parts[1], int(parts[2], 0), int(parts[3], 0)))
            elif cmd == "dumpclass":
                out(sc.exports_sync.dumpclass(parts[1]))
            else:
                print("UNKNOWN", cmd, flush=True)
        except Exception as e:
            print("ERR", type(e).__name__, str(e)[:1500], flush=True)

    try:
        sc.unload()
    except Exception:
        pass
    try:
        s.detach()
    except Exception:
        pass


if __name__ == "__main__":
    main()
