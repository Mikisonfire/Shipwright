# Example: custom actor types

Declares three working actor types in `unbound/actors.json` and places them by name in Link's house
(`link_home`, room 0, setup 0), using only vanilla assets by path. It is the test fixture for
[`actors.md`](../../actors.md).

| Placement | Type | What it exercises |
|---|---|---|
| `90`, left of the door as you enter | `example/carpenter` | skeleton + looping animation, collision, talk with the type's default message (`params` 0), head tracking on limb 15 |
| `91`, right of the door | `example/malon_pose` | flex skeleton held on one frame, `segments` 8 and 9 (eyes, mouth), `hideLimbs` (2 and 5, as her vanilla draw code), message from `params` |
| `92`, by the door | `example/talking_pot` | static display-list model that talks like a sign, message from `params` |
| `93` | `example/no_animation` | a skeleton with no animation: the type is rejected (an OoT skeleton has no usable rest pose) and the placement skipped |
| `94` | `example/uses_a_later_key` | an entry with a key this build does not know (`base`): the type is rejected and the placement skipped |
| `95` | `example/not_registered` | a name nothing registered: the placement is skipped |
| `96`, back wall | `example/glass_pane` | `translucent`: the Spirit Temple mirror's glass, drawn in the translucent pass |
| `97` | `example/bad_skeleton` | a skeleton path that does not exist: the type registers, this actor does not spawn |
| `98` | `example/bad_animation` | an "animation" that is a texture: the type registers, this actor does not spawn |
| `99`, left wall | `En_Kanban` | a vanilla actor placed by name: a wooden sign |

Two entries are never placed: a type keyed `En_Kanban` (rejected, the name is a vanilla actor's) and
`example/look_on_static` (rejected, `look` needs a skeleton).

The messages are `0xA001`–`0xA003` in `text/eng/messages.json`.

Package with any zip tool, keeping the paths, and drop the result in SoH's `mods/` folder:

    cd custom-actors && zip -r ../custom-actors.o2r unbound.json unbound text scenes

## Expected

- The log registers six types from `0x1000` (in key order, interleaved with any other mounted
  mod's types) and rejects four: `En_Kanban` ("already names an actor"), `look_on_static`
  ("needs a skeleton"), `no_animation` ("needs an animation") and `uses_a_later_key` ("not a key
  this build knows"). When the house loads, placements `93`–`95` are skipped as naming no known
  actor, and `97` and `98` fail to spawn with the path they could not use.
- In the house: the carpenter stands in his idle loop, turns his head to follow Link within 200
  units and while talking, and says `0xA001` (three lines over two boxes). Malon stands still in
  one frame of her singing pose with open eyes and a smile, and says `0xA002`. The pot says
  `0xA003`. None of them can be walked through. A small pane of glass stands against the back wall,
  see-through. A wooden sign stands by the left wall.
- Malon shows no extra hands: `hideLimbs` hides limbs 2 and 5, as `EnMa1_OverrideLimbDraw` does.
- The Actor Viewer (Developer Tools) finds each type by its display name, and can spawn it.
- Without the mod, Link's house is unchanged.
