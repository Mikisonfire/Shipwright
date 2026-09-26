# Unbound: custom actors

**Status: implemented on branch `unbound-custom-actors`; play-tested 2026-09-25** (every item of the
verification plan, in game and through a Prelude export). The test fixture is
[`examples/custom-actors`](./examples/custom-actors/README.md). The "Proposed SPEC text" section
moves into [`SPEC.md`](./SPEC.md) when the branch is merged, and this file becomes the how-doc,
like the others in this directory.

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
   fork's C actor registered in ActorDB under a name is placed by that name. Any name ActorDB
   knows is accepted, spelled as ActorDB spells it, vanilla included (`"En_Kanban"`). The
   converter keeps writing vanilla actors as numbers.
3. **Per-placement setup stays in `params`.** As in vanilla, the type says what the actor is and
   `params` says how this placement is set up. For the first version `params` has one meaning (the
   message id, below).
4. **First version: declared types only.** One C driver runs every declared type, reading the
   type's settings from the registry: a model (animated or static), a collision cylinder,
   talking, and head tracking. Types that
   extend a vanilla actor (`base`) are a later phase; nothing in this version depends on them.

## What a modder writes

A type's model is one of two kinds:

- **Animated:** a skeleton, posed by an animation, looping or held on one frame. NPCs, animals,
  anything with limbs.
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
    "talk": { "message": "0xA001" },
    "look": { "limb": 15 }
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
  "11": { "id": "mymod/old_man", "pos": [300, 0, 80],  "rot": [0, 0, 0],     "params": "0xA002" }
}
```

The first says message `0xA001` (the type's default); the second says `0xA002`. Both messages are
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
| any other key | — | **rejects the entry**. Keys this version does not define are reserved for later versions (`base`, `params`, `script`). A build that predates a key therefore rejects the type, and its placements are skipped as unknown names, instead of spawning an actor without the behavior. The same rule holds inside `model`, `collision`, `talk` and `look`: a key none of the tables below lists rejects the entry. |

**`model`** — exactly one of `skeleton` (an animated model) or `displayList` (a static model);
both, or neither, **rejects the entry**.

| Key | Type / meaning |
|---|---|
| `skeleton` | path of a skeleton resource, normal or flex, with standard or LOD limbs. A curve skeleton, or one with skin limbs (Epona's), is not supported: the actor does not spawn and an error is logged. |
| `animation` | path of an animation for `skeleton`, with the skeleton's limb count. Required with `skeleton`: absent **rejects the entry**, because an OoT skeleton has no usable rest pose (with every joint angle zero it folds up). An animation for fewer limbs than the skeleton has stops the actor from spawning, with an error. For a still model, hold one frame with `frame`. Ignored with `displayList`. |
| `frame` | number: when present, the animation is held on this frame (a pose), clamped to the animation's first and last frames; absent, the animation loops |
| `speed` | number: playback rate for a looping animation, in frames per update; default 1. Clamped to the animation's length either way (negative plays backwards). |
| `displayList` | path of a display list: the whole model, drawn as it is |
| `translucent` | boolean: draw in the translucent pass instead of the opaque one, for models with real transparency (glass, ghosts, water). Default false. Cut-out transparency such as leaves and fences does not need it: the display list's own render mode handles that in the opaque pass. The model's own render mode decides whether it blends: a vanilla character model, which sets an opaque mode, is only sorted with the translucent pass and does not turn see-through. |
| `scale` | number; default 0.01 (the scale of most vanilla NPCs) |
| `yOffset` | number: model-space vertical offset, applied before scale; default 0 |
| `segments` | object: key a segment number 8–12 as a §2 integer string, value a texture path, bound before the model draws (NPC eye and mouth textures). Any other key, and a value that is not a non-empty string, is ignored with an error. A path that is not a texture stops the actor from spawning, as any other path does. A segment 8–12 the type does not name is bound to an empty display list, which is what vanilla binds on the segment many character models call to set their render mode. The environment colour is opaque black while the model draws. |
| `hideLimbs` | array of integers: limbs, numbered as `look.limb` is, whose own mesh is not drawn; their child limbs still draw. Vanilla character code hides spare hands and props it swaps in (Malon's limbs 2 and 5, child Zelda's 3–6). Entries below 1 are ignored with an error. |
| `shadow` | number: size of a round ground shadow, on the scale vanilla NPCs give theirs (child Malon 18, the carpenter 42); default 0 = none. It does not change with `scale`: the same value draws the same shadow on any model. The shadow is drawn on the floor under the actor's position when it spawns, when that floor is at most 50 units above or 500 below it. |
| `cullRadius` | number, world units: how far the model reaches from the actor's position. The game stops drawing an actor whose position is off screen by more than about 350 units, which cuts off larger models at the screen edge; a larger `cullRadius` widens that margin. Default 0 = the game's default. |
| `drawDistance` | number, world units: the actor stops drawing (and updating) beyond about this distance in front of the camera, plus `cullRadius`. Default 1000, the game's default. |

Asset paths are resolved when an actor of the type spawns, not when the registry loads. A path
that does not resolve stops that actor from spawning, with an error; it does not reject the type.

A path may name a vanilla asset or one the mod ships itself, at any path in its archive (§1.4).
Mod-supplied display lists, vertex arrays, textures, skeletons and animations are ordinary SoH
resources of the same types vanilla objects use, as room meshes already are (§4.3). Writers
should keep them under a path of their own (`objects/<mod>/…`) and never under `alt/`. For a
model the mod ships:

- Vertices are in **model space** around the actor's origin, and `scale` converts them to world
  units. A room mesh is exported in world units, so the same geometry placed as an actor needs
  `scale` 1, or coordinates exported larger to match a smaller `scale`.
- The display list sets up its own render state (render mode, combiner, geometry mode, textures),
  as a room mesh's does. The actor sets only the matrix and the segments in `segments`.
- Whether the model is lit is the display list's choice: with normals and lighting enabled it is
  lit like vanilla actors; with vertex colours and lighting off it is shaded like room geometry,
  which matches the scene around it.

**`collision`** — a solid cylinder the player cannot pass through.

| Key | Type / meaning |
|---|---|
| `radius`, `height` | integers, world units; default 0 (a zero radius or height means no collision) |
| `yShift` | integer: vertical offset of the cylinder's base; default 0 |

**`talk`**

| Key | Type / meaning |
|---|---|
| `message` | integer message id 0–65534 (§5): the default text. Default 0 = none, in which case only placements that set `params` talk. A value outside the range reads as 0, with an error. |
| `range` | number: talk range in world units; default 50 + `collision.radius` (vanilla's default) |

The message shown is `params` (read as unsigned 16-bit) when it is non-zero and not `0xFFFF`,
otherwise `talk.message`. When both are zero the actor cannot be talked to. A message that does
not exist shows whatever the game shows for a missing id, as with any actor.

**`look`** — the head turns toward the player, within the neck's limits, as vanilla NPCs do.

| Key | Type / meaning |
|---|---|
| `limb` | integer: the head limb, numbered as vanilla limb-draw code numbers limbs (the root limb is 1; vanilla NPC heads are usually 15). Required: absent, or not a limb of the skeleton, the actor spawns without head tracking and an error is logged. |
| `pivot` | number: distance along the head limb's X axis, in model units, from the limb's origin to the point the head turns about. Default 0, the limb's origin, which is the neck on vanilla rigs. |
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
> of the list loads. A name changes nothing else about the actor: a vanilla actor placed by name
> still needs its object in the room's `objects`, as when it is placed by number. A declared type
> needs none.
>
> An integer `id` must be below `0x1000`. Numbers from `0x1000` up are assigned to registered
> types at load and change with the mounted mods, so an entry that uses one is skipped with an
> error; such actors are placed by name.
>
> Names are accepted in room `actors` only; spawns and transition actors keep integer ids. A
> transition actor whose `id` is a name, or a number from `0x1000` up, does not spawn, with an
> error; it keeps its place in the list, whose indices other data refers to.
>
> `params` as an object is reserved for named arguments in a later version. An entry whose
> `params` is an object is skipped with an error.

(Before this change a string had the wrong type and read as missing — id 0, the player actor.
Integer ids from `0x1000` up named no actor. A document that relied on either changes meaning; no
valid document did.)

### §9 — limits

> Custom actor types: ≤ 28 672 per mounted set (actor ids are signed 16-bit and custom types are
> numbered from 0x1000).

### Compatibility with older builds

Every Unbound release so far reads a string `id` as the wrong type, which §2 treats as missing:
id 0, the player actor. A scene that places a custom actor by name therefore spawns an extra Link
on those builds instead of being refused, and `requires.formatVersion` cannot prevent it, because
a layer that fails the version check is still merged (§6). Mods that use custom actors need the
release that adds them. Prelude should say so when it exports one, and the release notes should
say it too. Builds with this change skip any name they do not know, so the problem does not recur
for later additions.

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
Init:    ResolveType → InitModel (InitShape → InitFloor → InitCulling → InitSegments → InitMesh)
         → InitCollision → InitTalk → InitLook → InitFocus
Update:  UpdateTalk → UpdateLook → UpdateCollision → UpdateAnimation
Draw:    DrawSegments → DrawSkeleton or DrawDisplayList (limb callbacks: TurnHead, RecordHeadFocus)
Destroy: free the skeleton and the collider, if they were set up
```

