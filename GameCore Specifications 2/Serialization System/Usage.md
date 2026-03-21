# Serialization System — Usage

## Making an Actor Persistent

Add `UPersistenceRegistrationComponent` to the actor and set its `PersistenceTag`. The actor **must** implement `ISourceIDInterface` to provide a stable `FGuid`.

```cpp
// MyPersistentActor.h
UCLASS()
class AMyPersistentActor : public AActor, public ISourceIDInterface
{
    GENERATED_BODY()
public:
    AMyPersistentActor()
    {
        PersistenceComp = CreateDefaultSubobject<UPersistenceRegistrationComponent>(TEXT("Persistence"));
        // Set PersistenceTag in BP defaults or constructor:
        // PersistenceComp->PersistenceTag = TAG_Persistence_Entity_Player;
    }

    // ISourceIDInterface
    virtual FGuid GetEntityGUID_Implementation() const override { return EntityGUID; }

private:
    UPROPERTY()
    UPersistenceRegistrationComponent* PersistenceComp;

    FGuid EntityGUID; // Set at spawn from DB or generated on first creation
};
```

---

## Implementing IPersistableComponent

Any `UActorComponent` can implement `IPersistableComponent`. The fields `bDirty` and `DirtyGeneration` are instance members that must be declared on the **implementing class** (not inherited — UInterface cannot store instance data).

```cpp
// InventoryComponent.h
UCLASS()
class UInventoryComponent : public UActorComponent, public IPersistableComponent
{
    GENERATED_BODY()
public:
    // --- IPersistableComponent ---
    virtual FName GetPersistenceKey() const override { return TEXT("Inventory"); }
    virtual uint32 GetSchemaVersion() const override { return 2; }
    virtual void Serialize_Save(FArchive& Ar) override;
    virtual void Serialize_Load(FArchive& Ar, uint32 SavedVersion) override;
    virtual void Migrate(FArchive& Ar, uint32 From, uint32 To) override;

    // Required dirty-tracking state (declared here, not on the interface)
    bool   bDirty          = false;
    uint32 DirtyGeneration = 0;
    mutable TWeakObjectPtr<UPersistenceRegistrationComponent> CachedRegComp;

    // Game logic
    void AddItem(FInventoryItem Item);

private:
    TArray<FInventoryItem> Items;
};
```

```cpp
// InventoryComponent.cpp
void UInventoryComponent::Serialize_Save(FArchive& Ar)
{
    int32 Count = Items.Num();
    Ar << Count;
    for (auto& Item : Items) Ar << Item;
}

void UInventoryComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count;
    Ar << Count;
    Items.SetNum(Count);
    for (auto& Item : Items) Ar << Item;
}

void UInventoryComponent::Migrate(FArchive& Ar, uint32 From, uint32 To)
{
    if (From == 1 && To == 2)
    {
        // Version 1 had no Durability field — inject defaults after load
        for (auto& Item : Items)
            Item.Durability = 100;
    }
}

void UInventoryComponent::AddItem(FInventoryItem Item)
{
    Items.Add(Item);
    NotifyDirty(this); // Propagates to UPersistenceRegistrationComponent
}
```

---

## Registering Tags and Binding Transport (Game Module)

Done once at game startup, typically in a `UGameInstanceSubsystem` or `UGameMode`.

