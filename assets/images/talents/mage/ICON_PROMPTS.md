# Mage Tower Talent Icons - Prompt Pack

## Global Style (use for all icons)
- Square fantasy game skill icon, painterly high-contrast, readable at 64x64.
- Single central silhouette, clean edge separation, vignette corners.
- Crystal/rune frame-safe composition, no tiny text, no watermark, no UI frame.
- Lighting from upper-left, strong midtone contrast, subtle rim light.
- Palette by branch:
  - Core: crystal blue-white, silver-violet, soft inner glow.
  - Ember (Fire): molten orange, ember red, charcoal black.
  - Rime (Ice): pale cyan, frost white, deep blue.
  - Arc (Arcane): violet, magenta-purple, electric white.
  - Convergence (dual-school): split/blended palette of the two schools it bridges (see per-icon notes -- Fire+Ice, Fire+Arcane, or Ice+Arcane).
  - Capstone (single-school mastery): a richer, more saturated version of that Well's palette -- these should read as "the deep version" of Ember/Rime/Arc, not a new color.
  - Ultimate: all three school colors braided together, with a gold accent (#e5c66f) marking it as the top-tier node.
- Export target: 512x512 PNG, then downscale to 64x64 with slight sharpen.
  - Note: in-engine node sizes vary a little by tier (convergence/capstone nodes render at 52x52, most others at 64x64, the ultimate node at 60x60) -- a single 512x512 master downscaled per-slot covers all of them; no need for separate master sizes.

## Shared Negative Prompt (append to every prompt)
"no letters, no numbers, no logo, no watermark, no signature, no photorealism, no blurry subject, no cluttered background, no multiple focal objects"

## Per-Icon Prompts

### resonant_attunement
Filename: mage_resonant_attunement.png
Prompt:
"Fantasy skill icon of a floating crystal shard resonating with faint sound-wave rings, soft blue-white glow pulsing outward, silver-violet highlights, calm centered composition, painterly game icon style"

### focused_casting
Filename: mage_focused_casting.png
Prompt:
"Fantasy skill icon of a mage's hand channeling a tight beam of focused light into a small crystal, converging energy lines, bright core with silver-violet rim glow, clean centered silhouette, painterly game icon style"

### fire_well
Filename: mage_fire_well.png
Prompt:
"Fantasy skill icon of a restrained ember core suspended inside a cracked crystal shell, molten orange glow leaking through fissures, charcoal-black crystal facets, warm inner light, painterly game icon style"

### ice_well
Filename: mage_ice_well.png
Prompt:
"Fantasy skill icon of a frost-veined crystal well with cold current spiraling upward, pale cyan and frost-white tones, faint blue mist, crisp faceted silhouette, painterly game icon style"

### arcane_well
Filename: mage_arcane_well.png
Prompt:
"Fantasy skill icon of an open arcane rift within a crystal, violet lightning threads coiling inside, electric-white sparks at the edges, dark violet backdrop, painterly game icon style"

### ember_cascade
Filename: mage_ember_cascade.png
Prompt:
"Fantasy skill icon of flame licking outward from one ember to several smaller embers in a spreading chain, radiating orange-red heat trails, charcoal dust flecks, dynamic diagonal composition, painterly game icon style"

### cinder_wick
Filename: mage_cinder_wick.png
Prompt:
"Fantasy skill icon of a single glowing ember with a slow-burning wick trailing thin smoke, sustained low flame, deep red-orange core fading to ash gray, quiet smoldering mood, painterly game icon style"

### frostbite
Filename: mage_frostbite.png
Prompt:
"Fantasy skill icon of a frostbite mark spreading across a surface, crystalline ice crystals branching like frozen veins, pale cyan with deep blue shadow, sharp jagged silhouette, painterly game icon style"

### glacial_lance
Filename: mage_glacial_lance.png
Prompt:
"Fantasy skill icon of a long narrow icicle lance punching forward in a straight thrust, motion-streak frost trail behind it, ice-white core with cyan edge glow, precise piercing composition, painterly game icon style"

### volatile_matrix
Filename: mage_volatile_matrix.png
Prompt:
"Fantasy skill icon of an unstable geometric energy lattice flickering between violet and white, a sudden surge spark breaking its symmetry, chaotic-but-contained composition, painterly game icon style"

### overweave
Filename: mage_overweave.png
Prompt:
"Fantasy skill icon of arcane threads weaving outward and looping between distant points, violet current extending past the frame's center, electric-white connecting nodes, painterly game icon style"

### thermal_shock
Filename: mage_thermal_shock.png
Prompt:
"Fantasy skill icon split diagonally between molten ember orange and frost cyan meeting at a jagged fracture line, a cracking shatter effect where heat and cold collide, high-contrast dual-tone composition, painterly game icon style"
(Convergence: Fire + Ice)

### wildfire_surge
Filename: mage_wildfire_surge.png
Prompt:
"Fantasy skill icon of a sudden violet spark igniting into an orange flame burst, a crit-like flash of white light at the ignition point, fire and arcane energy merging, dynamic radial composition, painterly game icon style"
(Convergence: Fire + Arcane)

### frozen_precision
Filename: mage_frozen_precision.png
Prompt:
"Fantasy skill icon of a violet arcane targeting reticle locking onto a frost-covered point, thin ice crystals forming around the focal mark, cyan and violet dual glow, precise centered composition, painterly game icon style"
(Convergence: Ice + Arcane)

### inferno
Filename: mage_inferno.png
Prompt:
"Fantasy skill icon of a towering pillar of raging fire consuming the frame, a deep ember core with a bright white-hot center, heavy smoke and embers rising, dominant orange-red palette, dramatic painterly game icon style"
(Capstone: Ember mastery)

### absolute_zero
Filename: mage_absolute_zero.png
Prompt:
"Fantasy skill icon of an object encased in thick crystalline ice, a cracking frost aura radiating outward, deep blue-white palette with sharp cold highlights, imposing centered composition, painterly game icon style"
(Capstone: Rime mastery)

### archons_focus
Filename: mage_archons_focus.png
Prompt:
"Fantasy skill icon of a perfectly symmetric violet arcane eye or sigil radiating focused clarity beams, crisp electric-white linework, dark violet backdrop, serene high-precision composition, painterly game icon style"
(Capstone: Arc mastery)

### primordial_convergence
Filename: mage_primordial_convergence.png
Prompt:
"Fantasy skill icon of three energy currents -- ember orange, frost cyan, and arcane violet -- spiraling together into one blinding golden-white core, braided elemental streams, ultimate-tier radiant composition, painterly game icon style"
(Ultimate: requires all three Convergences)

## Recommended Batch Settings
- Model style strength: medium-high.
- CFG or prompt adherence: high.
- Steps: medium-high.
- Generate 4 variants per icon, pick strongest 64x64 readability.
- After downscale, test against the dark UI background (#070d10 field, #050808 node background).

## Optional Path Mapping
If you want to wire them immediately, place icons in this folder and set each node icon path in the tower file:
assets/images/talents/mage/
