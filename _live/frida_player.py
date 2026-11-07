"""Print the player pawn's location/yaw once."""
import sys, time, json
import frida

LIVE = r"G:\Freespace\Queens_Domain\_live"
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    sys.exit("process not found")
out = []
def on_message(m, d):
    if m.get("type") == "send":
        out.append(m["payload"])
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_player.js", encoding="utf-8").read())
sc.on("message", on_message)
sc.load()
time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 6.0)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
for p in out:
    if "loc" in p:
        print("pawn %s loc=(%.1f, %.1f, %.1f) yaw=%.1f" % (p["pawn"], p["loc"]["x"], p["loc"]["y"], p["loc"]["z"], p["yaw"]))
    else:
        print(json.dumps(p))
