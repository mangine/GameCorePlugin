# AHISMProxyHostActor

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`AHISMProxyHostActor` is the **primary level-placed actor** of the HISM Proxy Actor System. It is the single point of configuration and the editor entry point for building HISM proxy setups. Developers never manually create HISM components, bridge components, or wire delegates — the host actor manages all of that automatically based on its `InstanceTypes` array.

**Files:** `HISMProxy/HISMProxyHostActor.h / .cpp`

---

## `FHISMProxyInstanceType`

Each entry in `AHISMProxyHostActor::InstanceTypes` represents one mesh type. Adding an entry in the editor automatically creates a paired HISM component and bridge component on the host actor.

```cpp
// Defined in HISMProxyInstanceType.h — included by HISMProxyHostActor.h
USTRUCT(BlueprintType)
struct GAMECORE_API FHISMProxyInstanceType
{
    GENERATED_BODY()

    // ── Identity ──────────────────────────────────────────────────────────────

    // Human-readable name used for component naming and editor display.
    // Becomes the suffix in "HISM_<TypeName>" and "Bridge_<TypeName>".
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName TypeName = NAME_None;

    // ── Mesh ──────────────────────────────────────────────────────────────────

    // The static mesh rendered for all instances of this type.
    // Changing this at runtime is not supported — treat as editor-only.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    // ── Proxy ──────────────────────────────────────────────────────────────────

    // Blueprint subclass of AHISMProxyActor to use for this type.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass = nullptr;

    // Shared proximity and timing config.
    // Multiple types can share the same config asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyConfig> Config = nullptr;

    // ── Pool Sizing ────────────────────────────────────────────────────────────

    // Actors pre-allocated at BeginPlay. This covers typical concurrent load.
    // Pool grows beyond this at runtime if exhausted (up to MaxPoolSize).
    // Formula: MinPoolSize = PI * (ActivationRadius/100)^2 * InstanceDensity
    //                        * ExpectedConcurrentPlayers * 1.2
    // where InstanceDensity = TotalInstances / AreaM².
    // Example: 500 oaks / 40000m², radius=15m → ~8.8/player → 64 players → ~676.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool",
              meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    // Hard cap on pool growth. Growth beyond this logs an error and stops.
    // Set to 0 to disable growth (strict pre-allocation only).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool",
              meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    // Number of actors spawned per growth step when the pool is exhausted.
    // Growth is a one-time spike — size MinPoolSize correctly to avoid it.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool",
              meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Runtime (auto-managed, do not edit) ───────────────────────────────────

    // Created automatically when this entry is added. Named "HISM_<TypeName>".
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM = nullptr;

    // Created automatically when this entry is added. Named "Bridge_<TypeName>".
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyBridgeComponent> Bridge = nullptr;

    // Position-derived type index. Written to PerInstanceCustomData[1].
    // Always derived from array index — never trusted from serialized storage.
    UPROPERTY(VisibleAnywhere)
    int32 TypeIndex = INDEX_NONE;
};
```

---

## Class Definition

```cpp
UCLASS(Blueprintable)
class GAMECORE_API AHISMProxyHostActor : public AActor
{
    GENERATED_BODY()
public:
    AHISMProxyHostActor();

    UPROPERTY(EditAnywhere, Category = "HISM Proxy",
              meta = (TitleProperty = "TypeName"))
    TArray<FHISMProxyInstanceType> InstanceTypes;

#if WITH_EDITOR
    // Adds one instance to the HISM for TypeIndex at the given world transform.
    // Called by the Details panel and the foliage converter.
    void AddInstanceForType(int32 TypeIndex, const FTransform& WorldTransform);

    // Validates all entries and writes results to the Message Log.
    void ValidateSetup() const;

    // Returns the TypeIndex whose Mesh matches. INDEX_NONE if not found.
    int32 FindTypeIndexByMesh(const UStaticMesh* Mesh) const;

    virtual void PostEditChangeProperty(
        FPropertyChangedEvent& PropertyChangedEvent) override;

    // Called after load and on construction — ensures type indices are always
    // consistent with array position, regardless of serialized TypeIndex values.
    virtual void PostLoad() override;
    virtual void OnConstruction(const FTransform& Transform) override;
#endif

protected:
    virtual void BeginPlay() override;

private:
#if WITH_EDITOR
    void CreateComponentsForEntry(FHISMProxyInstanceType& Entry, int32 EntryIndex);
    void DestroyOrphanedComponents();
    void RebuildTypeIndices();
#endif
};
```

