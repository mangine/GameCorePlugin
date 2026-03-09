# UPersistenceRegistrationComponent

# UPersistenceRegistrationComponent

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Persistence/PersistenceRegistrationComponent.h`

**Type:** `UActorComponent`

The single persistence entry point on any actor. Dropping this component opts the actor into the persistence system entirely. It registers with `UPersistenceSubsystem`, caches all `IPersistableComponent` implementations at `BeginPlay`, handles dirty marking, and serializes to the `SaveQueue` before the actor is destroyed.

---

## Responsibilities

- Register/unregister the owning actor with `UPersistenceSubsystem` on `BeginPlay`/`EndPlay`
- Cache all `IPersistableComponent` references once at init — no repeated queries
- Receive dirty signals from any component and forward GUID to the subsystem's `DirtySet`
- On `EndPlay`, if dirty, serialize immediately into the `SaveQueue`
- Expose `BuildPayload()` for the subsystem to call at flush time
- Track `SaveGeneration` for dirty-clear correctness

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UPersistenceRegistrationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    // GameplayTag identifying the persistence category for this actor.
    // Used by the subsystem for delegate routing (e.g. Persistence.Entity.Player).
    // Must match a tag registered with the subsystem via RegisterPersistenceTag().
    UPROPERTY(EditDefaultsOnly, Category="Persistence",
              meta=(Categories="Persistence"))
    FGameplayTag PersistenceTag;

    // Called by any IPersistableComponent on this actor when state changes.
    void MarkDirty();

    // Called by subsystem at flush time. Serializes all dirty persistable components.
    // Pass bFullSave=true to serialize all components regardless of dirty state.
    FEntityPersistencePayload BuildPayload(bool bFullSave = false);

    // Returns the actor's stable GUID via ISourceIDInterface.
    FGuid GetEntityGUID() const;

    // Incremented each flush. Used by IPersistableComponent::ClearIfSaved().
    uint32 SaveGeneration = 0;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UPROPERTY()
    TArray<TScriptInterface<IPersistableComponent>> CachedPersistables;

    bool bDirty = false;
};
```

---

## BeginPlay — Registration & Caching

```cpp
void UPersistenceRegistrationComponent::BeginPlay()
{
    Super::BeginPlay();

    // Validate tag is set
    if (!PersistenceTag.IsValid())
    {
        UE_LOG(LogPersistence, Warning,
            TEXT("[%s] PersistenceRegistrationComponent has no PersistenceTag set. Actor will not persist."),
            *GetOwner()->GetName());
        return;
    }

    // Cache all persistable components once
    for (UActorComponent* Comp : GetOwner()->GetComponents())
        if (Comp->Implements<UPersistableComponent>())
            CachedPersistables.Add(Comp);

    if (auto* Subsystem = GetWorld()->GetGameInstance()
            ->GetSubsystem<UPersistenceSubsystem>())
    {
        Subsystem->RegisterEntity(this);
    }
}
```

> **Note:** Components added dynamically after `BeginPlay` are not cached. Dynamic attachment is unsupported. Expose `RefreshCache()` if needed in future.
> 

---

## EndPlay — Dead Actor Handling

```cpp
void UPersistenceRegistrationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    auto* Subsystem = GetWorld()->GetGameInstance()
        ->GetSubsystem<UPersistenceSubsystem>();

    if (Subsystem)
    {
        if (bDirty)
        {
            // Serialize NOW while UObjects are still valid.
            // Full save on disconnect/death — capture everything.
            Subsystem->MoveToSaveQueue(GetEntityGUID(), BuildPayload(true));
        }

        Subsystem->UnregisterEntity(GetEntityGUID());
    }

    Super::EndPlay(Reason);
}
```

> `EEndPlayReason::EndPlayInEditor` should be filtered in editor builds to avoid spurious saves during PIE teardown.
> 

---

## MarkDirty

```cpp
void UPersistenceRegistrationComponent::MarkDirty()
{
    if (bDirty) return; // Already queued — no-op

    bDirty = true;

    if (auto* Subsystem = GetWorld()->GetGameInstance()
            ->GetSubsystem<UPersistenceSubsystem>())
    {
        Subsystem->EnqueueDirty(GetEntityGUID());
    }
}
```

---

## BuildPayload

```cpp
FEntityPersistencePayload UPersistenceRegistrationComponent::BuildPayload(bool bFullSave)
{
    FEntityPersistencePayload Payload;
    Payload.EntityId         = GetEntityGUID();
    Payload.ServerInstanceId = UPersistenceSubsystem::GetServerInstanceId();
    Payload.PersistenceTag   = PersistenceTag;
    Payload.Timestamp        = FDateTime::UtcNow().ToUnixTimestamp();

    ++SaveGeneration;

    for (auto& PersistableRef : CachedPersistables)
    {
        IPersistableComponent* Persistable = PersistableRef.GetInterface();
        if (!Persistable) continue;

        if (!bFullSave && !Persistable->bDirty) continue; // Partial save

        FComponentPersistenceBlob Blob;
        Blob.Key     = Persistable->GetPersistenceKey();
        Blob.Version = Persistable->GetSchemaVersion();

        FMemoryWriter Writer(Blob.Data);
        Persistable->Serialize_Save(Writer);

        Payload.Components.Add(MoveTemp(Blob));
        Persistable->ClearIfSaved(SaveGeneration);
    }

    bDirty = false;
    return Payload;
}
```

---

## GetEntityGUID

```cpp
FGuid UPersistenceRegistrationComponent::GetEntityGUID() const
{
    if (GetOwner()->Implements<USourceIDInterface>())
        return ISourceIDInterface::Execute_GetEntityGUID(GetOwner());

    UE_LOG(LogPersistence, Warning,
        TEXT("[%s] does not implement ISourceIDInterface. Cannot persist."),
        *GetOwner()->GetName());
    return FGuid();
}
```

---

## Interaction with ISourceIDInterface

`ISourceIDInterface` is extended with:

```cpp
virtual FGuid GetEntityGUID() const = 0;
```

Actors without this interface return an invalid GUID and are silently skipped by the subsystem with a logged warning.

---

## Notes & Caveats

- Only **one** `UPersistenceRegistrationComponent` per actor is supported.
- The component is **server-side only** and must not replicate.
- `BuildPayload()` must only be called on the **game thread**.
- `PersistenceTag` must be registered with the subsystem before `BeginPlay`. Unregistered tags will log an error and skip the actor.
- Filter `EEndPlayReason::EndPlayInEditor` to avoid PIE teardown saving.

---

## Future Improvements

- `RefreshCache()` for runtime-added components
- PIE teardown filter on `EndPlay`
- Dev-build validation that `PersistenceTag` is registered at `BeginPlay`