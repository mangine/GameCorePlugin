# Zone Actor & Shape Components

---

## `UZoneShapeComponent` (abstract base)

Abstract component that encapsulates shape-specific containment logic. Both concrete variants extend this.

```cpp
UCLASS(Abstract)
class GAMECORE_API UZoneShapeComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    // Returns true if WorldPoint is inside this shape.
    UFUNCTION(BlueprintCallable)
    virtual bool ContainsPoint(const FVector& WorldPoint) const PURE_VIRTUAL(ContainsPoint, return false;);

    // Returns a world-space AABB used for grid registration and broad-phase pre-filter.
    virtual FBox GetWorldBounds() const PURE_VIRTUAL(GetWorldBounds, return FBox(EForceInit::ForceInit););

#if WITH_EDITOR
    virtual void DrawDebugShape(float Duration = 0.f) const {}
#endif
};
```

---

## `UZoneBoxShapeComponent`

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneBoxShapeComponent : public UZoneShapeComponent
{
    GENERATED_BODY()

public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    FVector HalfExtent = FVector(500.f);

    virtual bool ContainsPoint(const FVector& WorldPoint) const override;
    virtual FBox GetWorldBounds() const override;

private:
    // Cached inverse transform, updated on component transform change.
    FTransform CachedInverseTransform;

public:
    virtual void OnRegister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags Flags, ETeleportType Teleport) override;
};
```

### `ContainsPoint` implementation

```cpp
bool UZoneBoxShapeComponent::ContainsPoint(const FVector& WorldPoint) const
{
    const FVector Local = CachedInverseTransform.TransformPosition(WorldPoint);
    return FMath::Abs(Local.X) <= HalfExtent.X
        && FMath::Abs(Local.Y) <= HalfExtent.Y
        && FMath::Abs(Local.Z) <= HalfExtent.Z;
}
```

---

## `UZoneConvexPolygonShapeComponent`

```cpp
UCLASS(BlueprintType, meta=(BlueprintSpawnableComponent))
class GAMECORE_API UZoneConvexPolygonShapeComponent : public UZoneShapeComponent
{
    GENERATED_BODY()

public:
    // 2D footprint in local XY space. Min 3 points, wound CCW from above.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    TArray<FVector2D> LocalPolygonPoints;

    // Z range in world space.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MinZ = -500.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Zone|Shape")
    float MaxZ = 500.f;

    virtual bool ContainsPoint(const FVector& WorldPoint) const override;
    virtual FBox GetWorldBounds() const override;

    virtual void OnRegister() override;

private:
    // Precomputed world-space 2D polygon (updated on register).
    TArray<FVector2D> WorldPolygon;
    FBox CachedBounds;

    void RebuildWorldPolygon();
    static bool PointInConvexPolygon2D(const FVector2D& P, const TArray<FVector2D>& Poly);
};
```

### `ContainsPoint` implementation

```cpp
bool UZoneConvexPolygonShapeComponent::ContainsPoint(const FVector& WorldPoint) const
{
    // Height band check first (cheap)
    if (WorldPoint.Z < MinZ || WorldPoint.Z > MaxZ) return false;

    return PointInConvexPolygon2D(FVector2D(WorldPoint.X, WorldPoint.Y), WorldPolygon);
}
```

### `PointInConvexPolygon2D` — winding number test

```cpp
bool UZoneConvexPolygonShapeComponent::PointInConvexPolygon2D(
    const FVector2D& P, const TArray<FVector2D>& Poly)
{
    // Cross-product sign consistency check for convex polygon.
    // All edges must have the same sign for an interior point.
    const int32 N = Poly.Num();
    if (N < 3) return false;

    float LastCross = 0.f;
    for (int32 i = 0; i < N; ++i)
    {
        const FVector2D& A = Poly[i];
        const FVector2D& B = Poly[(i + 1) % N];
        const FVector2D Edge = B - A;
        const FVector2D ToP  = P - A;
        const float Cross = Edge.X * ToP.Y - Edge.Y * ToP.X;
        if (LastCross != 0.f && FMath::Sign(Cross) != FMath::Sign(LastCross))
            return false;
        if (Cross != 0.f) LastCross = Cross;
    }
    return true;
}
```

> **Note:** `RebuildWorldPolygon()` transforms `LocalPolygonPoints` using the component's world transform XY and caches the result. Call it in `OnRegister`. Since zones are static after placement, the cache is never stale during play.

---

## `AZoneActor`

The world actor that owns one shape component and one `FZoneDynamicState`. This is the replicated entity.

```cpp
UCLASS(BlueprintType)
class GAMECORE_API AZoneActor : public AActor
{
    GENERATED_BODY()

public:
    AZoneActor();

