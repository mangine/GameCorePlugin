#include "GameCore.h"
#include "Modules/ModuleManager.h"
#include "GameplayTagsManager.h"

// Native gameplay tag registration is handled per-system in their respective .cpp files.
// Systems add their tags via UGameplayTagsManager::AddNativeGameplayTag in their own StartupModule
// or via static initializers.

void FGameCoreModule::StartupModule()
{
    // GameCore module startup.
    // Individual systems register their native gameplay tags from their own .cpp files.
}

void FGameCoreModule::ShutdownModule()
{
    // GameCore module shutdown.
}

IMPLEMENT_MODULE(FGameCoreModule, GameCore)
