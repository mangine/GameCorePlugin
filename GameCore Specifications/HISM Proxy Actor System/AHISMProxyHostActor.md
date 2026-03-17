# AHISMProxyHostActor

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

`AHISMProxyHostActor` is the **primary level-placed actor** of the HISM Proxy Actor System. It is the single point of configuration and the editor entry point. Developers never manually create HISM components, bridge components, or wire delegates — the host actor manages all of that automatically.

**Files:** `HISMProxy/HISMProxyHostActor.h / .cpp`

---

## `FHISMProxyInstanceType`

```cpp
USTRUCT(BlueprintType)
struct GAMECORE_API FHISMProxyInstanceType
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName TypeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    // Must be a concrete Blueprint subclass of AHISMProxyActor.
    // Must NOT be AHISMProxyActor::StaticClass() itself (validated by ValidateSetup).
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyConfig> Config = nullptr;

    // ── Pool Sizing ────────────────────────────────────────────────────────────
    // Formula: MinPoolSize = ceil(PI * (ActivationRadius/100)^2
    //                             * InstanceDensity * ConcurrentPlayers * 1.2)
    // where InstanceDensity = TotalInstances / AreaM^2.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    // Hard ceiling on growth. 0 = strict pre-allocation (no growth).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Runtime (auto-managed) ─────────────────────────────────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyBridgeComponent> Bridge = nullptr;

    // Always derived from array index. Never trust the serialized value.
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
    void AddInstanceForType(int32 TypeIndex, const FTransform& WorldTransform);
    void ValidateSetup() const;
    int32 FindTypeIndexByMesh(const UStaticMesh* Mesh) const;

    virtual void PostEditChangeProperty(
        FPropertyChangedEvent& PropertyChangedEvent) override;
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
    bool bIsRebuilding = false; // re-entry guard for OnConstruction
#endif
};
```

---

## `PostEditChangeProperty` — Orphan-Safe Reconciliation

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

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];
        if (Entry.HISM == nullptr)
            CreateComponentsForEntry(Entry, i);
    }

    DestroyOrphanedComponents();
    RebuildTypeIndices();
    MarkPackageDirty();
}
```

---

## `DestroyOrphanedComponents` — Tag-Based Ownership

Previous implementations used name-prefix matching (`"HISM_"`, `"Bridge_"`) to identify managed components, risking false positives if a user added a component with a similar name. The correct approach is to **tag components at creation time** and check the tag at cleanup time.

```cpp
// Component tag written by CreateComponentsForEntry.
// Identifies a component as owned by the HISM Proxy system on this actor.
static const FName HISMProxyManagedTag = TEXT("HISMProxyManaged");

void AHISMProxyHostActor::DestroyOrphanedComponents()
{
    // Build the live set from current InstanceTypes.
    TSet<UActorComponent*> LiveComponents;
    for (const FHISMProxyInstanceType& Entry : InstanceTypes)
    {
        if (Entry.HISM)   LiveComponents.Add(Entry.HISM);
        if (Entry.Bridge) LiveComponents.Add(Entry.Bridge);
    }

    // Find components tagged as managed by this system that are no longer live.
    TArray<UActorComponent*> ToDestroy;
    for (UActorComponent* Comp : GetComponents())
    {
        if (Comp && Comp->ComponentTags.Contains(HISMProxyManagedTag)
                 && !LiveComponents.Contains(Comp))
        {
            ToDestroy.Add(Comp);
        }
    }

    for (UActorComponent* Comp : ToDestroy)
        Comp->DestroyComponent();
}

