"""Attach Frida to the game, stream UWorld clock samples to a CSV, and mirror
key-marker text so menu presses can be aligned with the clock."""
import sys, os, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
OUT = os.path.join(LIVE, "frida_clock.csv")
MARK = os.path.join(LIVE, "key_marker.txt")
SCRIPT = os.path.join(LIVE, "frida_clock.js")
DWELL = float(sys.argv[1]) if len(sys.argv) > 1 else 900.0

f = open(OUT, "w", encoding="utf-8")
f.write("# frida clock %.0f\n" % time.time())
f.write("t_ms,utc,timesec,unpaused,real,audio,delta,pausedelay\n")
f.flush()

def on_message(message, data):
    if message.get("type") != "send":
        f.write("# %s\n" % json.dumps(message)[:300]); f.flush(); return
    p = message["payload"]
    if "ready" in p:
        f.write("# ready base=%s\n" % p.get("base")); f.flush(); return
    if "err" in p:
        f.write("# err %s\n" % p.get("err")); f.flush(); return
    if p.get("world") is None:
        f.write("# world null\n"); f.flush(); return
    def fmt(k):
        v = p.get(k)
        return "" if v is None else ("%.4f" % v)
    f.write("%d,%s,%s,%s,%s,%s,%s,%s\n" % (
        p["t"], time.strftime("%H:%M:%S", time.localtime(p["t"] / 1000.0)),
        fmt("timesec"), fmt("unpaused"), fmt("real"), fmt("audio"),
        fmt("delta"), fmt("pausedelay")))
    f.flush()

pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid
        pname = p.name
        break
if pid is None:
    sys.exit("process not found")

print("target:", pname, pid, flush=True)
session = frida.attach(pid)
script = session.create_script(open(SCRIPT, encoding="utf-8").read())
script.on("message", on_message)
script.load()
print("attached", flush=True)

end = time.time() + DWELL
last = ""
while time.time() < end:
    time.sleep(0.1)
    try:
        if os.path.exists(MARK):
            s = open(MARK, encoding="utf-8", errors="replace").read().strip()
            if s and s != last:
                last = s
                f.write("# MARKER %s\n" % s); f.flush()
    except Exception:
        pass

try:
    script.unload()
except Exception:
    pass
try:
    session.detach()
except Exception:
    pass
f.close()
print("done", flush=True)
