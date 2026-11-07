import sys
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
from hunt import *

for s in ["Create GEngine", "GEngine->ParseCommandline()", "GEngine->Init",
          "GEngine->Start()", "UWorld::InitializeActorsForPlay",
          "UEngine::InitializeObjectReferences"]:
    for rva, t in find_string(s, exact=True, limit=3):
        print("### %r @ %08X" % (t, rva))
        for site, func, mn in refs_to(rva):
            print("    site %08X  func %08X  %s" % (site, func, mn))
        print()
