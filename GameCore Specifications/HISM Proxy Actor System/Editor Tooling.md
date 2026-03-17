# Editor Tooling

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

Editor tooling for the HISM Proxy Actor System lives in the `GameCoreEditor` module, loaded only in editor builds. It consists of two components:

1. **`FHISMProxyHostActorDetails`** — Details panel customization for `AHISMProxyHostActor`.
2. **`UHISMFoliageConversionUtility`** — Editor utility that imports Foliage Tool instances into a target `AHISMProxyHostActor`.

---

## Module Setup

```cpp
// GameCoreEditor.cpp
void FGameCoreEditorModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    PropertyModule.RegisterCustomClassLayout(
        AHISMProxyHostActor::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(
            &FHISMProxyHostActorDetails::MakeInstance));

    PropertyModule.NotifyCustomizationModuleChanged();
}

void FGameCoreEditorModule::ShutdownModule()
{
    if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
    {
        FPropertyEditorModule& PropertyModule =
            FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.UnregisterCustomClassLayout(
            AHISMProxyHostActor::StaticClass()->GetFName());
    }
}
```

`GameCoreEditor.Build.cs`:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "UnrealEd",
    "PropertyEditor",
    "SlateCore",
    "Slate",
    "Foliage",
    "EditorSubsystem",
    "EditorFramework"
});
```

---

## `FHISMProxyHostActorDetails`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMProxyHostActorDetails.h / .cpp`

Adds to the Details panel:
- **Validate Setup** button (top of HISM Proxy category)
- Per-entry row: instance count (read-only) + **Add Instance at Pivot** button

```cpp
class GAMECOREDITOR_API FHISMProxyHostActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void AddInstanceTypeRows(IDetailLayoutBuilder& DetailBuilder,
                              AHISMProxyHostActor* HostActor);
    FReply OnAddInstanceClicked(AHISMProxyHostActor* HostActor, int32 TypeIndex);
    FReply OnValidateClicked(AHISMProxyHostActor* HostActor);

    TWeakObjectPtr<AHISMProxyHostActor> CachedHostActor;
};
```

```cpp
void FHISMProxyHostActorDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.IsEmpty()) { return; }

    AHISMProxyHostActor* HostActor = Cast<AHISMProxyHostActor>(Objects[0].Get());
    if (!HostActor) { return; }
    CachedHostActor = HostActor;

    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("HISM Proxy");

    Category.AddCustomRow(FText::FromString("Validate Setup"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(FText::FromString("Validate Setup"))
        .OnClicked_Raw(this, &FHISMProxyHostActorDetails::OnValidateClicked, HostActor)
    ];

    AddInstanceTypeRows(DetailBuilder, HostActor);
}

void FHISMProxyHostActorDetails::AddInstanceTypeRows(
    IDetailLayoutBuilder& DetailBuilder, AHISMProxyHostActor* HostActor)
{
    IDetailCategoryBuilder& Category = DetailBuilder.EditCategory("HISM Proxy");

    for (int32 i = 0; i < HostActor->InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = HostActor->InstanceTypes[i];
        const int32 Count = Entry.HISM ? Entry.HISM->GetInstanceCount() : 0;

        Category.AddCustomRow(FText::FromString(Entry.TypeName.ToString()))
        .NameContent()
        [
            SNew(STextBlock).Text(FText::Format(
                INVTEXT("{0}  ({1} instances)"),
                FText::FromName(Entry.TypeName),
                FText::AsNumber(Count)))
        ]
        .ValueContent()
        [
            SNew(SButton)
            .Text(FText::FromString("Add Instance at Pivot"))
            .ToolTipText(FText::FromString(
                "Move this actor's pivot to the desired world position, then click."))
            .OnClicked_Raw(this,
                &FHISMProxyHostActorDetails::OnAddInstanceClicked, HostActor, i)
        ];
    }
}

FReply FHISMProxyHostActorDetails::OnAddInstanceClicked(
    AHISMProxyHostActor* HostActor, int32 TypeIndex)
{
    if (!HostActor) { return FReply::Handled(); }

    const FScopedTransaction Transaction(
        FText::FromString("Add HISM Proxy Instance"));
    HostActor->Modify();
    HostActor->AddInstanceForType(TypeIndex, HostActor->GetActorTransform());

    // Refresh the Details panel to update instance counts.
    FPropertyEditorModule& PropertyModule =
        FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
    PropertyModule.NotifyCustomizationModuleChanged();

    return FReply::Handled();
}

FReply FHISMProxyHostActorDetails::OnValidateClicked(
    AHISMProxyHostActor* HostActor)
{
    if (HostActor) { HostActor->ValidateSetup(); }
    return FReply::Handled();
}
```

