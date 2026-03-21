# Alignment System — Usage

---

## 1. Create an Alignment Definition Asset

In the editor: right-click in the Content Browser → **GameCore → Alignment Definition**.

Set:
- `AlignmentTag` — unique axis tag, e.g. `Alignment.GoodEvil` (defined in your game module's `DefaultGameplayTags.ini`).
- `EffectiveMin / EffectiveMax` — the range returned to game logic.
- `SaturatedMin / SaturatedMax` — the wider accumulation range (must be `<= EffectiveMin` and `>= EffectiveMax`).
- `ChangeRequirements` — optional `URequirementList` asset that gates mutations on this axis.

---

## 2. Add UAlignmentComponent to APlayerState

```cpp
// MyPlayerState.h
UPROPERTY(VisibleAnywhere)
TObjectPtr<UAlignmentComponent> AlignmentComponent;

// MyPlayerState.cpp constructor
AlignmentComponent = CreateDefaultSubobject<UAlignmentComponent>(TEXT("AlignmentComponent"));
```

Add `UPersistenceRegistrationComponent` if persistence is needed (alignment participates automatically once both components are present on the same actor).

---

## 3. Register Axes (Server Only)

Call `RegisterAlignment` before any delta is applied. Safe to call in `BeginPlay` or from a game mode / player controller setup function.

```cpp
// AMyPlayerState::BeginPlay — runs on server
void AMyPlayerState::BeginPlay()
{
    Super::BeginPlay();

    if (!HasAuthority()) return;

    if (UAlignmentComponent* Comp = FindComponentByClass<UAlignmentComponent>())
    {
        Comp->RegisterAlignment(GoodEvilDefinition);  // tag = Alignment.GoodEvil
        Comp->RegisterAlignment(LawChaosDefinition);  // tag = Alignment.LawChaos
    }
}
```

Registering the same definition twice is a no-op (idempotent).

---

## 4. Apply Alignment Deltas (Server Only)

```cpp
// From any server-side system (quest, combat, dialogue, etc.)
void UMyQuestSystem::OnKilledInnocentNPC(APlayerState* PlayerState)
{
    UAlignmentComponent* Comp = PlayerState->FindComponentByClass<UAlignmentComponent>();
    if (!Comp) return;

    FRequirementContext Ctx;
    Ctx.Subject = PlayerState;

    TArray<FAlignmentDelta> Deltas;
    Deltas.Add({ AlignmentTags::GoodEvil, -25.f });  // Move toward evil
    Deltas.Add({ AlignmentTags::LawChaos, -10.f });  // Move toward chaos

    Comp->ApplyAlignmentDeltas(Deltas, Ctx);
}
```

Axes whose `ChangeRequirements` fail are silently skipped. The rest of the batch continues.

---

## 5. Query Alignment (Server or Client)

```cpp
// Effective value — clamped to [EffectiveMin, EffectiveMax]. Safe on both server and client.
float EvilScore = AlignmentComp->GetEffectiveAlignment(AlignmentTags::GoodEvil);

// Underlying value — raw accumulated value. Use for persistence/debug only.
float RawEvil = AlignmentComp->GetUnderlyingAlignment(AlignmentTags::GoodEvil);

// Check if an axis is registered
bool bRegistered = AlignmentComp->IsAlignmentRegistered(AlignmentTags::GoodEvil);
```

`GetEffectiveAlignment` works on clients because `EffectiveMin/Max` are stored on each replicated `FAlignmentEntry`.

---

## 6. Listen for Alignment Changes (Server-Side)

```cpp
// UMySystem.h
FGameplayMessageListenerHandle AlignmentHandle;

// UMySystem.cpp
void UMySystem::BeginPlay()
{
    Super::BeginPlay();

    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        AlignmentHandle = Bus->StartListening<FAlignmentChangedMessage>(
            GameCoreEventTags::Alignment_Changed,
            [this](FGameplayTag, const FAlignmentChangedMessage& Msg)
            {
                APlayerState* PS = Msg.PlayerState.Get();
                if (!PS) return;

                for (const FAlignmentChangedEntry& Entry : Msg.Changes)
                {
                    if (Entry.AlignmentTag == AlignmentTags::GoodEvil)
                    {
                        HandleGoodEvilChanged(PS, Entry.NewEffective, Entry.AppliedDelta);
                    }
                }
            });
    }
}

void UMySystem::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UGameCoreEventBus* Bus = UGameCoreEventBus::Get(this))
    {
        Bus->StopListening(AlignmentHandle);
    }
    Super::EndPlay(Reason);
}
```

> **Always store the handle and call `StopListening` in `EndPlay`.** Leaked handles keep a dangling lambda alive inside GMS.

---

## 7. React to Alignment Changes on the Client (UI)

Clients do not receive GMS events. Bind to the replicated delegate instead:

```cpp
// In your HUD or UI widget, after obtaining the local player's PlayerState:
void UMyAlignmentWidget::SetupForPlayer(AMyPlayerState* PS)
{
    if (UAlignmentComponent* Comp = PS->FindComponentByClass<UAlignmentComponent>())
    {
        Comp->OnAlignmentDataReplicated.AddDynamic(
            this, &UMyAlignmentWidget::RefreshAlignmentDisplay);
    }
}

void UMyAlignmentWidget::RefreshAlignmentDisplay()
{
    // Pull current effective values and update UI
    float EvilScore = AlignmentComp->GetEffectiveAlignment(AlignmentTags::GoodEvil);
    UpdateEvilMeter(EvilScore);
}
```

---

## 8. Persistence — How It Works

`UAlignmentComponent` implements `IPersistableComponent`. As long as the owning `APlayerState` also has a `UPersistenceRegistrationComponent`, alignment data is saved and restored automatically.

No custom save/load code is required. If you need to inspect the save data:

```cpp
// These are called by UPersistenceRegistrationComponent — you do NOT need to call them manually.
// Shown here for reference only.

// Save — called by UPersistenceSubsystem
void UAlignmentComponent::Serialize_Save(FArchive& Ar)
{
    // Writes: count, then (AlignmentTag, UnderlyingValue) pairs
}

// Load — called by UPersistenceSubsystem after RegisterAlignment calls
void UAlignmentComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    // Reads back and restores UnderlyingValue for each registered axis
    // Silently ignores tags not currently registered (axis removed from game)
}
```

`UnderlyingValue` is the only field persisted. Effective value is always derived at query time.

---

## 9. Adding a New Axis Tag

1. Add to your **game module's** `DefaultGameplayTags.ini`:
   ```ini
   +GameplayTagList=(Tag="Alignment.Honor")
   ```
2. Create a `UAlignmentDefinition` asset with that tag.
3. Call `RegisterAlignment` with the new asset in your `APlayerState::BeginPlay`.

No changes to the GameCore plugin are needed. The system is entirely tag-driven.
