# GameCore Core — Code Review

**Part of:** GameCore Plugin | **Module:** `GameCore` | **UE Version:** 5.7

---

## Overview

GameCore Core is intentionally minimal — two headers, no runtime objects beyond `UGroupProviderDelegates`. Both interfaces are structurally clean and sit correctly at the bottom of the dependency graph. The main risks are subtle design gaps rather than outright bugs.

---

## Issues Found

### 1. `GetSourceTag_Implementation` is pure virtual — incompatible with `BlueprintNativeEvent`

**Severity: Medium — compile-time issue in Blueprint subclasses**

`BlueprintNativeEvent` generates a `_Implementation` method that Blueprint classes are expected to override in the VM. Declaring it as `= 0` (pure virtual) means a Blueprint class that implements `ISourceIDInterface` and does not provide a C++ `_Implementation` body will fail to link.

For a server-only, C++-only interface this is acceptable in practice, but the `BlueprintType` / `BlueprintNativeEvent` declarations create a misleading contract. If Blueprint implementation is never intended, the interface should be `NotBlueprintType` and the function should be a plain `virtual ... = 0`.

**Recommendation:** Either:
- Remove `BlueprintType` and `BlueprintNativeEvent` specifiers, replace with a plain pure-virtual C++ method. Clean, honest, no VM overhead.
- Or provide a non-pure `_Implementation` that returns `FGameplayTag::EmptyTag` and add a non-shipping `ensureMsgf(false, ...)` so forgotten implementations are caught during development.

---

### 2. `UGroupProviderDelegates` delegates are not cleared on component uninitialization

**Severity: Low — potential dangling delegate in edge cases**

`TDelegate` members on `UGroupProviderDelegates` bind to `UObject` targets via `BindUObject`. If the bound target (e.g. a `UMyPartyComponent`) is destroyed before `UGroupProviderDelegates` and the delegate is executed, the call goes to a garbage object.

Unreal's `TDelegate` does not automatically invalidate when the bound `UObject` is GC'd (unlike `TWeakObjectPtr`). The window is narrow in practice (component teardown order is predictable), but it is still a latent hazard.

**Recommendation:** Either:
- Use `BindWeakLambda` or `BindUObject` with an explicit unbind in the bound object's `EndPlay`/destructor.
- Or add `UGroupProviderDelegates::BeginDestroy` to call `UnbindAll()` on all delegates, preventing execution after the component is pending kill.

---

### 3. `IGroupProvider` has no notification mechanism — callers must poll

**Severity: Design Gap — not a bug, but a known architectural limitation**

`IGroupProvider` is polling-only. Systems that need to react to group changes (e.g. scaling a tracker when a member joins mid-quest) must either poll on tick (expensive) or subscribe directly to the concrete group system (coupling).

The current mitigation is documented: "subscribe to group system events directly." This is correct, but it means `IGroupProvider` does not fully decouple consumers from the group system — they still need to know which events to subscribe to.

**Recommendation:** Consider adding an `OnGroupChanged` multicast delegate to `UGroupProviderDelegates` that the concrete group system fires when membership changes. This keeps the delegate component as the single binding point for consuming systems — they listen to the component, not the group system directly. This would be a non-breaking addition.

---

### 4. `GetGroupMembers` output-parameter design is correct but underdocumented at call sites

**Severity: Low — developer experience issue**

The spec correctly documents that `GetGroupMembers` output is "valid for the current frame only." However, this constraint is not enforceable at compile time, and the comment in the header alone is likely insufficient — developers unfamiliar with the interface will cache the array.

**Recommendation:** Add a `#if !UE_BUILD_SHIPPING` stale-read detection in `UGroupProviderDelegates::ForwardGetGroupMembers` that timestamps the output array (via a wrapper struct), or at minimum add a prominent `// DO NOT CACHE — valid this frame only` comment directly on the `TArray` member at the call site in the class header.

---

### 5. `GroupProvider.md` was an orphan in `GameCore Core/` — not linked from the main page

**Severity: Documentation only — no code impact**

The original `GameCore Core 31bd261a36cf81059b2bf7cbe0931438.md` only linked to `ISourceIDInterface`. `GroupProvider.md` existed in the `GameCore Core/` folder but was not referenced from the parent page, making it invisible to anyone navigating the spec from the top.

**Resolution:** Both interfaces are documented in this spec revision. The Architecture and Usage pages cover both explicitly.

---

## Positive Notes

- The `Source.*` tag hierarchy convention is well-defined and consistent with the rest of the plugin's tag usage.
- `UGroupProviderDelegates` safe fallbacks (size = 1, leader = false, members = self) are correct — they enable solo behavior before any group system is wired up, which matches the phased integration model.
- Both interfaces are correctly header-only — no unnecessary `.cpp` translation units.
- The `const_cast<AMyPlayerState*>(this)` in the fallback `ForwardGetGroupMembers` is unavoidable given `GetGroupMembers` takes a non-const `TArray&`, but it is contained and not a broader pattern.
