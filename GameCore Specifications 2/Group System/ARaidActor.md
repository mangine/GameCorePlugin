# ARaidActor

**File:** `Group/RaidActor.h` / `RaidActor.cpp`
**Authority:** Server only

Server-side container for a multi-group raid. Holds references to constituent `AGroupActor` instances and delegates all management logic to `URaidComponent`.

---

## Class Declaration

```cpp
UCLASS(NotBlueprintable)
class GAMECORE_API ARaidActor : public AActor
{
    GENERATED_BODY()
public:
    ARaidActor();

    URaidComponent* GetRaidComponent() const { return RaidComponent; }

private:
    UPROPERTY()
    TObjectPtr<URaidComponent> RaidComponent;
};
```

---

## Constructor

```cpp
ARaidActor::ARaidActor()
{
    bReplicates       = false;
    bNetLoadOnClient  = false;
    PrimaryActorTick.bCanEverTick = false;
    RaidComponent = CreateDefaultSubobject<URaidComponent>(TEXT("RaidComponent"));
}
```

---

## Implementation Notes

- `bReplicates = false` and `bNetLoadOnClient = false` are mandatory. Clients never see this actor.
- `PrimaryActorTick.bCanEverTick = false` — no per-frame logic on the actor itself.
- All mutation logic lives in `URaidComponent`.
