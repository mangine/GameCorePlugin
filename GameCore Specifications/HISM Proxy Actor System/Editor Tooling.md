# Editor Tooling

**Sub-page of:** [HISM Proxy Actor System](HISM%20Proxy%20Actor%20System.md)

Editor tooling for the HISM Proxy Actor System lives in the `GameCoreEditor` module, which is loaded only in editor builds. It consists of two components:

1. **`UHISMProxyHostActorDetails`** — A Details panel customization for `AHISMProxyHostActor` that adds per-type "Add Instance" buttons and a global "Validate Setup" button.
2. **`UHISMFoliageConversionUtility`** — An `UEditorUtilityObject` that imports instances from the UE Foliage Tool (`AInstancedFoliageActor`) into a target `AHISMProxyHostActor`.

---

## Module Setup

The `GameCoreEditor` module must declare `GameCore` as a dependency and register the Details customization at startup.

```cpp
// GameCoreEditor.cpp — module startup
void FGameCoreEditorModule::StartupModule()
{
    FPropertyEditorModule& PropertyModule =
        FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");

    PropertyModule.RegisterCustomClassLayout(
        AHISMProxyHostActor::StaticClass()->GetFName(),
        FOnGetDetailCustomizationInstance::CreateStatic(
            &UHISMProxyHostActorDetails::MakeInstance));

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

`GameCoreEditor.Build.cs` must include:

```csharp
PrivateDependencyModuleNames.AddRange(new string[]
{
    "GameCore",
    "UnrealEd",
    "PropertyEditor",
    "SlateCore",
    "Slate",
    "Foliage",       // for AInstancedFoliageActor access
    "EditorSubsystem"
});
```

---

## `UHISMProxyHostActorDetails`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMProxyHostActorDetails.h / .cpp`

