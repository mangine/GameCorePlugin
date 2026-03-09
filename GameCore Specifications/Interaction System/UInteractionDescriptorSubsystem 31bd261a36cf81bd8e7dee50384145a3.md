# UInteractionDescriptorSubsystem

**Sub-page of:** [Interaction System](../Interaction%20System%20317d261a36cf8196ae77fc3c2e1e352d.md)

`UInteractionDescriptorSubsystem` is a **`UGameInstanceSubsystem`** that owns the shared descriptor cache. It guarantees exactly one `UInteractionUIDescriptor` instance per class, regardless of how many actors or interaction components reference that class.

This is the sole allocation site for descriptor objects. Nothing else calls `NewObject<UInteractionUIDescriptor>` directly.

**Files:** `Interaction/InteractionDescriptorSubsystem.h / .cpp`

---

# Design Rationale

Without a shared cache, every call to `ResolveOptions` that references a descriptor class would allocate a new instance — or worse, every `UInteractionComponent` would hold its own instance. In an MMORPG with hundreds of actors sharing the same entry config (e.g. every NPC using `UNPCDescriptor`), this produces hundreds of identical UObject instances under GC pressure.

`UGameInstanceSubsystem` is the right lifetime scope: it lives for the duration of the game session, survives level transitions, and is accessible from any game code via `GetGameInstance()->GetSubsystem<UInteractionDescriptorSubsystem>()`. It requires no manual setup — UE instantiates it automatically.

---

# Class Definition

```cpp
UCLASS()
class GAMECORE_API UInteractionDescriptorSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // Returns the shared descriptor instance for the given class.
    // Creates and caches it on first call. Subsequent calls return the cached instance.
    // Returns null if Class is null.
    UFUNCTION(BlueprintCallable, Category = "Interaction")
    UInteractionUIDescriptor* GetOrCreate(TSubclassOf<UInteractionUIDescriptor> Class);

    // Explicitly remove a cached descriptor, forcing re-creation on next GetOrCreate.
    // Rarely needed — intended for hot-reload and editor utility use only.
    void Invalidate(TSubclassOf<UInteractionUIDescriptor> Class);

    // Clear the entire cache. Called automatically on subsystem Deinitialize.
    void ClearAll();

protected:
    virtual void Deinitialize() override;

private:
    // One entry per descriptor class. Outer is this subsystem (GC root).
    UPROPERTY()
    TMap<TSubclassOf<UInteractionUIDescriptor>, TObjectPtr<UInteractionUIDescriptor>> Cache;
};
```

---

# Implementation

```cpp
UInteractionUIDescriptor* UInteractionDescriptorSubsystem::GetOrCreate(
    TSubclassOf<UInteractionUIDescriptor> Class)
{
    if (!Class) return nullptr;

    if (TObjectPtr<UInteractionUIDescriptor>* Found = Cache.Find(Class))
        return *Found;

    UInteractionUIDescriptor* New = NewObject<UInteractionUIDescriptor>(this, Class);
    Cache.Add(Class, New);
    return New;
}

void UInteractionDescriptorSubsystem::Invalidate(
    TSubclassOf<UInteractionUIDescriptor> Class)
{
    Cache.Remove(Class);
}

void UInteractionDescriptorSubsystem::ClearAll()
{
    Cache.Empty();
}

void UInteractionDescriptorSubsystem::Deinitialize()
{
    ClearAll();
    Super::Deinitialize();
}
```

> **GC safety.** The `Cache` map is a `UPROPERTY`, so all cached instances are rooted through this subsystem and will not be garbage-collected while the game instance is alive. Using `this` as the `Outer` in `NewObject` also ensures UE’s object hierarchy is well-formed.
> 

---

# Usage in `UInteractionManagerComponent`

`ResolveOptions` is the only call site that accesses this subsystem. The subsystem is cached at `BeginPlay` to avoid repeated `GetSubsystem` calls on the resolve path:

```cpp
// UInteractionManagerComponent — BeginPlay:
DescriptorSubsystem = GetGameInstance()->GetSubsystem<UInteractionDescriptorSubsystem>();

// UInteractionManagerComponent — during ResolveOptions, per option:
if (Config->UIDescriptorClass && DescriptorSubsystem)
    Option.UIDescriptor = DescriptorSubsystem->GetOrCreate(Config->UIDescriptorClass);
```

Caching the subsystem pointer at `BeginPlay` means `ResolveOptions` pays zero subsystem-lookup overhead on the hot path.

---

# Memory Profile

| Scenario | Descriptor instances |
| --- | --- |
| 300 NPCs, all using `UNPCDescriptor` | **1** |
| 200 chests, all using `UChestDescriptor` | **1** |
| 50 helms using `UHelmDescriptor`, 50 using `UEliteHelmDescriptor` | **2** |
| N actors, K distinct descriptor classes | **K** |

---

# Known Constraints

- **Descriptor instances are created lazily** — only when first needed. There is no pre-warming step. For predictable memory layout, call `GetOrCreate` for known classes during loading if required.
- **Descriptors must be stateless.** The shared instance contract is broken if a descriptor stores per-actor or per-frame data on itself. See `UInteractionUIDescriptor` for the full stateless requirement.
- **Not thread-safe.** `ResolveOptions` runs on the game thread only — no synchronisation is needed. Do not call `GetOrCreate` from background threads.