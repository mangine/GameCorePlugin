# UZoneTrackerComponent

**File:** `GameCore/Source/GameCore/Zone/ZoneTrackerComponent.h` / `.cpp`

Opt-in component that gives an actor zone enter/exit awareness. Polls `UZoneSubsystem` at a configurable interval, diffs the zone set, and broadcasts `FZoneTransitionMessage` via `UGameCoreEventBus`.

Runs identically on server and client. Both broadcast independently — this is correct and intentional.

---

## Class Definition

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneTrackerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UZoneTrackerComponent();

    // How often (seconds) to re-query. 0 = every tick.
    // 0.1s is appropriate for players. Ships or slow actors can use 0.3s.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    float QueryInterval = 0.1f;

    // If set, uses this component's world location instead of the actor root.
    // Useful for large actors (ships) where a specific anchor point is needed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    TObjectPtr<USceneComponent> LocationAnchor;

    // Read-only: zones this actor is currently inside, sorted by Priority descending.
    UFUNCTION(BlueprintCallable, Category="Zone")
    const TArray<AZoneActor*>& GetCurrentZones() const { return CurrentZones; }

    // True if the actor is inside at least one zone with the given type tag.
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool IsInZoneOfType(FGameplayTag TypeTag) const;

    // ---- UActorComponent overrides ----
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    TArray<AZoneActor*> CurrentZones;
    float TimeSinceLastQuery = 0.f;

    void EvaluateZones();
    FVector GetQueryLocation() const;
    void BroadcastTransition(AZoneActor* Zone, bool bEntered) const;
};
```

---

## Implementation

```cpp
UZoneTrackerComponent::UZoneTrackerComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UZoneTrackerComponent::BeginPlay()
{
    Super::BeginPlay();
    // CurrentZones starts empty. First EvaluateZones() fires bEntered=true
    // for all zones the actor is already inside. This is intentional.
}

void UZoneTrackerComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Func)
{
    Super::TickComponent(DeltaTime, TickType, Func);

    TimeSinceLastQuery += DeltaTime;
    if (TimeSinceLastQuery >= QueryInterval)
    {
        TimeSinceLastQuery = 0.f;
        EvaluateZones();
    }
}

bool UZoneTrackerComponent::IsInZoneOfType(FGameplayTag TypeTag) const
{
    for (const AZoneActor* Zone : CurrentZones)
    {
        if (Zone && Zone->DataAsset && Zone->DataAsset->ZoneTypeTag.MatchesTagExact(TypeTag))
            return true;
    }
    return false;
}

void UZoneTrackerComponent::EvaluateZones()
{
    UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>();
    if (!Sub) return;

    const FVector Location = GetQueryLocation();
    TArray<AZoneActor*> NewZones = Sub->QueryZonesAtPoint(Location);

    // Exits: in CurrentZones but not in NewZones
    for (AZoneActor* Zone : CurrentZones)
    {
        if (!NewZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/false);
    }

    // Enters: in NewZones but not in CurrentZones
    for (AZoneActor* Zone : NewZones)
    {
        if (!CurrentZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/true);
    }

    CurrentZones = MoveTemp(NewZones);
}

void UZoneTrackerComponent::BroadcastTransition(AZoneActor* Zone, bool bEntered) const
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        FZoneTransitionMessage Msg;
        Msg.TrackedActor = GetOwner();
        Msg.ZoneActor    = Zone;
        Msg.StaticData   = Zone->DataAsset;
        Msg.DynamicState = Zone->DynamicState;
        Msg.bEntered     = bEntered;
        Bus->Broadcast<FZoneTransitionMessage>(
            GameCore::Zone::Tags::Channel_Transition,
            Msg,
            EGameCoreEventScope::Both);
    }
}

FVector UZoneTrackerComponent::GetQueryLocation() const
{
    if (LocationAnchor)
        return LocationAnchor->GetComponentLocation();
    return GetOwner()->GetActorLocation();
}
```

---

## Notes

- **Scope is `Both`:** Zone transitions are cosmetically meaningful on the client and authoritatively meaningful on the server. Both machines broadcast independently — listeners decide what to do based on their own authority context.
- **Initial state:** On `BeginPlay`, `CurrentZones` is empty. The first `EvaluateZones` generates `bEntered = true` for all zones the actor starts inside. Listeners must be registered before this fires (i.e. before the component's first tick).
- **Stale actor safety:** `QueryZonesAtPoint` uses `TWeakObjectPtr` internally; the returned array contains only valid pointers. No extra validity check is needed in `EvaluateZones`.
- **Tick interval vs. precision:** At `QueryInterval = 0.1s` and player speed 500 UU/s, worst-case travel between checks is 50 UU. For very small trigger zones (< 200 UU radius) set `QueryInterval = 0`.
- **Tick group:** Consider `TG_PostPhysics` if the actor's position is set during `TG_PrePhysics` — otherwise the tracker reads last frame's position for the current frame's query.
