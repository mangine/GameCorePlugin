using UnrealBuildTool;

public class GameCore : ModuleRules
{
    public GameCore(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "GameplayMessageRuntime",   // UGameplayMessageSubsystem — Event Bus, Alignment, etc.
            "StructUtils",              // FInstancedStruct — Event Bus, Requirements
            "NetCore",                  // FFastArraySerializer, Push Model — Alignment, Currency, Faction, Group, Interaction, Inventory, Progression, Journal
            "EnhancedInput",            // UInputAction soft reference — Interaction System
            "DeveloperSettings",        // UDeveloperSettings — Faction, Notification settings
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "GameplayAbilities",        // Optional — UAbilitySystemComponent for Progression XP multiplier and Loot Luck attribute
        });

        // Enable Push Model replication support (required for MARK_PROPERTY_DIRTY_FROM_NAME)
        SetupGameplayDebuggerSupport(Target);
        SetupIrisSupport(Target);
    }
}
