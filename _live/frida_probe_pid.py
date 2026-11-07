"""Attach to a specific PID and print the world-clock samples (proves the world is live)."""
import sys, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
pid = int(sys.argv[1])
dwell = float(sys.argv[2]) if len(sys.argv) > 2 else 3.0
script_src = open(LIVE + r"\frida_clock.js", encoding="utf-8").read()

samples = []
def on_message(message, data):
    if message.get("type") == "send":
        p = message["payload"]
        if "world" in p and p.get("world") is not None:
            samples.append((p.get("timesec"), p.get("real"), p.get("delta")))
        elif "ready" in p:
            print("base", p.get("base"))

session = frida.attach(pid)
script = session.create_script(script_src)
script.on("message", on_message)
script.load()
time.sleep(dwell)
try: script.unload()
except Exception: pass
try: session.detach()
except Exception: pass
if samples:
    t0, r0, _ = samples[0]
    t1, r1, _ = samples[-1]
    print("pid %d: n=%d timesec %.3f->%.3f real %.3f->%.3f delta=%.4f" %
          (pid, len(samples), t0, t1, r0, r1, samples[-1][2]))
    print("VERDICT:", "WORLD-LIVE" if t1 > t0 else "FROZEN")
else:
    print("pid %d: no world samples (menu/loading)" % pid)
