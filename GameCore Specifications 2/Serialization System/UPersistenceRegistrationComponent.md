# UPersistenceRegistrationComponent

**Module:** `GameCore`  
**Location:** `GameCore/Source/GameCore/Persistence/PersistenceRegistrationComponent.h/.cpp`  
**Type:** `UActorComponent`

The single persistence entry point on any actor. Adding this component opts the actor into the persistence system. It registers with `UPersistenceSubsystem`, caches all `IPersistableComponent` implementations at `BeginPlay`, handles dirty marking, and produces `FEntityPersistencePayload` on demand.

**Server-side only.** Must not replicate. One per actor.

---

## Class Declaration

```cpp
UCLASS(ClassGroup=(GameCore), meta=(BlueprintSpawnableComponent))
class GAMECORE_API UPersistenceRegistrationComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    /**
     * Category tag for this actor. Routes payloads to the correct FOnPayloadReady delegate.
     * Must match a tag registered with UPersistenceSubsystem::RegisterPersistenceTag().
     * Set in Blueprint defaults or constructor.
     */
    UPROPERTY(EditDefaultsOnly, Category="Persistence",
              meta=(Categories="Persistence"))
    FGameplayTag PersistenceTag;

    /**
     * Incremented on every BuildPayload call.
     * Read by IPersistableComponent::NotifyDirty to stamp DirtyGeneration.
     * Used by ClearIfSaved to distinguish stale from fresh dirty state.
     */
    uint32 SaveGeneration = 0;

    /** Called by IPersistableComponent::NotifyDirty on any state change. */
    void MarkDirty();

    /**
     * Produces an FEntityPersistencePayload.
     * bFullSave=true  → serialize all components regardless of dirty state.
     * bFullSave=false → serialize only components with bDirty=true.
     * Must be called on the game thread only.
     */
    FEntityPersistencePayload BuildPayload(bool bFullSave = false);

    /** Returns the actor's stable GUID via ISourceIDInterface. */
    FGuid GetEntityGUID() const;

    /** Read-only access for UPersistenceSubsystem::OnRawPayloadReceived. */
    const TArray<TScriptInterface<IPersistableComponent>>& GetCachedPersistables() const
    {
        return CachedPersistables;
    }

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

## BeginPlay — Registration & Cache

```cpp
void UPersistenceRegistrationComponent::BeginPlay()
{
    Super::BeginPlay();

#if WITH_EDITOR
    // Skip registration during PIE teardown
    if (GetWorld() && GetWorld()->WorldType == EWorldType::PIE
        && GetWorld()->bIsTearingDown)
        return;
#endif

    if (!PersistenceTag.IsValid())
    {
        UE_LOG(LogPersistence, Warning,
            TEXT("[%s] UPersistenceRegistrationComponent has no PersistenceTag. Actor will not persist."),
            *GetOwner()->GetName());
        return;
    }

    // Cache all persistable components once — no repeated queries
    for (UActorComponent* Comp : GetOwner()->GetComponents())
    {
        if (Comp->Implements<UPersistableComponent>())
            CachedPersistables.Add(Comp);
    }

#if !UE_BUILD_SHIPPING
    // Dev: validate unique persistence keys
    TSet<FName> Keys;
    for (auto& Ref : CachedPersistables)
    {
        FName Key = Ref.GetInterface()->GetPersistenceKey();
        if (!Keys.Add(Key).IsAlreadyInSet())
            UE_LOG(LogPersistence, Error,
                TEXT("[%s] Duplicate PersistenceKey '%s' — only one blob will survive per save."),
                *GetOwner()->GetName(), *Key.ToString());
    }
#endif

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
        Subsystem->RegisterEntity(this);
}
```

> Components added dynamically after `BeginPlay` are not cached. Dynamic attachment is unsupported. Add a `RefreshCache()` method if needed.

---

## EndPlay — Unregister & Immediate Save

```cpp
void UPersistenceRegistrationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
#if WITH_EDITOR
    // Suppress spurious saves during PIE teardown
    if (Reason == EEndPlayReason::EndPlayInEditor)
    {
        Super::EndPlay(Reason);
        return;
    }
#endif

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
    {
        if (bDirty)
        {
            // Serialize while UObjects are still valid.
            // RequestFullSave dispatches immediately through the normal delegate path.
            Subsystem->RequestFullSave(GetOwner(), ESerializationReason::Logout);
        }

        Subsystem->UnregisterEntity(GetEntityGUID());
    }

    Super::EndPlay(Reason);
}
```

> Using `RequestFullSave` instead of an internal `MoveToSaveQueue` (which does not exist) keeps dispatch consistent — all payloads travel through `DispatchPayload` and its delegate routing.

---

## MarkDirty

```cpp
void UPersistenceRegistrationComponent::MarkDirty()
{
    if (bDirty) return; // Guard: already queued, no-op

    bDirty = true;

    if (auto* Subsystem = GetGameInstance()->GetSubsystem<UPersistenceSubsystem>())
        Subsystem->EnqueueDirty(GetEntityGUID());
}
```

---

## BuildPayload

```cpp
FEntityPersistencePayload UPersistenceRegistrationComponent::BuildPayload(bool bFullSave)
{
    FEntityPersistencePayload Payload;
    Payload.EntityId         = GetEntityGUID();
    Payload.ServerInstanceId = GetGameInstance()
                                ->GetSubsystem<UPersistenceSubsystem>()
                                ->ServerInstanceId;
    Payload.PersistenceTag   = PersistenceTag;
    Payload.Timestamp        = FDateTime::UtcNow().ToUnixTimestamp();

    ++SaveGeneration;

    for (auto& PersistableRef : CachedPersistables)
    {
        IPersistableComponent* Persistable = PersistableRef.GetInterface();
        if (!Persistable) continue;
        if (!bFullSave && !Persistable->bDirty) continue;

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

> **Note:** `Persistable->bDirty` is accessed directly here. Since `bDirty` must be declared as a public member on the implementing class (not the interface), this requires a static or dynamic cast to the concrete type, OR `bDirty` access is moved to a virtual accessor on the interface. The cleaner solution is adding `virtual bool IsDirty() const = 0;` to `IPersistableComponent`. See Code Review for details.

---

## GetEntityGUID

```cpp
FGuid UPersistenceRegistrationComponent::GetEntityGUID() const
{
    if (GetOwner()->Implements<USourceIDInterface>())
        return ISourceIDInterface::Execute_GetEntityGUID(GetOwner());

#if !UE_BUILD_SHIPPING
    UE_LOG(LogPersistence, Warning,
        TEXT("[%s] does not implement ISourceIDInterface. Cannot persist."),
        *GetOwner()->GetName());
#endif
    return FGuid();
}
```

---

## Notes & Constraints

- **One per actor.** Multiple `UPersistenceRegistrationComponent` on the same actor is undefined behavior.
- **Game thread only.** `BuildPayload` must only be called from the game thread.
- **Server only.** Must not replicate. Gate with `ShouldCreateSubsystem` or `HasAuthority()` checks where needed.
- `PersistenceTag` must be registered with the subsystem **before** `BeginPlay`. Unregistered tags are logged and the actor is skipped.
- Components added dynamically after `BeginPlay` are invisible to the persistence system.
- `EEndPlayReason::EndPlayInEditor` is filtered to suppress PIE teardown saves.
