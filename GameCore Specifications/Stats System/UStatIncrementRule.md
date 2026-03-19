# UStatIncrementRule

## Role

Abstract, instanced UObject that encapsulates two responsibilities:
1. Which GMS channel this rule listens to.
2. How to extract a `float` increment from the channel's message payload (`FInstancedStruct`).

One subclass is needed per distinct GMS message struct shape. A single subclass can be reused across any number of `UStatDefinition` assets.

---

## Abstract Base Class

```cpp
// GameCore module
UCLASS(Abstract, EditInlineNew, DefaultToInstanced)
class GAMECORE_API UStatIncrementRule : public UObject
{
    GENERATED_BODY()

public:
    // Returns the GMS channel this rule listens to.
    // UStatComponent uses this to register the GMS listener at BeginPlay.
    UFUNCTION(BlueprintNativeEvent)
    FGameplayTag GetChannelTag() const;
    virtual FGameplayTag GetChannelTag_Implementation() const
        PURE_VIRTUAL(UStatIncrementRule::GetChannelTag_Implementation, return FGameplayTag::EmptyTag;);

    // Called by UStatComponent when a message arrives on GetChannelTag().
    // Payload is the raw FInstancedStruct from the GMS message context.
    // Return the float to add to the stat. Return 0.f to suppress.
    UFUNCTION(BlueprintNativeEvent)
    float ExtractIncrement(const FInstancedStruct& Payload) const;
    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const
        PURE_VIRTUAL(UStatIncrementRule::ExtractIncrement_Implementation, return 0.f;);
};
```

---

## Built-in: UConstantIncrementRule (GameCore)

Covers the most common case: increment by a fixed amount regardless of payload content.

```cpp
UCLASS(DisplayName="Constant Increment")
class GAMECORE_API UConstantIncrementRule : public UStatIncrementRule
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category="Rule")
    FGameplayTag ChannelTag;

    // Fixed amount added to the stat each time the message fires.
    UPROPERTY(EditDefaultsOnly, Category="Rule", meta=(ClampMin="0.0"))
    float Amount = 1.f;

    virtual FGameplayTag GetChannelTag_Implementation() const override { return ChannelTag; }
    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override { return Amount; }
};
```

---

## Game Module Example: UDamageIncrementRule

Extracts a float from a known struct. Written once per message type.

```cpp
// In game module
UCLASS(DisplayName="Damage Dealt Increment")
class MYGAME_API UDamageIncrementRule : public UStatIncrementRule
{
    GENERATED_BODY()

public:
    UPROPERTY(EditDefaultsOnly, Category="Rule")
    FGameplayTag ChannelTag;

    virtual FGameplayTag GetChannelTag_Implementation() const override { return ChannelTag; }

    virtual float ExtractIncrement_Implementation(const FInstancedStruct& Payload) const override
    {
        // FInstancedStruct::Get<T>() returns nullptr if the type doesn't match.
        if (const FDamageDealtMessage* Msg = Payload.GetPtr<FDamageDealtMessage>())
        {
            return Msg->DamageAmount;
        }
        return 0.f;
    }
};
```

---

## Notes

- `EditInlineNew` + `DefaultToInstanced` makes these objects editable inline inside `UStatDefinition` without needing a separate asset.
- `ExtractIncrement` must be const and stateless — no side effects.
- If `ExtractIncrement` returns `<= 0.f`, `UStatComponent` skips the increment silently (no event bus broadcast).
- `GetChannelTag` is `BlueprintNativeEvent` to allow BP subclassing for rapid prototyping; ship builds should prefer C++ subclasses.
- Never store mutable state on a `UStatIncrementRule` — it is shared across all players via the DataAsset.
