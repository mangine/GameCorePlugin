using UnrealBuildTool;

public class GameCoreEditor : ModuleRules
{
    public GameCoreEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameCore",             // Runtime types (ULootTable, FLootTableEntry, ILootRewardable, etc.)
            "GameplayTags",
            "UnrealEd",             // IDetailCustomization, IPropertyTypeCustomization
            "PropertyEditor",       // FPropertyEditorModule, IDetailLayoutBuilder
            "AssetTools",           // FAssetTypeActions_Base — Dialogue asset actions
            "Slate",                // Widget construction
            "SlateCore",            // Slate fundamentals
            "EditorWidgets",        // SObjectPropertyEntryBox
            "AssetRegistry",        // FAssetData — interface-filtered asset picker
            "Foliage",              // AInstancedFoliageActor, FFoliageInfo — HISM Foliage converter
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "InputCore",
        });
    }
}
