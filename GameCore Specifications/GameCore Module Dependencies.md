# GameCore Module Dependencies

Static dependency map for all systems in the GameCore plugin. Arrows indicate a dependency direction — the system at the arrow tail depends on the system at the arrowhead. Solid arrows are hard dependencies; dashed arrows are optional/soft.

---

<svg width="100%" viewBox="0 0 680 720" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="arr" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="10" markerHeight="10" orient="auto-start-reverse">
      <path d="M1 1L9 5L1 9" fill="none" stroke="#5F5E5A" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
    </marker>
    <marker id="arr-opt" viewBox="0 0 10 10" refX="7" refY="5" markerWidth="10" markerHeight="10" orient="auto-start-reverse">
      <path d="M1 1L9 5L1 9" fill="none" stroke="#5F5E5A" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"/>
    </marker>
  </defs>

  <!-- ── LAYER 0: UE built-ins ── -->
  <rect x="40" y="40" width="140" height="44" rx="8" fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#2C2C2A" x="110" y="67" text-anchor="middle">GameplayTags</text>

  <rect x="220" y="40" width="180" height="44" rx="8" fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="12" font-weight="500" fill="#2C2C2A" x="310" y="59" text-anchor="middle">GameplayMessageSubsystem</text>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="310" y="76" text-anchor="middle">UE built-in</text>

  <rect x="440" y="40" width="200" height="44" rx="8" fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="12" font-weight="500" fill="#2C2C2A" x="540" y="59" text-anchor="middle">AbilitySystemComponent</text>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="540" y="76" text-anchor="middle">optional / game-side</text>

  <!-- ── LAYER 1: infrastructure ── -->
  <rect x="40" y="140" width="180" height="56" rx="8" fill="#EEEDFE" stroke="#534AB7" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#3C3489" x="130" y="162" text-anchor="middle">GameCore Backend</text>
  <text font-family="sans-serif" font-size="11" fill="#534AB7" x="130" y="180" text-anchor="middle">FGameCoreBackend + subsystem</text>

  <rect x="260" y="140" width="180" height="56" rx="8" fill="#E1F5EE" stroke="#0F6E56" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#085041" x="350" y="162" text-anchor="middle">Event Bus</text>
  <text font-family="sans-serif" font-size="11" fill="#0F6E56" x="350" y="180" text-anchor="middle">UGameCoreEventSubsystem</text>

  <rect x="480" y="140" width="160" height="56" rx="8" fill="#EEEDFE" stroke="#534AB7" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#3C3489" x="560" y="162" text-anchor="middle">Tags System</text>
  <text font-family="sans-serif" font-size="11" fill="#534AB7" x="560" y="180" text-anchor="middle">ITaggedInterface + component</text>

  <!-- ── LAYER 2: persistence ── -->
  <rect x="40" y="260" width="200" height="56" rx="8" fill="#FAECE7" stroke="#993C1D" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#712B13" x="140" y="282" text-anchor="middle">Serialization System</text>
  <text font-family="sans-serif" font-size="11" fill="#993C1D" x="140" y="300" text-anchor="middle">IPersistableComponent + subsystem</text>

  <!-- ── LAYER 3: logic ── -->
  <rect x="280" y="260" width="360" height="56" rx="8" fill="#FAEEDA" stroke="#854F0B" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#633806" x="460" y="282" text-anchor="middle">Requirement System</text>
  <text font-family="sans-serif" font-size="11" fill="#854F0B" x="460" y="300" text-anchor="middle">URequirementList + watcher</text>

  <!-- ── LAYER 4: feature systems ── -->
  <rect x="40" y="380" width="200" height="56" rx="8" fill="#E6F1FB" stroke="#185FA5" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#0C447C" x="140" y="402" text-anchor="middle">Interaction System</text>
  <text font-family="sans-serif" font-size="11" fill="#185FA5" x="140" y="420" text-anchor="middle">UInteractionComponent</text>

  <rect x="270" y="380" width="160" height="56" rx="8" fill="#E6F1FB" stroke="#185FA5" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#0C447C" x="350" y="402" text-anchor="middle">State Machine</text>
  <text font-family="sans-serif" font-size="11" fill="#185FA5" x="350" y="420" text-anchor="middle">UStateMachineComponent</text>

  <rect x="460" y="380" width="180" height="56" rx="8" fill="#E6F1FB" stroke="#185FA5" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#0C447C" x="550" y="402" text-anchor="middle">Progression System</text>
  <text font-family="sans-serif" font-size="11" fill="#185FA5" x="550" y="420" text-anchor="middle">Leveling + PointPool</text>

  <!-- ── LAYER 5: game code ── -->
  <rect x="40" y="510" width="600" height="44" rx="8" fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="13" font-weight="500" fill="#2C2C2A" x="340" y="528" text-anchor="middle">Game Code / Game Plugins</text>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="340" y="546" text-anchor="middle">quest system, ship system, reward system, watcher adapter…</text>

  <!-- ═══ ARROWS ═══ -->

  <!-- GameplayTags → Backend -->
  <line x1="110" y1="84" x2="110" y2="136" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- GameplayTags → Event Bus -->
  <path d="M180 62 L310 62 L310 136" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- GameplayTags → Tags System -->
  <path d="M180 70 L480 155" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- GMS → Event Bus -->
  <line x1="350" y1="84" x2="350" y2="136" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- GAS → Tags System (optional) -->
  <line x1="540" y1="84" x2="560" y2="136" stroke="#888780" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#arr-opt)"/>

  <!-- Backend → Serialization -->
  <line x1="130" y1="196" x2="130" y2="256" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- Event Bus → Requirement -->
  <path d="M390 196 L430 256" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- Event Bus → State Machine -->
  <path d="M350 196 L350 376" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- Event Bus → Progression -->
  <path d="M440 168 L550 376" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- Tags System → Requirement -->
  <line x1="560" y1="196" x2="560" y2="256" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <!-- Tags System → Interaction -->
  <path d="M480 170 L240 376" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>

  <!-- Serialization → Progression -->
  <path d="M240 288 L456 392" fill="none" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>

  <!-- Requirement → Interaction (optional) -->
  <path d="M350 316 L240 376" fill="none" stroke="#888780" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#arr-opt)"/>
  <!-- Requirement → Progression (optional) -->
  <path d="M550 316 L550 376" fill="none" stroke="#888780" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#arr-opt)"/>
  <!-- Backend → Progression audit (optional) -->
  <path d="M220 162 Q440 162 456 376" fill="none" stroke="#888780" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#arr-opt)"/>

  <!-- Feature systems → Game Code -->
  <line x1="140" y1="436" x2="200" y2="506" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <line x1="350" y1="436" x2="330" y2="506" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <line x1="550" y1="436" x2="470" y2="506" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>

  <!-- ── LEGEND ── -->
  <line x1="40" y1="625" x2="90" y2="625" stroke="#888780" stroke-width="1.5" marker-end="url(#arr)"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="98" y="629" dominant-baseline="central">hard dependency</text>
  <line x1="240" y1="625" x2="290" y2="625" stroke="#888780" stroke-width="1.5" stroke-dasharray="5 3" marker-end="url(#arr-opt)"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="298" y="629" dominant-baseline="central">optional / soft dependency</text>

  <!-- Color key -->
  <rect x="40" y="648" width="12" height="12" rx="2" fill="#F1EFE8" stroke="#888780" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="58" y="657" dominant-baseline="central">UE built-in / external</text>

  <rect x="190" y="648" width="12" height="12" rx="2" fill="#EEEDFE" stroke="#534AB7" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="208" y="657" dominant-baseline="central">infrastructure</text>

  <rect x="300" y="648" width="12" height="12" rx="2" fill="#E1F5EE" stroke="#0F6E56" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="318" y="657" dominant-baseline="central">messaging</text>

  <rect x="400" y="648" width="12" height="12" rx="2" fill="#FAECE7" stroke="#993C1D" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="418" y="657" dominant-baseline="central">persistence</text>

  <rect x="498" y="648" width="12" height="12" rx="2" fill="#FAEEDA" stroke="#854F0B" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="516" y="657" dominant-baseline="central">logic</text>

  <rect x="560" y="648" width="12" height="12" rx="2" fill="#E6F1FB" stroke="#185FA5" stroke-width="0.5"/>
  <text font-family="sans-serif" font-size="11" fill="#5F5E5A" x="578" y="657" dominant-baseline="central">feature systems</text>
</svg>

---

## Minimum Requirements Per System

| System | Hard dependencies | Optional |
| --- | --- | --- |
| Tags System | `GameplayTags` | GAS (forwarding actor only) |
| Backend | `GameplayTags` | — |
| Event Bus | `GameplayTags`, `GameplayMessageSubsystem` | — |
| Serialization | `GameplayTags`, Backend (logging) | — |
| Requirement System | `GameplayTags`, Event Bus, Tags System | — |
| State Machine | `GameplayTags`, Event Bus | Tags System (`GrantedTags` on nodes) |
| Progression | `GameplayTags`, Event Bus, Serialization | Requirement System (prerequisites), Backend (audit), GAS (XP multiplier) |
| Interaction | `GameplayTags`, Tags System | Requirement System (entry requirements) |

---

## Adoption Order

The cleanest adoption order when integrating GameCore systems incrementally:

```
Tags System → Backend → Event Bus → Serialization → Requirement System
  → then any feature system (Interaction, State Machine, Progression) in any order
```

No feature system depends on another feature system. They are all peers that communicate exclusively through GMS event channels.
