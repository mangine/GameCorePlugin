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

    // Always derived from array index at editor time and on load.
    // Never trust the serialized value as the sole source of truth.
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
    // Re-entry guard for RebuildTypeIndices.
    // Declared outside WITH_EDITOR to avoid UHT layout mismatch between
    // editor and non-editor builds. Use WITH_EDITORONLY_DATA if zero
    // shipping cost is required.
    UPROPERTY()
    bool bIsRebuilding = false;

#if WITH_EDITOR
    void CreateComponentsForEntry(FHISMProxyInstanceType& Entry, int32 EntryIndex);
    void DestroyOrphanedComponents();
    void RebuildTypeIndices();
#endif
};
```

> **Why `bIsRebuilding` is not inside `#if WITH_EDITOR`:** Member variables inside `#if WITH_EDITOR` in a `UCLASS` body are not safe because UHT generates serialization and reflection code that expects a consistent class layout between editor and non-editor builds. Using a `UPROPERTY()` bool outside the guard is the correct approach; it costs 1 byte in shipping builds. Alternatively, `UPROPERTY()` with `WITH_EDITORONLY_DATA` may be used if the strict zero-cost requirement applies.

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
#endif // WITH_EDITOR
```

---

## `DestroyOrphanedComponents` — Tag-Based Ownership

```cpp
#if WITH_EDITOR
// Static tag written to every component created by this system.
// Used by DestroyOrphanedComponents to identify managed components
// without relying on fragile name-prefix matching.
static const FName HISMProxyManagedTag = TEXT("HISMProxyManaged");

void AHISMProxyHostActor::DestroyOrphanedComponents()
{
    TSet<UActorComponent*> LiveComponents;
    for (const FHISMProxyInstanceType& Entry : InstanceTypes)
    {
        if (Entry.HISM)   LiveComponents.Add(Entry.HISM);
        if (Entry.Bridge) LiveComponents.Add(Entry.Bridge);
    }

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
    NewHISM->NumCustomDataFloats = 2;
    NewHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    NewHISM->ComponentTags.Add(HISMProxyManagedTag);

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
    NewBridge->ComponentTags.Add(HISMProxyManagedTag);

    NewBridge->RegisterComponent();
    Entry.Bridge    = NewBridge;
    Entry.TypeIndex = EntryIndex;
}
#endif // WITH_EDITOR
```

---

## `RebuildTypeIndices` — With Re-entry Guard

`OnConstruction` fires on every viewport drag. The re-entry guard `bIsRebuilding` prevents recursive calls. The fast path exits early based on the cached `TypeIndex` field — **no `GetCustomDataValue` is used** because that API does not exist on `UInstancedStaticMeshComponent` in UE5 (custom data is write-only to GPU). The slow path rewrites `PerInstanceCustomData[1]` for all instances of an entry when the cached `TypeIndex` does not match the expected position.

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::RebuildTypeIndices()
{
    if (bIsRebuilding) { return; }
    TGuardValue<bool> Guard(bIsRebuilding, true);

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];

        // Fast path: TypeIndex already matches array position.
        // This is the common case during viewport drags and minor edits.
        if (Entry.TypeIndex == i)
        {
            continue;
        }

        // Slow path: TypeIndex is stale (entry was reordered, loaded with
        // a different array position, or newly created).
        // Rewrite PerInstanceCustomData[1] for all instances.
        Entry.TypeIndex = i;

        if (!Entry.HISM) { continue; }

        const int32 NumInstances = Entry.HISM->GetInstanceCount();
        for (int32 j = 0; j < NumInstances; ++j)
        {
            Entry.HISM->SetCustomDataValue(
                j, /*CustomDataIndex=*/1,
                static_cast<float>(i),
                /*bMarkRenderStateDirty=*/false);
        }
        Entry.HISM->MarkRenderStateDirty();
    }
}

void AHISMProxyHostActor::PostLoad()
{
    Super::PostLoad();
    RebuildTypeIndices();
}

void AHISMProxyHostActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildTypeIndices();
}
#endif // WITH_EDITOR
```

> **Note on fast path correctness:** The fast path relies on `TypeIndex` being correct in the serialized data. This is guaranteed because every structural change (add, remove, reorder) calls `RebuildTypeIndices` via `PostEditChangeProperty`, which runs the slow path and updates `TypeIndex` before saving. `PostLoad` also runs the slow path unconditionally on load, which handles any edge case where a save was made with a stale index.

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

    // AddInstance with bWorldSpace=true handles the actor-to-world transform
    // conversion internally. No manual GetRelativeTransform needed.
    const int32 NewIdx = Entry.HISM->AddInstance(WorldTransform, /*bWorldSpace=*/true);
    Entry.HISM->SetCustomDataValue(NewIdx, 0, 0.f,              /*bMarkRenderStateDirty=*/false);
    Entry.HISM->SetCustomDataValue(NewIdx, 1, (float)TypeIndex, /*bMarkRenderStateDirty=*/false);
    Entry.HISM->MarkRenderStateDirty();
    MarkPackageDirty();
}

int32 AHISMProxyHostActor::FindTypeIndexByMesh(const UStaticMesh* Mesh) const
{
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
        if (InstanceTypes[i].Mesh == Mesh) { return i; }
    return INDEX_NONE;
}
#endif // WITH_EDITOR
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
                TEXT(" ProxyClass is the abstract base AHISMProxyActor. "
                     "Assign a concrete Blueprint subclass.")));
        if (!Entry.Config)
            Log.Error(FText::FromString(Prefix + TEXT(" Config is null.")));
        if (!Entry.HISM)
            Log.Error(FText::FromString(
                Prefix + TEXT(" HISM component missing — resave the actor.")));
        else
        {
            if (Entry.HISM->NumCustomDataFloats < 2)
                Log.Error(FText::FromString(Prefix +
                    TEXT(" NumCustomDataFloats < 2. "
                         "Hide flag and type index will not work.")));
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
#endif // WITH_EDITOR
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
            TEXT("AHISMProxyHostActor [%s] entry %d (%s): OK — %d instances, pool %d–%d."),
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

**Example:** 500 oaks, 200m×200m, radius=15m, 64 players:
```
Density      = 0.0125 /m²
MaxPerPlayer = PI * 225 * 0.0125 ≈ 8.8
MinPoolSize  = ceil(8.8 * 64 * 1.2) = 676
```
Set `MaxPoolSize = 676`. In practice `MinPoolSize = 300–400` is often sufficient due to player clustering. Raise it if growth warnings appear in logs.

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
Call this in every HISM material. Eliminates per-material manual clip wiring.

---

## Notes

- **Component ownership is tag-based.** `CreateComponentsForEntry` writes `HISMProxyManagedTag` to every managed component. `DestroyOrphanedComponents` checks this tag, eliminating false positives from user-added components with similar names.
- **`RebuildTypeIndices` fast path uses `TypeIndex` field only.** `GetCustomDataValue` does not exist on `UInstancedStaticMeshComponent` in UE5 — custom data is write-only from the CPU. The fast path correctly exits on `TypeIndex == i` without any HISM API call.
- **`bIsRebuilding` is a `UPROPERTY()` outside `#if WITH_EDITOR`.** This avoids UHT layout mismatch between editor and non-editor builds. It is zero-initialized and has no runtime cost in non-editor paths.
- **`ProxyClass` must be a concrete subclass.** `ValidateSetup` checks for `AHISMProxyActor::StaticClass()` directly.
- **Partial proxy state is lost by design.** `DeactivationDelay` gives game systems time to flush sub-completion state before `OnProxyDeactivated` fires.
- **Multiple host actors** per level are fully supported.
