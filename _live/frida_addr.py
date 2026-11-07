"""Read loc/yaw at an actor address: python frida_addr.py <addr>"""
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
sc = s.create_script(open(LIVE + r"\frida_addr.js", encoding="utf-8").read())
sc.load()
time.sleep(0.4)
try:
    r = sc.exports_sync.read(sys.argv[1])
    print(json.dumps(r))
except Exception as e:
    print("ERR", e)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
