import sys, json; sys.path.insert(0,'.')
import pakread
pk=pakread.Pak()
d=pk.payload("QueensDomain/QueensDomain.uproject")
print("uproject bytes",len(d),"starts:",d[:60])
try:
    j=json.loads(d.decode('utf-8-sig')); print("valid JSON, keys:",list(j)[:8])
except Exception as e: print("JSON parse failed:",e)
# check pruned-dir names all present in full index
full={dn+n for dn,fl in pk.full_dirs for n,_ in fl}
pruned={dn+n for dn,fl in pk.pruned_dirs for n,_ in fl}
print("pruned names",len(pruned),"missing from full:",len(pruned-full), list(pruned-full)[:5])