---

## `PostEditChangeProperty` — Orphan-Safe Reconciliation

This is the core editor mechanism. The critical fix over the naive approach: we detect **removed entries** by comparing the set of component names that *should* exist against those that *do* exist, and destroy orphans.

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName MemberName = PropertyChangedEvent.MemberProperty
        ? PropertyChangedEvent.MemberProperty->GetFName() : NAME_None;

    if (MemberName != GET_MEMBER_NAME_CHECKED(AHISMProxyHostActor, InstanceTypes))
        return;

    // Step 1 — create components for new entries (HISM == nullptr).
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];
        if (Entry.HISM == nullptr)
            CreateComponentsForEntry(Entry, i);
    }

    // Step 2 — destroy components for removed entries.
    // An entry is considered removed when no InstanceTypes entry references
    // the component anymore. We identify managed components by name prefix.
    DestroyOrphanedComponents();

    // Step 3 — always rebuild type indices after any structural change.
    // This corrects PerInstanceCustomData[1] for all existing instances.
    RebuildTypeIndices();

    MarkPackageDirty();
}

void AHISMProxyHostActor::DestroyOrphanedComponents()
{
    // Build the set of HISM component pointers that are currently live in InstanceTypes.
    TSet<UActorComponent*> LiveComponents;
    for (const FHISMProxyInstanceType& Entry : InstanceTypes)
    {
        if (Entry.HISM)   LiveComponents.Add(Entry.HISM);
        if (Entry.Bridge) LiveComponents.Add(Entry.Bridge);
    }

    // Collect components on this actor that were created by this system
    // (identified by name prefix) but are no longer referenced by any entry.
    TArray<UActorComponent*> ToDestroy;
    for (UActorComponent* Comp : GetComponents())
    {
        if (!Comp) { continue; }
        const FString CompName = Comp->GetName();
        const bool bIsManagedHISM   = CompName.StartsWith(TEXT("HISM_"));
        const bool bIsManagedBridge = CompName.StartsWith(TEXT("Bridge_"));

        if ((bIsManagedHISM || bIsManagedBridge) && !LiveComponents.Contains(Comp))
            ToDestroy.Add(Comp);
    }

    for (UActorComponent* Comp : ToDestroy)
    {
        Comp->DestroyComponent();
    }
}

void AHISMProxyHostActor::CreateComponentsForEntry(
    FHISMProxyInstanceType& Entry, int32 EntryIndex)
{
    // ── Create HISM component ────────────────────────────────────────────────
    const FName HISMName = *FString::Printf(TEXT("HISM_%s"), *Entry.TypeName.ToString());
    UHierarchicalInstancedStaticMeshComponent* NewHISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), HISMName,
            RF_Transactional);

    NewHISM->SetStaticMesh(Entry.Mesh);
    // Slot 0: hide flag. Slot 1: type index. Game custom data starts at slot 2.
    NewHISM->NumCustomDataFloats = 2;
    // HISM itself has no gameplay collision — proxies handle that.
    NewHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(
        GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

    Entry.HISM = NewHISM;

    // ── Create Bridge component ──────────────────────────────────────────────
    const FName BridgeName = *FString::Printf(TEXT("Bridge_%s"), *Entry.TypeName.ToString());
    UHISMProxyBridgeComponent* NewBridge =
        NewObject<UHISMProxyBridgeComponent>(
            this, UHISMProxyBridgeComponent::StaticClass(), BridgeName,
            RF_Transactional);

    // Wire bridge to its HISM explicitly — never FindComponentByClass.
    NewBridge->TargetHISM      = NewHISM;
    NewBridge->Config          = Entry.Config;
    NewBridge->MinPoolSize     = Entry.MinPoolSize;
    NewBridge->MaxPoolSize     = Entry.MaxPoolSize;
    NewBridge->GrowthBatchSize = Entry.GrowthBatchSize;
    NewBridge->ProxyClass      = Entry.ProxyClass;

    NewBridge->RegisterComponent();

    Entry.Bridge    = NewBridge;
    Entry.TypeIndex = EntryIndex;
}

