// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interfaces/GroupProvider.h"
#include "GroupActor.generated.h"

class UGroupComponent;
class ARaidActor;

/**
 * AGroupActor
 *
 * Server-only actor owning one group.
 * Implements IGroupProvider so Quest, Requirement, and other systems
 * can consume group data without a hard dependency on the Group module.
 * Delegates all member management to UGroupComponent.
 *
 * bReplicates = false — clients never see this actor.
 */
UCLASS(NotBlueprintable)
class GAMECORE_API AGroupActor : public AActor, public IGroupProvider
{
    GENERATED_BODY()
public:
    AGroupActor();

    UGroupComponent* GetGroupComponent() const { return GroupComponent; }

    // Returns the raid this group belongs to, or nullptr.
    ARaidActor* GetOwningRaid() const { return OwningRaid.Get(); }

    // ── IGroupProvider ──────────────────────────────────────────────────────
    virtual int32   GetGroupSize()                                  const override;
    virtual bool    IsGroupLeader()                                 const override;
    virtual void    GetGroupMembers(TArray<APlayerState*>& Out)     const override;
    virtual AActor* GetGroupActor()                                 const override { return const_cast<AGroupActor*>(this); }

private:
    UPROPERTY()
    TObjectPtr<UGroupComponent> GroupComponent;

    // Backref to the raid this group belongs to. Null if not in a raid.
    TWeakObjectPtr<ARaidActor> OwningRaid;

    friend class URaidComponent;
};
