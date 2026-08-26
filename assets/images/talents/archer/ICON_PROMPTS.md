# Archer Tower Talent Icons - Prompt Pack

## Global Style (use for all icons)
- Square fantasy game skill icon, painterly high-contrast, readable at 32x32.
- Single central silhouette, clean edge separation, dark vignette corners.
- Metallic frame-safe composition, no tiny text, no watermark, no UI frame.
- Lighting from upper-left, strong midtone contrast, subtle rim light.
- Palette by branch:
  - Core: amber, leather brown, steel gray.
  - Stone: sandstone, slate gray, dust amber.
  - Metal: iron gray, silver highlights, deep charcoal.
  - Electric: cyan, blue-white arcs, dark navy shadows.
- Export target: 512x512 PNG, then downscale to 32x32 with slight sharpen.

## Shared Negative Prompt (append to every prompt)
"no letters, no numbers, no logo, no watermark, no signature, no photorealism, no blurry subject, no cluttered background, no multiple focal objects"

## Per-Icon Prompts

### quickdraw_rig
Filename: archer_quickdraw_rig.png
Prompt:
"Fantasy skill icon of a taut recurve bow with motion streaks around the string hand, quick-release mechanism emphasized, amber and steel tones, dynamic diagonal composition, clean central silhouette, painterly game icon style"

### hardened_draw
Filename: archer_hardened_draw.png
Prompt:
"Fantasy skill icon of reinforced bow limbs wrapped with iron braces and rivets, bow being fully drawn to maximum tension, warm amber highlights on metal stress points, strong central framing, painterly game icon"

### stone_specialization
Filename: archer_stone_specialization.png
Prompt:
"Fantasy skill icon of a heavy stone arrowhead mounted on a shaft, chipped rock texture and dust motes, impact-focused shape language, sandstone and slate palette, clean silhouette, painterly game icon"

### metal_specialization
Filename: archer_metal_specialization.png
Prompt:
"Fantasy skill icon of forged steel arrowheads in a tight fan, polished edges and cold metallic sheen, precision and speed implied, iron-gray palette with silver highlights, painterly game icon"

### electric_specialization
Filename: archer_electric_specialization.png
Prompt:
"Fantasy skill icon of an arrowhead charged with crackling lightning arcs, cyan electrical corona around the tip, dark navy background for contrast, central high-energy silhouette, painterly game icon"

### stone_shatter
Filename: archer_stone_shatter.png
Prompt:
"Fantasy skill icon of a stone-tipped arrow striking armor and exploding into shards, radial debris burst, dust cloud and fracture lines, warm stone palette, dramatic impact lighting, painterly game icon"

### stone_penetrator
Filename: archer_stone_penetrator.png
Prompt:
"Fantasy skill icon of a narrow dense stone spike arrow piercing through layered plates, straight thrust composition, compressed force lines, slate and amber tones, clean high-read silhouette, painterly game icon"

### metal_overdraw
Filename: archer_metal_overdraw.png
Prompt:
"Fantasy skill icon of a bow overdraw with reinforced cams and steel cable tension, string pulled beyond normal anchor point, speed-focused motion arcs, cool metallic palette, painterly game icon"

### metal_serrated
Filename: archer_metal_serrated.png
Prompt:
"Fantasy skill icon of serrated arrow vanes and jagged broadhead edges, slicing trails and flecks of sparks, aggressive angular silhouette, iron and charcoal palette, painterly game icon"

### electric_chain
Filename: archer_electric_chain.png
Prompt:
"Fantasy skill icon of one charged arrow discharging chain lightning to multiple targets, three branching electric forks, cyan-white arcs over dark background, clear branching silhouette, painterly game icon"

### electric_focus
Filename: archer_electric_focus.png
Prompt:
"Fantasy skill icon of a concentrated electric arrow beam with narrowed arc cone, runic focusing ring around the shaft, cyan core glow with crisp edges, precise centered composition, painterly game icon"

### metal_triple_shot
Filename: archer_metal_triple_shot.png
Prompt:
"Fantasy skill icon of three steel arrows released in a spread volley, synchronized motion lines, front arrow prominent with two flanking trails, cold steel highlights, painterly game icon"

### metal_precision
Filename: archer_metal_precision.png
Prompt:
"Fantasy skill icon of a steel arrow passing through a circular target ring dead-center, minimal spread, calm controlled composition, silver edge highlights and dark backing, painterly game icon"

### electric_ricochet
Filename: archer_electric_ricochet.png
Prompt:
"Fantasy skill icon of a charged arrow bouncing between angled surfaces with electric afterimages, zig-zag trajectory and spark impacts, cyan arc trails, dynamic painterly game icon"

### electric_static_field
Filename: archer_electric_static_field.png
Prompt:
"Fantasy skill icon of an arrow embedded in ground creating a circular static field, crackling perimeter arcs and ionized haze, cyan ring over dark terrain, painterly game icon"

### stone_metal_hybrid
Filename: archer_stone_metal_hybrid.png
Prompt:
"Fantasy skill icon of a composite arrowhead fused from rough stone core and forged metal shell, dual-material split design, balanced amber and steel palette, premium hybrid silhouette, painterly game icon"

### stone_aftershock
Filename: archer_stone_aftershock.png
Prompt:
"Fantasy skill icon of ground shockwave rings from a heavy stone arrow impact, fractured earth and secondary debris pulses, warm dust tones with dark contrast, painterly game icon"

## Recommended Batch Settings
- Model style strength: medium-high.
- CFG or prompt adherence: high.
- Steps: medium-high.
- Generate 4 variants per icon, pick strongest 32x32 readability.
- After downscale to 32x32, test against dark UI background.

## Optional Path Mapping
If you want to wire them immediately, place icons in this folder and set each node icon path in the tower file:
assets/images/talents/archer/