void AHISMProxyHostActor::CreateComponentsForEntry(
    FHISMProxyInstanceType& Entry, int32 EntryIndex)
{
    const FName HISMName = *FString::Printf(TEXT("HISM_%s"), *Entry.TypeName.ToString());
    UHierarchicalInstancedStaticMeshComponent* NewHISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this, UHierarchicalInstancedStaticMeshComponent::StaticClass(),
            HISMName, RF_Transactional);

    NewHISM->SetStaticMesh(Entry.Mesh);
    NewHISM->NumCustomDataFloats = 2;  // slot 0: hide flag, slot 1: type index
    NewHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewHISM->ComponentTags.Add(HISMProxyManagedTag); // ownership marker

    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(
        GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
    Entry.HISM = NewHISM;

    const FName BridgeName = *FString::Printf(TEXT("Bridge_%s"), *Entry.TypeName.ToString());
    UHISMProxyBridgeComponent* NewBridge =
        NewObject<UHISMProxyBridgeComponent>(
            this, UHISMProxyBridgeComponent::StaticClass(),
            BridgeName, RF_Transactional);

    NewBridge->TargetHISM      = NewHISM;
    NewBridge->Config          = Entry.Config;
    NewBridge->MinPoolSize     = Entry.MinPoolSize;
    NewBridge->MaxPoolSize     = Entry.MaxPoolSize;
    NewBridge->GrowthBatchSize = Entry.GrowthBatchSize;
    NewBridge->ProxyClass      = Entry.ProxyClass;
    NewBridge->ComponentTags.Add(HISMProxyManagedTag); // ownership marker

    NewBridge->RegisterComponent();
    Entry.Bridge    = NewBridge;
    Entry.TypeIndex = EntryIndex;
}
```

---

## `RebuildTypeIndices` — With Re-entry Guard

`OnConstruction` fires on every viewport drag and property edit. Without a guard, `RebuildTypeIndices` would call `SetCustomDataValue` hundreds of times per second while dragging the actor, causing editor hitching.

```cpp
void AHISMProxyHostActor::RebuildTypeIndices()
{
    if (bIsRebuilding) { return; }
    TGuardValue<bool> Guard(bIsRebuilding, true);

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        // Check if the index is already correct before doing any work.
        if (InstanceTypes[i].TypeIndex == i
            && InstanceTypes[i].HISM != nullptr)
        {
            // Verify the first instance's CustomData to detect actual staleness.
            // If it matches, skip the full rewrite (fast path).
            if (InstanceTypes[i].HISM->GetInstanceCount() == 0)
            {
                InstanceTypes[i].TypeIndex = i;
                continue;
            }

            // Sample slot 1 of instance 0. If it matches, assume all are correct.
            // Full correctness guaranteed by PostEditChangeProperty on reorder.
            float StoredIndex = 0.f;
            InstanceTypes[i].HISM->GetCustomDataValue(0, 1, StoredIndex);
            if (FMath::RoundToInt(StoredIndex) == i)
            {
                InstanceTypes[i].TypeIndex = i;
                continue; // fast path: no rewrite needed
            }
        }

        // Slow path: rewrite all instance custom data for this entry.
        InstanceTypes[i].TypeIndex = i;
        if (!InstanceTypes[i].HISM) { continue; }

        const int32 NumInstances = InstanceTypes[i].HISM->GetInstanceCount();
        for (int32 j = 0; j < NumInstances; ++j)
        {
            InstanceTypes[i].HISM->SetCustomDataValue(
                j, 1, static_cast<float>(i), /*bMarkRenderStateDirty=*/false);
        }
        InstanceTypes[i].HISM->MarkRenderStateDirty();
    }
}

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
    // RebuildTypeIndices has a fast path that exits early if indices are correct.
    // The re-entry guard prevents recursion if OnConstruction fires during rebuild.
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

    const int32 NewIdx = Entry.HISM->AddInstance(WorldTransform, /*bWorldSpace=*/true);
    Entry.HISM->SetCustomDataValue(NewIdx, 0, 0.f,              false);
    Entry.HISM->SetCustomDataValue(NewIdx, 1, (float)TypeIndex, false);
    Entry.HISM->MarkRenderStateDirty();
    MarkPackageDirty();
}

