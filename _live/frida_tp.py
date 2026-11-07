"""Queue a teleport of <actor> to (x,y,z) on the game thread, then keep the hooks
attached while the caller attacks. python frida_tp.py <actor> <x> <y> <z> <dwell>"""
import sys, os, time, json
import frida
LIVE = r"G:\Freespace\Queens_Domain\_live"
OUT = os.path.join(LIVE, "frida_tp.log")
actor, x, y, z = sys.argv[1], float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])
dwell = float(sys.argv[5]) if len(sys.argv) > 5 else 45.0
f = open(OUT, "w", encoding="utf-8")
def on_message(m, d):
    if m.get("type") == "send":
        f.write(json.dumps(m["payload"]) + "\n"); f.flush(); print(json.dumps(m["payload"])[:300], flush=True)
    else:
        f.write("# %s\n" % json.dumps(m)[:300]); f.flush()
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None: sys.exit("process not found")
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_tp.js", encoding="utf-8").read())
sc.on("message", on_message)
sc.load()
mode = sys.argv[6] if len(sys.argv) > 6 else "tp"
if mode in ("pin", "pin+off"):
    if mode == "pin+off":
        print("tickoff:", sc.exports_sync.tickoff(actor), flush=True)
        time.sleep(1.0)
    print("pin:", sc.exports_sync.pin(actor, x, y, z), flush=True)
else:
    print("tp:", sc.exports_sync.tp(actor, x, y, z), flush=True)
end = time.time() + dwell
while time.time() < end:
    time.sleep(0.5)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
f.close()
print("done", flush=True)
