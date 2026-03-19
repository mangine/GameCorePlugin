# Zone Tracker Component

`UZoneTrackerComponent` is the opt-in component that gives an actor enter/exit awareness. It polls the zone subsystem each tick (at a configurable interval) and compares the current zone set against the previous frame's set to detect transitions. It runs on both server and client independently.

---

## Class Definition

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneTrackerComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UZoneTrackerComponent();

    // How often (seconds) to re-query the subsystem. 0 = every tick.
    // Default 0.1s is appropriate for players. Ships might use 0.3s.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    float QueryInterval = 0.1f;

    // If set, use this component's location instead of the actor root.
    // Useful for large actors (ships) where you want a specific anchor point.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone")
    TObjectPtr<USceneComponent> LocationAnchor;

    // Read-only: current zones this actor is inside, sorted by priority.
    UFUNCTION(BlueprintCallable, Category="Zone")
    const TArray<AZoneActor*>& GetCurrentZones() const { return CurrentZones; }

    // Returns true if this actor is currently inside a zone with the given type tag.
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool IsInZoneOfType(FGameplayTag TypeTag) const;

    // ---- UActorComponent overrides ----
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

private:
    TArray<AZoneActor*> CurrentZones;   // Zones from last evaluated frame
    float TimeSinceLastQuery = 0.f;

    void EvaluateZones();
    FVector GetQueryLocation() const;

    void BroadcastTransition(AZoneActor* Zone, bool bEntered) const;
};
```

---

## Key Method Implementations

### `TickComponent`

```cpp
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
```

### `EvaluateZones`

Core logic: diff the old and new zone sets, fire GMS messages for each transition.

```cpp
void UZoneTrackerComponent::EvaluateZones()
{
    UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>();
    if (!Sub) return;

    const FVector Location = GetQueryLocation();
    TArray<AZoneActor*> NewZones = Sub->QueryZonesAtPoint(Location);

    // Detect exits: in CurrentZones but not in NewZones
    for (AZoneActor* Zone : CurrentZones)
    {
        if (!NewZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/false);
    }

    // Detect enters: in NewZones but not in CurrentZones
    for (AZoneActor* Zone : NewZones)
    {
        if (!CurrentZones.Contains(Zone))
            BroadcastTransition(Zone, /*bEntered=*/true);
    }

    CurrentZones = MoveTemp(NewZones);
}
```

### `BroadcastTransition`

```cpp
void UZoneTrackerComponent::BroadcastTransition(AZoneActor* Zone, bool bEntered) const
{
    FZoneTransitionMessage Msg;
    Msg.TrackedActor = GetOwner();
    Msg.ZoneActor    = Zone;
    Msg.StaticData   = Zone->DataAsset;
    Msg.DynamicState = Zone->DynamicState;
    Msg.bEntered     = bEntered;

    UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);
    GMS.BroadcastMessage(GameCore::Zone::Tags::Channel_Transition, Msg);
}
```

### `GetQueryLocation`

```cpp
FVector UZoneTrackerComponent::GetQueryLocation() const
{
    if (LocationAnchor)
        return LocationAnchor->GetComponentLocation();
    return GetOwner()->GetActorLocation();
}
```

---

## Notes

- **Tick interval vs precision:** 0.1 s at 500 UU/s player speed means worst-case 50 UU travel between checks. For a zone border this is negligible. For very small trigger zones (< 200 UU radius) consider setting `QueryInterval = 0`.
- **Client authority:** The component runs on every net role. On the client it fires events for local cosmetics immediately. On the server it fires events for authoritative gameplay consequences. Both are correct — listeners must be authoritative about what they do with the event, not the event itself.
- **Stale actor safety:** `QueryZonesAtPoint` uses `TWeakObjectPtr` internally; the returned array contains only valid pointers. No explicit validity check needed in `EvaluateZones`.
- **Initial state:** On `BeginPlay`, `CurrentZones` is empty. The first `EvaluateZones` call will generate `bEntered = true` for all zones the actor starts inside. This is correct and intentional — listeners always get an initial enter event.
