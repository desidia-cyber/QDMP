"""Run frida_scan.js and print a compact nearest-actor / enemy report."""
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
    out.append(m.get("payload") if m.get("type") == "send" else {"_error": json.dumps(m)[:300]})
s = frida.attach(pid)
sc = s.create_script(open(LIVE + r"\frida_scan.js", encoding="utf-8").read())
sc.on("message", on_message)
sc.load()
time.sleep(float(sys.argv[1]) if len(sys.argv) > 1 else 30.0)
try: sc.unload()
except Exception: pass
try: s.detach()
except Exception: pass
for p in out:
    if "pawnLoc" not in p:
        print(json.dumps(p)[:800]); continue
    print("objects=%s actors=%s pawn=%s loc=%s" % (p.get("objects"), p.get("actors"), p.get("pawn"), p.get("pawnLoc")))
    print("-- nearest --")
    for a in p.get("nearest", []):
        print("  %6s  %-36s %s" % (a["dist"], a["cls"], a["addr"]))
    print("-- enemy-ish actors (_C) --")
    for a in p.get("enemies", []):
        print("  %6s  %-36s %s  loc=%s" % (a["dist"], a["cls"], a["addr"], a["loc"]))
    print("-- same level (|dz|<220, <6000) --")
    for a in p.get("sameLevel", []):
        print("  %6s  %-36s %s  loc=%s" % (a["dist"], a["cls"], a["addr"], a["loc"]))
    open(LIVE + r"\scan.json", "w", encoding="utf-8").write(json.dumps(p, indent=1))
