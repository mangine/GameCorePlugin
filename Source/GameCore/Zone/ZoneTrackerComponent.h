#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "ZoneTrackerComponent.generated.h"

class AZoneActor;

/**
 * UZoneTrackerComponent
 *
 * Opt-in component that gives an actor zone enter/exit awareness.
 * Polls UZoneSubsystem at a configurable interval, diffs the zone set,
 * and broadcasts FZoneTransitionMessage via UGameCoreEventBus (scope: Both).
 *
 * Runs identically on server and client. Both broadcast independently — correct and intentional.
 *
 * Initial state: CurrentZones starts empty. The first EvaluateZones fires bEntered=true
 * for all zones the actor is already inside.
 */
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneTrackerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UZoneTrackerComponent();

    /**
     * How often (seconds) to re-query.
     * 0 = every tick.
     * 0.1s is appropriate for players. Ships or slow actors can use 0.3s.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    float QueryInterval = 0.1f;

    /**
     * If set, uses this component's world location instead of the actor root.
     * Useful for large actors (ships) where a specific anchor point is needed.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    TObjectPtr<USceneComponent> LocationAnchor;

    /** Read-only: zones this actor is currently inside, sorted by Priority descending. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    const TArray<AZoneActor*>& GetCurrentZones() const { return CurrentZones; }

    /** True if the actor is currently inside at least one zone with the given type tag. */
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool IsInZoneOfType(FGameplayTag TypeTag) const;

    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    TArray<AZoneActor*> CurrentZones;
    float TimeSinceLastQuery = 0.f;

    void    EvaluateZones();
    FVector GetQueryLocation() const;
    void    BroadcastTransition(AZoneActor* Zone, bool bEntered) const;
};
