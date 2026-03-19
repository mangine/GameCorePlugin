# UCurrencyWalletDefinition

**Sub-page of:** [Currency System](../Currency%20System.md)

**File:** `Currency/CurrencyWalletDefinition.h / .cpp`

A `UDataAsset` that declares which currencies a wallet holds and how each is configured. One asset per wallet archetype. Never replicated, never persisted — read-only authoring data.

---

## Class Declaration

```cpp
UCLASS(BlueprintType, DisplayName = "Currency Wallet Definition")
class GAMECORE_API UCurrencyWalletDefinition : public UDataAsset
{
    GENERATED_BODY()

public:
    // Currency slots this wallet supports.
    // Key   = FGameplayTag identifying the currency (e.g. Currency.Gold).
    // Value = FCurrencySlotConfig with clamp and initial amount.
    //
    // Mutations for tags NOT present in this map are rejected with
    // EWalletMutationResult::CurrencyNotConfigured.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    TMap<FGameplayTag, FCurrencySlotConfig> Slots;

    // Returns the config for the given tag, or nullptr if not configured.
    // Used by UCurrencySubsystem during validation — do not call on hot path without caching.
    const FCurrencySlotConfig* FindSlotConfig(const FGameplayTag& Tag) const
    {
        return Slots.Find(Tag);
    }
};
```

---

## Wallet Archetypes

Create one `UCurrencyWalletDefinition` asset per distinct wallet type. Examples:

| Asset Name | Used By | Typical Slots |
|---|---|---|
| `DA_Wallet_Player` | `APlayerState` | `Currency.Gold`, `Currency.Gems`, `Currency.Reputation.Pirate` |
| `DA_Wallet_Guild` | Guild Actor | `Currency.Gold` |
| `DA_Wallet_Trade` | Trade Session Actor | All currencies players may place in escrow |
| `DA_Wallet_Bank` | Bank Actor | `Currency.Gold` with high or uncapped Max |
| `DA_Wallet_NPC_Shop` | Shop NPC | `Currency.Gold` with configured Min for available stock |

---

## Usage Notes

- **Do not configure a currency slot with `Min == Max`** — this creates an immutable zero-capacity wallet and all mutations will return `ClampViolation`.
- **Premium currency (`Currency.Gems`) should have an explicit `Max`** configured on player wallets. Uncapped premium currency is an exploit risk.
- The definition is resolved once at `BeginPlay` by `UCurrencyWalletComponent`. The pointer is cached — no per-mutation asset lookup occurs.
- Wallets support only the currencies listed in their definition. A player wallet that does not list `Currency.GuildPoints` will reject any grant of it with `CurrencyNotConfigured`. This is intentional — configure intentionally, reject by default.
