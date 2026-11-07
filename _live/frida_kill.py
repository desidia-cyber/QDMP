"""Attach frida_kill.js, queue a lethal ApplyDamage on a target, and stream the log."""
import sys, os, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
OUT = os.path.join(LIVE, "frida_kill.log")
target = sys.argv[1]
dwell = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

f = open(OUT, "w", encoding="utf-8")
f.write("# frida kill %s target=%s\n" % (time.strftime("%H:%M:%S"), target))
f.flush()

def on_message(m, d):
    if m.get("type") != "send":
        f.write("# %s\n" % json.dumps(m)[:400]); f.flush(); return
    f.write(json.dumps(m["payload"]) + "\n"); f.flush()
    print(json.dumps(m["payload"])[:300], flush=True)

pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    sys.exit("process not found")
print("target pid", pid, flush=True)
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_kill.js", encoding="utf-8").read())
sc.on("message", on_message)
sc.load()
print("hooks loaded", flush=True)
print("settarget:", sc.exports_sync.settarget(target), flush=True)
end = time.time() + dwell
while time.time() < end:
    time.sleep(0.5)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
f.close()
print("done", flush=True)
