#include "LevelingComponent.h"
#include "PointPoolComponent.h"
#include "Net/UnrealNetwork.h"
#include "JsonObjectConverter.h"

DEFINE_LOG_CATEGORY_STATIC(LogGameCoreProgression, Log, All);

ULevelingComponent::ULevelingComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = false;
}

void ULevelingComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ULevelingComponent, ProgressionData);
}

// ── Registration ──────────────────────────────────────────────────────────────

bool ULevelingComponent::RegisterProgression(ULevelProgressionDefinition* Definition)
{
    if (!Definition) return false;
    if (!Definition->ProgressionTag.IsValid()) return false;
    if (Definitions.Contains(Definition->ProgressionTag)) return false;

    // Check prerequisites.
    if (!Definition->ArePrerequisitesMet(this, GetOwner())) return false;

    Definitions.Add(Definition->ProgressionTag, Definition);

    // Add a new runtime data entry.
    FProgressionLevelData& NewData = ProgressionData.Items.AddDefaulted_GetRef();
    NewData.ProgressionTag = Definition->ProgressionTag;
    NewData.CurrentLevel   = 1;
    NewData.CurrentXP      = 0;
    ProgressionData.MarkItemDirty(NewData);

    NotifyDirty();
    return true;
}

void ULevelingComponent::UnregisterProgression(FGameplayTag ProgressionTag)
{
    Definitions.Remove(ProgressionTag);
    ProgressionData.Items.RemoveAll([ProgressionTag](const FProgressionLevelData& D)
        { return D.ProgressionTag == ProgressionTag; });

    NotifyDirty();
}

// ── ApplyXP ───────────────────────────────────────────────────────────────────

void ULevelingComponent::ApplyXP(FGameplayTag ProgressionTag, int32 WarXP, int32 ContentLevel)
{
    LastAppliedXPDelta = 0;

    ULevelProgressionDefinition* Def = Definitions.FindRef(ProgressionTag);
    if (!Def) return;

    const float Reduction = Def->ReductionPolicy
        ? Def->ReductionPolicy->Evaluate(GetLevel(ProgressionTag), ContentLevel)
        : 1.f;

    const int32 FinalXP = FMath::RoundToInt(WarXP * Reduction);
    LastAppliedXPDelta = FinalXP;
    if (FinalXP == 0) return;

    FProgressionLevelData* Data = FindProgressionData(ProgressionTag);
    if (!Data) return;

    const int32 NewXP = Def->bAllowLevelDecrement
        ? Data->CurrentXP + FinalXP                       // Signed — can go negative
        : FMath::Max(Data->CurrentXP + FinalXP, 0);       // Floor at 0
    Data->CurrentXP = NewXP;
    ProgressionData.MarkItemDirty(*Data);

    // Intra-system delegate (UProgressionSubsystem reads LastAppliedXPDelta post-call).
    OnXPChanged.Broadcast(ProgressionTag, NewXP, FinalXP);

    // Check level-up: loop in case multiple levels gained in one grant.
    while (Data->CurrentLevel < Def->MaxLevel)
    {
        const int32 XPNeeded = Def->GetXPRequiredForLevel(Data->CurrentLevel);
        if (XPNeeded <= 0 || Data->CurrentXP < XPNeeded) break;
        Data->CurrentXP -= XPNeeded;
        ProcessLevelUp(*Data, Def);
    }

    // Check level-down (opt-in via bAllowLevelDecrement).
    if (Def->bAllowLevelDecrement)
    {
        while (Data->CurrentLevel > 1 && Data->CurrentXP < 0)
        {
            const int32 XPOfPrevLevel = Def->GetXPRequiredForLevel(Data->CurrentLevel - 1);
            Data->CurrentXP += XPOfPrevLevel;
            ProcessLevelDown(*Data, Def);
        }
        // Final floor: level 1, XP 0.
        if (Data->CurrentLevel == 1)
            Data->CurrentXP = FMath::Max(Data->CurrentXP, 0);
    }

    NotifyDirty();
}

// ── ProcessLevelUp / ProcessLevelDown / GrantPointsForLevel ───────────────────

void ULevelingComponent::ProcessLevelUp(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    Data.CurrentLevel++;
    ProgressionData.MarkItemDirty(Data);

    GrantPointsForLevel(Def, Data.CurrentLevel);

    // Intra-system — UProgressionSubsystem binds for audit.
    // Event Bus LevelUp message with full Instigator is broadcast by the subsystem post-ApplyXP.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.CurrentLevel);
}

void ULevelingComponent::ProcessLevelDown(FProgressionLevelData& Data, ULevelProgressionDefinition* Def)
{
    Data.CurrentLevel--;
    ProgressionData.MarkItemDirty(Data);

    // Reuse OnLevelUp — NewLevel is lower; listeners inspect NewLevel vs OldLevel in the GMS message.
    OnLevelUp.Broadcast(Data.ProgressionTag, Data.CurrentLevel);
}

void ULevelingComponent::GrantPointsForLevel(ULevelProgressionDefinition* Def, int32 NewLevel)
{
    if (!Def->LevelUpGrant.PoolTag.IsValid()) return;

    const int32 Amount = Def->GetGrantAmountForLevel(NewLevel);
    if (Amount <= 0) return;

    AActor* Owner = GetOwner();
    if (!Owner) return;

    UPointPoolComponent* PoolComp = Owner->FindComponentByClass<UPointPoolComponent>();
    if (!PoolComp)
    {
        UE_LOG(LogGameCoreProgression, Warning,
            TEXT("ULevelingComponent: %s leveled up but owner has no UPointPoolComponent — pool grant skipped."),
            *Owner->GetName());
        return;
    }

    const EPointAddResult Result = PoolComp->AddPoints(Def->LevelUpGrant.PoolTag, Amount);
    if (Result == EPointAddResult::PartialCap)
    {
        UE_LOG(LogGameCoreProgression, Warning,
            TEXT("ULevelingComponent: Pool %s cap hit during level-up grant for %s. Points partially lost."),
            *Def->LevelUpGrant.PoolTag.ToString(), *Owner->GetName());
    }
}