> **Note:** `FScopedTransaction` wraps `AddInstanceForType` so the instance addition is undoable via Ctrl+Z. Always call `HostActor->Modify()` before mutating the actor in editor operations.

---

## `UHISMFoliageConversionUtility`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMFoliageConversionUtility.h / .cpp`

### Why the Previous Implementation Was Unsafe

The earlier version accumulated `int32` instance indices during `ForEachInstance` and then called `RemoveInstances` with those indices. This is incorrect: `RemoveInstances` compacts the instance array, invalidating indices for all instances beyond the first removed index. The result is wrong instances being removed or an out-of-bounds access.

**The fix:** collect all transforms first into a local array, then add them all to the host actor, then remove foliage using the foliage system's own stable `FFoliageInstanceId`-based removal API which does not suffer from index shifting.

### Class Definition

```cpp
UCLASS(Blueprintable)
class GAMECOREDITOR_API UHISMFoliageConversionUtility : public UEditorUtilityObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    TObjectPtr<AHISMProxyHostActor> TargetHostActor;

    // Recommended: true. Leaving foliage causes duplicate mesh rendering.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    bool bRemoveFoliageAfterConversion = true;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    void ConvertFoliageToProxyHost();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    FString PreviewConversion() const;
};
```

### `ConvertFoliageToProxyHost` — Safe Implementation

