# Currency System — Usage

---

## Setup: Wallet Definition Asset

Create a `UCurrencyWalletDefinition` data asset per wallet archetype in the editor:

| Asset Name | Used By | Typical Slots |
|---|---|---|
| `DA_Wallet_Player` | `APlayerState` | `Currency.Gold`, `Currency.Gems`, `Currency.Reputation.Pirate` |
| `DA_Wallet_Guild` | Guild Actor | `Currency.Gold` |
| `DA_Wallet_Trade` | Trade Session Actor | All currencies players may place in escrow |
| `DA_Wallet_Bank` | Bank Actor | `Currency.Gold` (high or uncapped Max) |
| `DA_Wallet_NPC_Shop` | Shop NPC | `Currency.Gold` with configured Min |

Assign the definition to `UCurrencyWalletComponent::Definition` in the component defaults.

---

## Setup: Adding a Wallet to an Actor

```cpp
// AMyPlayerState.h
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Currency")
TObjectPtr<UCurrencyWalletComponent> WalletComponent;

// AMyPlayerState.cpp
AMyPlayerState::AMyPlayerState()
{
    WalletComponent = CreateDefaultSubobject<UCurrencyWalletComponent>(TEXT("WalletComponent"));
    // Assign WalletComponent->Definition in Blueprint defaults or here via asset reference
}
```

For player wallets, attach a `UPersistenceRegistrationComponent` to the same actor to enable save/load:

```cpp
PersistenceComponent = CreateDefaultSubobject<UPersistenceRegistrationComponent>(TEXT("PersistenceComponent"));
// Configure PersistenceComponent->BackendTag in defaults (e.g. TAG_Persistence_Entity_Player)
```

The `UCurrencyWalletComponent` automatically registers itself with the `UPersistenceRegistrationComponent` at `BeginPlay` if one is found on the same actor.

---

## Granting Currency (Server Only)

```cpp
// Inside server-authoritative game code
UCurrencySubsystem* CurrencySys = GetWorld()->GetSubsystem<UCurrencySubsystem>();
if (!CurrencySys) return;

// Grant 500 gold to the player
EWalletMutationResult Result = CurrencySys->ModifyCurrency(
    PlayerState->WalletComponent,
    TAG_Currency_Gold,
    500,                   // Delta (positive = gain)
    QuestGiverInterface,   // Source — who granted it
    PlayerInterface,       // Target — who receives it
    FGuid::NewGuid()       // SessionId for audit pairing (optional for simple grants)
);

if (Result != EWalletMutationResult::Success)
{
    // Log / handle — never ignore return codes in economy code
    FGameCoreBackend::GetLogging(TAG_Log_Economy)->LogWarning(
        TEXT("Currency"), FString::Printf(TEXT("ModifyCurrency failed: %d"), (int32)Result));
}
```

---

## Spending Currency (Server Only)

```cpp
// Spend 100 gold — negative delta
EWalletMutationResult Result = CurrencySys->ModifyCurrency(
    PlayerState->WalletComponent,
    TAG_Currency_Gold,
    -100,                  // Negative = spend
    PlayerInterface,       // Source
    ShopInterface,         // Target
    FGuid::NewGuid()
);

switch (Result)
{
    case EWalletMutationResult::Success:           /* proceed */            break;
    case EWalletMutationResult::InsufficientFunds: /* notify player */     break;
    case EWalletMutationResult::ClampViolation:    /* config issue */      break;
    default:                                       /* log unexpectedly */  break;
}
```

---

## Atomic Transfer (Server Only)

```cpp
// Transfer 200 gold from player escrow wallet to shop wallet (atomic)
FGuid TransferSession = FGuid::NewGuid(); // Same SessionId on both audit entries

EWalletMutationResult Result = CurrencySys->TransferCurrency(
    PlayerState->WalletComponent,   // From
    ShopActor->WalletComponent,     // To
    TAG_Currency_Gold,
    200,                            // Amount (must be > 0)
    PlayerInterface,                // Source identity
    ShopInterface,                  // Target identity
    TransferSession
);

// Both wallets mutated or neither — no partial state possible
```

---

## Reading Currency (Client or Server)

```cpp
// Get current gold amount — safe on both client and server
int64 Gold = PlayerState->WalletComponent->GetAmount(TAG_Currency_Gold);

// Check if the player can afford a purchase
bool bCanBuy = PlayerState->WalletComponent->CanAfford(TAG_Currency_Gold, 100);

// Check if a currency is configured on this wallet
bool bSupported = PlayerState->WalletComponent->SupportsCurrency(TAG_Currency_Gems);
```

---

## Listening for Currency Changes

### On Server (after mutation)
```cpp
// Bind in BeginPlay or wherever you hold a reference to the component
PlayerState->WalletComponent->OnCurrencyChanged.AddUObject(
    this, &UQuestTracker::OnPlayerCurrencyChanged);

void UQuestTracker::OnPlayerCurrencyChanged(
    FGameplayTag CurrencyTag, int64 OldAmount, int64 NewAmount)
{
    if (CurrencyTag == TAG_Currency_Gold)
        CheckGoldObjective(NewAmount);
}
```

### On Client (after replication)
The same `OnCurrencyChanged` delegate fires on the owning client inside the FastArray callbacks after each replicated change. Bind in the same way from UI code.

```cpp
// Inside a UMG widget or HUD component
void UGoldDisplayWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if (AMyPlayerState* PS = GetOwningPlayerState<AMyPlayerState>())
    {
        PS->WalletComponent->OnCurrencyChanged.AddUObject(
            this, &UGoldDisplayWidget::OnGoldChanged);
        // Set initial value
        UpdateDisplay(PS->WalletComponent->GetAmount(TAG_Currency_Gold));
    }
}

void UGoldDisplayWidget::OnGoldChanged(FGameplayTag Tag, int64 Old, int64 New)
{
    if (Tag == TAG_Currency_Gold)
        UpdateDisplay(New);
}
```

---

## Trade / Escrow Wallet (Ephemeral)

Trade wallets live on a temporary trade session Actor. They are **not** registered with `UPersistenceRegistrationComponent` — omit the persistence component entirely from the trade actor.

```cpp
// ATradeSessionActor.cpp
ATradeSessionActor::ATradeSessionActor()
{
    // Wallet for player A escrow
    WalletA = CreateDefaultSubobject<UCurrencyWalletComponent>(TEXT("WalletA"));
    // Wallet for player B escrow
    WalletB = CreateDefaultSubobject<UCurrencyWalletComponent>(TEXT("WalletB"));
    // NO UPersistenceRegistrationComponent — these are ephemeral.
    // Crash recovery relies on audit log SessionId pairing.
}
```

Each transfer to/from escrow uses a shared `FGuid` SessionId. Unmatched debit/credit pairs in the audit log indicate a crash mid-transfer and are used for recovery.

---

## Backend Wiring (Game Module)

The game module maps audit and logging tags to named backend instances. The currency system uses:

```cpp
// In UMyGameInstance::Init
Backend->MapTagToAudit(TAG_Audit_Currency_Modify,          TEXT("Gameplay"));
Backend->MapTagToAudit(TAG_Audit_Currency_Transfer,        TEXT("Gameplay"));
Backend->MapTagToAudit(TAG_Audit_Currency_TransferCommit,  TEXT("Gameplay"));
Backend->MapTagToAudit(TAG_Audit_Currency_Recovery,        TEXT("Gameplay"));
Backend->MapTagToLogging(TAG_Log_Economy,                  TEXT("GameplayLog"));
```

The currency subsystem does not need to know which physical backend is used — routing is transparent.
