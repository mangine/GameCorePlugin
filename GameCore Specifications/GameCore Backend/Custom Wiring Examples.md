# Custom Wiring Examples

**Sub-page of:** [GameCore Backend](../GameCore%20Backend%2031cd261a36cf810fa874e884e5a0d634.md)

These examples show how to wire `FGameCoreBackend` to any external system — your own logging library, a third-party analytics SDK, a custom persistence layer — **without implementing the full service interfaces** and **without using `UGameCoreBackendSubsystem`** at all.

All wiring is done via the lightweight delegate hooks on `FGameCoreBackend`. The hooks are `TFunction` static fields — bind them in `GameInstance::Init`, clear them in `GameInstance::Shutdown`.

> **Cost:** One null-check branch per call when a hook is bound. Branch-predicted-not-taken when unbound. Effectively zero overhead in both cases.

---

## Wiring Options Matrix

| Goal | Mechanism | Subsystem Required? |
|---|---|---|
| Use full GameCore backend stack | `UGameCoreBackendSubsystem` + service interfaces | Yes |
| Wire logging only to a custom sink | `FGameCoreBackend::OnLog` delegate | No |
| Wire audit only to a custom sink | `FGameCoreBackend::OnAudit` delegate | No |
| Wire persistence writes to a custom sink | `FGameCoreBackend::OnPersistenceWrite` delegate | No |
| Mix: subsystem for logging, delegate for audit | Subsystem registered + `OnAudit` bound | Yes (partial) |
| No backend at all | Nothing | No |

When **no subsystem** and **no delegate** is wired:
- Logging → `UE_LOG` via `FNullLoggingService` (always visible)
- Audit → silent no-op
- Persistence writes → silent no-op, read callbacks return `bSuccess=false`

---

## Example 1 — Logging Only (no subsystem)

Route all GameCore logs to your own logging library without touching `UGameCoreBackendSubsystem`.

```cpp
// MyGameInstance.cpp
#include "Backend/GameCoreBackend.h"
#include "MyLoggingLibrary.h"

void UMyGameInstance::Init()
{
    Super::Init();

    // Bind the log hook — called for every FGameCoreBackend::Log() call in the plugin.
    // ELogSeverity maps to your own severity enum or string.
    FGameCoreBackend::OnLog = [](ELogSeverity Severity, const FString& Category,
                                 const FString& Message, const FString& Payload)
    {
        EMyLogLevel Level = EMyLogLevel::Info;
        switch (Severity)
        {
            case ELogSeverity::Warning:  Level = EMyLogLevel::Warn;     break;
            case ELogSeverity::Error:    Level = EMyLogLevel::Error;    break;
            case ELogSeverity::Critical: Level = EMyLogLevel::Critical; break;
            default: break;
        }
        FString Full = Payload.IsEmpty() ? Message : FString::Printf(TEXT("%s | %s"), *Message, *Payload);
        MyLoggingLibrary::Send(Category, Full, Level);
    };
}

void UMyGameInstance::Shutdown()
{
    // Always clear hooks on shutdown to avoid dangling lambda captures.
    FGameCoreBackend::OnLog = nullptr;
    Super::Shutdown();
}
```

**Result:** `UE_LOG` is completely bypassed — all GameCore log output goes to `MyLoggingLibrary`. No `UGameCoreBackendSubsystem` is created.

---

## Example 2 — Audit Only (no subsystem)

Route audit events to a third-party analytics SDK (e.g. GameAnalytics, Amplitude, custom Kafka producer) without using any GameCore backend infrastructure.

```cpp
// MyGameInstance.cpp
#include "Backend/GameCoreBackend.h"
#include "MyAnalyticsSDK.h"

void UMyGameInstance::Init()
{
    Super::Init();

    FGameCoreBackend::OnAudit = [](const FAuditEntry& Entry)
    {
        // Build a simple key-value event for the SDK.
        // EventTag.ToString() → e.g. "Audit.Progression.LevelUp"
        TMap<FString, FString> Properties;
        Properties.Add(TEXT("event"),   Entry.EventTag.ToString());
        Properties.Add(TEXT("actor"),   Entry.ActorId.ToString());
        Properties.Add(TEXT("payload"), Entry.Payload);

        MyAnalyticsSDK::TrackEvent(Entry.EventTag.GetTagName().ToString(), Properties);
    };
}

void UMyGameInstance::Shutdown()
{
    FGameCoreBackend::OnAudit = nullptr;
    Super::Shutdown();
}
```