- **Asset paths.** SoH resolves an asset path only when it carries the `__OTR__` signature
  (`ResourceMgr_OTRSigCheck`). The type stores each path with the prefix added once at
  registration, and those strings live as long as the registry, so the driver passes them
  anywhere vanilla code passes an asset symbol.
- **Checking assets.** Before using a path, the driver loads the resource and checks its type:
  a skeleton must be normal or flex (`SOH::Skeleton::type`) with standard or LOD limbs
  (`limbType`: a skin limb has no display list where the skeleton drawer reads one), an
  animation must be a normal one (not Link's) with at least one joint entry per joint-table
  entry (`rotationIndices.size() >= skelAnime.limbCount`, the skeleton's limbs plus the root
  position: `SkelAnime_GetFrameData` reads that many), a display list and a segment texture must
  be one. A wrong or missing asset kills that actor with an error instead of handing the game a
  bad pointer.
- **Model.** The skeleton resource records its type and limb count, so `InitModel` chooses
  `SkelAnime_InitFlex` or `SkelAnime_Init` at runtime and lets it
  allocate the joint tables (`SkelAnime_Free` in destroy). Looping is
  `Animation_Change(..., ANIMMODE_LOOP, ...)` at `speed`, clamped to the animation's length:
  `SkelAnime_LoopFull` wraps the frame once per update, so a longer step would leave the
  animation's data. A pose is the same call with speed 0, starting and ending on `frame`.
- **Shadow.** `ActorShape_Init` applies `yOffset` and the circle shadow. `ActorShadow_Draw`
  scales the shadow by the actor's scale and draws only over `actor->floorPoly`, which vanilla
  actors get from `Actor_UpdateBgCheckInfo`. The driver never runs that (it would move the actor
  onto the floor), so `InitFloor` does one `BgCheck_EntityRaycastFloor5` from 50 units above the
  position, as the vanilla check does, and records the floor only. The shadow scale is
  `shadow × 0.01 / scale`, so `shadow` means the same at any `scale`.
- **Culling.** `Actor_Init` gives every actor a zone of 1 000 forward, 350 to the sides and up,
  700 down. `InitCulling` raises the side, up and down margins to `cullRadius` and sets the
  forward distance to `drawDistance`, as vanilla scenery does by hand (`EnWood02`: 4 000 / 2 000
  / 2 400).
- **Draw pass.** A static model is `Gfx_DrawDListOpa` or, with `translucent`, `Gfx_DrawDListXlu`.
  An animated model is `SkelAnime_DrawOpa`/`SkelAnime_DrawFlexOpa`, or with `translucent` the
  `Gfx*`-returning `SkelAnime_Draw`/`SkelAnime_DrawFlex` writing into `POLY_XLU_DISP`, as vanilla
  translucent actors do. `BindSegments` writes to the same pass.
- **Segments.** `BindSegments` first binds every segment 8–12 to `gEmptyDL`, then issues
  `gSPSegment` for each entry, exactly as vanilla NPC draw code does for eyes and mouths, and sets
  the env colour to opaque black. The empty default is bound in the translucent pass too: vanilla
  binds its translucent render mode (`D_80116280`) there only while fading an actor out through
  env alpha, which the driver has no use for, so a character model drawn translucent keeps its
  opaque render mode. Character models such as adult Ruto's, adult Zelda's and
  Darunia's call a segment to set their render mode; vanilla binds `&D_80116280[2]` there, which
  is an end-of-list (entries 0–1 are the translucent mode used while fading). Without a default
  the segment would hold whatever the previous actor bound. Segment 13 is excluded because flex
  skeletons use it for their matrices.
- **Hidden limbs.** The limb-draw callback clears the limb's display list; the limb's transform
  still applies, so its children draw where they should.
- **Collision.** One `ColliderCylinder`, OC only (`OC1_ON | OC1_TYPE_ALL`, `OC2_TYPE_2`),
  `colChkInfo.mass = MASS_IMMOVABLE`, submitted each frame with `CollisionCheck_SetOC`. No
  gravity or floor check: the actor stays exactly where it was placed.
- **Talk.** The standard vanilla sequence. When not talking, offer to talk with
  `func_8002F2CC(actor, play, range)` and keep `actor->textId` set. `Actor_ProcessTalkRequest`
  starts talking; the Player actor opens the textbox. Whether the actor is talking is read from
  the Player every frame (`PLAYER_STATE1_TALKING` and `player->talkActor`), not latched on
  `Actor_TextboxIsClosing`: that is true for one frame only, and an actor culled on that frame
  never updates to see it. Multi-box text and follow-up messages chained by control codes need
  nothing extra. A choice box closes the conversation whatever the answer, because there is no
  behavior to branch to yet; `Message_Update` closes it. A box that ends in an event (or is
  persistent) waits for its actor, so the driver closes it when the player advances
  (`TEXT_STATE_EVENT` and `Message_ShouldAdvance`), as vanilla actors do. These limits are
  intentional.
- **Look.** The same pattern as vanilla NPCs, which all hard-code it per actor (`EnKo`, `EnMa1`,
  `EnToryo`, … usually on limb 15). `UpdateLook` calls `Npc_TrackPoint` (preset 0: 60° of head
  yaw) in `NPC_TRACKING_HEAD` mode while the player is within `range` or talking, and in
  `NPC_TRACKING_NONE` otherwise, so the head eases back to rest. The head's limb-draw callback
  applies the limb's own transform, then turns about `pivot` on the limb's X axis: `headRot.y`
  about X and `headRot.x` about Z. The X turn is the same as `EnToryo_OverrideLimbDraw` adding
  `headRot.y` to the limb's X rotation. (`EnMa1`/`EnKo` translate 1 200–1 400 units *before* the
  limb's transform, in the parent's space, to reach the same neck point; after the transform that
  point is the limb's origin, hence `pivot` 0.) The post-limb-draw callback writes the pivot's
  world position to `actor->focus.pos`, which the next frame's tracking uses for its height. Torso
  tracking is left out: vanilla rigs disagree on the sign of the torso turn, so it would need a
  per-rig setting.
- **Focus without a head:** the top of the collision cylinder, or the actor's position.
- **Targeting:** a placement that talks uses target mode 6 (100 units, as vanilla NPCs). The
  type's ActorDB flags make it targetable; a placement with no message clears them on itself.

### Actor-id audit

Nothing in SoH sizes or indexes a table by actor id: every lookup goes through ActorDB, whose
`RetrieveEntry` is bounds-checked. `ACTOR_ID_MAX` is only a "no actor" value in randomizer tables,
keyed by (id, scene, params), which a custom id cannot collide with. Enemy randomizer scans by id and
passes unknown ids through. Anchor packets carry no actor ids; Sail writes them as JSON ints.
Save states copy the heap wholesale and hold no id tables. Fixed along the way:

- `Actor_Spawn` only `assert`ed that the id had an actor. Release builds drop asserts, so an id
  with no actor (a gap below `0x1000`, a typo in a scene, a debug-console or Crowd Control spawn)
  allocated a zero-size actor and wrote past it. It now logs and spawns nothing.
- `ActorDB::AddEntry` `assert`s on a duplicate id or name, which also vanish in release. The
  registry registers through `ActorDB::TryAddEntry`, which checks both in release builds and adds
  nothing when either is taken.
- Actor Viewer: *Spawn as Child* refused every id past the vanilla table (`En_Partner`'s too), and
  the search-result list looped forever at 256+ results because of a `u8` index. Its search also
  skips the empty ids below the custom types.

Transition actors mask their id with `0x1FFF` (`z_actor.c`); names are accepted in room actors
only, and the scene reader turns a name or a number from `0x1000` up in a transition actor into
`-1`, which the spawn loop skips (it treats a negative id as already spawned), so custom types
never reach that path. Room actors with a number from `0x1000` up are skipped for the same
reason the numbers are never written: they depend on the mounted mods.

## Later phases (not in this version)

Recorded so the format leaves room for them. The unknown-key rule above is what keeps that room:
an older build rejects a type it cannot run instead of placing an actor that does nothing.

1. **More built-in behaviors:** follow a path (scene `paths`), switch animation while talking,
   blinking (a list of eye textures cycled on a segment), torso tracking, `look` axes for rigs
   built unlike vanilla's.
2. **Mesh collision for static models.** Decided against for v1. A cylinder is enough for trees,
   signs and statues, but a rock the player can stand on, a bridge or a platform needs its model's
   shape as collision:
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
2. **Culling.** Resolved: `model.cullRadius` and `model.drawDistance`.

## What Prelude needs

- Author `unbound/actors.json` entries: type name, model (an animated skeleton or a static
  display list), collision, talk, look.
- Place declared types (and named fork actors) by name in the actor palette, writing
  `"id": "<name>"`. This replaces typing raw ids. A vanilla actor placed by name still needs its
  object in the room's `objects`, exactly as when placed by number.
- Preview: draw the skeleton in the chosen animation frame, or the display list.
- For `look`: let the user pick the head limb from the skeleton's limb list, numbered from 1 at
  the root (vanilla limb-draw numbering), since the index differs between rigs.
- Validate: skeleton is normal or flex with standard or LOD limbs, animation limb count matches
  the skeleton, the talk message exists in the mod's text, `params` for a talking type is a
  message id.
- Offer `cullRadius` (from the model's bounds) and `drawDistance` for large props.
- Record the change in [`prelude-handoff.md`](./prelude-handoff.md) when it lands.

## Verification plan

1. A mod declaring one skeleton type and one display-list type loads; both appear in the actor
   viewer under their names, with ids from 0x1000.
2. Placed by name in a custom room: the model draws in the right pose, a looping animation loops,
   and a `translucent` model blends.
3. Collision blocks the player; a type without `collision` does not.
4. Talking: Z-target, talk, the textbox shows the type's message; a second placement with
   `params` shows its own; a chained multi-message conversation plays through and returns to idle.
5. Look: a vanilla NPC skeleton with limb 15 turns its head to follow the player
   within `range` and while talking, returns to rest outside it, and the targeting arrow sits
   over the head.
6. Errors: unknown name in a room (entry skipped, room loads); duplicate of a vanilla name
   (entry rejected); a bad skeleton path (that actor does not spawn, the rest of the room does);
   an unknown key such as `base`, or `model.lod` inside an object (entry rejected); `look` on a
   static model (entry rejected); a skeleton with no animation (entry rejected); a skin-limb
   skeleton, or an animation for fewer limbs (that actor does not spawn); a room actor with id
   `0x1000` or object `params` (entry skipped).
7. Shadows: a type with `shadow` draws a round shadow on the ground under it, the same size at
   any `scale`.
8. An event-ended message closes when advanced; after a conversation the actor can be talked to
   again, also after walking away mid-close.
9. No-mod parity: with no `unbound/actors.json`, ActorDB, `En_Partner`'s id and vanilla rooms are
   unchanged.
