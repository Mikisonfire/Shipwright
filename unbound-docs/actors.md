# Unbound: custom actors (proposal)

**Status: proposal. Nothing here is implemented.** This document fixes the design and the scope of
a first version before any code is written. When it lands, the "Proposed SPEC text" section moves
into [`SPEC.md`](./SPEC.md) and this file becomes the how-doc, like the others in this directory.

## Why

Modders want actors the game does not have. Today the only way is C code in a fork of SoH:

- Every fork re-syncs with Unbound after each release before it can use the scene features Prelude
  exports, and until it does, Prelude output and the fork drift apart.
- Prelude lets a user type a raw actor id to place a fork's actor. That number is only meaningful
  in one build: ActorDB hands out custom ids in registration order, so adding a built-in actor to
  Unbound shifts every fork's ids and silently changes what existing scenes spawn.
- Most of what people want first is small: an NPC that stands in a pose and says something, a
  prop with a model the game does not use. That should not need a C compiler.

The long-term goal is **scriptable custom actors**. Scripting needs a lot of design and is
deliberately not part of this proposal. This proposal is the first step toward it: custom actor
*types*, declared in data, with a few built-in behaviors, addressed by name. The later steps are
listed at the end so the format leaves room for them now.

## Decisions

1. **New actor types, not per-instance properties.** A type is declared once in a registry and
   placed any number of times. The alternative, extra properties on individual placements of
   existing actors, was rejected: it duplicates data at every placement, adds a third channel
   beside the actor id and `params`, and gives nothing a one-off type cannot. Types are also where
   scripts will attach, as in every engine modders know and as OoT itself works (C code per actor
   id, `params` per placement).
2. **Types are referenced by name.** A scene's actor entry may name its actor
   (`"id": "mymod/old_man"`). The numeric id is assigned at load and never written to a file, the
   same rule scenes and entrances already follow (SPEC §7). This also fixes the fork problem: a
   fork's C actor registered in ActorDB under a name is placed by that name.
3. **Per-placement setup stays in `params`.** As in vanilla, the type says what the actor is and
   `params` says how this placement is set up. For the first version `params` has one meaning (the
   message id, below).
4. **First version: declared types only.** One C driver runs every declared type, reading the
   type's settings from the registry: a model (animated or static), a collision cylinder,
   talking, and head tracking. Types that
   extend a vanilla actor (`base`) are a later phase; nothing in this version depends on them.

## What a modder writes

A type's model is one of two kinds:

- **Animated:** a skeleton, posed by an animation (looping, or held on one frame) or left in its
  bind pose. NPCs, animals, anything with limbs.
- **Static:** a single display list. Trees, rocks, lanterns, fences, signs, statues: most of the
  scenery a modder wants to add.

Every behavior works with either kind, so a static signpost can talk, and a static statue can
block the player. Only head tracking (`look`) needs a skeleton, because it turns a limb.

A registry entry declares the type:

```json
{
  "mymod/old_man": {
    "name": "Old Man (sitting)",
    "model": {
      "skeleton": "objects/object_xxx/gOldManSkel",
      "animation": "objects/object_xxx/gOldManSitAnim",
      "scale": 0.01,
      "segments": { "8": "objects/object_xxx/gOldManEyeOpenTex" },
      "shadow": 20
    },
    "collision": { "radius": 20, "height": 50 },
    "talk": { "message": "0x9001" },
    "look": { "limb": 15, "pivot": 1200 }
  },
  "mymod/pine_tree": {
    "model": { "displayList": "objects/mymod_props/gPineTreeDL", "scale": 0.1, "shadow": 40 },
    "collision": { "radius": 25, "height": 200 }
  },
  "mymod/signpost": {
    "model": { "displayList": "objects/mymod_props/gSignpostDL", "scale": 0.1 },
    "collision": { "radius": 12, "height": 40 },
    "talk": {}
  },
  "mymod/ghost_lantern": {
    "model": { "displayList": "objects/mymod_props/gGhostLanternDL", "scale": 0.1,
               "translucent": true }
  }
}
```

The signpost has no default message, so each placement sets its own text through `params`.

A room places it by name. Two placements of one type can say different things through `params`:

```json
"actors": {
  "10": { "id": "mymod/old_man", "pos": [120, 0, -40], "rot": [0, 16384, 0], "params": 0 },
  "11": { "id": "mymod/old_man", "pos": [300, 0, 80],  "rot": [0, 0, 0],     "params": "0x9002" }
}
```

