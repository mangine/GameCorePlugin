# HISM Proxy Actor System — Editor Deviations

## Files covered
`Source/GameCoreEditor/HISMProxy/`

---

## DEV-E-1: Log category declared as static per-file rather than a shared extern

**Spec says:** `UE_LOG(LogGameCoreEditor, ...)` in `HISMFoliageConversionUtility.cpp`.
**Implementation:** `DEFINE_LOG_CATEGORY_STATIC(LogGameCoreEditor, Log, All)` declared locally in `HISMFoliageConversionUtility.cpp`.
**Reason:** No shared `LogGameCoreEditor` category exists in the project's existing editor module. Static declaration matches the project convention (see `PersistenceRegistrationComponent.cpp` in the runtime module). All log output is functionally identical.

---

## DEV-E-2: `PreviewConversion` implemented in full

**Spec says:** `PreviewConversion()` is declared with `UFUNCTION(CallInEditor, BlueprintCallable)` but no implementation is given.
**Implementation:** A read-only preview pass is implemented: iterates all foliage types, reports which meshes would be converted (with instance count and resolved TypeIndex) and which would be skipped, with a summary total. No mutations occur.
**Reason:** The function is declared in the spec and must have an implementation to compile. The implementation is conservative (read-only) and provides exactly the information a designer would need before running the destructive conversion.

---

## DEV-E-3: `GameCoreEditor.cpp` updated to register `FHISMProxyHostActorDetails`

**Spec says:** `GameCoreEditorModule.cpp` registers `FHISMProxyHostActorDetails::MakeInstance` in `StartupModule` and unregisters in `ShutdownModule`.
**Implementation:** Registration added to the existing `GameCoreEditor.cpp` alongside the pre-existing LootTable customizations.
**Reason:** The project uses a single editor module source file. The spec's registration snippet is integrated there rather than in a hypothetical separate file. Logic and registration/unregistration pattern are identical to the spec.

---

## No other deviations. All Slate widget construction, lambda capture patterns (CreateLambda per AD-12), FScopedTransaction usage, three-pass foliage conversion, descending-index removal (AD-10), PostEditChange refresh (AD-11), and notification patterns match the specification exactly.
