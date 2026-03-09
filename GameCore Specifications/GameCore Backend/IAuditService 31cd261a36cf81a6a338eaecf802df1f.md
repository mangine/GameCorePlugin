# IAuditService

**Module:** `GameCore`

**Location:** `GameCore/Source/GameCore/Backend/AuditService.h`

Interface for recording auditable game events. Used for any action that may require anti-cheat validation or exploit investigation: XP gains, item drops, trades, currency changes, etc.

Source identification uses `ISourceIDInterface::GetSourceDisplayName()` — callers are responsible for resolving the display name before constructing `FAuditEntry`. The `InstanceGUID` identifies the **event instance**, not the source actor.

---

## FAuditEntry

```cpp
USTRUCT()
struct GAMECORE_API FAuditEntry
{
    GENERATED_BODY()

    // Human-readable source ID, resolved via ISourceIDInterface::GetSourceDisplayName()
    // Convention: "PlayerName (GUID)" or "Source.Mob.Skeleton.Level10"
    FString   FullID;

    // Unique identifier for this specific event instance
    // Not the source actor's GUID — use FGuid::NewGuid() at event creation
    FGuid     InstanceGUID;

    // Game-defined JSON payload. Event type and context defined by the calling system.
    // Example: {"event":"xp_gain","amount":250,"skill":"Combat"}
    FString   Payload;

    FDateTime Timestamp;
    FString   ServerId;
};
```

---

## Interface Declaration

```cpp
UINTERFACE(MinimalAPI, NotBlueprintable)
class UAuditService : public UInterface { GENERATED_BODY() };

class GAMECORE_API IAuditService
{
    GENERATED_BODY()

public:
    // Called only by UGameCoreBackendSubsystem during registration
    virtual bool Connect(const FString& ConnectionString) = 0;

    virtual void RecordEvent(const FAuditEntry& Entry) = 0;
    virtual void RecordBatch(const TArray<FAuditEntry>& Entries) = 0;
};
```

---

## Null Fallback Implementation

```cpp
class GAMECORE_API FNullAuditService : public IAuditService
{
public:
    virtual bool Connect(const FString&) override { return true; }

    virtual void RecordEvent(const FAuditEntry& Entry) override
    {
        UE_LOG(LogGameCore, Log,
            TEXT("[NullAudit] Event from '%s' [%s]: %s"),
            *Entry.FullID,
            *Entry.InstanceGUID.ToString(),
            *Entry.Payload);
    }

    virtual void RecordBatch(const TArray<FAuditEntry>& Entries) override
    {
        for (const FAuditEntry& Entry : Entries)
            RecordEvent(Entry);
    }
};
```

---

## Building a FAuditEntry

Callers resolve source identity from `ISourceIDInterface` before constructing the entry:

```cpp
// Example from ULevelingComponent::AddXP
void SubmitXPAudit(
    TScriptInterface<ISourceIDInterface> Source,
    int32 Amount,
    const FString& SkillTag,
    const FString& ServerId)
{
    FAuditEntry Entry;

    if (Source.GetObject())
    {
        // Combine display name + source tag for full traceability
        Entry.FullID = FString::Printf(TEXT("%s (%s)"),
            *Source->GetSourceDisplayName().ToString(),
            *Source->GetSourceTag().ToString());
    }
    else
    {
        Entry.FullID = TEXT("Unknown");
    }

    Entry.InstanceGUID = FGuid::NewGuid();
    Entry.Payload      = FString::Printf(
        TEXT("{\"event\":\"xp_gain\",\"amount\":%d,\"skill\":\"%s\"}"),
        Amount, *SkillTag);
    Entry.Timestamp    = FDateTime::UtcNow();
    Entry.ServerId     = ServerId;

    Backend->GetAudit()->RecordEvent(Entry);
}
```

---

## Notes

- `FullID` is a free-form string intentionally — it needs to be human-readable for CS tooling, not machine-parsed. The source tag already provides the structured identity.
- `Payload` format is game-defined. GameCore imposes no schema on it beyond `FString`.
- `InstanceGUID` enables deduplication in the backend if events are delivered more than once (e.g. retry on network failure).
- `Connect()` is public for interface technical reasons but **must only be called by `UGameCoreBackendSubsystem`**.