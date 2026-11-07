import sys, bisect
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *

print("bytes 0x02CE8560..0x02CE85D0:")
for a in range(0x02CE8560, 0x02CE85D0, 16):
    print("  %08X  %s" % (a, hexs(a, 16)))

F = functions()
starts = [x[0] for x in F]
i = bisect.bisect_left(starts, 0x02CE8400)
print()
print("pdata entries around 0x02CE85B0:")
for k in range(i, i + 6):
    b, e, u = F[k]
    print("   [%08X..%08X) unwind %08X  size 0x%X" % (b, e, u, e - b))

print()
print("pdata entries around 0x00774A70 (known good: FEngineLoop::Init):")
i = bisect.bisect_left(starts, 0x00774A70)
for k in range(i, i + 3):
    b, e, u = F[k]
    print("   [%08X..%08X) unwind %08X  size 0x%X" % (b, e, u, e - b))
