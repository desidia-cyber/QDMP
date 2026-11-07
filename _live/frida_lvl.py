"""Level-manager / streaming probe runner."""
import sys, time, json
import frida
LIVE = r"G:\Freespace\Queens_Domain\_live"
cmd = sys.argv[1] if len(sys.argv) > 1 else "info"
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    raise SystemExit("process not found")
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_lvl.js", encoding="utf-8").read())
sc.load()
time.sleep(0.6)
try:
    if cmd == "info":
        print(json.dumps(sc.exports_sync.info(), indent=1)[:12000])
    elif cmd == "levels":
        print(json.dumps(sc.exports_sync.levels(), indent=1)[:12000])
    elif cmd == "shops":
        print(json.dumps(sc.exports_sync.shops(), indent=1)[:8000])
    elif cmd == "add":
        # add <listAddr> <sel> <prio>
        print(sc.exports_sync.add(sys.argv[2], int(sys.argv[3]), int(sys.argv[4])))
        time.sleep(2.0)
    elif cmd == "stack":
        print(json.dumps(sc.exports_sync.stack(), indent=1)[:4000])
    elif cmd == "getmgr":
        print(sc.exports_sync.getmgr())
        time.sleep(1.0)
    elif cmd == "find":
        print(json.dumps(sc.exports_sync.find(sys.argv[2]), indent=1)[:8000])
    elif cmd == "owners":
        print(json.dumps(sc.exports_sync.owners(), indent=1)[:8000])
except Exception as e:
    print("ERR", e)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
