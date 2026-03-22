// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorUtilityObject.h"
#include "HISMFoliageConversionUtility.generated.h"

class AHISMProxyHostActor;

/**
 * UHISMFoliageConversionUtility
 *
 * Editor utility object that imports Foliage Tool instances from the current
 * level's AInstancedFoliageActor into a target AHISMProxyHostActor.
 *
 * Three-pass implementation:
 *   Pass 1: Read all transforms into local storage (no mutations).
 *   Pass 2: Add transforms to host actor.
 *   Pass 3: Remove from foliage highest-index-first (descending removal
 *           avoids index invalidation from RemoveInstance's internal compaction).
 *
 * All mutations wrapped in FScopedTransaction — fully undoable via Ctrl+Z.
 */
UCLASS(Blueprintable)
class GAMECOREEDITOR_API UHISMFoliageConversionUtility : public UEditorUtilityObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    TObjectPtr<AHISMProxyHostActor> TargetHostActor;

    /**
     * When true, removes matching foliage instances after adding to host actor.
     * Must be true to prevent double rendering.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Conversion")
    bool bRemoveFoliageAfterConversion = true;

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    void ConvertFoliageToProxyHost();

    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Conversion")
    FString PreviewConversion() const;
};
