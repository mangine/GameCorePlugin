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
    // This is the actor that replaces the HISM instance when a player is nearby.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass = nullptr;

    // Shared proximity and timing config (activation radius, delay, tick interval).
    // Multiple types can share the same config asset.
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyConfig> Config = nullptr;

    // Max concurrent live proxies for this type.
    // Should be sized to worst-case simultaneous player proximity, not average.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
    int32 PoolSize = 8;

    // ── Runtime (auto-managed, do not edit) ───────────────────────────────────

    // Created automatically when this entry is added. Named "HISM_<TypeName>".
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM = nullptr;

    // Created automatically when this entry is added. Named "Bridge_<TypeName>".
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyBridgeComponent> Bridge = nullptr;

    // Float index written into PerInstanceCustomData[1] for each instance.
    // Assigned automatically by the host actor when the entry is created.
    // Used at runtime by the bridge to select the correct proxy class.
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

    // ── Configuration ─────────────────────────────────────────────────────────

    // Designer-facing array. Adding an entry here (in the editor) triggers
    // automatic HISM + bridge component creation via PostEditChangeProperty.
    UPROPERTY(EditAnywhere, Category = "HISM Proxy",
              meta = (TitleProperty = "TypeName"))
    TArray<FHISMProxyInstanceType> InstanceTypes;

    // ── Editor API ────────────────────────────────────────────────────────────
#if WITH_EDITOR
    // Adds one instance to the HISM for TypeIndex at the given world transform.
    // Called by the Details panel "Add Instance" button and the foliage converter.
    // Writes PerInstanceCustomData[0]=0 (visible) and [1]=TypeIndex (type).
    void AddInstanceForType(int32 TypeIndex, const FTransform& WorldTransform);

    // Validates all entries: checks Mesh, ProxyClass, Config, NumCustomDataFloats,
    // pool vs instance count. Writes results to the Message Log.
    // Called by the Details panel "Validate Setup" button.
    void ValidateSetup() const;

    // Returns the TypeIndex for the entry whose Mesh matches the given asset.
    // Returns INDEX_NONE if not found. Used by the foliage converter.
    int32 FindTypeIndexByMesh(const UStaticMesh* Mesh) const;

    virtual void PostEditChangeProperty(
        FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
    virtual void BeginPlay() override;

private:
#if WITH_EDITOR
    // Creates the HISM and Bridge components for a new InstanceTypes entry.
    // Sets mesh, NumCustomDataFloats=2, and wires bridge.TargetHISM.
    void CreateComponentsForEntry(FHISMProxyInstanceType& Entry, int32 EntryIndex);

    // Destroys the HISM and Bridge components for a removed entry.
    void DestroyComponentsForEntry(FHISMProxyInstanceType& Entry);

    // Assigns TypeIndex values to all entries based on array position.
    // Called after any add/remove/reorder.
    void RebuildTypeIndices();
#endif
};
```

---

## `PostEditChangeProperty` — How Auto-Wiring Works

This is the core editor mechanism. Every time the designer modifies `InstanceTypes` in the Details panel, UE calls this function.

```cpp
#if WITH_EDITOR
void AHISMProxyHostActor::PostEditChangeProperty(
    FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    const FName MemberName   = PropertyChangedEvent.MemberProperty
                                ? PropertyChangedEvent.MemberProperty->GetFName()
                                : NAME_None;

    // Only react to changes inside the InstanceTypes array.
    if (MemberName != GET_MEMBER_NAME_CHECKED(AHISMProxyHostActor, InstanceTypes))
    {
        return;
    }

    // Reconcile components with the current InstanceTypes array.
    // Approach: destroy all auto-managed components, then recreate from scratch.
    // This is safe because instance DATA is in the HISM's instance buffer,
    // not in the component pointer — recreating the component does not lose instances
    // as long as we transfer them (or the component was just added and is empty).
    //
    // For a robust implementation, track which entries are new (HISM == nullptr)
    // vs existing (HISM != nullptr) vs removed (no matching entry).

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];

        if (Entry.HISM == nullptr)
        {
            // New entry — create components.
            CreateComponentsForEntry(Entry, i);
        }
        else if (Entry.TypeName != Entry.HISM->GetFName().ToString().Mid(5))
        {
            // TypeName was renamed — update component names.
            // UE component renaming: use Rename() on the component.
            Entry.HISM->Rename(*FString::Printf(TEXT("HISM_%s"), *Entry.TypeName.ToString()));
            Entry.Bridge->Rename(*FString::Printf(TEXT("Bridge_%s"), *Entry.TypeName.ToString()));
        }
        // else: entry exists and is unchanged — no action needed.
    }

    // Rebuild type indices after any structural change.
    RebuildTypeIndices();

    MarkPackageDirty();
}

void AHISMProxyHostActor::CreateComponentsForEntry(
    FHISMProxyInstanceType& Entry, int32 EntryIndex)
{
    // Create HISM component.
    FName HISMName = *FString::Printf(TEXT("HISM_%s"), *Entry.TypeName.ToString());
    UHierarchicalInstancedStaticMeshComponent* NewHISM =
        NewObject<UHierarchicalInstancedStaticMeshComponent>(
            this, UHierarchicalInstancedStaticMeshComponent::StaticClass(), HISMName);

    NewHISM->SetStaticMesh(Entry.Mesh);

    // Reserve slot 0 (hide flag) + slot 1 (type index). Game data starts at 2.
    NewHISM->NumCustomDataFloats = 2;

    // No collision on the HISM itself — proxies handle gameplay collision.
    NewHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    NewHISM->RegisterComponent();
    NewHISM->AttachToComponent(GetRootComponent(),
        FAttachmentTransformRules::KeepRelativeTransform);

    Entry.HISM = NewHISM;

    // Create Bridge component.
    FName BridgeName = *FString::Printf(TEXT("Bridge_%s"), *Entry.TypeName.ToString());
    UHISMProxyBridgeComponent* NewBridge =
        NewObject<UHISMProxyBridgeComponent>(this,
            UHISMProxyBridgeComponent::StaticClass(), BridgeName);

    NewBridge->TargetHISM = NewHISM;   // explicit wiring — no FindComponentByClass
    NewBridge->Config     = Entry.Config;
    NewBridge->PoolSize   = Entry.PoolSize;
    NewBridge->ProxyClass = Entry.ProxyClass;

    NewBridge->RegisterComponent();

    Entry.Bridge = NewBridge;
    Entry.TypeIndex = EntryIndex;
}

