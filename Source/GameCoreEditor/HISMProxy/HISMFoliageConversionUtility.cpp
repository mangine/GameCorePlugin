// Copyright GameCore Plugin. All Rights Reserved.

#include "HISMProxy/HISMFoliageConversionUtility.h"
#include "HISMProxy/HISMProxyHostActor.h"

#include "InstancedFoliageActor.h"
#include "FoliageType_InstancedStaticMesh.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"

#include "ScopedTransaction.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Engine/World.h"
#include "Engine/Level.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreEditor, Log, All);

// ─────────────────────────────────────────────────────────────────────────────
// ConvertFoliageToProxyHost
// ─────────────────────────────────────────────────────────────────────────────

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

            // Descending removal avoids index shifts from RemoveInstance compaction (AD-10).
            RemovalIndices.Sort([](int32 A, int32 B) { return A > B; });

            for (int32 RemoveIdx : RemovalIndices)
                FoliageHISM->RemoveInstance(RemoveIdx);

            bAnyRemoved = true;
        }
    }

    // FFoliageInfo::Refresh signature varies between UE versions.
    // PostEditChange triggers a full internal refresh via the standard change
    // notification path — stable across UE 5.x (AD-11).
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

// ─────────────────────────────────────────────────────────────────────────────
// PreviewConversion
// ─────────────────────────────────────────────────────────────────────────────

FString UHISMFoliageConversionUtility::PreviewConversion() const
{
    if (!TargetHostActor)
        return TEXT("ERROR: TargetHostActor is null.");

    UWorld* World = TargetHostActor->GetWorld();
    AInstancedFoliageActor* FoliageActor =
        AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(
            World, /*bCreateIfNone=*/false);

    if (!FoliageActor)
        return TEXT("No foliage actor found in current level.");

    TMap<UFoliageType*, FFoliageInfo*> AllFoliageInfo =
        FoliageActor->GetAllFoliageInfo();

    FString Result;
    int32   TotalWouldConvert = 0;
    int32   TotalWouldSkip    = 0;

    for (auto& [FoliageType, FoliageInfo] : AllFoliageInfo)
    {
        if (!FoliageInfo || !FoliageType) { continue; }

        UFoliageType_InstancedStaticMesh* ISMType =
            Cast<UFoliageType_InstancedStaticMesh>(FoliageType);
        if (!ISMType || !ISMType->GetStaticMesh()) { continue; }

        UHierarchicalInstancedStaticMeshComponent* FoliageHISM =
            FoliageInfo->GetComponent();
        const int32 NumInstances = FoliageHISM ? FoliageHISM->GetInstanceCount() : 0;

        const int32 TypeIndex =
            TargetHostActor->FindTypeIndexByMesh(ISMType->GetStaticMesh());

        if (TypeIndex == INDEX_NONE)
        {
            Result += FString::Printf(TEXT("SKIP  [%s] — not in InstanceTypes (%d instances)\n"),
                *ISMType->GetStaticMesh()->GetName(), NumInstances);
            TotalWouldSkip += NumInstances;
        }
        else
        {
            Result += FString::Printf(TEXT("WILL CONVERT  [%s] → TypeIndex %d (%d instances)\n"),
                *ISMType->GetStaticMesh()->GetName(), TypeIndex, NumInstances);
            TotalWouldConvert += NumInstances;
        }
    }

    Result += FString::Printf(TEXT("\nTotal to convert: %d  |  Total to skip: %d"),
        TotalWouldConvert, TotalWouldSkip);
    return Result;
}