int32 AHISMProxyHostActor::FindTypeIndexByMesh(const UStaticMesh* Mesh) const
{
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
        if (InstanceTypes[i].Mesh == Mesh) { return i; }
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
            Log.Error(FText::FromString(
                FString::Printf(TEXT("Entry %d has no TypeName."), i)));
        if (!Entry.Mesh)
            Log.Error(FText::FromString(Prefix + TEXT(" Mesh is null.")));
        if (!Entry.ProxyClass)
            Log.Error(FText::FromString(Prefix + TEXT(" ProxyClass is null.")));
        else if (Entry.ProxyClass == AHISMProxyActor::StaticClass())
            Log.Error(FText::FromString(Prefix +
                TEXT(" ProxyClass is the abstract base AHISMProxyActor. Assign a concrete Blueprint subclass.")));
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
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): HISM is null."),
                *GetName(), i, *Entry.TypeName.ToString());
            continue;
        }
        if (!Entry.Bridge)
        {
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge is null."),
                *GetName(), i, *Entry.TypeName.ToString());
            continue;
        }
        if (!Entry.Bridge->TargetHISM)
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.TargetHISM is null."),
                *GetName(), i, *Entry.TypeName.ToString());
        if (!Entry.Bridge->Config)
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.Config is null."),
                *GetName(), i, *Entry.TypeName.ToString());
        if (!Entry.Bridge->ProxyClass)
            UE_LOG(LogGameCore, Error,
                TEXT("AHISMProxyHostActor [%s] entry %d (%s): Bridge.ProxyClass is null."),
                *GetName(), i, *Entry.TypeName.ToString());

        UE_LOG(LogGameCore, Verbose,
            TEXT("AHISMProxyHostActor [%s] entry %d (%s): OK — %d instances, pool %d-%d."),
            *GetName(), i, *Entry.TypeName.ToString(),
            Entry.HISM->GetInstanceCount(), Entry.MinPoolSize, Entry.MaxPoolSize);
    }
}
```

---

## Pool Sizing Guide

```
InstanceDensity = TotalInstances / AreaM²
RadiusM         = ActivationRadius / 100.0
MaxPerPlayer    = PI * RadiusM² * InstanceDensity
MinPoolSize     = ceil(MaxPerPlayer * ExpectedConcurrentPlayers * 1.2)
```

**Example:** 500 oaks, 200m×200m area, radius=15m, 64 players:
```
Density      = 0.0125 /m²
MaxPerPlayer = PI * 225 * 0.0125 ≈ 8.8
MinPoolSize  = ceil(8.8 * 64 * 1.2) = 676
```
Set `MaxPoolSize = 676`. Players cluster, so `MinPoolSize = 300–400` is often sufficient. Raise it if growth warnings appear.

---

## Material Function

Create `MF_HISMProxyHide` at `Content/GameCore/Materials/Functions/MF_HISMProxyHide.uasset`:
```
[PerInstanceCustomData] Index=0, Default=0.0
      |
[If]  A >= 0.5 → -1 (discard) | else → 1 (render)
      |
[Clip]
```
Call this in every HISM material. No per-material manual wiring needed.

---

## Notes

- **Component ownership is tag-based.** `CreateComponentsForEntry` writes `HISMProxyManagedTag` to every managed component. `DestroyOrphanedComponents` checks this tag, not name prefixes, eliminating false positives.
- **`RebuildTypeIndices` has a fast path.** It samples `PerInstanceCustomData` slot 1 of instance 0 and skips the full rewrite if the index is already correct. This makes `OnConstruction` calls during viewport drags cheap.
- **`bIsRebuilding` guard prevents re-entry.** `OnConstruction` could otherwise be triggered during the rebuild itself.
- **`ProxyClass` must be a concrete subclass.** `ValidateSetup` now explicitly checks for `AHISMProxyActor::StaticClass()` and reports an error.
- **Partial proxy state is lost by design.** `DeactivationDelay` is the window for game systems to flush sub-completion state before `OnProxyDeactivated` fires.
- **Multiple host actors** per level are fully supported.
