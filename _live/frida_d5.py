"""Attach the D5 hooks (StoreEnemySaveState / ApplyDamage / OnEnemyDied) and stream
every hit to a log. Detaches after DWELL seconds or when killed."""
import sys, os, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
OUT = os.path.join(LIVE, "frida_d5.log")
SCRIPT = os.path.join(LIVE, "frida_d5.js")
DWELL = float(sys.argv[1]) if len(sys.argv) > 1 else 600.0

f = open(OUT, "w", encoding="utf-8")
f.write("# frida d5 hooks %s\n" % time.strftime("%H:%M:%S"))
f.flush()

def on_message(message, data):
    if message.get("type") != "send":
        f.write("# %s\n" % json.dumps(message)[:300]); f.flush(); return
    p = message["payload"]
    f.write(json.dumps(p) + "\n"); f.flush()
    print(json.dumps(p)[:240], flush=True)

pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; pname = p.name; break
if pid is None:
    sys.exit("process not found")
print("target:", pname, pid, flush=True)
session = frida.attach(pid)
script = session.create_script(open(SCRIPT, encoding="utf-8").read())
script.on("message", on_message)
script.load()
print("hooks loaded", flush=True)
end = time.time() + DWELL
while time.time() < end:
    time.sleep(0.5)
try: script.unload()
except Exception: pass
try: session.detach()
except Exception: pass
f.close()
print("done", flush=True)