void AHISMProxyHostActor::RebuildTypeIndices()
{
    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        InstanceTypes[i].TypeIndex = i;
        // Update PerInstanceCustomData[1] for all existing instances of this entry.
        // Required if entries were reordered.
        if (InstanceTypes[i].HISM)
        {
            const int32 NumInstances = InstanceTypes[i].HISM->GetInstanceCount();
            for (int32 j = 0; j < NumInstances; ++j)
            {
                InstanceTypes[i].HISM->SetCustomDataValue(j, 1, (float)i, false);
            }
            InstanceTypes[i].HISM->MarkRenderStateDirty();
        }
    }
}
#endif
```

---

## `AddInstanceForType`

Called by the Details panel button and the foliage converter. Adds one instance to the correct HISM and writes both custom data slots.

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

    // Convert world transform to local (HISM stores in local space).
    const FTransform LocalTransform =
        WorldTransform.GetRelativeTransform(GetActorTransform());

    const int32 NewInstanceIdx = Entry.HISM->AddInstance(LocalTransform,
        /*bWorldSpace=*/false);

    // Slot 0: hide flag = 0.0 (visible)
    Entry.HISM->SetCustomDataValue(NewInstanceIdx, 0, 0.f, false);
    // Slot 1: type index
    Entry.HISM->SetCustomDataValue(NewInstanceIdx, 1, (float)TypeIndex, false);

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
    bool bAnyError = false;

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = InstanceTypes[i];
        const FString Prefix = FString::Printf(TEXT("[%s]"), *Entry.TypeName.ToString());

        if (!Entry.Mesh)
            Log.Error(FText::FromString(Prefix + " Mesh is null."));
        if (!Entry.ProxyClass)
            Log.Error(FText::FromString(Prefix + " ProxyClass is null."));
        if (!Entry.Config)
            Log.Error(FText::FromString(Prefix + " Config is null."));
        if (!Entry.HISM)
            Log.Error(FText::FromString(Prefix + " HISM component is missing — re-open actor."));
        else
        {
            if (Entry.HISM->NumCustomDataFloats < 2)
                Log.Error(FText::FromString(Prefix + " NumCustomDataFloats < 2. System will not work."));

            const int32 InstanceCount = Entry.HISM->GetInstanceCount();
            if (InstanceCount == 0)
                Log.Warning(FText::FromString(Prefix + " Has 0 instances."));
            if (Entry.PoolSize < 1)
                Log.Error(FText::FromString(Prefix + " PoolSize is 0."));
        }
        if (!Entry.Bridge)
            Log.Error(FText::FromString(Prefix + " Bridge component is missing."));
    }

    if (!bAnyError)
        Log.Info(FText::FromString(TEXT("Validation passed for ") + GetName()));

    Log.Open(EMessageSeverity::Info, /*bOpenLog=*/true);
}
#endif
```

---

## `BeginPlay` — Runtime Wiring

At runtime, the host actor does not need to re-create components (they are serialized with the level). It only needs to ensure bridge delegates are wired for type-based dispatch.

```cpp
void AHISMProxyHostActor::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority()) { return; }

    for (int32 i = 0; i < InstanceTypes.Num(); ++i)
    {
        FHISMProxyInstanceType& Entry = InstanceTypes[i];
        if (!Entry.Bridge || !Entry.HISM) { continue; }

        // Each bridge manages a single homogeneous HISM —
        // all instances use the same proxy class.
        // The OnQueryInstanceType delegate always returns the same tag.
        // We wire this internally so game code never has to.
        //
        // OnQueryInstanceEligibility is left unbound by default
        // (all instances eligible). Game systems bind it if needed.
        //
        // No delegate binding needed for OnQueryInstanceType when the host actor
        // manages the HISM — the bridge uses its ProxyClass directly.
        // See UHISMProxyBridgeComponent::BeginPlay for details.
    }
}
```

---

## Notes

- **Do not add `UHISMProxyBridgeComponent` manually** to a host actor. The host actor creates and owns them. Manual addition will create unmanaged components.
- **`PerInstanceCustomData[1]` type indices are stable** as long as `InstanceTypes` array order does not change after instances have been added. Reordering entries rebuilds all type indices automatically in `RebuildTypeIndices`, but this marks the level dirty and should be treated as a significant authoring operation.
- **Mesh changes** on an existing entry (changing `Entry.Mesh` after instances exist) require manually clearing all instances from that HISM and re-adding them with the new mesh. The system does not migrate instances automatically.
- **Multiple host actors** in the same level are fully supported — one per logical cluster (forest A, forest B, shipyard props, etc.).
- **Blueprint subclass of `AHISMProxyHostActor`** is allowed and useful if the game needs to override `BeginPlay` to bind `OnQueryInstanceEligibility` on all bridges at once from a single location.
