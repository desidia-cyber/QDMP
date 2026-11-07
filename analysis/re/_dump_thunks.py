"""Validate exec-thunk -> implementation derivation for the pause surface.

The heuristic (`last direct call in the thunk`) is checked by printing the whole
thunk. UE4 native exec thunks have the shape

    mov rax, rdx            ; FFrame*
    mov [rsp+..], rcx       ; Context
    ...
    <fill params from the FFrame>
    call <impl>
    <store result, epilogue>

so the call that is followed immediately by the epilogue is the implementation.
Where more than one call exists, all of them are printed and the choice is shown.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q

THUNKS = [
    ("execSetGamePaused", 0x02DC3580),
    ("execIsGamePaused", 0x02DC07B0),
    ("execSetGlobalTimeDilation", 0x02DC39F0),
    ("execSetActorTickEnabled", 0x02D58C10),
    ("execGetActorTimeDilation", 0x02D54EA0),
    ("execSetComponentTickEnabled", 0x02D5C8B0),
]

HELPERS = [
    ("UWorld::GetWorldSettings?", 0x02D37250),
    ("UWorld::InternalGetNetMode", 0x02D39CF0),
]


def dump(rva, label, maxn=120):
    f = q.func_at(rva)
    if f is None:
        print("%-34s %08X  (no .pdata)" % (label, rva))
        end = rva + 0x80
    else:
        print("%-34s %08X  [%08X..%08X) %d bytes" % (label, rva, f[0], f[1], f[1] - f[0]))
        end = f[1]
    n = 0
    for ins in q.disasm(rva, end_rva=end):
        mark = ""
        if ins.mnemonic in ("call", "jmp"):
            t = q.rel_target(ins)
            if t is not None:
                mark = "   -> %08X" % t
        print("    %08X  %-9s %-40s%s" % (ins.address, ins.mnemonic, ins.op_str, mark))
        n += 1
        if n >= maxn:
            print("    ...")
            break
    print()


def main():
    for label, rva in THUNKS:
        dump(rva, label)
    print("=" * 78)
    for label, rva in HELPERS:
        dump(rva, label, maxn=40)


if __name__ == "__main__":
    main()
