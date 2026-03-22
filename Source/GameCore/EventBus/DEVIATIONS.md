# Event Bus System — Deviations from Spec

## D1 — `SubscribeTagIfNeeded`: removed erroneous `this` parameter from `Bus->StartListening`

**Spec (UGameCoreEventWatcher.md):**
```cpp
FGameplayMessageListenerHandle BusHandle = Bus->StartListening(
    Tag, this,
    [this, Tag](FGameplayTag InTag, const FInstancedStruct& Payload)
    { OnBusEvent(InTag, Payload); });
```

**Implemented as:**
```cpp
FGameplayMessageListenerHandle BusHandle = Bus->StartListening(
    Tag,
    [this, Tag](FGameplayTag InTag, const FInstancedStruct& Payload)
    { OnBusEvent(InTag, Payload); });
```

**Reason:** `UGameCoreEventBus::StartListening` (raw overload) accepts exactly two parameters: `FGameplayTag Channel` and `TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback`. The `this` argument in the spec is not a valid parameter for that signature and would produce a compile error. The instruction document for this task explicitly identifies this as a spec error and directs the fix. The lambda correctly captures `this` to call `OnBusEvent`.

---

## D2 — `GameCoreEventTags.cpp`: tags registered via static initializer instead of `FGameCoreModule::StartupModule`

**Spec (GameCore Event Messages.md / Usage.md):**
Registration described as occurring inside `FGameCoreModule::StartupModule()`.

**Implemented as:**
A file-scope `static FGameCoreEventTagsRegistrar` struct with a constructor that calls `AddNativeGameplayTag` for all five tags.

**Reason:** `GameCore.cpp` (the existing module file) explicitly states:
> "Individual systems register their native gameplay tags from their own .cpp files."

It does not call into `GameCoreEventTags`. A static initializer in `GameCoreEventTags.cpp` is consistent with that per-system pattern and avoids modifying `GameCore.cpp`. The registration timing is equivalent — both approaches run before any gameplay code executes.

---

## D3 — `#include "Engine/Engine.h"` added to `.cpp` files

**Spec:** Not mentioned.

**Implemented:** Added `#include "Engine/Engine.h"` to `GameCoreEventBus.cpp` and `GameCoreEventWatcher.cpp`.

**Reason:** Both `.cpp` files reference `GEngine` (for `GEngine->GetWorldFromContextObject`). This global is declared in `Engine/Engine.h`. Without it the translation unit will fail to compile. `CoreMinimal.h` does not pull in `GEngine`.

---

## No Other Deviations

All class declarations, method signatures, member layouts, scope logic, re-entrancy snapshot pattern, lazy subscribe/unsubscribe, handle semantics, and template implementations match the spec exactly.
