// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HISMProxyHostActor.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UHISMProxyConfig;
class UHISMProxyBridgeComponent;
class AHISMProxyActor;

// ─────────────────────────────────────────────────────────────────────────────
// FHISMProxyInstanceType
// ─────────────────────────────────────────────────────────────────────────────

/**
 * FHISMProxyInstanceType
 *
 * One entry per mesh type. The host actor auto-creates HISM and Bridge
 * components when an entry is populated in the editor.
 */
USTRUCT(BlueprintType)
struct GAMECORE_API FHISMProxyInstanceType
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName TypeName = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UStaticMesh> Mesh = nullptr;

    /**
     * Must be a concrete Blueprint subclass of AHISMProxyActor.
     * Must NOT be AHISMProxyActor::StaticClass() itself — validated by ValidateSetup.
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSubclassOf<AHISMProxyActor> ProxyClass = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyConfig> Config = nullptr;

    // ── Pool Sizing ──────────────────────────────────────────────────────────
    // Formula: MinPoolSize = ceil(PI * (ActivationRadius/100)^2
    //                             * InstanceDensity * ConcurrentPlayers * 1.2)
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 MinPoolSize = 8;

    /** Hard ceiling on pool growth. 0 = strict pre-allocation (no growth allowed). */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "0"))
    int32 MaxPoolSize = 64;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pool", meta = (ClampMin = "1"))
    int32 GrowthBatchSize = 8;

    // ── Runtime (auto-managed, do not set manually) ──────────────────────────

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHierarchicalInstancedStaticMeshComponent> HISM = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    TObjectPtr<UHISMProxyBridgeComponent> Bridge = nullptr;

    /**
     * Always derived from array index. Serialized value is refreshed on load
     * and on every structural change. Do not trust as a standalone source of truth.
     */
    UPROPERTY(VisibleAnywhere)
    int32 TypeIndex = INDEX_NONE;
};

// ─────────────────────────────────────────────────────────────────────────────
// AHISMProxyHostActor
// ─────────────────────────────────────────────────────────────────────────────

/**
 * AHISMProxyHostActor
 *
 * The primary level-placed actor of the HISM Proxy system. Developers configure
 * InstanceTypes; the host creates HISM and bridge components automatically and
 * validates them at BeginPlay.
 *
 * Server-only: pool management runs on the server. Clients receive state via
 * standard Actor replication.
 */
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
    /**
     * Re-entry guard for RebuildTypeIndices.
     * Declared outside WITH_EDITOR to avoid UHT class layout mismatch between
     * editor and non-editor builds (AD-8). Costs 1 byte in shipping — acceptable.
     */
    UPROPERTY()
    bool bIsRebuilding = false;

#if WITH_EDITOR
    /** Tag written to every auto-created component for ownership tracking. */
    static const FName HISMProxyManagedTag; // = TEXT("HISMProxyManaged")

    void CreateComponentsForEntry(FHISMProxyInstanceType& Entry, int32 EntryIndex);
    void DestroyOrphanedComponents();
    void RebuildTypeIndices();
#endif
};
