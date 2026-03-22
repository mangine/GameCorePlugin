// Copyright GameCore Plugin. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class AHISMProxyHostActor;
class IDetailLayoutBuilder;

/**
 * FHISMProxyHostActorDetails
 *
 * Details panel customization for AHISMProxyHostActor. Adds:
 *   - Validate Setup button (runs AHISMProxyHostActor::ValidateSetup())
 *   - Per-entry rows showing instance count + Add Instance at Pivot button
 */
class GAMECOREEDITOR_API FHISMProxyHostActorDetails : public IDetailCustomization
{
public:
    static TSharedRef<IDetailCustomization> MakeInstance();
    virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
    void AddInstanceTypeRows(IDetailLayoutBuilder& DetailBuilder,
                              AHISMProxyHostActor* HostActor);

    TWeakObjectPtr<AHISMProxyHostActor> CachedHostActor;
};
