#include "Backend/AuditService.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Policies/CondensedJsonPrintPolicy.h"

DEFINE_LOG_CATEGORY(LogGameCore);

// ---------------------------------------------------------------------------
// FAuditServiceBase
// ---------------------------------------------------------------------------

bool FAuditServiceBase::Connect(const FString& ConnectionString)
{
    // Base class has no connection — concrete implementations override if needed.
    return true;
}

void FAuditServiceBase::SetServerId(const FString& InServerId)
{
    ServerId    = InServerId;
    bServerIdSet = true;
    FlushQueue();
}

void FAuditServiceBase::RecordEvent(const FAuditEntry& Entry)
{
    EnqueueInternal(StampEntry(Entry));
}

void FAuditServiceBase::RecordBatch(TArray<FAuditEntry>&& Entries, bool bTransactional)
{
    const uint64 GroupId = bTransactional ? NextGroupId++ : 0;
    for (FAuditEntry& Entry : Entries)
    {
        FAuditEntryInternal Internal = StampEntry(Entry);
        Internal.bTransactional     = bTransactional;
        Internal.TransactionGroupId = bTransactional ? GroupId : 0;
        EnqueueInternal(MoveTemp(Internal));
    }
}

void FAuditServiceBase::Flush()
{
    FlushQueue();
}

FAuditEntryInternal FAuditServiceBase::StampEntry(const FAuditEntry& Entry) const
{
    FAuditEntryInternal Internal;
    Internal.Entry        = Entry;
    Internal.InstanceGUID = FGuid::NewGuid();
    Internal.ServerId     = ServerId;
    Internal.Timestamp    = FDateTime::UtcNow();
    return Internal;
}

void FAuditServiceBase::EnqueueInternal(FAuditEntryInternal&& Internal)
{
    if (!bServerIdSet)
    {
        // Warn once when queue hits 50% while ServerId is unset
        if (!bWarnedOnHalfFull && PendingQueue.Num() >= MaxQueueSize / 2)
        {
            UE_LOG(LogGameCore, Warning,
                TEXT("[AuditService] Queue at 50%% capacity but ServerId not yet set."));
            bWarnedOnHalfFull = true;
        }
    }

    if (PendingQueue.Num() >= MaxQueueSize)
    {
        UE_LOG(LogGameCore, Warning,
            TEXT("[AuditService] Queue overflow — dropping oldest entry."));
        PendingQueue.RemoveAt(0);
    }

    PendingQueue.Add(MoveTemp(Internal));

    // Pressure flush
    if (bServerIdSet && PendingQueue.Num() >= FMath::RoundToInt(MaxQueueSize * FlushThresholdPercent))
    {
        FlushQueue();
    }
}

void FAuditServiceBase::FlushQueue()
{
    if (PendingQueue.IsEmpty()) return;

    // Group by TransactionGroupId
    TMap<uint64, TArray<FAuditEntryInternal>> Groups;
    for (FAuditEntryInternal& Internal : PendingQueue)
    {
        const uint64 Key = Internal.bTransactional ? Internal.TransactionGroupId : 0;
        Groups.FindOrAdd(Key).Add(Internal);
    }
    PendingQueue.Empty();

    for (auto& [GroupId, Batch] : Groups)
    {
        const bool bTxn = (GroupId != 0);
        // Split into MaxBatchSize chunks
        for (int32 Offset = 0; Offset < Batch.Num(); Offset += MaxBatchSize)
        {
            const int32 Count = FMath::Min(MaxBatchSize, Batch.Num() - Offset);
            TArray<FAuditEntryInternal> Chunk(Batch.GetData() + Offset, Count);
            DispatchBatch(Chunk, bTxn);
        }
    }
}

// ---------------------------------------------------------------------------
// FAuditPayloadBuilder
// ---------------------------------------------------------------------------

FAuditPayloadBuilder& FAuditPayloadBuilder::SetInt(const FString& Key, int64 Value)
{
    JsonObject->SetNumberField(Key, static_cast<double>(Value));
    return *this;
}

FAuditPayloadBuilder& FAuditPayloadBuilder::SetFloat(const FString& Key, float Value)
{
    JsonObject->SetNumberField(Key, static_cast<double>(Value));
    return *this;
}

FAuditPayloadBuilder& FAuditPayloadBuilder::SetString(const FString& Key, const FString& Value)
{
    JsonObject->SetStringField(Key, Value);
    return *this;
}

FAuditPayloadBuilder& FAuditPayloadBuilder::SetBool(const FString& Key, bool Value)
{
    JsonObject->SetBoolField(Key, Value);
    return *this;
}

