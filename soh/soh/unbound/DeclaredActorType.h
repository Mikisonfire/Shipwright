#pragma once
// SOH [Unbound] A custom actor type declared in data (unbound/actors.json). ActorRegistry.cpp reads the JSON into
// this struct; the driver (DeclaredActor.cpp) sees only the struct. Design: unbound-docs/actors.md.
#include <libultraship/libultra.h>

#include <string>
#include <utility>
#include <vector>

struct DeclaredActorType {
    std::string name;        // registry key; also the actor's ActorDB name
    std::string displayName; // ActorDB description

    // Model: exactly one of `skeleton` or `displayList` is set. Every path carries the "__OTR__" prefix the game's
    // asset loaders look for, so it can be passed wherever vanilla code passes an asset symbol.
    std::string skeleton;
    std::string animation;  // required with a skeleton
    bool holdFrame = false; // true = hold the animation on `frame`; false = loop it at `speed`
    f32 frame = 0.0f;
    f32 speed = 1.0f;
    std::string displayList;
    bool translucent = false;
    f32 scale = 0.01f;
    f32 yOffset = 0.0f;
    f32 shadow = 0.0f;                                // radius of a round shadow; 0 = none
    std::vector<std::pair<u8, std::string>> segments; // segment 8-12 -> texture path
    std::vector<s32> hideLimbs;                       // limb-draw numbering (root = 1)

    // Collision: a solid cylinder, present when radius and height are both positive.
    s16 radius = 0;
    s16 height = 0;
    s16 yShift = 0;

    // Talk: the type can talk; a placement's message is its params when non-zero, otherwise `message`.
    bool talks = false;
    u16 message = 0;
    f32 range = 0.0f;

    // Look: the head limb (limb-draw numbering, root = 1) turns toward the player.
    bool looks = false;
    s32 limb = 0;
    f32 pivot = 0.0f;
    f32 lookRange = 200.0f;

    bool HasCollision() const {
        return radius > 0 && height > 0;
    }
};
