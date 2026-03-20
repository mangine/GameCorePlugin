# Dialogue System — Enums

**File:** `Dialogue/DialogueEnums.h`

All enums for the Dialogue System. Kept in a single header to avoid include cycles between nodes, types, and components.

---

```cpp
// File: Dialogue/DialogueEnums.h
#pragma once

#include "CoreMinimal.h"
#include "DialogueEnums.generated.h"

// Session mode set once per UDialogueAsset by designers.
UENUM(BlueprintType)
enum class EDialogueSessionMode : uint8
{
    // One instigator. Direct replication to that actor's UDialogueManagerComponent.
    Single  UMETA(DisplayName = "Single"),
    // Multiple participants. One Chooser (choice submissions accepted),
    // rest are Observers (state replicated, submissions rejected).
    Group   UMETA(DisplayName = "Group"),
};

// Reason a dialogue session ended.
UENUM(BlueprintType)
enum class EDialogueEndReason : uint8
{
    Completed               UMETA(DisplayName = "Completed"),            // Reached UDialogueNode_End normally
    Interrupted             UMETA(DisplayName = "Interrupted"),          // ForceEnd called externally
    ChooserDisconnected     UMETA(DisplayName = "Chooser Disconnected"),  // Chooser left the session
    AssetError              UMETA(DisplayName = "Asset Error"),           // Invalid node index or null node
};

// Action the interpreter should take after executing a node.
// Returned inside FDialogueStepResult — consumed by FDialogueSimulator::Advance.
UENUM()
enum class EDialogueStepAction : uint8
{
    Continue,        // Advance immediately to NextNodeIndex.
    WaitForACK,      // Push FDialogueClientState to clients; wait for Server_ReceiveACK.
    WaitForChoice,   // Push FDialogueClientState with choices; wait for Server_ReceiveChoice.
    EndSession,      // Session is complete.
    SubDialoguePush, // Push a new asset onto the session stack.
};

// Discriminant for FDialogueVariant.
UENUM()
enum class EDialogueVariantType : uint8
{
    Bool,
    Int,
    Tag,
};
```
