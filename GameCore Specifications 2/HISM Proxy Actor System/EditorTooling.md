# Editor Tooling — HISM Proxy Actor System

**Module:** `GameCoreEditor` (editor-only, never loaded in shipping)  
**Files:**
- `GameCoreEditor/Public/HISMProxy/HISMProxyHostActorDetails.h / .cpp`
- `GameCoreEditor/Public/HISMProxy/HISMFoliageConversionUtility.h / .cpp`

---

## Module Registration

```cpp
// GameCoreEditorModule.cpp
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
    // Must unregister — omitting causes a crash on editor reload.
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

Details panel customization for `AHISMProxyHostActor`. Adds:
- **Validate Setup** button (runs `AHISMProxyHostActor::ValidateSetup()`)
- Per-entry rows showing instance count + **Add Instance at Pivot** button

```cpp
class GAMECOREDITOR_API FHISMProxyHostActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void AddInstanceTypeRows(IDetailLayoutBuilder& DetailBuilder,
                              AHISMProxyHostActor* HostActor);

    TWeakObjectPtr<AHISMProxyHostActor> CachedHostActor;
};
```

```cpp
void FHISMProxyHostActorDetails::CustomizeDetails(
    IDetailLayoutBuilder& DetailBuilder)
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
            // FOnClicked is TDelegate<FReply()> — zero args.
            // CreateLambda captures state by value. _Raw with trailing payload
            // does not compile for zero-argument delegates. See AD-12.
            .OnClicked(FOnClicked::CreateLambda(
                [CapturedActor, CapturedIndex]()
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

---

## `UHISMFoliageConversionUtility`

Editor utility object that imports Foliage Tool instances into a host actor.

```cpp
UCLASS(Blueprintable)
class GAMECOREDITOR_API UHISMFoliageConversionUtility : public UEditorUtilityObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    TObjectPtr<AHISMProxyHostActor> TargetHostActor;

    // When true, removes matching foliage instances after adding to host actor.
    // Must be true to prevent double rendering.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    bool bRemoveFoliageAfterConversion = true;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    void ConvertFoliageToProxyHost();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    FString PreviewConversion() const;
};
```

### `ConvertFoliageToProxyHost` — Three-Pass Implementation

**Why three passes:**
- Pass 1: Read all transforms into local storage. No mutations.
- Pass 2: Add to host actor. Fully independent of foliage.
- Pass 3: Remove from foliage **highest-index-first**. Descending removal avoids index invalidation from `RemoveInstance`'s internal compaction.

**Why no `FFoliageInstanceId`:** Internal Foliage module type, not accessible from a plugin in UE 5.7.

**Why `FoliageActor->PostEditChange()`:** `FFoliageInfo::Refresh` signature varies between UE versions. `PostEditChange` triggers a full internal refresh via the standard change notification path — stable across UE 5.x.

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

    TMap<UFoliageType*, FFoliageInfo*> AllFoliageInfo =
        FoliageActor->GetAllFoliageInfo();

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

        // ── Pass 1: Collect transforms ──────────────────────────────────────
        struct FCollectedInstance
        {
            FTransform WorldTransform;
            int32      SourceIndex;
        };
        TArray<FCollectedInstance> Collected;
        Collected.Reserve(NumInstances);

        for (int32 j = 0; j < NumInstances; ++j)
        {
            FTransform WorldT;
            FoliageHISM->GetInstanceTransform(j, WorldT, /*bWorldSpace=*/true);
            Collected.Add({ WorldT, j });
        }

        // ── Pass 2: Add to host actor ───────────────────────────────────────
        for (const FCollectedInstance& C : Collected)
        {
            TargetHostActor->AddInstanceForType(TypeIndex, C.WorldTransform);
            ++TotalConverted;
        }

        // ── Pass 3: Remove from foliage highest-index-first ─────────────────
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

    if (bAnyRemoved)
        FoliageActor->PostEditChange();

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

- **`FOnClicked::CreateLambda` is required.** `FOnClicked` is `TDelegate<FReply()>` — zero arguments. `_Raw` with trailing payload arguments does not compile for zero-argument delegates.
- **`FScopedTransaction` wraps all editor mutations.** Fully undoable via Ctrl+Z. Always call `Modify()` before mutating.
- **`ShutdownModule` must unregister the customization.** Omitting causes a crash on editor plugin reload.
- **Sub-levels:** Run the converter once per sub-level while active. `GetInstancedFoliageActorForCurrentLevel` returns only the active level's foliage.
