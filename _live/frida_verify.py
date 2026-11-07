"""Run the residual-verification probes against the live game."""
import sys, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
OUT = LIVE + r"\verify.log"
dwell = float(sys.argv[1]) if len(sys.argv) > 1 else 180.0
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    sys.exit("process not found")

f = open(OUT, "w", encoding="utf-8")
def log(o):
    line = json.dumps(o, default=str)
    f.write(line + "\n"); f.flush(); print(line[:300], flush=True)

def on_message(m, d):
    if m.get("type") == "send":
        log(m["payload"])
    else:
        log({"_msg": json.dumps(m)[:300]})

s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_verify.js", encoding="utf-8").read())
sc.on("message", on_message)
sc.load()
print("attached", flush=True)

# A6 once
try: log({"A6": sc.exports_sync.am()})
except Exception as e: log({"A6": "ERR " + str(e)})

did_b6 = did_b8 = False
end = time.time() + dwell
while time.time() < end:
    time.sleep(1.0)
    try:
        clk = sc.exports_sync.clock()
        if clk: log({"clock": clk})
    except Exception as e:
        log({"clock": "ERR " + str(e)})
    if not did_b6:
        try:
            r = sc.exports_sync.b6()
            if r and "pawn" in r:
                log({"B6": r}); did_b6 = True
        except Exception: pass
    if not did_b8:
        try:
            r = sc.exports_sync.b8()
            if r and "pawn" in r:
                log({"B8": r}); did_b8 = True
        except Exception: pass
    try: log({"A7": sc.exports_sync.a7()})
    except Exception: pass

try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
f.close()
print("done", flush=True)
