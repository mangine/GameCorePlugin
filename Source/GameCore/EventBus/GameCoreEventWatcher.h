#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "GameplayMessageSubsystem.h"
#include "GameCoreEventBus.h"
#include "GameCoreEventWatcher.generated.h"

struct GAMECORE_API FEventWatchHandle
{
    uint32 Id = 0;

    bool IsValid()  const { return Id != 0; }
    bool operator==(const FEventWatchHandle& Other) const { return Id == Other.Id; }
    bool operator!=(const FEventWatchHandle& Other) const { return Id != Other.Id; }
};

inline uint32 GetTypeHash(const FEventWatchHandle& H) { return H.Id; }

UCLASS()
class GAMECORE_API UGameCoreEventWatcher : public UWorldSubsystem
{
    GENERATED_BODY()
public:

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    static UGameCoreEventWatcher* Get(const UObject* WorldContext);

    // ── Registration ──────────────────────────────────────────────────────────

    /**
     * Registers a callback for one or more tags.
     *
     * Scope controls which net role triggers delivery:
     *   ServerOnly  — fires only on server / standalone (not NM_Client)
     *   ClientOnly  — fires on NM_Client and NM_Standalone
     *   Both        — fires everywhere
     *
     * Use TWeakObjectPtr in the callback lambda for owner lifetime safety.
     * The watcher does not manage owner lifetime.
     *
     * @return FEventWatchHandle. Store it and pass to Unregister at teardown.
     */
    FEventWatchHandle Register(
        const UObject* Owner,
        const FGameplayTagContainer& Tags,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /** Single-tag overload with explicit scope. */
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        EGameCoreEventScope Scope,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /** Single-tag overload with implicit Both scope. */
    FEventWatchHandle Register(
        const UObject* Owner,
        FGameplayTag Tag,
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback);

    /**
     * Removes the registration and all associated tag subscriptions.
     * Unsubscribes from bus if this was the last registration for a given tag.
     * Safe to call with an invalid handle.
     */
    void Unregister(FEventWatchHandle Handle);

private:

    struct FWatchEntry
    {
        FEventWatchHandle Handle;
        FGameplayTagContainer Tags;
        EGameCoreEventScope Scope = EGameCoreEventScope::Both;
        TFunction<void(FGameplayTag, const FInstancedStruct&)> Callback;
#if !UE_BUILD_SHIPPING
        FString OwnerDebugName;
#endif
    };

    TMap<uint32, FWatchEntry>                          Entries;        // Handle.Id → entry
    TMap<FGameplayTag, TSet<uint32>>                   TagToHandles;   // Tag → set of Handle.Ids
    TMap<FGameplayTag, FGameplayMessageListenerHandle> BusHandles;     // Tag → GMS listener handle
    uint32 NextHandleId = 1;

    void SubscribeTagIfNeeded(FGameplayTag Tag);
    void UnsubscribeTagIfEmpty(FGameplayTag Tag);
    void OnBusEvent(FGameplayTag Tag, const FInstancedStruct& Payload);
    bool PassesScopeCheck(EGameCoreEventScope Scope) const;
};
