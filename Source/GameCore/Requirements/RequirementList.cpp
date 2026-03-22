// Copyright GameCore Plugin. All Rights Reserved.
#include "RequirementList.h"
#include "RequirementComposite.h"
#include "RequirementLibrary.h"
#include "EventBus/GameCoreEventWatcher.h"

// ── Imperative Evaluation ──────────────────────────────────────────────────

FRequirementResult URequirementList::Evaluate(const FRequirementContext& Context) const
{
	return URequirementLibrary::EvaluateAll(Requirements, Operator, Context);
}

FRequirementResult URequirementList::EvaluateFromEvent(const FRequirementContext& Context) const
{
	return URequirementLibrary::EvaluateAllFromEvent(Requirements, Operator, Context);
}

// ── Reactive Watch Registration ────────────────────────────────────────────

FEventWatchHandle URequirementList::RegisterWatch(
	const UObject* Owner,
	TFunction<void(bool)> OnResult) const
{
	if (!Owner || !OnResult) return FEventWatchHandle{};

	UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner);
	if (!Watcher) return FEventWatchHandle{};

	FGameplayTagContainer WatchedTags;
	CollectWatchedEvents(WatchedTags);
	if (WatchedTags.IsEmpty()) return FEventWatchHandle{};

	// Shared last-result state: OnResult fires only when pass/fail changes.
	// TSharedPtr so it outlives the registration and is safe across calls.
	auto LastResult = MakeShared<TOptional<bool>>();

	TWeakObjectPtr<const URequirementList> WeakList = this;
	EGameCoreEventScope Scope = AuthorityToScope();

	return Watcher->Register(Owner, WatchedTags, Scope,
		[WeakList, OnResult, LastResult]
		(FGameplayTag /*Tag*/, const FInstancedStruct& Payload)
		{
			const URequirementList* L = WeakList.Get();
			if (!L) return;

			FRequirementContext Ctx;
			Ctx.Data = Payload;

			FRequirementResult Result = L->EvaluateFromEvent(Ctx);

			if (!LastResult->IsSet() || LastResult->GetValue() != Result.bPassed)
			{
				*LastResult = Result.bPassed;
				OnResult(Result.bPassed);
			}
		});
}

void URequirementList::UnregisterWatch(const UObject* Owner, FEventWatchHandle Handle)
{
	if (UGameCoreEventWatcher* Watcher = UGameCoreEventWatcher::Get(Owner))
		Watcher->Unregister(Handle);
}

// ── Internal Utilities ─────────────────────────────────────────────────────

void URequirementList::CollectWatchedEvents(FGameplayTagContainer& OutTags) const
{
	for (const TObjectPtr<URequirement>& Req : Requirements)
	{
		if (!Req) continue;
		Req->GetWatchedEvents(OutTags);

		// Recurse into composites to collect children's tags as well.
		if (const URequirement_Composite* Composite = Cast<URequirement_Composite>(Req.Get()))
		{
			TArray<TObjectPtr<URequirement>> Stack(Composite->Children);
			while (!Stack.IsEmpty())
			{
				TObjectPtr<URequirement> Child = Stack.Pop(EAllowShrinking::No);
				if (!Child) continue;
				Child->GetWatchedEvents(OutTags);
				if (const URequirement_Composite* ChildComposite = Cast<URequirement_Composite>(Child.Get()))
				{
					Stack.Append(ChildComposite->Children);
				}
			}
		}
	}
}

TArray<URequirement*> URequirementList::GetAllRequirements() const
{
	TArray<URequirement*> Result;

	TArray<TObjectPtr<URequirement>> Stack(Requirements);
	while (!Stack.IsEmpty())
	{
		TObjectPtr<URequirement> Req = Stack.Pop(EAllowShrinking::No);
		if (!Req) continue;
		Result.Add(Req.Get());
		if (const URequirement_Composite* Composite = Cast<URequirement_Composite>(Req.Get()))
		{
			Stack.Append(Composite->Children);
		}
	}

	return Result;
}

// ── Private Helpers ────────────────────────────────────────────────────────

EGameCoreEventScope URequirementList::AuthorityToScope() const
{
	switch (Authority)
	{
	case ERequirementEvalAuthority::ServerOnly:      return EGameCoreEventScope::ServerOnly;
	case ERequirementEvalAuthority::ClientOnly:      return EGameCoreEventScope::ClientOnly;
	case ERequirementEvalAuthority::ClientValidated: return EGameCoreEventScope::Both;
	}
	return EGameCoreEventScope::ServerOnly;
}