```cpp
void UHISMFoliageConversionUtility::ConvertFoliageToProxyHost()
{
    if (!TargetHostActor)
    {
        UE_LOG(LogGameCoreEditor, Error,
            TEXT("ConvertFoliageToProxyHost: TargetHostActor is null."));
        return;
    }

    UWorld* World = TargetHostActor->GetWorld();
    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(
            World, /*bCreateIfNone=*/false);

    if (!FoliageActor)
    {
        UE_LOG(LogGameCoreEditor, Warning,
            TEXT("ConvertFoliageToProxyHost: No foliage in current level."));
        return;
    }

    const FScopedTransaction Transaction(
        FText::FromString("Convert Foliage to HISM Proxy Host"));
    TargetHostActor->Modify();
    FoliageActor->Modify();

    int32 TotalConverted = 0;
    int32 TotalSkipped   = 0;

    TMap<UFoliageType*, FFoliageInfo*> AllFoliageInfo = FoliageActor->GetAllFoliageInfo();

    for (auto& [FoliageType, FoliageInfo] : AllFoliageInfo)
    {
        if (!FoliageInfo || !FoliageType) { continue; }

        UFoliageType_InstancedStaticMesh* ISMType =
            Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
        if (!ISMType || !ISMType->GetStaticMesh()) { continue; }

        const int32 TypeIndex =
            TargetHostActor->FindTypeIndexByMesh(ISMType->GetStaticMesh());

        if (TypeIndex == INDEX_NONE)
        {
            UE_LOG(LogGameCoreEditor, Warning,
                TEXT("ConvertFoliageToProxyHost: Mesh '%s' not in InstanceTypes — skipped."),
                *ISMType->GetStaticMesh()->GetName());
            ++TotalSkipped;
            continue;
        }

        // --- Pass 1: collect all transforms and instance IDs into local arrays.
        // We MUST NOT call AddInstanceForType or RemoveInstances during this pass,
        // as either would invalidate the internal foliage data structures.
        struct FCollectedInstance
        {
            FTransform WorldTransform;
            FFoliageInstanceId InstanceId;
        };
        TArray<FCollectedInstance> Collected;

        const FFoliageInstanceHash* HashGrid = FoliageInfo->GetInstanceHash();
        // GetInstanceHash may be null if the foliage info was just created.
        // Fall back to component-level iteration.

        // GetAllInstances returns a TArray<FFoliageInstance> indexed 0..N-1.
        // Each FFoliageInstance has a Location, Rotation, DrawScale3D.
        // We get world transforms via the component's GetInstanceTransform.
        if (UHierarchicalInstancedStaticMeshComponent* FoliageHISM =
            FoliageInfo->GetComponent())
        {
            const int32 NumInstances = FoliageHISM->GetInstanceCount();
            Collected.Reserve(NumInstances);

            for (int32 j = 0; j < NumInstances; ++j)
            {
                FTransform WorldT;
                FoliageHISM->GetInstanceTransform(j, WorldT, /*bWorldSpace=*/true);

                FCollectedInstance& C = Collected.AddDefaulted_GetRef();
                C.WorldTransform = WorldT;
                // Store the instance index as ID — used for stable batch removal below.
                C.InstanceId.Index = j;
            }
        }

        if (Collected.IsEmpty()) { continue; }

        // --- Pass 2: add all collected transforms to the host actor.
        for (const FCollectedInstance& C : Collected)
        {
            TargetHostActor->AddInstanceForType(TypeIndex, C.WorldTransform);
            ++TotalConverted;
        }

        // --- Pass 3: remove from foliage — done after ALL adds are complete.
        // Build the removal index array from highest to lowest so that
        // compaction of earlier indices does not affect later ones.
        // This is the correct approach when using index-based removal.
        if (bRemoveFoliageAfterConversion)
        {
            TArray<int32> RemovalIndices;
            RemovalIndices.Reserve(Collected.Num());
            for (const FCollectedInstance& C : Collected)
                RemovalIndices.Add(C.InstanceId.Index);

            // Sort descending so we remove from the end of the array first.
            // Removing from end to front means earlier indices remain stable.
            RemovalIndices.Sort([](int32 A, int32 B) { return A > B; });

            if (UHierarchicalInstancedStaticMeshComponent* FoliageHISM =
                FoliageInfo->GetComponent())
            {
                // RemoveInstances on the component directly, one at a time
                // from highest to lowest index — safe against compaction.
                for (int32 RemoveIdx : RemovalIndices)
                {
                    FoliageHISM->RemoveInstance(RemoveIdx);
                }
            }

            // Notify the foliage info that its component data changed.
            FoliageInfo->Refresh(FoliageActor, true, true);
        }
    }

    World->GetCurrentLevel()->MarkPackageDirty();

    UE_LOG(LogGameCoreEditor, Log,
        TEXT("ConvertFoliageToProxyHost: converted %d instances, skipped %d types."),
        TotalConverted, TotalSkipped);

    FNotificationInfo Info(
        FText::Format(INVTEXT("Converted {0} instances to HISM Proxy Host."),
                      FText::AsNumber(TotalConverted)));
    Info.bFireAndForget = true;
    Info.ExpireDuration = 4.f;
    FSlateNotificationManager::Get().AddNotification(Info);
}
```

### Why Three Passes?

| Pass | What | Why |
|---|---|---|
| 1 — Collect | Read all transforms + indices into `Collected[]` | Foliage data must not be mutated while being read |
| 2 — Add | Call `AddInstanceForType` for each collected transform | Host actor HISM is independent — safe to mutate |
| 3 — Remove | Remove from foliage highest-index-first | Descending order keeps earlier indices stable during compaction |

This three-pass design eliminates the index-invalidation bug entirely.

---

## Notes

- **`FScopedTransaction`** wraps the entire conversion — it is fully undoable via Ctrl+Z.
- **Foliage tool vs host actor are mutually exclusive renderers.** Always set `bRemoveFoliageAfterConversion = true` to avoid double rendering.
- **Sub-levels / World Partition:** `GetInstancedFoliageActorForCurrentLevel` returns only the persistent level's foliage actor. If foliage spans sub-levels, run the converter once per sub-level while it is active and loaded.
- **`FoliageInfo->Refresh`** rebuilds the foliage info's internal spatial hash and LOD data after removal. Required for the editor to display the correct remaining instance count.
- **Details panel customization** must be unregistered in `ShutdownModule` — see Module Setup above.
- **Add Instance at Pivot** is wrapped in `FScopedTransaction` and calls `HostActor->Modify()` before mutating state — this makes it fully undoable.
