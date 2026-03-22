// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "FactionTypes.h"
#include "FactionDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#include "FactionRelationshipTable.generated.h"

/**
 * UFactionRelationshipTable
 *
 * Singleton project asset. Lists all UFactionDefinition assets and all
 * explicit faction-pair relationships.
 *
 * One table per project, assigned in UGameCoreFactionSettings.
 * UFactionSubsystem uses this as the sole source for BuildCache().
 */
UCLASS(BlueprintType)
class GAMECORE_API UFactionRelationshipTable : public UPrimaryDataAsset
{
    GENERATED_BODY()
public:

    // All UFactionDefinition assets registered for this project.
    // A faction referenced in ExplicitRelationships but absent here
    // is logged as an error in non-shipping builds by ValidateTable().
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Factions")
    TArray<TSoftObjectPtr<UFactionDefinition>> Factions;

    // Explicit faction-pair relationships.
    // Pairs are order-independent — UFactionSubsystem::BuildCache sorts before insertion.
    // If a pair is absent, UFactionSubsystem resolves via DefaultRelationship FMath::Min.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Relationships")
    TArray<FFactionRelationshipOverride> ExplicitRelationships;

#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(
        FDataValidationContext& Context) const override;
#endif
};
