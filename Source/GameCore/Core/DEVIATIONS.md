# GameCore Core — Deviations from Spec

## SourceIDInterface.h
No deviations from spec.

## GroupProvider.h
One addition beyond spec: `#include "GameFramework/PlayerState.h"` was added at the top of
`GroupProvider.h`. The spec states the file is header-only and that `ForwardGetGroupMembers`
uses `GetOwner<APlayerState>()`. Without the include, `APlayerState` is an incomplete type
at the point of the inline `GetOwner<APlayerState>()` call, causing a compile error.
The spec's Critical Rules explicitly call out this include as required (rule 6), so this is
a spec-directed addition, not a true deviation.