A `IDetailCustomization` subclass. It replaces the default Details panel for `AHISMProxyHostActor` with a layout that adds:
- An "Add Instance" button per `InstanceTypes` entry (adds one instance at the actor's pivot, or at a selected transform if a tool is active)
- A "Validate Setup" button at the top of the panel
- A read-only instance count display per entry

### Class Definition

```cpp
class FHISMProxyHostActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    // Adds the per-type rows (instance count + Add Instance button) to the panel.
    void AddInstanceTypeRows(IDetailLayoutBuilder& DetailBuilder,
                              AHISMProxyHostActor* HostActor);

    // Click handler for the per-type "Add Instance" button.
    FReply OnAddInstanceClicked(AHISMProxyHostActor* HostActor, int32 TypeIndex);

    // Click handler for the global "Validate Setup" button.
    FReply OnValidateClicked(AHISMProxyHostActor* HostActor);

    // Weak reference to the actor being customized.
    TWeakObjectPtr<AHISMProxyHostActor> CachedHostActor;
};
```

### `CustomizeDetails` Implementation

```cpp
void FHISMProxyHostActorDetails::CustomizeDetails(
    IDetailLayoutBuilder& DetailBuilder)
{
    // Get the actor being inspected.
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.IsEmpty()) { return; }

    AHISMProxyHostActor* HostActor =
        Cast<AHISMProxyHostActor>(Objects[0].Get());
    if (!HostActor) { return; }
    CachedHostActor = HostActor;

    // Add the Validate button at the top of the HISM Proxy category.
    IDetailCategoryBuilder& Category =
        DetailBuilder.EditCategory("HISM Proxy");

    Category.AddCustomRow(FText::FromString("Validate Setup"))
    .WholeRowContent()
    [
        SNew(SButton)
        .Text(FText::FromString("Validate Setup"))
        .OnClicked_Raw(this, &FHISMProxyHostActorDetails::OnValidateClicked, HostActor)
    ];

    // Add per-type rows.
    AddInstanceTypeRows(DetailBuilder, HostActor);
}

void FHISMProxyHostActorDetails::AddInstanceTypeRows(
    IDetailLayoutBuilder& DetailBuilder, AHISMProxyHostActor* HostActor)
{
    IDetailCategoryBuilder& Category =
        DetailBuilder.EditCategory("HISM Proxy");

    for (int32 i = 0; i < HostActor->InstanceTypes.Num(); ++i)
    {
        const FHISMProxyInstanceType& Entry = HostActor->InstanceTypes[i];
        const int32 InstanceCount = Entry.HISM ? Entry.HISM->GetInstanceCount() : 0;

        const FString RowLabel = FString::Printf(
            TEXT("%s  (%d instances)"), *Entry.TypeName.ToString(), InstanceCount);

        Category.AddCustomRow(FText::FromString(RowLabel))
        .NameContent()
        [
            SNew(STextBlock).Text(FText::FromString(RowLabel))
        ]
        .ValueContent()
        [
            SNew(SButton)
            .Text(FText::FromString("Add Instance at Pivot"))
            .OnClicked_Raw(this,
                &FHISMProxyHostActorDetails::OnAddInstanceClicked, HostActor, i)
        ];
    }
}

FReply FHISMProxyHostActorDetails::OnAddInstanceClicked(
    AHISMProxyHostActor* HostActor, int32 TypeIndex)
{
    if (HostActor)
    {
        // Uses the actor's current pivot as placement transform.
        // Designers move the actor to the desired position, click Add, repeat.
        HostActor->AddInstanceForType(TypeIndex, HostActor->GetActorTransform());
        // Force the Details panel to refresh instance counts.
        FPropertyEditorModule& PropertyModule =
            FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
        PropertyModule.NotifyCustomizationModuleChanged();
    }
    return FReply::Handled();
}

FReply FHISMProxyHostActorDetails::OnValidateClicked(
    AHISMProxyHostActor* HostActor)
{
    if (HostActor) { HostActor->ValidateSetup(); }
    return FReply::Handled();
}
```

---

## `UHISMFoliageConversionUtility`

**Files:** `GameCoreEditor/Public/HISMProxy/HISMFoliageConversionUtility.h / .cpp`

An `UEditorUtilityObject` that reads instance transforms from an `AInstancedFoliageActor` in the current level and writes them into a target `AHISMProxyHostActor`. This is the **recommended workflow for mass instance placement** — paint with the Foliage Tool, then convert once.

### Class Definition

```cpp
UCLASS(Blueprintable, meta=(ShowWorldContextPin))
class GAMECOREDITOR_API UHISMFoliageConversionUtility : public UEditorUtilityObject
{
    GENERATED_BODY()
public:
    // The host actor to import instances into.
    // Must already have InstanceTypes configured with matching meshes.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    TObjectPtr<AHISMProxyHostActor> TargetHostActor;

    // If true, removes the imported instances from the Foliage Actor after conversion.
    // Recommended: true. Leaving foliage in place causes duplicate rendering.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    bool bRemoveFoliageAfterConversion = true;

    // Runs the conversion. Matches foliage mesh types to InstanceTypes entries by
    // UStaticMesh pointer equality. Unmatched meshes are skipped with a warning.
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    void ConvertFoliageToProxyHost();

    // Returns a summary string of what would be converted without making changes.
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    FString PreviewConversion() const;
};
```

### `ConvertFoliageToProxyHost` Implementation

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
    if (!World) { return; }

    // Find the Foliage Actor in the current level.
    // UE stores all foliage in AInstancedFoliageActor (one per level).
    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World);
    if (!FoliageActor)
    {
        UE_LOG(LogGameCoreEditor, Warning,
            TEXT("ConvertFoliageToProxyHost: no foliage found in current level."));
        return;
    }

    int32 TotalConverted = 0;
    int32 TotalSkipped   = 0;

    // Iterate all foliage mesh components.
    // GetAllFoliageInfo returns a TMap<UFoliageType*, FFoliageInfo*>.
    for (auto& [FoliageType, FoliageInfo] : FoliageActor->GetAllFoliageInfo())
    {
        if (!FoliageInfo || !FoliageType) { continue; }

        // Get the source mesh from the foliage type.
        // UFoliageType_InstancedStaticMesh holds the mesh reference.
        UFoliageType_InstancedStaticMesh* ISMFoliageType =
            Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
        if (!ISMFoliageType || !ISMFoliageType->GetStaticMesh()) { continue; }

        const UStaticMesh* Mesh = ISMFoliageType->GetStaticMesh();
        const int32 TypeIndex = TargetHostActor->FindTypeIndexByMesh(Mesh);

        if (TypeIndex == INDEX_NONE)
        {
            UE_LOG(LogGameCoreEditor, Warning,
                TEXT("ConvertFoliageToProxyHost: mesh '%s' has no matching InstanceType — skipped."),
                *Mesh->GetName());
            ++TotalSkipped;
            continue;
        }

        // Iterate all instances of this foliage type.
        TArray<int32> IndicesToRemove;
        FoliageInfo->ForEachInstance([&](FFoliageInstanceId Id,
                                         const FFoliageInstance& Instance)
        {
            // Convert foliage local transform to world transform.
            const FTransform WorldTransform = Instance.GetInstanceWorldTransform(
                FoliageActor->GetActorTransform());

            TargetHostActor->AddInstanceForType(TypeIndex, WorldTransform);
            IndicesToRemove.Add(Id.Index);
            ++TotalConverted;
            return true; // continue iteration
        });

        if (bRemoveFoliageAfterConversion && !IndicesToRemove.IsEmpty())
        {
            // Remove converted instances from the Foliage Actor.
            FoliageInfo->RemoveInstances(FoliageActor, IndicesToRemove,
                /*bRebuildTree=*/true);
        }
    }

    UE_LOG(LogGameCoreEditor, Log,
        TEXT("ConvertFoliageToProxyHost: converted %d instances, skipped %d mesh types."),
        TotalConverted, TotalSkipped);

    // Mark the level dirty so Unreal saves the changes.
    World->GetCurrentLevel()->MarkPackageDirty();

    FNotificationInfo Info(
        FText::Format(INVTEXT("Converted {0} instances to HISM Proxy Host."),
                      FText::AsNumber(TotalConverted)));
    Info.bFireAndForget = true;
    Info.ExpireDuration = 4.f;
    FSlateNotificationManager::Get().AddNotification(Info);
}
```

---

## Using the Foliage Converter

1. Right-click in the Content Browser → **Editor Utilities → Editor Utility Object**
2. Set parent class to `UHISMFoliageConversionUtility`
3. Open the asset, set `TargetHostActor` to your host actor in the level
4. Click **Convert Foliage To Proxy Host** in the Details panel (it is a `CallInEditor` function)

Alternatively, subclass `UHISMFoliageConversionUtility` as an `UEditorUtilityWidget` to expose it as a proper editor UI panel.

---

## Notes

- **Foliage Tool vs host actor rendering are mutually exclusive.** If `bRemoveFoliageAfterConversion = true` (recommended), the foliage instances are removed and only the host actor's HISM renders them. If left as false, both render the same mesh at the same positions — visually doubled.
- **`AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel`** returns the foliage actor for the persistent level. For sub-levels or world partition setups, additional iteration over sub-level foliage actors may be required.
- **The Details panel customization** must be unregistered on module shutdown or UE will crash when reloading the editor. The `ShutdownModule` implementation above handles this.
- **`CallInEditor`** on `ConvertFoliageToProxyHost` makes it appear as a button in the Editor Utility Object's Details panel when opened in the editor. No UI Blueprint required for basic use.
