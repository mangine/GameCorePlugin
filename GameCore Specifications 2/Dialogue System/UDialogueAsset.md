# UDialogueAsset

**File:** `Dialogue/Assets/DialogueAsset.h / .cpp`  
**Base:** `UDataAsset`

---

## Overview

The authored content unit for a dialogue. Contains a flat array of instanced `UDialogueNode` objects and a start index. Shared (read-only) across all sessions that reference it — never modified at runtime.

Designers create these in the Content Browser via right-click → **GameCore → Dialogue Asset**. Double-clicking opens the Dialogue Preview Tool.

---

## Class Definition

```cpp
// File: Dialogue/Assets/DialogueAsset.h

UCLASS()
class GAMECORE_API UDialogueAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // Session mode. Single wraps the instigator in a one-element array internally.
    // Set once per asset — changing mid-session is not supported.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    EDialogueSessionMode SessionMode = EDialogueSessionMode::Single;

    // Index of the first node executed when a session starts.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Dialogue")
    int32 StartNodeIndex = 0;

    // All nodes in this dialogue. Instanced — each is a UDialogueNode subclass
    // configured in this asset. Indices are stable after cook.
    UPROPERTY(EditAnywhere, Instanced, Category = "Dialogue")
    TArray<TObjectPtr<UDialogueNode>> Nodes;

    // Returns the node at Index, or nullptr if out of bounds.
    // Always check the return value before use.
    const UDialogueNode* GetNode(int32 Index) const;

#if WITH_EDITOR
    // Validates that all UDialogueNode_Line instances use StringTable-sourced FText.
    // Logs a warning for each violation. Raw string literals in line nodes are a
    // localization bug caught at save time, not at runtime.
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
```

---

## Implementation Notes

### GetNode
```cpp
const UDialogueNode* UDialogueAsset::GetNode(int32 Index) const
{
    if (!Nodes.IsValidIndex(Index))
        return nullptr;
    return Nodes[Index].Get();
}
```

### IsDataValid
Iterate all nodes. For every `UDialogueNode_Line`, check `LineText.IsFromStringTable()`. If false, add a warning to `Context` and set result to `EDataValidationResult::Invalid`. Also check `FDialogueChoiceConfig::Label` in any `UDialogueNode_PlayerChoice` nodes. `StartNodeIndex` must be a valid index into `Nodes`.

---

## Important Notes

- The asset must be **never mutated at runtime**. Multiple sessions may reference the same asset concurrently — all node state is in `FDialogueSession`, not on the node objects.
- `StartNodeIndex` defaults to 0. If the first node in the array is not a valid starting node (e.g. it's an End node with no predecessors), this is a design error caught by the preview tool at open time.
- The `Instanced` specifier on `Nodes` means node subclass instances are embedded directly in the asset's serialization — no separate assets needed per node.