```cpp
// GamePersistenceWiring.cpp  (game module, not GameCore plugin)
void UGamePersistenceWiring::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    auto* PersistSys = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    auto* Backend    = GetGameInstance()->GetSubsystem<UGameCoreBackendSubsystem>();
    if (!PersistSys || !Backend) return;

    IKeyStorageService* PlayerDB = Backend->GetKeyStorage(TEXT("PlayerDB"));
    IKeyStorageService* WorldDB  = Backend->GetKeyStorage(TEXT("WorldDB"));

    // Register tags
    PersistSys->RegisterPersistenceTag(TAG_Persistence_Entity_Player);
    PersistSys->RegisterPersistenceTag(TAG_Persistence_World_State);

    // Bind save delegates
    if (PlayerDB)
    {
        PersistSys->GetSaveDelegate(TAG_Persistence_Entity_Player)
            ->AddLambda([PlayerDB](const FEntityPersistencePayload& Payload)
            {
                PlayerDB->Set(
                    Payload.PersistenceTag,
                    Payload.EntityId,
                    Payload,
                    Payload.bFlushImmediately,
                    Payload.bCritical);
            });
    }

    // Bind load request listener
    PersistSys->OnLoadRequested.AddLambda(
        [PlayerDB, PersistSys](FGuid EntityId, FGameplayTag Tag)
        {
            if (Tag == TAG_Persistence_Entity_Player && PlayerDB)
            {
                // Fetch from DB; call OnRawPayloadReceived or OnLoadFailed on result
                PlayerDB->Get(Tag, EntityId,
                    [PersistSys, EntityId](bool bSuccess, const FEntityPersistencePayload& Payload)
                    {
                        if (bSuccess)
                            // You need the actor reference here — game module must resolve it
                            // PersistSys->OnRawPayloadReceived(Actor, Payload);
                        else
                            PersistSys->OnLoadFailed(EntityId);
                    });
            }
        });
}
```

---

## Loading an Actor

```cpp
void AMyGameMode::OnPlayerLogin(APlayerController* PC)
{
    FGuid PlayerGUID = /* resolve from login session */;

    auto* PersistSys = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    PersistSys->RequestLoad(PlayerGUID, TAG_Persistence_Entity_Player,
        [PC](bool bSuccess)
        {
            if (bSuccess)
                // Player data deserialized — proceed
                PC->OnPersistenceLoaded();
            else
                // Handle failure: spawn fresh, retry, or disconnect
                PC->OnPersistenceLoadFailed();
        });
}
```

---

## Forcing a Save on Critical Events

```cpp
// On logout
void AMyGameMode::OnPlayerLogout(AActor* PlayerActor)
{
    auto* PersistSys = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    PersistSys->RequestFullSave(PlayerActor, ESerializationReason::Logout);
    // Payload will be stamped bCritical=true, bFlushImmediately=true
}

// On zone transfer
void AMyGameMode::OnZoneTransfer(AActor* PlayerActor)
{
    auto* PersistSys = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    PersistSys->RequestFullSave(PlayerActor, ESerializationReason::ZoneTransfer);
    // Payload will be stamped bCritical=true, bFlushImmediately=false
}

// On server shutdown
void AMyGameMode::BeginShutdown()
{
    auto* PersistSys = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>();
    PersistSys->RequestShutdownSave();
    // Synchronous; all registered entities saved with bCritical=true, bFlushImmediately=true
}
```

---

## Configuration (DefaultGame.ini)

```ini
[/Script/GameCore.PersistenceSubsystem]
SaveInterval=300.0
PartialSavesBetweenFullSave=9
ActorsPerFlushTick=100
LoadTimeoutSeconds=30.0
ServerInstanceId=<stable-guid-for-this-server-role>
```

`ServerInstanceId` must be set per server role before launch. It is stamped into every payload for audit and cross-restart deduplication.

---

## Schema Versioning Guidelines

| Change Type | Action Required |
|---|---|
| Add field with default value | Increment `GetSchemaVersion()`; default no-op `Migrate()` is safe |
| Remove or rename field | Increment version + implement `Migrate()` to skip/remap old bytes |
| Reorder fields | Always implement `Migrate()` — binary layout is positional |
| Rename `GetPersistenceKey()` | **Never do this after shipping** — it is the blob identifier |

---

## Dirty Trigger Guidelines

| Change Type | Should call NotifyDirty? | Notes |
|---|---|---|
| Item gained / quest complete | Yes | Critical state — enqueue immediately |
| Level up / stat change | Yes | Critical state |
| Position / rotation | No | Saved by periodic full cycle |
| Animation state / VFX | No | Transient — never persisted |
