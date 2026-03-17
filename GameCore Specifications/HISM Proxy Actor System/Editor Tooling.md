# Editor Tooling

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

Editor tooling lives in the `GameCoreEditor` module, loaded only in editor builds.

1. **`FHISMProxyHostActorDetails`** — Details panel customization for `AHISMProxyHostActor`.
2. **`UHISMFoliageConversionUtility`** — Editor utility that imports Foliage Tool instances into a host actor.

---

## Module Setup

```cpp
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
    "GameCore", "UnrealEd", "PropertyEditor",
    "SlateCore", "Slate", "Foliage",
    "EditorSubsystem", "EditorFramework"
});
```

---

## `FHISMProxyHostActorDetails`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMProxyHostActorDetails.h / .cpp`

Adds to the Details panel: **Validate Setup** button and per-entry rows with instance count + **Add Instance at Pivot** button.

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
        .OnClicked(FOnClicked::CreateLambda([HostActor]()
        {
            if (HostActor) { HostActor->ValidateSetup(); }
            return FReply::Handled();
        }))
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

        // Capture by value: HostActor ptr and index i.
        // HostActor is also held by CachedHostActor (weak ref) but we capture
        // the raw pointer here because button lambdas outlive the loop iteration.
        // Guard with IsValid check inside the lambda.
        AHISMProxyHostActor* CapturedActor = HostActor;
        const int32 CapturedIndex = i;

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
            .OnClicked(FOnClicked::CreateLambda([CapturedActor, CapturedIndex]()
            {
                if (IsValid(CapturedActor))
                {
                    const FScopedTransaction Transaction(
                        FText::FromString("Add HISM Proxy Instance"));
                    CapturedActor->Modify();
                    CapturedActor->AddInstanceForType(
                        CapturedIndex, CapturedActor->GetActorTransform());

                    FPropertyEditorModule& PM =
                        FModuleManager::GetModuleChecked<FPropertyEditorModule>(
                            "PropertyEditor");
                    PM.NotifyCustomizationModuleChanged();
                }
                return FReply::Handled();
            }))
        ];
    }
}
```

> **Why lambdas instead of `_Raw` with trailing arguments:** Slate's `FOnClicked` is `TDelegate<FReply()>` — a zero-argument delegate. `OnClicked_Raw` with trailing payload arguments does not compile for zero-argument delegates. The correct pattern is `FOnClicked::CreateLambda(...)` which captures the needed values. Each lambda captures `CapturedActor` and `CapturedIndex` by value, and guards with `IsValid` before use.

---

## `UHISMFoliageConversionUtility`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMFoliageConversionUtility.h / .cpp`

```cpp
UCLASS(Blueprintable)
class GAMECOREDITOR_API UHISMFoliageConversionUtility : public UEditorUtilityObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    TObjectPtr<AHISMProxyHostActor> TargetHostActor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    bool bRemoveFoliageAfterConversion = true;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    void ConvertFoliageToProxyHost();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    FString PreviewConversion() const;
};
```

### `ConvertFoliageToProxyHost` — Three-Pass Safe Implementation

**Why three passes:** Mutating foliage data while iterating it causes undefined behaviour. Pass 1 reads all data into a local array. Pass 2 writes to the host actor (independent). Pass 3 removes from foliage highest-index-first so compaction does not invalidate earlier indices.

**Why no `FFoliageInstanceId`:** This type is internal to the Foliage module and not publicly accessible in UE 5.7 headers. Instance indices as plain `int32` are sufficient when combined with descending-order removal.

**Why `FoliageActor->PostEditChange()` instead of `FoliageInfo->Refresh(...)`:** `FFoliageInfo::Refresh` changed signature between UE versions and is not reliably callable with a fixed argument list. `PostEditChange` on the foliage actor triggers a full internal refresh through the standard UObject change notification path, which is stable across UE 5.x.

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
    bool  bAnyRemoved    = false;

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

        UHierarchicalInstancedStaticMeshComponent* FoliageHISM =
            FoliageInfo->GetComponent();
        if (!FoliageHISM) { continue; }

        const int32 NumInstances = FoliageHISM->GetInstanceCount();
        if (NumInstances == 0) { continue; }

        // ── Pass 1: Collect all transforms into local storage. ──────────────
        // Do NOT call AddInstanceForType or RemoveInstance here.
        struct FCollectedInstance
        {
            FTransform WorldTransform;
            int32      SourceIndex;   // plain int32 — FFoliageInstanceId is internal
        };
        TArray<FCollectedInstance> Collected;
        Collected.Reserve(NumInstances);

        for (int32 j = 0; j < NumInstances; ++j)
        {
            FTransform WorldT;
            FoliageHISM->GetInstanceTransform(j, WorldT, /*bWorldSpace=*/true);
            Collected.Add({ WorldT, j });
        }

        // ── Pass 2: Add to host actor. ─────────────────────────────────────
        // Host actor HISM is independent of foliage — safe to mutate.
        for (const FCollectedInstance& C : Collected)
        {
            TargetHostActor->AddInstanceForType(TypeIndex, C.WorldTransform);
            ++TotalConverted;
        }

        // ── Pass 3: Remove from foliage highest-index-first. ──────────────
        // Removing from the end of the array keeps earlier indices stable
        // as the internal buffer compacts after each removal.
        if (bRemoveFoliageAfterConversion)
        {
            TArray<int32> RemovalIndices;
            RemovalIndices.Reserve(Collected.Num());
            for (const FCollectedInstance& C : Collected)
                RemovalIndices.Add(C.SourceIndex);

            RemovalIndices.Sort([](int32 A, int32 B) { return A > B; });

            for (int32 RemoveIdx : RemovalIndices)
                FoliageHISM->RemoveInstance(RemoveIdx);

            bAnyRemoved = true;
        }
    }

    // Trigger a full internal refresh on the foliage actor via the standard
    // change notification path. More stable than calling FFoliageInfo::Refresh
    // directly, whose signature varies between UE versions.
    if (bAnyRemoved)
    {
        FoliageActor->PostEditChange();
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

---

## Notes

- **`FOnClicked::CreateLambda` is required for Slate buttons.** `OnClicked_Raw` with trailing payload arguments does not compile for `TDelegate<FReply()>` (zero-argument). All button click handlers use lambdas that capture needed state by value.
- **`FFoliageInstanceId` is not used.** It is an internal Foliage module type not exposed in UE 5.7 public headers. Plain `int32` source indices with descending-order removal achieve the same result.
- **`FoliageActor->PostEditChange()` triggers the refresh.** More stable than `FFoliageInfo::Refresh(...)` whose signature changed between UE versions.
- **`FScopedTransaction` wraps the entire conversion.** Fully undoable via Ctrl+Z.
- **Sub-levels / World Partition:** `GetInstancedFoliageActorForCurrentLevel` returns only the persistent level's foliage. Run the converter once per sub-level while it is active.
- **Details panel must be unregistered on shutdown.** `ShutdownModule` calls `UnregisterCustomClassLayout` — omitting this causes a crash on editor reload.
