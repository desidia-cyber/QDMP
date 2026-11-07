import sys
p = r"C:\Users\ALVIBO\AppData\Local\QueensDomain\Saved\SaveGames\World0.sav"
a = int(sys.argv[1], 0) if len(sys.argv) > 1 else 1100
b = int(sys.argv[2], 0) if len(sys.argv) > 2 else 1420
d = open(p, "rb").read()
for i in range(a, min(b, len(d)), 16):
    c = d[i:i + 16]
    hexs = " ".join("%02x" % x for x in c).ljust(47)
    txt = "".join(chr(x) if 32 <= x < 127 else "." for x in c)
    print("%05x  %s  %s" % (i, hexs, txt))
