# UCurrencyWalletDefinition

**File:** `Currency/CurrencyWalletDefinition.h / .cpp`  
**Base:** `UDataAsset`

Declares which currencies a wallet holds and how each is configured. One asset per wallet archetype. **Never replicated, never persisted** — read-only authoring data resolved once at `BeginPlay`.

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
    // Value = FCurrencySlotConfig with clamp range and initial amount.
    //
    // Mutations for tags NOT present in this map are rejected with
    // EWalletMutationResult::CurrencyNotConfigured.
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Currency")
    TMap<FGameplayTag, FCurrencySlotConfig> Slots;

    // Returns config for the given tag, or nullptr if not configured.
    // Called by UCurrencySubsystem during validation. Pointer is valid for the lifetime
    // of the data asset (i.e. the game session). Do not cache across asset reload.
    const FCurrencySlotConfig* FindSlotConfig(const FGameplayTag& Tag) const
    {
        return Slots.Find(Tag);
    }
};
```

---

## Notes

- **One asset per archetype.** `DA_Wallet_Player`, `DA_Wallet_Guild`, `DA_Wallet_Trade`, `DA_Wallet_Bank`, `DA_Wallet_NPC_Shop`.
- The definition pointer is cached on `UCurrencyWalletComponent` at `BeginPlay`. No per-mutation asset lookup.
- **Do not configure `Min == Max`.** That creates a zero-capacity slot — all mutations return `ClampViolation`.
- **Always set an explicit `Max` for `Currency.Gems`** (or any premium currency) on player wallet definitions. Uncapped premium currency is an exploit risk.
- Wallets reject mutations for any tag not listed here. Configure intentionally, reject by default.
