# Enemy Authoring Guide

How to build a new tower defense enemy so it matches the existing pipeline. This is reverse-engineered from `assets/blender/goblin_scout.blend` (live-inspected in Blender) and the two shipped enemies, `goblin_scout.enemy.lua` and `goblin1.enemy.lua`. Treat it as a checklist, not a hard rulebook — deviate when a new creature genuinely needs it, but note why in the .blend file or this doc if the convention changes.

## Files that make up one enemy

| File | Location | Notes |
|---|---|---|
| `<name>.blend` | `assets/blender/` | Source Blender file. Not shipped in the game. |
| texture `.png`s | `assets/blender/textures/` | Shared across a creature family where possible (see Materials below). |
| `<name>.glb` | `assets/models/enemy/` | Exported model. **You export this by hand once the design is approved — not part of this template's automation.** |
| `<name>.enemy.lua` | `assets/models/enemy/` | Data definition read by the game. Copy `_TEMPLATE.enemy.lua`. |

## Blender conventions (from `goblin_scout.blend`)

### Naming

Everything is keyed off the enemy's PascalCase name (`GoblinScout`):

- Collection: `GoblinScout` — contains exactly the armature and the body mesh (add more objects here if the creature needs extra parts, e.g. `GoblinScout_Weapon`).
- Armature object: `GoblinScout_Armature`, armature data-block: `GoblinScout_Skeleton`.
- Mesh object: `GoblinScout_Body`, mesh data-block: `GoblinScout_BodyMesh`.
- Materials: `GoblinScout<Part>_Mat`, one per texture group (see Materials).

### Rig

`GoblinScout_Armature` uses a humanoid skeleton:

```
Root
└─ Hips
   ├─ Spine
   │  ├─ Head
   │  ├─ Shoulder_L → Elbow_L → Hand_L → Fingers_L (5x)
   │  └─ Shoulder_R → Elbow_R → Hand_R → Fingers_R (5x)
   ├─ Hip_L → Knee_L → Foot_L
   └─ Hip_R → Knee_R → Foot_R
```

Reuse this bone naming/hierarchy for any new biped enemy when appropriate — the mesh's vertex groups match the bone names 1:1, and reusing the rig means any animation-driving code on the engine side keeps working without changes. Only design a different skeleton if the creature isn't a biped (e.g. a quadruped or flyer).  The bone structure isn't required for the engine as it will generically display any model with it's animation, this is more of a guide for what a biped creature should contain.

### Required animations (Actions)

Every enemy needs exactly these four actions, named precisely (case-sensitive — game code looks them up by name):

- `Idle`
- `Walking`
- `Death`
- `Cheer`

Frame ranges are per-enemy; on `goblin_scout` they're `Idle` 0–48, `Walking` 0–24, `Death` 0–30. Use those as a rough pacing reference, not a hard requirement.

### Materials & textures

- One material per texture/body-part group, using Blender's node system with a single Image Texture node feeding it.
- `goblin_scout` has four: Skin, Eye, Bone, Cloth (`GoblinScoutSkin_Mat`, `GoblinScoutEye_Mat`, `GoblinScoutBone_Mat`, `GoblinScoutCloth_Mat`).
- Textures live in `assets/blender/textures/`, named `<Part>Tex.png` (e.g. `GoblinSkin2.png`, `EyeTex2.png`, `BoneTex2.png`, `ClothTex2.png`).
- If the new enemy is a variant of an existing creature family (another goblin, say), reuse the same texture files instead of exporting new ones — only make new textures when the creature actually looks different.

### Mesh & modifiers

- `Armature` modifier is required for skinning to the rig.
- `Subsurf` is optional — `goblin_scout` uses one for smoothing; add it if the base mesh needs it.
- Set the object's origin at the character's feet / ground-contact point (world Z ≈ 0), not the center of mass — this is what lets the game place the enemy on the path correctly.
- Poly budget: `goblin_scout`'s body mesh is ~3,350 triangles / 3,520 vertices. Treat that as a reasonable ceiling for a small enemy rather than a strict cap.
- Model at the rig's natural scale (roughly 1–1.1 m tall, matching the existing skeleton) rather than sizing the mesh itself up or down — use `render.renderScale` in the `.lua` file for that instead (see below).

### Export

Once you've approved the design in Blender, export it yourself to `assets/models/enemy/<name>.glb`. This template's workflow stops at the approved `.blend` — no automated export step.

## The `.enemy.lua` data file

Copy `_TEMPLATE.enemy.lua` (in `assets/models/enemy/`) and fill in every field. Field-by-field, using `goblin_scout` (fast/fragile "Scout") and `goblin1` (slow/tanky "Grunt") as calibration anchors:

- `id` — snake_case, must match the `.glb` filename with no extension.
- `displayName` / `description` — player-facing text.
- `model` — path to the exported `.glb`.
- `stats.health` — 22 (Scout) to 35 (Grunt) is the current range.
- `stats.shield` — flat damage absorbed before health; both existing goblins use 0.
- `stats.armor` — flat damage reduction per hit; 1 (Scout) to 2 (Grunt).
- `stats.resistances.{poison,fire,arcane}` — percent, 0–100; both goblins share 25/10/0, suggesting this is closer to a per-creature-family trait than a per-enemy dial.
- `stats.moveSpeed` — units/second; 3.6 (Scout, fast) down to 2.8 (Grunt, slow) — speed and health trade off against each other across the two existing enemies.
- `stats.rewardMoney` — gold on kill; 14 (Scout) to 20 (Grunt), roughly tracking toughness.
- `stats.baseDamage` — damage dealt to the player's base; 3 (Scout) to 5 (Grunt).
- `render.renderScale` — uniform in-engine scale; 0.85 (Scout) vs 1.0 (Grunt) even though both are modeled on the same rig height — this is the size-tuning knob, not the mesh.
- `render.facingYawOffsetDegrees` — corrects the model's forward axis to match its travel direction; 0 for Scout, 180 for Grunt (their source meshes were modeled facing opposite directions).

## Checklist for a new enemy

1. Duplicate an existing `.blend` (or build fresh) following the naming, rig, material, and origin conventions above.
2. Skin the mesh to the shared 12-bone rig (or reuse the rig data directly if the body plan is compatible).
3. Add `Idle`, `Walking`, and `Death` actions.
4. Get the design approved.
5. Export to `assets/models/enemy/<name>.glb`.
6. Copy `_TEMPLATE.enemy.lua` to `assets/models/enemy/<name>.enemy.lua` and fill it in, using the field guide above to calibrate stats against the existing enemies.
