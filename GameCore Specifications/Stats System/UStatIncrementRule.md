# UStatIncrementRule

## Role

Abstract, instanced UObject authored on a `UStatDefinition`. Each rule binds one GMS2 channel to a stat and extracts the increment amount from the incoming `FInstancedStruct` payload.

`UStatComponent` reads these rules at `BeginPlay` and auto-registers one `UGameCoreEventBus2` listener per rule. No game-module wiring is needed for event-driven increments.

One subclass per distinct payload shape. A single subclass can be reused across any number of `UStatDefinition` assets.

---

## Abstract Base Class

```cpp
// GameCore module
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UStatIncrementRule : public UObject
{
    GENERATED_BODY()

public:
    // The GMS2 channel this rule listens on.
    // Must return a valid leaf FGameplayTag.
    // Validated at BeginPlay in non-shipping builds.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Rule")
    FGameplayTag GetChannelTag() const;
    virtual FGameplayTag GetChannelTag_Implementation() const
        PURE_VIRTUAL(UStatIncrementRule::GetChannelTag_Implementation, return FGameplayTag::EmptyTag;);

    // Extract the increment amount from the incoming payload.
    //
    // Called by UStatComponent's auto-registered listener every time a message
    // arrives on GetChannelTag(). Return 0.f to suppress the increment.
    //
    // Implementations use Payload.GetPtr<FMyStruct>() to access typed fields.
    // If the cast returns nullptr (wrong type on channel), return 0.f.
    //
    // Must be const and stateless — this object is shared across all players
    // via the DataAsset. Never store per-player mutable state here.
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Rule")
    float ExtractIncrement(const FInstancedStruct& Payload) const;
    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const
        PURE_VIRTUAL(UStatIncrementRule::ExtractIncrement_Implementation, return 0.f;);
};
```

---

## Built-in: UConstantIncrementRule (GameCore)

Ignores the payload entirely and returns a fixed configured amount. Covers the most common case: every message on the channel = fixed increment.

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

    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override
    {
        return Amount; // Payload ignored — fixed amount.
    }
};
```

---

## Game Module Subclass Pattern

For payload-driven increments, subclass `UStatIncrementRule` in the game module and override `ExtractIncrement`.

```cpp
// Game module — UDamageIncrementRule.h
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
        {
            return Msg->DamageAmount;
        }
        return 0.f; // Type mismatch — suppress.
    }
};
```

Set `ChannelTag` = `GameplayMessage.Combat.DamageDealt` on the authored asset. Done — no wiring subsystem needed.

---

## Blueprint Subclass Pattern

Blueprint subclasses override `ExtractIncrement` using the `GetPtr` utility exposed via Blueprint. Useful for rapid prototyping; ship builds should prefer C++ subclasses for performance.

```
BP_DamageIncrementRule
  GetChannelTag → return GameplayMessage.Combat.DamageDealt
  ExtractIncrement(Payload)
    → Cast Payload to FDamageDealtMessage
    → if valid: return DamageAmount
    → else: return 0.0
```

> `FInstancedStruct::GetPtr<T>()` is not natively exposed to Blueprint. For Blueprint subclasses, expose a typed helper in the game module:
>
> ```cpp
> UFUNCTION(BlueprintCallable, Category="Stats")
> static const FDamageDealtMessage* GetDamageDealtMessage(const FInstancedStruct& Payload)
> {
>     return Payload.GetPtr<FDamageDealtMessage>();
> }
> ```

---

## Notes

- `ExtractIncrement` must be **const and stateless**. This object is shared across all players via the DataAsset. Never store mutable per-player data on a rule.
- Return `0.f` to suppress an increment (type mismatch, requirements not met inside the rule, etc.). `UStatComponent` skips `AddToStat` when the returned delta is `<= 0`.
- `EditInlineNew` + `DefaultToInstanced` makes rules editable inline inside `UStatDefinition` without a separate asset file.
- `GetChannelTag()` is validated in non-shipping builds by `UStatComponent::ValidateDefinitions()`.
- `GetChannelTag` and `ExtractIncrement` are both `BlueprintNativeEvent` — C++ subclasses override the `_Implementation` suffix; Blueprint subclasses override the event node directly.
