# GameCore Tags — Deviations from Spec

## TaggedInterface.h
No deviations from spec.

## GameplayTagComponent.h / GameplayTagComponent.cpp
The spec presented `UGameplayTagComponent` as a single inline class definition. The
implementation splits it into a proper `.h` / `.cpp` pair (declaration in header,
definitions in source file) to follow UE5 project conventions and avoid link errors
from multiple translation units including the header. Method bodies for `OnRep_Tags`,
`AddGameplayTag_Implementation`, `RemoveGameplayTag_Implementation`, and
`SetGameplayTags` were moved to the `.cpp`. The `.h` retains the full inline
`GetGameplayTags()`, `HasGameplayTag_Implementation`, `HasAllGameplayTags_Implementation`,
and `HasAnyGameplayTags_Implementation` bodies, as these are hot-path and suitable for
inlining from the header.

`GetLifetimeReplicatedProps` declaration was added to the header (required by UE's
replication plumbing when the definition lives in the `.cpp`).

Required headers added to `GameplayTagComponent.cpp`:
- `#include "Net/UnrealNetwork.h"` — for `DOREPLIFETIME_WITH_PARAMS_FAST`
- `#include "Net/Core/PushModel/PushModel.h"` — for `MARK_PROPERTY_DIRTY_FROM_NAME`

Both are explicitly mandated by the spec's Critical Rules (rule 7).

## RequirementHasTag.h
`URequirement_HasTag` includes `Requirements/Requirement.h` as specified.
`Requirements/Requirement.h` exists in the repository at
`Source/GameCore/Requirements/Requirement.h`, so the include will resolve correctly.

`LOCTEXT` macros in the inline `Evaluate` body require a loctext namespace to be active
at the call site (via `#define LOCTEXT_NAMESPACE` or the class's `.cpp`). Since
`URequirement_HasTag` is currently header-only with inline body, the `LOCTEXT` calls will
resolve against whatever `LOCTEXT_NAMESPACE` the including translation unit defines.
When `RequirementHasTag.cpp` is introduced, the namespace should be defined there.
If compile warnings arise, move `Evaluate` to a `.cpp` and define `LOCTEXT_NAMESPACE`
appropriately.
