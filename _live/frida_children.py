"""List an actor's children (components). python frida_children.py <actorAddr>"""
import sys, time, json
import frida
LIVE = r"G:\Freespace\Queens_Domain\_live"
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    sys.exit("process not found")
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_children.js", encoding="utf-8").read())
sc.load()
time.sleep(0.5)
try:
    r = sc.exports_sync.children(sys.argv[1])
    for c in r:
        print("%-34s %-28s %s" % (c["cls"], c["name"], c["addr"]))
    print("total", len(r))
except Exception as e:
    print("ERR", e)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
