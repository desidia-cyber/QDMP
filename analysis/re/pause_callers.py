"""Callers of the pause/time-dilation writers — the direct answer to 'what pauses?'."""
import sys, marshal
sys.path.insert(0, r"G:\Freespace\Queens_Domain\analysis\re")
import qdlib as q
from hunt2 import callers, calls

TARGETS = {
    "UGameplayStatics::SetGamePaused": 0x028F3180,
    "UGameplayStatics::SetGlobalTimeDilation": 0x028F3410,
    "UGameplayStatics::IsGamePaused(impl)": 0x028E8DB0,
    "UWorld::IsPaused?(0x29A43C0)": 0x29A43C0,
    "AActor::SetActorTickEnabled": 0x026636F0,
    "AActor::SetTickableWhenPaused": 0x02664C00,
    "AActor::SetTickGroup": 0x02664BF0,
    "UGameplayStatics::GetGlobalTimeDilation": 0x028DF810,
    "UGameplayStatics::GetUnpausedTimeSeconds": 0x028E1AF0,
}
for name, rva in TARGETS.items():
    cs = callers(rva)
    print("\n=== %s @ %08X : %d callers ===" % (name, rva, len(cs)))
    for site, func, mn in cs[:40]:
        print("   %08X in FUN_%08X  %s" % (site, func, mn))
        # show a couple of instructions around the call site
        ins = q.disasm(func, count=200)
        idx = next((i for i, x in enumerate(ins) if x.address == site), None)
        if idx is not None:
            for x in ins[max(0, idx - 3):idx + 2]:
                print("        %08X %-8s %-40s" % (x.address, x.mnemonic, x.op_str))

print("\n=== UWorld::IsPaused? 0x29A43C0 (impl) ===")
for ins in q.disasm(0x29A43C0, count=25):
    print("  %08X %-9s %-44s" % (ins.address, ins.mnemonic, ins.op_str))

print("\n=== SetGamePaused impl 0x028F3180 ===")
for ins in q.disasm(0x028F3180, count=40):
    note = ""
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        note = " -> %08X" % (ins.operands[0].imm & 0xFFFFFFFF)
    print("  %08X %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))

print("\n=== SetGlobalTimeDilation impl 0x028F3410 ===")
for ins in q.disasm(0x028F3410, count=45):
    note = ""
    if ins.mnemonic in ("call", "jmp") and ins.operands and ins.operands[0].type == 2:
        note = " -> %08X" % (ins.operands[0].imm & 0xFFFFFFFF)
    print("  %08X %-9s %-44s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
