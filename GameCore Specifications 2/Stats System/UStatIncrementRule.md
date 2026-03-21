# UStatIncrementRule

**Module:** `GameCore` | **File:** `Stats/StatIncrementRule.h`

---

## Role

Abstract, instanced UObject authored on a `UStatDefinition`. Each rule binds one Event Bus channel to a stat increment. `UStatComponent` reads these rules at `BeginPlay` and auto-registers one listener per rule — no game-module wiring code is ever needed for event-driven increments.

One subclass per distinct payload shape. A single subclass can be reused across any number of `UStatDefinition` assets.

---

## Abstract Base Class

```cpp
// GameCore/Source/GameCore/Stats/StatIncrementRule.h

// Abstract, instanced — rules are owned inline by UStatDefinition assets.
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UStatIncrementRule : public UObject
{
    GENERATED_BODY()
public:
    // The Event Bus channel this rule listens on.
    // Must return a valid leaf FGameplayTag.
    // Validated at BeginPlay in non-shipping builds.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Rule")
    FGameplayTag GetChannelTag() const;
    virtual FGameplayTag GetChannelTag_Implementation() const
        PURE_VIRTUAL(UStatIncrementRule::GetChannelTag_Implementation, return FGameplayTag::EmptyTag;);

    // Extract the increment amount from the incoming payload.
    //
    // Called by UStatComponent's auto-registered listener on every message.
    // Return 0.f to suppress the increment (type mismatch, missing data, etc.).
    //
    // Implementations use Payload.GetPtr<FMyStruct>() to access typed fields.
    //
    // IMPORTANT: Must be const and stateless. This object is shared across all
    // players via the DataAsset. Never store per-player mutable state here.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Rule")
    float ExtractIncrement(const FInstancedStruct& Payload) const;
    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const
        PURE_VIRTUAL(UStatIncrementRule::ExtractIncrement_Implementation, return 0.f;);
};
```

---

## Built-in: UConstantIncrementRule

Ignores the payload and returns a fixed configured amount. Covers the most common case: every message on the channel = fixed increment.

```cpp
// Can live in StatIncrementRule.h

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

    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override
    {
        return Amount; // Payload ignored — fixed amount.
    }
};
```

---

## Game Module Subclass Pattern

For payload-driven increments, subclass `UStatIncrementRule` in the game module:

```cpp
// Game module — DamageIncrementRule.h

UCLASS(DisplayName="Damage Amount Increment")
class MYGAME_API UDamageIncrementRule : public UStatIncrementRule
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, Category="Rule")
    FGameplayTag ChannelTag;

    virtual FGameplayTag GetChannelTag_Implementation() const override { return ChannelTag; }

    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override
    {
        if (const FDamageDealtMessage* Msg = Payload.GetPtr<FDamageDealtMessage>())
            return Msg->DamageAmount;
        return 0.f; // Type mismatch — suppress.
    }
};
```

Set `ChannelTag` = `GameplayMessage.Combat.DamageDealt` on the authored asset. No additional wiring code needed.

---

## Notes

- `ExtractIncrement` **must be const and stateless**. The rule object is shared across all players via the DataAsset. Never store mutable per-player data on a rule.
- Return `0.f` to suppress an increment. `UStatComponent` skips `AddToStat` when the returned delta is `<= 0`.
- `EditInlineNew` + `DefaultToInstanced` makes rules editable inline inside `UStatDefinition` without a separate asset file.
- `GetChannelTag()` is validated in non-shipping builds by `UStatComponent::ValidateDefinitions()`.
- Both methods are `BlueprintNativeEvent` — C++ subclasses override the `_Implementation` suffix; Blueprint subclasses override the event node directly.
- For Blueprint subclasses that need typed payload access, expose a typed `BlueprintCallable` helper in the game module's function library (see [Usage.md](./Usage.md#7--blueprint-increment-rule)).