The first says message `0x9001` (the type's default); the second says `0x9002`. Both messages are
ordinary entries in `text/<lang>/messages.json` (SPEC §5).

## Proposed SPEC text

Three changes, all additions under format version 2 (SPEC §10).

### New section — Actor registry: `unbound/actors.json`

One layer-merged document (§3), keyed by actor type name. Like §7, it carries no `$schema`.

| Key | Required | Type / meaning |
|---|---|---|
| key | — | type name: any unique, non-empty string that is not an integer in the §2 string form and is not an actor name the game already knows (vanilla names such as `En_Kanban`, and actors SoH or a build adds in code). Writers should namespace it (`mymod/old_man`). Any other key **rejects the entry**. An entry that is not an object is ignored. |
| `name` | no | display name; default = the key |
| `model` | yes | object (below). An entry without one is **rejected**. |
| `collision` | no | object (below); absent = the actor has no collision and can be walked through |
| `talk` | no | object (below); absent = the actor cannot be targeted or talked to |
| `look` | no | object (below): the head turns to follow the player. Needs a `model.skeleton`; on a static model it **rejects the entry**. |
| any other key | — | **rejects the entry**. Keys this version does not define are reserved for later versions (`base`, `params`, `script`); a mod that needs one must also declare `requires.formatVersion` (§6). |

**`model`** — exactly one of `skeleton` (an animated model) or `displayList` (a static model);
both, or neither, **rejects the entry**.

| Key | Type / meaning |
|---|---|
| `skeleton` | path of a skeleton resource, normal or flex. A curve skeleton is not supported: the actor does not spawn and an error is logged. |
| `animation` | path of an animation for `skeleton`, with the skeleton's limb count. Absent, the skeleton is drawn in its bind pose. Ignored with `displayList`. |
| `frame` | number: when present, the animation is held on this frame (a pose); absent, the animation loops |
| `speed` | number: playback rate for a looping animation; default 1 |
| `displayList` | path of a display list: the whole model, drawn as it is |
| `translucent` | boolean: draw in the translucent pass instead of the opaque one, for models with real transparency (glass, ghosts, water). Default false. Cut-out transparency such as leaves and fences does not need it: the display list's own render mode handles that in the opaque pass. |
| `scale` | number; default 0.01 (the scale of most vanilla NPCs) |
| `yOffset` | number: model-space vertical offset, applied before scale; default 0 |
| `segments` | object: key a segment number 8–12 as a §2 integer string, value a texture path, bound before the model draws (NPC eye and mouth textures). Any other key is ignored with an error. |
| `shadow` | number: radius of a round ground shadow; default 0 = none |

Asset paths are resolved when an actor of the type spawns, not when the registry loads. A path
that does not resolve stops that actor from spawning, with an error; it does not reject the type.

**`collision`** — a solid cylinder the player cannot pass through.

| Key | Type / meaning |
|---|---|
| `radius`, `height` | integers, world units; default 0 (a zero radius or height means no collision) |
| `yShift` | integer: vertical offset of the cylinder's base; default 0 |

**`talk`**

| Key | Type / meaning |
|---|---|
| `message` | integer message id 0–65534 (§5): the default text. Default 0 = none, in which case only placements that set `params` talk. |
| `range` | number: talk range in world units; default 50 + `collision.radius` (vanilla's default) |

The message shown is `params` (read as unsigned 16-bit) when it is non-zero and not `0xFFFF`,
otherwise `talk.message`. When both are zero the actor cannot be talked to. A message that does
not exist shows whatever the game shows for a missing id, as with any actor.

**`look`** — the head turns toward the player, within the neck's limits, as vanilla NPCs do.

| Key | Type / meaning |
|---|---|
| `limb` | integer: index of the head limb in the skeleton. Required: absent, or not a limb of the skeleton, the actor spawns without head tracking and an error is logged. |
| `pivot` | number: distance along the head limb, in model units, from the limb's origin to the point the head turns about (the neck). Default 0. Vanilla NPC heads use 1 200–1 400. |
| `range` | number: the head follows the player within this distance, in world units, and while talking; outside it the head returns to rest. Default 200. |

The head turns about the limb's own axes the way vanilla character rigs are built: turning left
and right about the limb's X axis, and up and down about its Z axis. A skeleton made another way
turns its head about the wrong axes.

When `look` is present, the actor's focus point (where the targeting arrow sits and the camera
looks while talking) is its head. Otherwise it is the top of the collision cylinder, or the
actor's position when it has no collision.

A registered type gets an actor id assigned by the game, in registry order (§3.5). The number
depends on which mods are mounted and must never be written by a tool; types are addressed by
name only.

### §4.3 — actor entry `id`

> `id` is an integer (§2) **or an actor name**: a string that is not an integer in the §2 string
> form names an actor type, either one registered in `unbound/actors.json` or an actor the game
> already knows by name. An entry whose name is not known is skipped with an error, and the rest
> of the list loads. Names are accepted in room `actors` only; spawns and transition actors keep
> integer ids.

(Before this change such a string had the wrong type and read as missing — id 0, the player
actor. No valid document changes meaning.)

### §9 — limits

> Custom actor types: ≤ 28 672 per mounted set (actor ids are signed 16-bit and custom types are
> numbered from 0x1000).

## Engine design

### Registry

- A new loader, `soh/soh/unbound/ActorRegistry.{h,cpp}`, reads the merged `unbound/actors.json`
  through `Unbound::LoadMergedJson` (the same path as `SceneDB::LoadCustomScenes`), validates each
  entry into a `DeclaredActorType` struct, and registers it with ActorDB. Each entry is
  registered in its own `try`, so one bad entry is logged and skipped and the rest load.
- **Load point: after `ActorDB::AddBuiltInCustomActors()`** in `OTRGlobals.cpp`. `InitMods()`
  runs before it today (mods are mounted, then `LoadCustomScenes` runs from `UpdateModFiles`), so
  loading from the same place as scenes would number the mod types *before* SoH's own `En_Partner`.
  Mods are only mounted at startup (`EnableMod` is marked "TODO: runtime changes"), so the registry
  loads once and never has to unregister.
- **Ids start at `CUSTOM_ACTOR_ID_BASE` (0x1000)**, like custom scenes start at 128. That keeps
  them clear of the vanilla table, of `ACTOR_ID_MAX` (a "no actor" sentinel in randomizer code),
  of SoH's built-in custom actors, and of forks that took fixed ids just past the vanilla table.
  This needs a public ActorDB call that registers at a given index; `AddEntry(name, desc, index)`
  exists but is private.
- **Duplicate names:** `ActorDB::AddEntry` `assert`s on a name it already has. The registry checks
  `RetrieveId` first and rejects the entry instead. Two mods declaring the same key are not
  duplicates: the merge (§3) combines them into one entry before registration, so the later mod
  patches the earlier one's type.
- Every registered type is its own ActorDB entry, with its own id, name, description and flags,
  pointing at the shared driver functions. Flags: `ACTOR_FLAG_ATTENTION_ENABLED |
  ACTOR_FLAG_FRIENDLY` when the type talks. Category: `ACTORCAT_NPC` when it talks, otherwise
  `ACTORCAT_PROP`. Object: `OBJECT_GAMEPLAY_KEEP`, which is always loaded, so a scene never has to
  list an object for a declared actor. The driver loads its assets by path instead.
- The driver finds its type with `ActorRegistry_Get(actor->id)`, an index into a vector by
  `id - CUSTOM_ACTOR_ID_BASE`.

### Name resolution in scenes

`BuildActorList` in `UnboundSceneFactory.cpp` resolves a string `id` through
`ActorDB_RetrieveId`. `ReadActor` is also used for spawns (`BuildStartPositions`), which must stay
integer, so resolution happens in `BuildActorList` and not in `ReadActor`. An unknown name logs
the room, the entry key and the name, and the entry is not added to the list.

Scene resources are parsed when a scene loads, long after the registry is built, so there is no
ordering problem. Vanilla-format (binary) scenes store a numeric id and cannot name a custom
actor. That matches custom entrances, which a binary scene cannot exit into.

### The driver

`soh/soh/unbound/DeclaredActor.{h,cpp}` holds the instance struct and the four ActorDB functions.
Each behavior is its own small function taking the instance and its type, so the orchestration
functions read as a list of steps, and so a script can later call the same functions:

```
Init:    ResolveType → InitModel → InitCollision → InitTalk → InitLook
Update:  UpdateTalk → UpdateLook → UpdateCollision → UpdateAnimation → UpdateFocus
Draw:    BindSegments → DrawModel (limb callbacks: TurnHead, RecordHeadFocus)
Destroy: FreeModel → FreeCollision
```

- **Asset paths.** SoH resolves an asset path only when it carries the `__OTR__` signature
  (`ResourceMgr_OTRSigCheck`). The type stores each path with the prefix added once at
  registration, and those strings live as long as the registry, so the driver passes them
  anywhere vanilla code passes an asset symbol.
- **Model.** The skeleton's header records its type (`SkeletonHeader.skeletonType`) and limb
  count, so `InitModel` chooses `SkelAnime_InitFlex` or `SkelAnime_Init` at runtime and lets it
  allocate the joint tables (`SkelAnime_Free` in destroy). Looping is
  `Animation_Change(..., ANIMMODE_LOOP, ...)` at `speed`. A pose is the same call with speed 0,
  starting and ending on `frame`. With no animation the driver zeroes the joint tables itself,
  which is the bind pose: `SkelAnime_Init` allocates them with `ZELDA_ARENA_MALLOC` and only fills
  them when given an animation, so they would otherwise hold garbage. `ActorShape_Init` applies
  `yOffset` and the circle shadow.
- **Draw pass.** A static model is `Gfx_DrawDListOpa` or, with `translucent`, `Gfx_DrawDListXlu`.
  An animated model is `SkelAnime_DrawOpa`/`SkelAnime_DrawFlexOpa`, or with `translucent` the
  `Gfx*`-returning `SkelAnime_Draw`/`SkelAnime_DrawFlex` writing into `POLY_XLU_DISP`, as vanilla
  translucent actors do. `BindSegments` writes to the same pass.
- **Segments.** `BindSegments` issues `gSPSegment` for each entry, exactly as vanilla NPC draw code
  does for eyes and mouths. Segment 13 is excluded because flex skeletons use it for their
  matrices.
- **Collision.** One `ColliderCylinder`, OC only (`OC1_ON | OC1_TYPE_ALL`, `OC2_TYPE_2`),
  `colChkInfo.mass = MASS_IMMOVABLE`, submitted each frame with `CollisionCheck_SetOC`. No
  gravity or floor check: the actor stays exactly where it was placed.
- **Talk.** The standard vanilla sequence. When not talking, offer to talk with
  `func_8002F2CC(actor, play, range)` and keep `actor->textId` set. `Actor_ProcessTalkRequest`
  starts talking; the Player actor opens the textbox. `Actor_TextboxIsClosing` returns to idle.
  Multi-box text and follow-up messages chained by control codes need nothing extra. A choice
  box closes the conversation whatever the answer, because there is no behavior to branch to yet.
  That limit is intentional.
- **Look.** The same pattern as vanilla NPCs, which all hard-code it per actor (`EnKo`, `EnMa1`,
  `EnToryo`, … with limb 15 and a pivot of 1 200–1 400). `UpdateLook` calls `Npc_TrackPoint` with
  the player as target while the player is within `range` or talking, and with tracking off
  otherwise, so the head eases back to rest. The head's limb-draw callback translates by `pivot`
  along X, applies `headRot.y` about X and `headRot.x` about Z, and translates back — the code
  `EnMa1_OverrideLimbDraw` uses. The post-limb-draw callback records the head's world position,
  and `UpdateFocus` copies it to `actor->focus.pos`. Torso tracking is left out: vanilla rigs
  disagree on the sign of the torso turn, so it would need a per-rig setting.

### To audit before merging

- Anything sized or indexed by actor id up to `ACTOR_ID_MAX`/`ACTOR_NUMBER_MAX`. `En_Partner`
  already lives past the vanilla table, which suggests most code copes, but it has not been
  checked table by table. Randomizer code uses `ACTOR_ID_MAX` as "no actor" (the reason for the
  id base above).
- Enemy randomizer and actor-list enhancements that iterate the ActorDB or assume every id has an
  overlay.
- Anchor/co-op messages that carry actor ids: they match only when both players run the same
  mods, which is already true of scene ids.
- The actor viewer and debug console `spawn`: both should list and spawn declared types by name
  through ActorDB with no change. Confirm.

## Later phases (not in this version)

Recorded so the format leaves room for them. The unknown-key rule above is what keeps that room:
an older build rejects a type it cannot run instead of placing an actor that does nothing.

1. **More built-in behaviors:** follow a path (scene `paths`), switch animation while talking,
   blinking (a list of eye textures cycled on a segment), torso tracking, `look` axes for rigs
   built unlike vanilla's.
2. **Mesh collision for static models.** A cylinder is enough for trees, signs and statues, but a
   rock the player can stand on, a bridge or a platform needs its model's shape as collision:
   `collision.mesh` naming a collision resource, registered as a dynamic collision actor
   (`DynaPolyActor`, the way vanilla's movable blocks and platforms work). Prelude would have to
   export a collision resource per model.
3. **Named params.** A type declares named fields packed into `params`
   (`"params": { "message": { "bits": "0-15" } }`) and Prelude shows a form field for each. Scripts
   will need per-placement arguments; this is how they get them without a separate property
   channel. v1's "`params` is the message id" rule is the one-field case of this.
4. **Types that extend a vanilla actor (`base`).** A new type that runs a vanilla actor's code
   under a new id, with a fixed set of overrides that need no code for a particular actor:
   - fixed `params` bits (`value` + `mask`; the placement supplies the rest)
   - asset swaps: asset path → asset path, active while the actor's own functions run, applied
     where SoH resolves paths (`GbiWrap.cpp`, `ResourceManagerHelpers.cpp`)
   - text remap: vanilla message id → mod message id, swapping the content while
     `msgCtx->textId` keeps the vanilla id so the actor's state machine is unaffected

   Known traps: effects the actor spawns draw later outside its functions (a pot's
   `EffectSsKakera` shards) and need to inherit the swaps; skeleton swaps need a matching rig; code
   that finds the vanilla actor by its id does not find the variant; the base's object must still
   be loaded. Behavior values (speeds, collider sizes, state logic) are *not* overridable this
   way. Each would need a hook written for that actor, and that is where scripting takes over.
5. **Scripting.** A `script` key on a type. The driver's split into separate behaviors, and the
   wrapper points `base` introduces, are where script callbacks (`onInit`, `onUpdate`, `onTalk`)
   attach.

## Open questions

1. **`params` for a type that does not talk.** v1 ignores it. Fine until named params exist.
2. **Vanilla names in `id`.** The proposed text accepts any name ActorDB knows, including vanilla
   (`"En_Kanban"`). That reads better, and it is how fork actors get placed by name, but it means
   the converter could write names instead of numbers. Recommendation: accept names, keep the
   converter writing numbers.
3. **Culling.** Declared actors get the default culling volume, so a very large model can vanish
   at the screen edge. Add a `model.cullRadius` if it shows up in practice.

## What Prelude needs

- Author `unbound/actors.json` entries: type name, model (an animated skeleton or a static
  display list), collision, talk, look.
- Place declared types (and named fork actors) by name in the actor palette, writing
  `"id": "<name>"`. This replaces typing raw ids.
- Preview: draw the skeleton in the chosen animation frame (or its bind pose), or the display list.
- For `look`: let the user pick the head limb from the skeleton's limb list, since the index
  differs between rigs, and preview the pivot.
- Validate: skeleton is normal or flex, animation limb count matches the skeleton, the talk
  message exists in the mod's text, `params` for a talking type is a message id.
- Record the change in [`prelude-handoff.md`](./prelude-handoff.md) when it lands.

## Verification plan

1. A mod declaring one skeleton type and one display-list type loads; both appear in the actor
   viewer under their names, with ids from 0x1000.
2. Placed by name in a custom room: the model draws in the right pose, a looping animation loops,
   a skeleton with no animation stands in its bind pose, and a `translucent` model blends.
3. Collision blocks the player; a type without `collision` does not.
4. Talking: Z-target, talk, the textbox shows the type's message; a second placement with
   `params` shows its own; a chained multi-message conversation plays through and returns to idle.
5. Look: a vanilla NPC skeleton with limb 15 and pivot 1 200 turns its head to follow the player
   within `range` and while talking, returns to rest outside it, and the targeting arrow sits
   over the head.
6. Errors: unknown name in a room (entry skipped, room loads); duplicate of a vanilla name
   (entry rejected); a bad skeleton path (that actor does not spawn, the rest of the room does);
   an unknown key such as `base` (entry rejected); `look` on a static model (entry rejected).
7. No-mod parity: with no `unbound/actors.json`, ActorDB, `En_Partner`'s id and vanilla rooms are
   unchanged.
