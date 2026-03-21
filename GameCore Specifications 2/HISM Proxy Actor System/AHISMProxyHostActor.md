# AHISMProxyHostActor

**File:** `GameCore/Source/GameCore/Public/HISMProxy/HISMProxyHostActor.h / .cpp`  
**Module:** `GameCore`

The primary level-placed actor of the HISM Proxy system. Developers configure `InstanceTypes`; the host creates HISM and bridge components automatically and validates them at BeginPlay.

---

## `FHISMProxyInstanceType`

One entry per mesh type. The host actor auto-creates `HISM` and `Bridge` components when an entry is populated in the editor.

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
    // Must NOT be AHISMProxyActor::StaticClass() itself — validated by ValidateSetup.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyConfig> Config = nullptr;

    // ── Pool Sizing ──────────────────────────────────────────────────────────
    // Formula: MinPoolSize = ceil(PI * (ActivationRadius/100)^2
    //                             * InstanceDensity * ConcurrentPlayers * 1.2)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    // Hard ceiling on pool growth. 0 = strict pre-allocation (no growth allowed).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Runtime (auto-managed, do not set manually) ──────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyBridgeComponent> Bridge = nullptr;

    // Always derived from array index. Serialized value is refreshed on load
    // and on every structural change. Do not trust as a standalone source of truth.
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
    // Declared outside WITH_EDITOR to avoid UHT class layout mismatch between
    // editor and non-editor builds. Costs 1 byte in shipping — acceptable.
    UPROPERTY()
    bool bIsRebuilding = false;

#if WITH_EDITOR
    // Tag written to every auto-created component for ownership tracking.
    static const FName HISMProxyManagedTag; // = TEXT("HISMProxyManaged")

    void CreateComponentsForEntry(FHISMProxyInstanceType& Entry, int32 EntryIndex);
    void DestroyOrphanedComponents();
    void RebuildTypeIndices();
#endif
};
```

---

## `PostEditChangeProperty`

Fires on every `InstanceTypes` change in the Details panel. Reconciles live components with the current `InstanceTypes` array.

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
#endif
```

---

## `CreateComponentsForEntry`

Creates and wires one HISM + one bridge for a newly added `InstanceTypes` entry. Both components receive the `"HISMProxyManaged"` tag for safe orphan detection.

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::CreateComponentsForEntry(
    FHISMProxyInstanceType& Entry, int32 EntryIndex)
{
    const FName HISMName = *FString::Printf(TEXT("HISM_%s"), *Entry.TypeName.ToString());
    UHierarchicalInstancedStaticMeshComponent* NewHISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this, UHierarchicalInstancedStaticMeshComponent::StaticClass(),
            HISMName, RF_Transactional);

    NewHISM->SetStaticMesh(Entry.Mesh);
    NewHISM->NumCustomDataFloats = 2;  // Slot 0: hide flag. Slot 1: type index.
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
#endif
```

---

## `DestroyOrphanedComponents`

Removes managed components no longer referenced by any `InstanceTypes` entry.

```cpp
#if WITH_EDITOR
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
#endif
```

> **Why tag-based?** Name-prefix matching (e.g. `StartsWith("HISM_")`) is fragile — a developer could legitimately add a component named `HISM_MyCollider` and it would be destroyed incorrectly. Component tags are the correct UE mechanism for system-level ownership marking.

---

## `RebuildTypeIndices`

Writes `PerInstanceCustomData[1]` (type index float) for every instance in each entry. Uses a re-entry guard and a fast path based on the cached `TypeIndex` field.

> **Why no `GetCustomDataValue`?** `UInstancedStaticMeshComponent` has no `GetCustomDataValue` method. Per-instance custom data is write-only from the CPU — data goes directly to the GPU render buffer. The fast path uses the serialized `TypeIndex` field as the staleness check.

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::RebuildTypeIndices()
{
    if (bIsRebuilding) { return; }
    TGuardValue<bool> Guard(bIsRebuilding, true);

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];

        // Fast path: index already correct (common during viewport drags)
        if (Entry.TypeIndex == i) { continue; }

        // Slow path: reorder/load detected — rewrite all instances
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
    RebuildTypeIndices(); // Always run slow path on load
}

void AHISMProxyHostActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildTypeIndices(); // Re-entry guard prevents recursion from viewport drags
}
#endif
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

    // AddInstance with bWorldSpace=true handles actor-to-world transform conversion.
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
#endif
```

---

## `ValidateSetup`

Runs authoring checks and outputs to the `HISMProxyValidation` Message Log category. Called by the Details panel button.

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

## `BeginPlay`

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

## Notes

- `PerInstanceCustomData` slot 0 = hide flag (reserved by this system). Slot 1 = type index (written by host). Game-specific custom data starts at slot 2. `NumCustomDataFloats` is set to 2 minimum at component creation.
- `ProxyClass` must be a **concrete Blueprint subclass** of `AHISMProxyActor`. The base class itself is `Abstract` and cannot be assigned.
- Multiple host actors per level are fully supported.
- All editor mutations (`AddInstanceForType`, `ConvertFoliageToProxyHost`) must be wrapped in `FScopedTransaction` and call `Modify()` before mutation.
