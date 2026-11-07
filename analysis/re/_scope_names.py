"""Identify engine pause internals by their own DECLARE_SCOPE_CYCLE_COUNTER literals.

The build retains scope-counter name strings (prior work proved this with
'UWorld::InitializeActorsForPlay' @ 0x03F05B00). A function that carries a
'<Class>::<Func>' literal is that function. This is a much stronger instrument
than guessing from field offsets.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import qdlib as q

try:
    sys.path.insert(0, HERE)
    import hunt
except Exception:
    hunt = None

CANDIDATES = [
    "UWorld::IsPaused",
    "UWorld::SetPause",
    "UWorld::Tick",
    "UWorld::GetWorldSettings",
    "AWorldSettings::SetPauserPlayerState",
    "AWorldSettings::SetTimeDilation",
    "AWorldSettings::GetEffectiveTimeDilation",
    "APlayerController::SetPause",
    "APlayerController::IsPaused",
    "APlayerController::ServerPause",
    "UGameplayStatsics::SetGamePaused",
    "UGameplayStatics::SetGamePaused",
    "UGameplayStatics::IsGamePaused",
    "UGameplayStatics::SetGlobalTimeDilation",
    "UGameplayStatics::GetGlobalTimeDilation",
    "AActor::SetActorTickEnabled",
    "AActor::SetTickableWhenPaused",
    "UActorComponent::SetComponentTickEnabled",
]


def main():
    print("string probe (exact):")
    for s in CANDIDATES:
        hits = q.find_string(s, exact=True, limit=4)
        print("  %-46s %s" % (s, ["%08X" % h[0] for h in hits] or "-"))

    print()
    print("string probe (substring 'Pause' in scope-literal shape):")
    for s in ["SetPauserPlayerState", "IsPaused", "SetTimeDilation", "SetGlobalTimeDilation"]:
        hits = q.find_string(s, exact=False, limit=25)
        print("  %s ->" % s)
        for rva, text in hits:
            print("     %08X  %r" % (rva, text[:90]))
        if not hits:
            print("     (none)")


if __name__ == "__main__":
    main()
