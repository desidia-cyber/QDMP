#!/usr/bin/env python3
"""Second world-interact wiring pass (run while holding the BUILD lock).

Three targeted edits after the first build:
  1. keep `ObjectCallStep` silent when a class does not implement a plan step (a
     secret wall does not have `SetToOpenState`; `Fn::Accept` would log a WARN per
     apply);
  2. make the object lane's `tracking ... visual=` line name the NEW plan's first
     step (a pot prints `Break`, not `none`);
  3. the breakable state probe for the Pot family is `Broken`, not `bBroken`.
"""
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
PROXY = os.path.join(ROOT, "mod", "native", "src", "proxy.cpp")
TEST = os.path.join(ROOT, "mod", "native", "test", "test_interact.cpp")

EDITS = [
    (PROXY,
"""    Fn f;
    bool resolved = false;
    __try { resolved = f.Resolve(actor, step.fn); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ErrSet(why, cap, "fault resolving ", step.fn); return -1; }
    if (!resolved) return 0;
""",
"""    // Absence is normal: a class implements one visual of a family and not the others,
    // so a miss is silent.  `Fn::Accept` logs a WARN for a miss, which would be
    // per-apply noise on every class that does not implement the first plan step.
    void* present = nullptr;
    __try { present = FindFunction(actor, step.fn); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ErrSet(why, cap, "fault resolving ", step.fn); return -1; }
    if (!present) return 0;
    Fn f;
    bool resolved = false;
    __try { resolved = f.Resolve(actor, step.fn); }
    __except (EXCEPTION_EXECUTE_HANDLER) { ErrSet(why, cap, "fault resolving ", step.fn); return -1; }
    if (!resolved) return 0;
""",
     "silent absent plan step"),
    (PROXY,
"""        const char* vis = objects::VisualFnFor(sh);
""",
"""        interact::Step vsteps[8];
        int vn = interact::Plan(sh, 1, vsteps, 8);
        const char* vis = (vn > 0) ? vsteps[0].fn : nullptr;
""",
     "tracking log uses the plan"),
    (TEST,
'    PropIs(objects::SH_BARRICADE, "bBroken", false, 1);\n',
'    PropIs(objects::SH_BARRICADE, "Broken", false, 1);\n',
     "pot state probe is Broken"),
]


def main():
    pending = {}
    for path, old, new, label in EDITS:
        if path not in pending:
            with open(path, "r", encoding="utf-8", newline="") as f:
                pending[path] = f.read()
        text = pending[path]
        eol = "\r\n" if "\r\n" in text else "\n"
        c = text.count(old.replace("\n", eol))
        if c != 1:
            print("ANCHOR %-32s count=%d (must be 1) in %s" % (label, c, path))
            return 2
    for path in pending:
        text = pending[path]
        eol = "\r\n" if "\r\n" in text else "\n"
        for p2, old, new, label in EDITS:
            if p2 != path:
                continue
            text = text.replace(old.replace("\n", eol), new.replace("\n", eol), 1)
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        print("patched", os.path.basename(path))
    return 0


if __name__ == "__main__":
    sys.exit(main())