// ── Read API ──────────────────────────────────────────────────────────────────

int32 ULevelingComponent::GetLevel(FGameplayTag ProgressionTag) const
{
    if (const FProgressionLevelData* Data = FindProgressionData(ProgressionTag))
        return Data->CurrentLevel;
    return 0;
}

int32 ULevelingComponent::GetXP(FGameplayTag ProgressionTag) const
{
    if (const FProgressionLevelData* Data = FindProgressionData(ProgressionTag))
        return Data->CurrentXP;
    return 0;
}

int32 ULevelingComponent::GetXPToNextLevel(FGameplayTag ProgressionTag) const
{
    const FProgressionLevelData* Data = FindProgressionData(ProgressionTag);
    if (!Data) return 0;

    ULevelProgressionDefinition* const* DefPtr = Definitions.Find(ProgressionTag);
    if (!DefPtr || !*DefPtr) return 0;

    const int32 XPNeeded = (*DefPtr)->GetXPRequiredForLevel(Data->CurrentLevel);
    return FMath::Max(0, XPNeeded - Data->CurrentXP);
}

bool ULevelingComponent::IsProgressionRegistered(FGameplayTag ProgressionTag) const
{
    return Definitions.Contains(ProgressionTag);
}

// ── Persistence ───────────────────────────────────────────────────────────────

void ULevelingComponent::Serialize_Save(FArchive& Ar)
{
    int32 Count = ProgressionData.Items.Num();
    Ar << Count;
    for (FProgressionLevelData& Entry : ProgressionData.Items)
    {
        FName TagName = FName(*Entry.ProgressionTag.ToString());
        Ar << TagName;
        Ar << Entry.CurrentLevel;
        Ar << Entry.CurrentXP;
    }
}

void ULevelingComponent::Serialize_Load(FArchive& Ar, uint32 SavedVersion)
{
    int32 Count = 0;
    Ar << Count;
    ProgressionData.Items.Reset(Count);
    for (int32 i = 0; i < Count; ++i)
    {
        FName TagName;
        Ar << TagName;
        int32 Level = 0, XP = 0;
        Ar << Level;
        Ar << XP;

        FProgressionLevelData& Entry = ProgressionData.Items.AddDefaulted_GetRef();
        Entry.ProgressionTag = FGameplayTag::RequestGameplayTag(TagName);
        Entry.CurrentLevel   = Level;
        Entry.CurrentXP      = XP;
    }
    bDirty = false;
}

void ULevelingComponent::ClearIfSaved(uint32 FlushedGeneration)
{
    if (DirtyGeneration <= FlushedGeneration)
        bDirty = false;
}

void ULevelingComponent::NotifyDirty()
{
    bDirty = true;
    ++DirtyGeneration;
}

FString ULevelingComponent::SerializeToString() const
{
    // GM tooling / debug only — not the production save path.
    TArray<TSharedPtr<FJsonValue>> ItemsJson;
    for (const FProgressionLevelData& Entry : ProgressionData.Items)
    {
        TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
        Obj->SetStringField(TEXT("Tag"),   Entry.ProgressionTag.ToString());
        Obj->SetNumberField(TEXT("Level"), Entry.CurrentLevel);
        Obj->SetNumberField(TEXT("XP"),    Entry.CurrentXP);
        ItemsJson.Add(MakeShared<FJsonValueObject>(Obj));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetArrayField(TEXT("Progressions"), ItemsJson);

    FString Output;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
    FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    return Output;
}

void ULevelingComponent::DeserializeFromString(const FString& Data)
{
    TSharedPtr<FJsonObject> Root;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Data);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return;

    const TArray<TSharedPtr<FJsonValue>>* ItemsJson = nullptr;
    if (!Root->TryGetArrayField(TEXT("Progressions"), ItemsJson)) return;

    ProgressionData.Items.Reset(ItemsJson->Num());
    for (const TSharedPtr<FJsonValue>& Val : *ItemsJson)
    {
        const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
        if (!Val->TryGetObject(ObjPtr) || !ObjPtr) continue;

        FString TagStr;
        int32   Level = 0, XP = 0;
        (*ObjPtr)->TryGetStringField(TEXT("Tag"),   TagStr);
        (*ObjPtr)->TryGetNumberField(TEXT("Level"), Level);
        (*ObjPtr)->TryGetNumberField(TEXT("XP"),    XP);

        FProgressionLevelData& Entry = ProgressionData.Items.AddDefaulted_GetRef();
        Entry.ProgressionTag = FGameplayTag::RequestGameplayTag(FName(*TagStr));
        Entry.CurrentLevel   = Level;
        Entry.CurrentXP      = XP;
    }
    NotifyDirty();
}

// ── Private Helpers ───────────────────────────────────────────────────────────

FProgressionLevelData* ULevelingComponent::FindProgressionData(FGameplayTag Tag)
{
    for (FProgressionLevelData& Entry : ProgressionData.Items)
    {
        if (Entry.ProgressionTag == Tag)
            return &Entry;
    }
    return nullptr;
}

const FProgressionLevelData* ULevelingComponent::FindProgressionData(FGameplayTag Tag) const
{
    for (const FProgressionLevelData& Entry : ProgressionData.Items)
    {
        if (Entry.ProgressionTag == Tag)
            return &Entry;
    }
    return nullptr;
}
