// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMProxyHostActor.h"
#include "HISMProxy/HISMProxyBridgeComponent.h"
#include "HISMProxy/HISMProxyActor.h"
#include "HISMProxy/HISMProxyConfig.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#if WITH_EDITOR
#include "Misc/MessageDialog.h"
#include "Logging/MessageLog.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGameCore, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// Static member definition
// ─────────────────────────────────────────────────────────────────────────────

#if WITH_EDITOR
const FName AHISMProxyHostActor::HISMProxyManagedTag = TEXT("HISMProxyManaged");
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

AHISMProxyHostActor::AHISMProxyHostActor()
{
    PrimaryActorTick.bCanEverTick = false;

    // Root component required for HISM attachment.
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

// ─────────────────────────────────────────────────────────────────────────────
// BeginPlay
// ─────────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────────
// Editor
// ─────────────────────────────────────────────────────────────────────────────

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

#endif // WITH_EDITOR
