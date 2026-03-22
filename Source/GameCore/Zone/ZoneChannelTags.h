#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

namespace GameCore::Zone::Tags
{
    // Broadcast by UZoneTrackerComponent on enter/exit events.
    // Payload: FZoneTransitionMessage.  Scope: Both.
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_Transition)

    // Broadcast by AZoneActor when FZoneDynamicState changes.
    // Payload: FZoneStateChangedMessage.  Server: ServerOnly.  Clients: ClientOnly (OnRep).
    UE_DECLARE_GAMEPLAY_TAG_EXTERN(Channel_StateChanged)
}