    // ---- Static Data ----
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneDataAsset> DataAsset;

    // ---- Shape ----
    // Set ShapeType to drive which component is active. Both components exist
    // on the actor; only the active one is used. Simpler than dynamic component spawning.
    UPROPERTY(EditAnywhere, Category="Zone")
    EZoneShapeType ShapeType = EZoneShapeType::Box;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneBoxShapeComponent> BoxShape;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Zone")
    TObjectPtr<UZoneConvexPolygonShapeComponent> ConvexShape;

    // ---- Mutable State (replicated) ----
    UPROPERTY(ReplicatedUsing=OnRep_DynamicState, BlueprintReadOnly, Category="Zone")
    FZoneDynamicState DynamicState;

    // ---- API ----

    // Returns true if WorldPoint is inside this zone.
    UFUNCTION(BlueprintCallable, Category="Zone")
    bool ContainsPoint(const FVector& WorldPoint) const;

    // World-space AABB for spatial index registration.
    FBox GetWorldBounds() const;

    // Server-only: update mutable state and replicate.
    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void SetOwnerTag(FGameplayTag NewOwner);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void AddDynamicTag(FGameplayTag Tag);

    UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Zone")
    void RemoveDynamicTag(FGameplayTag Tag);

    // Called after runtime spawn to configure shape from data.
    void InitializeZone(UZoneDataAsset* InDataAsset, const FZoneShapeData& InShapeData);

    // ---- UE overrides ----
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const override;

private:
    UFUNCTION()
    void OnRep_DynamicState();

    UZoneShapeComponent* GetActiveShape() const;
};
```

### `BeginPlay` / `EndPlay`

```cpp
void AZoneActor::BeginPlay()
{
    Super::BeginPlay();
    if (UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>())
        Sub->RegisterZone(this);
}

void AZoneActor::EndPlay(EEndPlayReason::Type Reason)
{
    if (UZoneSubsystem* Sub = GetWorld()->GetSubsystem<UZoneSubsystem>())
        Sub->UnregisterZone(this);
    Super::EndPlay(Reason);
}
```

### `OnRep_DynamicState`

Broadcasts a GMS message so client-side listeners react to ownership/tag changes on already-occupied zones.

```cpp
void AZoneActor::OnRep_DynamicState()
{
    UGameplayMessageSubsystem& GMS = UGameplayMessageSubsystem::Get(this);
    FZoneStateChangedMessage Msg;
    Msg.ZoneActor    = this;
    Msg.StaticData   = DataAsset;
    Msg.DynamicState = DynamicState;
    GMS.BroadcastMessage(GameCore::Zone::Tags::Channel_StateChanged, Msg);
}
```

### Replication

```cpp
void AZoneActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutProps) const
{
    Super::GetLifetimeReplicatedProps(OutProps);
    DOREPLIFETIME(AZoneActor, DynamicState);
}
```

> **Note:** `bReplicates = true` is set in the constructor. `bAlwaysRelevant = true` is intentional — zones must be known to all clients regardless of their position so that a freshly connected player has correct state. For very large worlds this can be revisited with interest management, but is correct for the target scale of 50–500 zones.
