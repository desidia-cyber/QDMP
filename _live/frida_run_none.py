import sys, json, time
import frida

SCRIPT = r"G:\Freespace\Queens_Domain\_live\frida_none.js"
DWELL = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
target = sys.argv[1] if len(sys.argv) > 1 else "QueensDomain"

def on_message(message, data):
    if message.get("type") == "send":
        print("MSG " + json.dumps(message["payload"])[:6000], flush=True)
    elif message.get("type") == "error":
        print("JS-ERROR " + json.dumps(message)[:3000], flush=True)
    else:
        print("EVT " + json.dumps(message)[:1000], flush=True)

pid = None
for p in frida.get_local_device().enumerate_processes():
    if target.lower() in p.name.lower():
        pid = p.pid; pname = p.name; break
if pid is None:
    sys.exit("process not found")
print("target:", pname, pid, flush=True)

session = frida.attach(pid)
src = open(SCRIPT, "r", encoding="utf-8").read()
script = session.create_script(src)
script.on("message", on_message)
try:
    script.load()
    print("load() returned ok", flush=True)
except Exception as e:
    print("load() raised (tolerated):", type(e).__name__, str(e)[:200], flush=True)

# give the async body time to run and stream its sends
deadline = time.time() + DWELL
while time.time() < deadline:
    time.sleep(0.25)

try:
    script.unload()
except Exception:
    pass
try:
    session.detach()
except Exception:
    pass
print("done", flush=True)
