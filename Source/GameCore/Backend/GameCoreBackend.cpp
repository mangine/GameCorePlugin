#include "Backend/GameCoreBackend.h"
#include "Backend/BackendSubsystem.h"
#include "Backend/LoggingService.h"
#include "Backend/KeyStorageService.h"
#include "Backend/QueryStorageService.h"
#include "Backend/AuditService.h"

// ---------------------------------------------------------------------------
// Static member definitions
// ---------------------------------------------------------------------------

UGameCoreBackendSubsystem* FGameCoreBackend::Instance = nullptr;

TFunction<void(ELogSeverity, const FString&, const FString&, const FString&)>
    FGameCoreBackend::OnLog = nullptr;
TFunction<void(const FAuditEntry&)>
    FGameCoreBackend::OnAudit = nullptr;
TFunction<void(FGameplayTag, FGuid, TArrayView<const uint8>)>
    FGameCoreBackend::OnPersistenceWrite = nullptr;

// File-scope null statics — constructed at module load, never destroyed until module unload.
// Safe to return pointers to; no order-of-destruction issues.
static FNullLoggingService      GNullLogging;
static FNullKeyStorageService   GNullKeyStorage;
static FNullQueryStorageService GNullQueryStorage;
static FNullAuditService        GNullAudit;

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void FGameCoreBackend::Register(UGameCoreBackendSubsystem* Subsystem)
{
    Instance = Subsystem;
}

void FGameCoreBackend::Unregister()
{
    Instance = nullptr;
}

// ---------------------------------------------------------------------------
// Canonical call methods
// ---------------------------------------------------------------------------

void FGameCoreBackend::Log(
    ELogSeverity   Severity,
    const FString& Category,
    const FString& Message,
    const FString& Payload)
{
    if (OnLog)
    {
        OnLog(Severity, Category, Message, Payload);
        return;
    }
    GetLogging(FGameplayTag{})->Log(Severity, Category, Message, Payload);
}

void FGameCoreBackend::Audit(const FAuditEntry& Entry)
{
    if (OnAudit)
    {
        OnAudit(Entry);
        return;
    }
    GetAudit(FGameplayTag{})->RecordEvent(Entry);
}

void FGameCoreBackend::PersistenceWrite(
    FGameplayTag            Tag,
    FGuid                   EntityId,
    TArrayView<const uint8> Bytes)
{
    if (OnPersistenceWrite)
    {
        OnPersistenceWrite(Tag, EntityId, Bytes);
        return;
    }
    GetKeyStorage(Tag)->Set(Tag, EntityId, TArray<uint8>(Bytes.GetData(), Bytes.Num()), false, false);
}

// ---------------------------------------------------------------------------
// Tag-based accessors
// ---------------------------------------------------------------------------

ILoggingService* FGameCoreBackend::GetLogging(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveLoggingTag(Tag);
        return Instance->GetLogging(Key);
    }
    return &GNullLogging;
}

IKeyStorageService* FGameCoreBackend::GetKeyStorage(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveKeyStorageTag(Tag);
        return Instance->GetKeyStorage(Key);
    }
    return &GNullKeyStorage;
}

IQueryStorageService* FGameCoreBackend::GetQueryStorage(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveQueryStorageTag(Tag);
        return Instance->GetQueryStorage(Key);
    }
    return &GNullQueryStorage;
}

IAuditService* FGameCoreBackend::GetAudit(FGameplayTag Tag)
{
    if (Instance)
    {
        const FName Key = Instance->ResolveAuditTag(Tag);
        return Instance->GetAudit(Key);
    }
    return &GNullAudit;
}

// ---------------------------------------------------------------------------
// FName-based accessors
// ---------------------------------------------------------------------------

ILoggingService*      FGameCoreBackend::GetLogging     (FName Key) { return Instance ? Instance->GetLogging(Key)      : &GNullLogging;      }
IKeyStorageService*   FGameCoreBackend::GetKeyStorage  (FName Key) { return Instance ? Instance->GetKeyStorage(Key)   : &GNullKeyStorage;   }
IQueryStorageService* FGameCoreBackend::GetQueryStorage(FName Key) { return Instance ? Instance->GetQueryStorage(Key) : &GNullQueryStorage; }
IAuditService*        FGameCoreBackend::GetAudit       (FName Key) { return Instance ? Instance->GetAudit(Key)        : &GNullAudit;        }
