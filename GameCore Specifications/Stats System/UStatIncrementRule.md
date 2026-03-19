# UStatIncrementRule

## Role

Abstract, instanced UObject that acts as **declarative metadata** on a `UStatDefinition`. It documents which GMS channel drives a stat and provides a named increment amount for editor clarity and tooling.

> **Important:** GMS is strictly typed — `RegisterListener<T>` requires the exact broadcast struct type at compile time. `UStatIncrementRule` does **not** auto-register GMS listeners. The game module is responsible for subscribing to GMS channels and calling `UStatComponent::AddToStat()` directly. See [Integration](./Integration.md).

One subclass per distinct increment shape. A single subclass can be reused across any number of `UStatDefinition` assets.

---

## Abstract Base Class

```cpp
// GameCore module
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UStatIncrementRule : public UObject
{
    GENERATED_BODY()

public:
    // Documents which GMS channel drives this rule.
    // Used for editor display, validation, and tooling only.
    // The game module is responsible for the actual GMS subscription.
    UFUNCTION(BlueprintNativeEvent)
    FGameplayTag GetChannelTag() const;
    virtual FGameplayTag GetChannelTag_Implementation() const
        PURE_VIRTUAL(UStatIncrementRule::GetChannelTag_Implementation, return FGameplayTag::EmptyTag;);

    // Returns the increment amount for this rule.
    // Called by the game module's GMS callback after receiving a typed message.
    // Subclasses override this to return dynamic values (e.g. damage amount from context).
    // Base case: return a fixed constant.
    UFUNCTION(BlueprintNativeEvent)
    float GetIncrement() const;
    virtual float GetIncrement_Implementation() const
        PURE_VIRTUAL(UStatIncrementRule::GetIncrement_Implementation, return 0.f;);
};
```

> **Note:** `GetIncrement()` takes no payload parameter because GMS is typed — the game module callback already has the fully-typed message struct. If the increment depends on payload data (e.g. damage amount), the game module reads it directly from the typed struct and passes the value to `AddToStat()`, bypassing `GetIncrement()` entirely. The rule in that case is purely a marker/config object.

---

## Built-in: UConstantIncrementRule (GameCore)

Covers the most common case: fixed amount increment, channel documented on the asset.

```cpp
UCLASS(DisplayName="Constant Increment")
class GAMECORE_API UConstantIncrementRule : public UStatIncrementRule
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category="Rule")
    FGameplayTag ChannelTag;

    UPROPERTY(EditDefaultsOnly, Category="Rule", meta=(ClampMin="0.0"))
    float Amount = 1.f;

    virtual FGameplayTag GetChannelTag_Implementation() const override { return ChannelTag; }
    virtual float GetIncrement_Implementation() const override { return Amount; }
};
```

---

## Game Module Usage Pattern

The game module subscribes to GMS with the correct typed struct, then calls `AddToStat()`. The rule's `GetIncrement()` is used when the increment is static; otherwise the game module reads the value from the payload directly.

```cpp
// In game module startup (e.g. GameMode or subsystem BeginPlay)
UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);

// Case 1: fixed increment — use rule's GetIncrement()
EnemyKilledHandle = GMS.RegisterListener<FEnemyKilledMessage>(
    TAG_GameplayMessage_Combat_EnemyKilled,
    [this](FGameplayTag, const FEnemyKilledMessage& Msg)
    {
        if (UStatComponent* Stats = GetStatComponent(Msg.KillerPlayerId))
        {
            // Rule on DA_Stat_EnemiesKilled documents channel + amount = 1
            Stats->AddToStat(TAG_Stat_EnemiesKilled, 1.f);
        }
    }
);

// Case 2: payload-driven increment — read directly from typed struct
DamageDealtHandle = GMS.RegisterListener<FDamageDealtMessage>(
    TAG_GameplayMessage_Combat_DamageDealt,
    [this](FGameplayTag, const FDamageDealtMessage& Msg)
    {
        if (UStatComponent* Stats = GetStatComponent(Msg.InstigatorPlayerId))
        {
            // DamageAmount comes directly from the typed struct
            Stats->AddToStat(TAG_Stat_TotalDamageDealt, Msg.DamageAmount);
        }
    }
);
```

---

## Notes

- `EditInlineNew` + `DefaultToInstanced` makes these objects editable inline inside `UStatDefinition` without needing a separate asset.
- `GetIncrement()` must be const and stateless — no side effects.
- Never store mutable state on a `UStatIncrementRule` — it is shared across all players via the DataAsset.
- The rule's `GetChannelTag()` is validated in non-shipping builds by `UStatComponent::ValidateDefinitions()` to catch misconfigured assets early.
- `GetChannelTag` is `BlueprintNativeEvent` to allow BP subclassing for rapid prototyping; ship builds should prefer C++ subclasses.
