#include "Quest/Events/QuestEventPayloads.h"

// ---------------------------------------------------------------------------
// Gameplay Tag definitions
// ---------------------------------------------------------------------------

UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_Started,         "GameCoreEvent.Quest.Started");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_Completed,       "GameCoreEvent.Quest.Completed");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_Failed,          "GameCoreEvent.Quest.Failed");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_Abandoned,       "GameCoreEvent.Quest.Abandoned");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_BecameAvailable, "GameCoreEvent.Quest.BecameAvailable");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_StageStarted,    "GameCoreEvent.Quest.StageStarted");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_StageCompleted,  "GameCoreEvent.Quest.StageCompleted");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_StageFailed,     "GameCoreEvent.Quest.StageFailed");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_TrackerUpdated,  "GameCoreEvent.Quest.TrackerUpdated");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_DailyReset,      "GameCoreEvent.Quest.DailyReset");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_WeeklyReset,     "GameCoreEvent.Quest.WeeklyReset");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_GroupInvite,     "GameCoreEvent.Quest.GroupInvite");
UE_DEFINE_GAMEPLAY_TAG(TAG_GameCoreEvent_Quest_MemberLeft,      "GameCoreEvent.Quest.MemberLeft");

UE_DEFINE_GAMEPLAY_TAG(TAG_RequirementEvent_Quest_TrackerUpdated, "RequirementEvent.Quest.TrackerUpdated");
UE_DEFINE_GAMEPLAY_TAG(TAG_RequirementEvent_Quest_StageChanged,   "RequirementEvent.Quest.StageChanged");
UE_DEFINE_GAMEPLAY_TAG(TAG_RequirementEvent_Quest_Completed,      "RequirementEvent.Quest.Completed");
