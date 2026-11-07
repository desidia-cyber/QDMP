"""Read world slots and GMalloc vtable: python frida_world.py"""
import time, json
import frida
LIVE = r"G:\Freespace\Queens_Domain\_live"
pid = None
for p in frida.get_local_device().enumerate_processes():
    if "QueensDomain" in p.name:
        pid = p.pid; break
if pid is None:
    raise SystemExit("process not found")
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_world.js", encoding="utf-8").read())
sc.load()
time.sleep(0.5)
print("world:", json.dumps(sc.exports_sync.w()))
print("gmalloc:", json.dumps(sc.exports_sync.gm()))
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