**Result:** Every `FGameCoreBackend::Audit(Entry)` call anywhere in the plugin reaches your SDK. No `IAuditService` implementation needed. No subsystem.

---

## Example 3 — Persistence Write Only (no subsystem)

Route persistence write payloads to a custom in-house database adapter.

```cpp
// MyGameInstance.cpp
#include "Backend/GameCoreBackend.h"
#include "MyDatabaseAdapter.h"

void UMyGameInstance::Init()
{
    Super::Init();

    // Capture a shared ref to your adapter — avoid raw pointer if adapter has
    // a shorter lifetime than the GameInstance.
    TSharedPtr<FMyDatabaseAdapter> Adapter = MyDB;

    FGameCoreBackend::OnPersistenceWrite =
        [Adapter](FGameplayTag Tag, FGuid EntityId, TArrayView<const uint8> Bytes)
    {
        // Tag identifies what kind of entity this payload belongs to.
        // EntityId is the stable GUID of the actor.
        // Bytes is the binary blob from IPersistableComponent::SerializeForSave.
        Adapter->Upsert(Tag.GetTagName().ToString(), EntityId, Bytes);
    };
}

void UMyGameInstance::Shutdown()
{
    FGameCoreBackend::OnPersistenceWrite = nullptr;
    Super::Shutdown();
}
```

**Result:** All persistence write calls from `UPersistenceSubsystem` flow to your adapter. The `IKeyStorageService` interface and `UGameCoreBackendSubsystem` are never involved.

---

## Example 4 — Mixed: Subsystem for Storage, Delegate for Audit

Use the full `UGameCoreBackendSubsystem` for key/query storage, but bypass `IAuditService` entirely and send audit events directly to a dedicated security pipeline.

```cpp
void UMyGameInstance::Init()
{
    Super::Init();

    // Wire full storage backend via subsystem
    auto* Backend = GetSubsystem<UGameCoreBackendSubsystem>();
    Backend->RegisterKeyStorageService(TEXT("PlayerDB"),  PlayerRedis,  PlayerDbConfig);
    Backend->RegisterKeyStorageService(TEXT("EconomyDB"), EconomyRedis, EconomyDbConfig);
    Backend->RegisterLoggingService(NAME_None, MyDatadogLogger, LogConfig);
    // Note: no audit service registered on the subsystem

    // Route audit through a lightweight delegate to a separate pipeline
    FGameCoreBackend::OnAudit = [](const FAuditEntry& Entry)
    {
        // Direct push to an internal Kafka producer — zero GameCore infrastructure involved
        FString Json = FAuditPayloadBuilder{}
            .SetTag   (TEXT("event"),  Entry.EventTag)
            .SetGuid  (TEXT("actor"),  Entry.ActorId)
            .SetString(TEXT("data"),   Entry.Payload)
            .ToString();
        FMyKafkaProducer::Publish(TEXT("gamecore-audit"), Json);
    };

    Super::Init();
}
```

**Result:** Storage and logging go through the subsystem. Audit is short-circuited to Kafka before it ever reaches the subsystem.

---

## Example 5 — Debug Audit Visibility in Development

The null audit is silent by design. In development builds, bind a debug hook to surface audit events to the output log without wiring a real backend.

```cpp
void UMyGameInstance::Init()
{
    Super::Init();

#if !UE_BUILD_SHIPPING
    FGameCoreBackend::OnAudit = [](const FAuditEntry& Entry)
    {
        UE_LOG(LogGameCore, Log,
            TEXT("[DEV Audit] %s | Actor=%s | %s"),
            *Entry.EventTag.ToString(),
            *Entry.ActorId.ToString(),
            *Entry.Payload);
    };
#endif
}
```

**Result:** Audit events are visible in PIE and dev builds. In shipping builds the hook is never bound — silent no-op, zero overhead.

---

## Lifecycle Rules

1. **Bind in `GameInstance::Init`** — before any GameCore system is ticked.
2. **Clear in `GameInstance::Shutdown`** — `FGameCoreBackend::OnLog = nullptr` etc. Prevents dangling captures if the lambda captures a `TSharedPtr` or other object that may be destroyed before the static `TFunction` is.
3. **Do not rebind mid-session** — delegates are not guarded by a mutex. Changing them while backend calls may be in flight is a data race.
4. **Delegate takes priority** — when a delegate is bound, the subsystem path is skipped entirely for that service type. You cannot combine both for the same call.