FAuditPayloadBuilder& FAuditPayloadBuilder::SetGuid(const FString& Key, const FGuid& Value)
{
    JsonObject->SetStringField(Key, Value.ToString(EGuidFormats::DigitsWithHyphens));
    return *this;
}

FAuditPayloadBuilder& FAuditPayloadBuilder::SetTag(const FString& Key, const FGameplayTag& Value)
{
    JsonObject->SetStringField(Key, Value.ToString());
    return *this;
}

FString FAuditPayloadBuilder::ToString() const
{
    FString Output;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
    return Output;
}

// ---------------------------------------------------------------------------
// FLoggingServiceBase
// ---------------------------------------------------------------------------

FLoggingServiceBase::FLoggingServiceBase()
    : Queue(4096)
{
}

FLoggingServiceBase::~FLoggingServiceBase()
{
    Flush();
    bShuttingDown.Set(1);
    if (FlushEvent)
    {
        FlushEvent->Trigger();
    }
    if (FlushThread)
    {
        FlushThread->WaitForCompletion();
        delete FlushThread;
        FlushThread = nullptr;
    }
    if (FlushEvent)
    {
        FPlatformProcess::ReturnSynchEventToPool(FlushEvent);
        FlushEvent = nullptr;
    }
}

void FLoggingServiceBase::Initialize(const FLoggingConfig& InConfig)
{
    Config = InConfig;

    // Re-size queue storage to configured max
    Queue = TCircularQueue<FLogEntry>(FMath::Max(64, Config.MaxQueueSize));

    FlushEvent = FPlatformProcess::GetSynchEventFromPool(false);

    if (ConnectToBackend(Config))
    {
        bConnected.Set(1);
        CurrentReconnectDelay = 0.f;
    }
    else
    {
        UE_LOG(LogGameCore, Warning, TEXT("[LoggingService] Initial connection failed. Will retry."));
        CurrentReconnectDelay = Config.ReconnectDelaySeconds;
    }

    FlushThread = FRunnableThread::Create(this, TEXT("GameCore.LoggingFlushThread"),
        0, TPri_BelowNormal);
}

void FLoggingServiceBase::Log(ELogSeverity Severity, const FString& Category,
    const FString& Message, const FString& Payload)
{
    FLogEntry Entry;
    Entry.Severity  = Severity;
    Entry.Category  = Category;
    Entry.Message   = Message;
    Entry.Payload   = Payload;
    Entry.Timestamp = FDateTime::UtcNow();

    if (!Queue.Enqueue(Entry))
    {
        // Queue full — oldest entries dropped implicitly by TCircularQueue wrapping
        UE_LOG(LogGameCore, Warning,
            TEXT("[LoggingService] Queue overflow — entry dropped."));
    }

    // Pressure flush signal
    if (FlushEvent && Queue.Count() >= static_cast<uint32>(Config.MaxQueueSize * Config.FlushThresholdPercent))
    {
        FlushEvent->Trigger();
    }
}

void FLoggingServiceBase::Flush()
{
    DrainQueue(true);
}

uint32 FLoggingServiceBase::Run()
{
    while (!bShuttingDown.GetValue())
    {
        const uint32 WaitMs = static_cast<uint32>(Config.FlushIntervalSeconds * 1000.0f);
        if (FlushEvent)
        {
            FlushEvent->Wait(WaitMs);
            FlushEvent->Reset();
        }
        else
        {
            FPlatformProcess::Sleep(Config.FlushIntervalSeconds);
        }

        if (bShuttingDown.GetValue()) break;

        if (!bConnected.GetValue())
        {
            AttemptReconnect();
        }

        DrainQueue(false);
    }
    return 0;
}

void FLoggingServiceBase::AttemptReconnect()
{
    FPlatformProcess::Sleep(CurrentReconnectDelay);
    if (ConnectToBackend(Config))
    {
        bConnected.Set(1);
        CurrentReconnectDelay = 0.f;
        UE_LOG(LogGameCore, Log, TEXT("[LoggingService] Reconnected successfully."));
    }
    else
    {
        CurrentReconnectDelay = FMath::Min(
            CurrentReconnectDelay > 0.f ? CurrentReconnectDelay * 2.f : Config.ReconnectDelaySeconds,
            Config.MaxReconnectDelaySeconds);
    }
}

void FLoggingServiceBase::DrainQueue(bool bForceSynchronous)
{
    if (!bConnected.GetValue()) return;

    TArray<FLogEntry> Batch;
    FLogEntry Entry;
    while (Queue.Dequeue(Entry))
    {
        Batch.Add(Entry);
        if (Batch.Num() >= Config.MaxBatchSize)
        {
            DispatchBatch(Batch);
            Batch.Reset();
        }
    }
    if (Batch.Num() > 0)
    {
        DispatchBatch(Batch);
    }
}
