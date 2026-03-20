# URequirement_FactionCompatibility

**File:** `Factions/Requirements/Requirement_FactionCompatibility.h / .cpp`

A `URequirement` subclass that validates an actor's existing primary faction memberships are compatible with joining a target faction. Used in `UFactionDefinition::JoinRequirements` to enforce mutual-exclusion constraints at join time.

The minimum allowed relationship is configurable per faction — the common case is blocking actors whose current factions are `Hostile` to the target, but requiring `Neutral` or `Ally` standing is equally expressible.

This requirement is **server-only** (`ERequirementDataAuthority::ServerOnly`) — `UFactionSubsystem` only runs with authority.

---

## Class Definition

```cpp
/**
 * URequirement_FactionCompatibility
 *
 * Blocks JoinFaction() if the joining actor has any primary faction membership
 * whose relationship toward TargetFactionTag is below MinimumAllowedRelationship.
 *
 * Server-only. Evaluates via UFactionSubsystem::GetRelationship.
 * Must not appear in ClientOnly or ClientValidated requirement sets.
 */
UCLASS(EditInlineNew, CollapseCategories,
    meta=(DisplayName="Faction Compatibility"))
class GAMECORE_API URequirement_FactionCompatibility : public URequirement
{
    GENERATED_BODY()
public:

    // The faction being joined. Relationship toward this faction is checked
    // for all existing primary memberships of the joining actor.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    FGameplayTag TargetFactionTag;

    // Minimum relationship the joining actor's existing primary factions must
    // have toward TargetFactionTag.
    //
    // Unfriendly (default): blocks join only if a current faction is Hostile.
    // Neutral:              blocks join if any conflict exists.
    // Ally:                 requires existing ally status (prestige unlock).
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Requirement")
    EFactionRelationship MinimumAllowedRelationship =
        EFactionRelationship::Unfriendly;

    // Server-only — UFactionSubsystem is authoritative.
    // Must not appear in ClientOnly or ClientValidated sets.
    virtual ERequirementDataAuthority GetDataAuthority() const override
    {
        return ERequirementDataAuthority::ServerOnly;
    }

    virtual FRequirementResult Evaluate(
        const FRequirementContext& Context) const override
    {
        if (!Context.Instigator) return FRequirementResult::Pass();

        const UFactionComponent* FC =
            Context.Instigator->FindComponentByClass<UFactionComponent>();
        if (!FC) return FRequirementResult::Pass(); // No factions = no conflict.

        const UFactionSubsystem* FS =
            Context.World
            ? Context.World->GetSubsystem<UFactionSubsystem>()
            : nullptr;
        if (!FS) return FRequirementResult::Pass();

        for (const FFactionMembership& M : FC->Memberships.Items)
        {
            if (!M.bPrimary) continue;

            const EFactionRelationship Rel =
                FS->GetRelationship(M.FactionTag, TargetFactionTag);

            if ((uint8)Rel < (uint8)MinimumAllowedRelationship)
            {
                const UFactionDefinition* Def = FS->GetDefinition(M.FactionTag);
                return FRequirementResult::Fail(
                    FText::Format(
                        LOCTEXT("FactionConflict",
                            "Your membership in {0} conflicts with joining this faction."),
                        Def ? Def->DisplayName
                            : FText::FromName(M.FactionTag.GetTagName())));
            }
        }
        return FRequirementResult::Pass();
    }

    // Event-driven watcher: invalidate when any faction membership changes.
    virtual void GetWatchedEvents_Implementation(
        FGameplayTagContainer& OutEvents) const override
    {
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag("GameCoreEvent.Faction.MemberJoined"));
        OutEvents.AddTag(
            FGameplayTag::RequestGameplayTag("GameCoreEvent.Faction.MemberLeft"));
    }

#if WITH_EDITOR
    virtual FString GetDescription() const override
    {
        return FString::Printf(
            TEXT("FactionCompatibility: existing primaries >= %s toward %s"),
            *StaticEnum<EFactionRelationship>()->GetNameStringByValue(
                (int64)MinimumAllowedRelationship),
            *TargetFactionTag.ToString());
    }
#endif
};
```

---

## Notes

- **No actor with no `UFactionComponent`** is ever blocked by this requirement — `FindComponentByClass` returning null passes silently. This is the correct behaviour: an actor with no factions has no conflicting memberships.
- **Secondary memberships are skipped** — only `bPrimary = true` entries are checked.
- `GetWatchedEvents_Implementation` exposes `MemberJoined` and `MemberLeft` so that any `URequirementList` watching this requirement automatically re-evaluates when membership changes. This is for use in watched requirement lists (e.g., quest prerequisites), not for `JoinFaction` itself which evaluates imperatively.
