# UNotificationChannelConfig

`UDataAsset`. Stores the list of `UNotificationChannelBinding` instances, each describing one Event Bus channel. Assigned in Project Settings. Loaded synchronously at subsystem `Initialize`.

**File:** `Notification/UNotificationChannelConfig.h`

---

## Declaration

```cpp
UCLASS(BlueprintType)
class GAMECORE_API UNotificationChannelConfig : public UDataAsset
{
    GENERATED_BODY()
public:
    // Inline-instanced bindings. Each binding owns its Channel tag and BuildEntry logic.
    // EditInlineNew + DefaultToInstanced on UNotificationChannelBinding allows
    // authoring binding instances directly in this asset without separate asset files.
    UPROPERTY(EditDefaultsOnly, Instanced, Category="Channels")
    TArray<TObjectPtr<UNotificationChannelBinding>> Bindings;
};
```

---

## Notes

- `Instanced` property flag + `EditInlineNew` + `DefaultToInstanced` on `UNotificationChannelBinding` are what enable inline editing in the Details panel.
- Two bindings with the same `Channel` tag will register two listeners for the same channel, producing duplicate notifications. `IsDataValid` should validate for duplicate channel tags — see Code Review.
- Null entries in `Bindings` are silently skipped by `RegisterChannelListeners`.
