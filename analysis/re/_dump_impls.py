"""Disassemble the pause/dilation implementation chain and follow it one level.

Also resolves the world/pawn accessor the impls use, and the UWorld::IsPaused tail
target, so the pause-flag write can be located.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q

TARGETS = [
    ("UGameplayStatics::SetGamePaused", 0x028F3180),
    ("UGameplayStatics::IsGamePaused", 0x028E8DB0),
    ("UGameplayStatics::SetGlobalTimeDilation", 0x028F3410),
    ("UGameplayStatics::GetGlobalTimeDilation", 0x028DF810),
    ("UGameplayStatics::GetWorldDeltaSeconds", 0x028E1CE0),
    ("UGameplayStatics::GetUnpausedTimeSeconds", 0x028E1AF0),
    ("UGameplayStatics::GetRealTimeSeconds", 0x028E0850),
    ("AActor::GetActorTimeDilation", 0x02652E30),
    ("AActor::SetActorTickEnabled", 0x026636F0),
]


def dump(rva, label, maxn=200):
    f = q.func_at(rva)
    if f is None:
        print("%-42s %08X  (no .pdata bound) -> linear 40 insns" % (label, rva))
        end = rva + 0x100
    else:
        print("%-42s %08X  [%08X..%08X)  %d bytes" % (label, rva, f[0], f[1], f[1] - f[0]))
        end = f[1]
    n = 0
    for ins in q.disasm(rva, end_rva=end):
        print("    %08X  %-9s %s" % (ins.address, ins.mnemonic, ins.op_str))
        n += 1
        if n >= maxn:
            print("    ...")
            break
    print()


def main():
    for label, rva in TARGETS:
        dump(rva, label)
    print("=" * 78)
    print("tail target from IsGamePaused: 0x29A43C0")
    dump(0x29A43C0, "UWorld::IsPaused (hypothesis)", maxn=60)


if __name__ == "__main__":
    main()