void AHISMProxyHostActor::RebuildTypeIndices()
{
    // TypeIndex is always position-derived. Never trust the serialized value.
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        InstanceTypes[i].TypeIndex = i;

        if (!InstanceTypes[i].HISM) { continue; }

        // Rewrite PerInstanceCustomData[1] for every instance of this entry.
        // This corrects stale indices caused by entry reordering or level reload.
        const int32 NumInstances = InstanceTypes[i].HISM->GetInstanceCount();
        for (int32 j = 0; j < NumInstances; ++j)
        {
            InstanceTypes[i].HISM->SetCustomDataValue(
                j, /*CustomDataIndex=*/1, static_cast<float>(i), /*bMarkRenderStateDirty=*/false);
        }
        InstanceTypes[i].HISM->MarkRenderStateDirty();
    }
}

// PostLoad and OnConstruction both call RebuildTypeIndices so that type indices
// are always correct regardless of when or how the actor was loaded.
// This eliminates the stale-index-after-reload bug (Critical Issue #3).
void AHISMProxyHostActor::PostLoad()
{
    Super::PostLoad();
#if WITH_EDITOR
    RebuildTypeIndices();
#endif
}

void AHISMProxyHostActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
#if WITH_EDITOR
    RebuildTypeIndices();
#endif
}
#endif // WITH_EDITOR
```

---

## `AddInstanceForType`

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::AddInstanceForType(
    int32 TypeIndex, const FTransform& WorldTransform)
{
    if (!InstanceTypes.IsValidIndex(TypeIndex)) { return; }
    FHISMProxyInstanceType& Entry = InstanceTypes[TypeIndex];
    if (!Entry.HISM)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("AddInstanceForType: entry %d has no HISM component."), TypeIndex);
        return;
    }

    // HISM AddInstance with bWorldSpace=true — no manual transform conversion needed.
    const int32 NewIdx = Entry.HISM->AddInstance(WorldTransform, /*bWorldSpace=*/true);

    Entry.HISM->SetCustomDataValue(NewIdx, 0, 0.f,              /*bMarkRenderStateDirty=*/false);
    Entry.HISM->SetCustomDataValue(NewIdx, 1, (float)TypeIndex, /*bMarkRenderStateDirty=*/false);
    Entry.HISM->MarkRenderStateDirty();

    MarkPackageDirty();
}

int32 AHISMProxyHostActor::FindTypeIndexByMesh(const UStaticMesh* Mesh) const
{
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        if (InstanceTypes[i].Mesh == Mesh) { return i; }
    }
    return INDEX_NONE;
}
#endif
```

---

