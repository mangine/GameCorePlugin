#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayMessageSubsystem.h"
#include "FNotificationEntry.h"
#include "FNotificationGroup.h"
#include "FNotificationCategoryRule.h"
#include "UNotificationChannelConfig.h"
#include "UNotificationCategoryConfig.h"
#include "UGameCoreNotificationSubsystem.generated.h"

/**
 * UGameCoreNotificationSubsystem
 *
 * ULocalPlayerSubsystem — single authority for notification state on a local player.
 * Owns group storage, expiry timers, Event Bus listener handles, and delegate broadcasting.
 *
 * Server and non-local-player instances never get this subsystem.
 * Zero server interaction. Zero persistence. Pure presentation-layer system.
 *
 * CachedTotalUnviewed invariant: every code path that sets bViewed=true or removes an
 * unviewed entry MUST decrement CachedTotalUnviewed. Missing an update causes silent
 * badge desync (Known Issue #1).
 */
UCLASS()
class GAMECORE_API UGameCoreNotificationSubsystem : public ULocalPlayerSubsystem
{
    GENERATED_BODY()

public:
    // ── Delegates ─────────────────────────────────────────────────────────────

    // Fired immediately after a new entry is added to a group.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationAdded,   const FNotificationEntry&, Entry);
    // Fired when a specific entry is marked viewed (individual or batch).
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationViewed,  FGuid, NotificationId);
    // Fired when an entry is removed due to expiry timer elapsing.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNotificationExpired, FGuid, NotificationId);
    // Fired after any mutation that changes a group's contents or unviewed count.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGroupChanged,        const FNotificationGroup&, Group);
    // Fired when total unviewed count across ALL groups drops to zero.
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllViewed);

    UPROPERTY(BlueprintAssignable, Category = "Notification|Delegates")
    FOnNotificationAdded   OnNotificationAdded;

    UPROPERTY(BlueprintAssignable, Category = "Notification|Delegates")
    FOnNotificationViewed  OnNotificationViewed;

    UPROPERTY(BlueprintAssignable, Category = "Notification|Delegates")
    FOnNotificationExpired OnNotificationExpired;

    UPROPERTY(BlueprintAssignable, Category = "Notification|Delegates")
    FOnGroupChanged        OnGroupChanged;

    UPROPERTY(BlueprintAssignable, Category = "Notification|Delegates")
    FOnAllViewed           OnAllViewed;

    // ── Queries ───────────────────────────────────────────────────────────────

    // Total unviewed entries across all groups. Uses cached counter — O(1).
    UFUNCTION(BlueprintCallable, Category = "Notification")
    int32 GetTotalUnviewedCount() const;

    // Unviewed entries for a specific category.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    int32 GetUnviewedCountForCategory(FGameplayTag CategoryTag) const;

    // Returns a copy of the group for the given category. Returns empty group if none exists.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    FNotificationGroup GetGroup(FGameplayTag CategoryTag) const;

    // All active groups. No guaranteed ordering — UI should sort by Entries.Last().Timestamp.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    TArray<FNotificationGroup> GetAllGroups() const;

    // ── Mutations ─────────────────────────────────────────────────────────────

    UFUNCTION(BlueprintCallable, Category = "Notification")
    void MarkViewed(FGuid NotificationId);

    UFUNCTION(BlueprintCallable, Category = "Notification")
    void MarkAllViewed();

    UFUNCTION(BlueprintCallable, Category = "Notification")
    void MarkCategoryViewed(FGameplayTag CategoryTag);

    // Removes a single entry from its group. Fires OnGroupChanged.
    // Clears any active expiry timer for the entry.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    void DismissNotification(FGuid NotificationId);

    // Removes all entries in the group for CategoryTag.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    void DismissCategory(FGameplayTag CategoryTag);

    // Direct push API. Bypasses Event Bus. Produces the same delegate chain as a channel-sourced push.
    // Entry.Id must be invalid (default) — the subsystem assigns a fresh FGuid.
    // Entry.CategoryTag must be valid — entries with invalid CategoryTag are silently discarded.
    UFUNCTION(BlueprintCallable, Category = "Notification")
    void PushNotification(FNotificationEntry Entry);

    // Called by UNotificationChannelBinding after BuildEntry. Public to allow binding access.
    // Not intended as a game-layer API — use PushNotification for direct pushes.
    void HandleIncomingEntry(FNotificationEntry Entry);

    // ── ULocalPlayerSubsystem ─────────────────────────────────────────────────
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

private:
    UPROPERTY()
    TObjectPtr<UNotificationChannelConfig>  ChannelConfig;

    UPROPERTY()
    TObjectPtr<UNotificationCategoryConfig> CategoryConfig;

    // CategoryTag → group. Source of truth for all active notifications.
    TMap<FGameplayTag, FNotificationGroup>  Groups;

    // NotificationId → expiry timer handle. Only entries with ExpirySeconds > 0 appear here.
    TMap<FGuid, FTimerHandle>               ExpiryTimers;

    // Event Bus listener handles. All unregistered in Deinitialize.
    // Stored per-binding to support per-binding unregistration.
    TArray<FGameplayMessageListenerHandle>  ListenerHandles;

    // Write-through cache of total unviewed entries across all groups.
    // Updated by every path that changes bViewed or removes an unviewed entry.
    int32 CachedTotalUnviewed = 0;

    void LoadConfig();
    void RegisterChannelListeners();
    void UnregisterChannelListeners();

    // Core push logic. Called by both PushNotification and HandleIncomingEntry.
    void PushEntryInternal(FNotificationEntry& Entry);

    void StartExpiryTimer(const FNotificationEntry& Entry);
    void ClearExpiryTimer(FGuid NotificationId);
    void OnEntryExpired(FGuid NotificationId);

    // Finds the entry and its owning group by scanning all groups.
    // Returns nullptr if not found. OutGroup is set when a non-null entry is returned.
    FNotificationEntry* FindEntry(FGuid NotificationId, FNotificationGroup** OutGroup = nullptr);

    // Returns the rule for CategoryTag, or a static default if none configured.
    const FNotificationCategoryRule& GetRule(FGameplayTag CategoryTag) const;
};
