// Copyright GameCore Plugin. All Rights Reserved.
#include "GroupTypes.h"

DEFINE_LOG_CATEGORY(LogGroup);

namespace GameCoreGroupTags
{
    FGameplayTag InviteReceived;
    FGameplayTag InviteExpired;
    FGameplayTag InviteDeclined;
    FGameplayTag Formed;
    FGameplayTag MemberJoined;
    FGameplayTag MemberLeft;
    FGameplayTag MemberKicked;
    FGameplayTag MemberDisconnected;
    FGameplayTag MemberReconnected;
    FGameplayTag LeaderChanged;
    FGameplayTag Disbanded;
    FGameplayTag Raid_GroupJoined;
    FGameplayTag Raid_GroupLeft;
    FGameplayTag Raid_LeaderChanged;
    FGameplayTag Raid_Disbanded;
}

namespace
{
    struct FGroupTagRegistrar
    {
        FGroupTagRegistrar()
        {
            UE_CALL_ONCE([]
            {
                GameCoreGroupTags::InviteReceived  = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.InviteReceived"));
                GameCoreGroupTags::InviteExpired   = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.InviteExpired"));
                GameCoreGroupTags::InviteDeclined  = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.InviteDeclined"));
                GameCoreGroupTags::Formed          = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.Formed"));
                GameCoreGroupTags::MemberJoined    = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.MemberJoined"));
                GameCoreGroupTags::MemberLeft      = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.MemberLeft"));
                GameCoreGroupTags::MemberKicked    = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.MemberKicked"));
                GameCoreGroupTags::MemberDisconnected = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.MemberDisconnected"));
                GameCoreGroupTags::MemberReconnected  = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.MemberReconnected"));
                GameCoreGroupTags::LeaderChanged   = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.LeaderChanged"));
                GameCoreGroupTags::Disbanded       = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Group.Disbanded"));
                GameCoreGroupTags::Raid_GroupJoined    = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Raid.GroupJoined"));
                GameCoreGroupTags::Raid_GroupLeft      = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Raid.GroupLeft"));
                GameCoreGroupTags::Raid_LeaderChanged  = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Raid.LeaderChanged"));
                GameCoreGroupTags::Raid_Disbanded      = FGameplayTag::RequestGameplayTag(TEXT("GameCoreEvent.Raid.Disbanded"));
            });
        }
    };
    static FGroupTagRegistrar GGroupTagRegistrar;
}