## `ValidateSetup`

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::ValidateSetup() const
{
    FMessageLog Log("HISMProxyValidation");

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = InstanceTypes[i];
        const FString Prefix = FString::Printf(TEXT("[%s]"), *Entry.TypeName.ToString());

        if (Entry.TypeName == NAME_None)
            Log.Error(FText::FromString(FString::Printf(
                TEXT("Entry %d has no TypeName."), i)));
        if (!Entry.Mesh)
            Log.Error(FText::FromString(Prefix + TEXT(" Mesh is null.")));
        if (!Entry.ProxyClass)
            Log.Error(FText::FromString(Prefix + TEXT(" ProxyClass is null.")));
        if (!Entry.Config)
            Log.Error(FText::FromString(Prefix + TEXT(" Config is null.")));
        if (!Entry.HISM)
            Log.Error(FText::FromString(Prefix + TEXT(" HISM component missing — resave the actor.")));
        else
        {
            if (Entry.HISM->NumCustomDataFloats < 2)
                Log.Error(FText::FromString(Prefix +
                    TEXT(" NumCustomDataFloats < 2. Hide flag and type index will not work.")));
            if (Entry.HISM->GetInstanceCount() == 0)
                Log.Warning(FText::FromString(Prefix + TEXT(" Has 0 instances.")));
        }
        if (!Entry.Bridge)
            Log.Error(FText::FromString(Prefix + TEXT(" Bridge component missing.")));
        if (Entry.MinPoolSize < 1)
            Log.Error(FText::FromString(Prefix + TEXT(" MinPoolSize must be >= 1.")));
        if (Entry.MaxPoolSize > 0 && Entry.MaxPoolSize < Entry.MinPoolSize)
            Log.Error(FText::FromString(Prefix +
                TEXT(" MaxPoolSize is less than MinPoolSize.")));
    }

    Log.Open(EMessageSeverity::Info, /*bOpenLog=*/true);
}
#endif
```

---

## `BeginPlay` — Runtime Validation

```cpp
void AHISMProxyHostActor::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority()) { return; }

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = InstanceTypes[i];

        if (!Entry.HISM)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): HISM component is null. "
                     "This entry will not function. Was PostEditChangeProperty run in the editor?"),
                *GetName(), i, *Entry.TypeName.ToString());
            continue;
        }
        if (!Entry.Bridge)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge component is null."),
                *GetName(), i, *Entry.TypeName.ToString());
            continue;
        }
        if (!Entry.Bridge->TargetHISM)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.TargetHISM is null. "
                     "The bridge will not activate any proxies."),
                *GetName(), i, *Entry.TypeName.ToString());
        }
        if (!Entry.Bridge->Config)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.Config is null."),
                *GetName(), i, *Entry.TypeName.ToString());
        }
        if (!Entry.Bridge->ProxyClass)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.ProxyClass is null."),
                *GetName(), i, *Entry.TypeName.ToString());
        }

        UE_LOG(LogGameCore, Verbose,
            TEXT("AHISMProxyHostActor [%s] entry %d (%s): OK — %d instances, pool %d-%d."),
            *GetName(), i, *Entry.TypeName.ToString(),
            Entry.HISM->GetInstanceCount(),
            Entry.MinPoolSize, Entry.MaxPoolSize);
    }
}
```

---

## Pool Sizing Guide

Size `MinPoolSize` to cover worst-case concurrent proximity without any runtime growth:

```
InstanceDensity  = TotalInstances / AreaM²
RadiusM          = ActivationRadius / 100.0
MaxPerPlayer     = PI * RadiusM² * InstanceDensity
MinPoolSize      = ceil(MaxPerPlayer * ExpectedConcurrentPlayers * 1.2)
```

**Example — forest of 500 oaks over 200m × 200m, ActivationRadius=1500cm, 64 players:**
```
InstanceDensity = 500 / 40000 = 0.0125 /m²
MaxPerPlayer    = PI * 15² * 0.0125 ≈ 8.8
MinPoolSize     = ceil(8.8 * 64 * 1.2) = ceil(675) = 676
```

In practice players cluster, so `MinPoolSize = 300–400` is often sufficient for 64 players in this scenario. Set `MaxPoolSize` to the theoretical maximum (676) as the safety ceiling. If growth triggers in production, raise `MinPoolSize`.

**Pool growth is a safety net, not the primary path.** A growth event logs a warning. If warnings appear in production, the `MinPoolSize` is undersized.

---

## Material Function Recommendation

Instead of every artist manually wiring the `PerInstanceCustomData[0]` clip logic, create a shared **Material Function** `MF_HISMProxyHide`:

```
MF_HISMProxyHide inputs:  (none)
MF_HISMProxyHide outputs: (none — calls Clip internally)

Implementation:
  [PerInstanceCustomData]  Index=0, Default=0.0
       |
  [If] A >= 0.5 → -1 (discard) | else → 1 (render)
       |
  [Clip]
```

Place this function call in every HISM material's graph. No parameters needed. This eliminates the risk of incorrectly wiring the clip node or forgetting `bUsedWithInstancedStaticMeshes`.

Store the function at: `Content/GameCore/Materials/Functions/MF_HISMProxyHide.uasset`

---

## Notes

- **Do not add `UHISMProxyBridgeComponent` manually** to a host actor. The host actor owns creation. Manual addition creates unmanaged components that `DestroyOrphanedComponents` will destroy on the next `PostEditChangeProperty`.
- **Entry reordering is safe** — `RebuildTypeIndices` rewrites `PerInstanceCustomData[1]` for all instances, and it is called from both `PostEditChangeProperty` and `PostLoad`/`OnConstruction`.
- **Partial proxy state is lost by design** when a proxy deactivates. The `DeactivationDelay` timer gives game systems time to flush any sub-completion state to their own storage before `OnProxyDeactivated` fires. Game systems must not rely on the proxy actor retaining state across pool cycles.
- **Multiple host actors** per level are fully supported — one per logical prop cluster.
- **Blueprint subclass** is useful for binding `OnQueryInstanceEligibility` across all bridges from a single `BeginPlay`.
