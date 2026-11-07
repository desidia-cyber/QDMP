import sys, bisect
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt2 import *

F = functions(); starts = [x[0] for x in F]
i = bisect.bisect_left(starts, 0x02CE85B0)
print("entries before 0x02CE85B0:")
for k in range(max(0, i - 6), i + 1):
    b, e, u = F[k]
    print("   [%08X..%08X) size 0x%-6X unwind %08X" % (b, e, e - b, u))

print()
print("is 0x02CE85B0 a direct call target?", len(callers(0x02CE85B0)))
print("is 0x00774A70 (FEngineLoop::Init) a direct call target?", len(callers(0x00774A70)))
print("is 0x02D391C0 a direct call target?", len(callers(0x02D391C0)))

# how many pdata starts are also call targets?  => reliability metric
cs = set(calls().keys())
n_call_start = sum(1 for s in starts if s in cs)
print()
print("pdata starts: %d ; of those also call targets: %d (%.1f%%)"
      % (len(starts), n_call_start, 100.0 * n_call_start / len(starts)))

# check: does any pdata begin land inside another pdata function?
bad = 0
for k in range(1, len(F)):
    if F[k][0] < F[k-1][1]:
        bad += 1
print("overlapping pdata entries:", bad)
