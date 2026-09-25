# Example: custom actor types

Declares three working actor types in `unbound/actors.json` and places them by name in Link's house
(`link_home`, room 0, setup 0), using only vanilla assets by path. It is the test fixture for
[`actors.md`](../../actors.md).

| Placement | Type | What it exercises |
|---|---|---|
| `90`, left of the door as you enter | `example/carpenter` | skeleton + looping animation, collision, talk with the type's default message (`params` 0), head tracking on limb 15 |
| `91`, right of the door | `example/malon_pose` | flex skeleton held on one frame, `segments` 8 and 9 (eyes, mouth), message from `params` |
| `92`, by the door | `example/talking_pot` | static display-list model that talks like a sign, message from `params` |
| `93` | `example/no_animation` | a skeleton with no animation: the type is rejected (an OoT skeleton has no usable rest pose) and the placement skipped |
| `94` | `example/uses_a_later_key` | an entry with a key this build does not know (`base`): the type is rejected and the placement skipped |
| `95` | `example/not_registered` | a name nothing registered: the placement is skipped |

The messages are `0xA001`–`0xA003` in `text/eng/messages.json`.

Package with any zip tool, keeping the paths, and drop the result in SoH's `mods/` folder:

    cd custom-actors && zip -r ../custom-actors.o2r unbound.json unbound text scenes

## Expected

- The log lists `actor type 'example/…' -> id 0x1000` through `0x1002`, errors that
  `example/no_animation` needs an animation and that `example/uses_a_later_key` has a key this
  build does not know, and, when the house loads, errors that placements `93`, `94` and `95` name
  no known actor.
- In the house: the carpenter stands in his idle loop, turns his head to follow Link within 200
  units and while talking, and says `0xA001` (three lines over two boxes). Malon stands still in
  one frame of her singing pose with open eyes and a smile, and says `0xA002`. The pot says
  `0xA003`. None of them can be walked through.
- Malon's vanilla draw code hides limbs 2 and 5 (`EnMa1_OverrideLimbDraw`); the driver draws every
  limb, so those extra pieces are expected here.
- The Actor Viewer (Developer Tools) finds each type by its display name, and can spawn it.
- Without the mod, Link's house is unchanged.
