import sys, re, collections
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

pat = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_~][A-Za-z0-9_]*)+$")
found = []
for rva, t in iter_strings(minlen=6):
    if pat.match(t):
        found.append((rva, t))
print("scoped-name strings:", len(found))

groups = collections.Counter(t.split("::")[0] for _, t in found)
print("top scopes:", groups.most_common(20))

want_prefix = ("UObject::", "FName", "UWorld::", "UEngine::", "AActor::", "UGameplayStatics::",
               "UClass::", "UFunction::", "Static", "ULevel::", "APlayerController::", "UGameInstance::")
print()
for rva, t in found:
    if t.startswith(want_prefix):
        print("  %08X  %-58s refs=%s" % (rva, t, ["%08X" % s[0] for s in refs_to(rva)][:3]))
