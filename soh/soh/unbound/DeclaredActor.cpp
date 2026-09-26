// SOH [Unbound] The declared-actor driver (unbound-docs/actors.md, "The driver"). Each behavior is its own function
// taking the actor and its type, so the four ActorDB functions read as a list of steps, and so a script can later
// call the same functions.
#include "DeclaredActor.h"

#include <algorithm>
#include <libultraship/libultraship.h>
#include <spdlog/spdlog.h>

#include "soh/ResourceManagerHelpers.h"
#include "soh/frame_interpolation.h" // gives OPEN_DISPS's block-scope declarations their C linkage
#include "soh/resource/type/Animation.h"
#include "soh/resource/type/Skeleton.h"
#include "soh/resource/type/SohResourceType.h"
#include "soh/unbound/ActorRegistry.h"
#include <fast/resource/ResourceType.h>

extern "C" {
#include "z64.h"
#include "macros.h"
#include "functions.h"
#include "variables.h"
}

namespace {

typedef struct DeclaredActor {
    Actor actor;
    const DeclaredActorType* type;
    SkelAnime skelAnime;
    ColliderCylinder collider;
    NpcInteractInfo interactInfo;
    u8 hasSkeleton;
    u8 hasAnimation;
    u8 hasCollision;
    u8 talkable;
    u8 talking;
    u8 looking;
} DeclaredActor;

ColliderCylinderInit sCylinderInit = {
    {
        COLTYPE_NONE,
        AT_NONE,
        AC_NONE,
        OC1_ON | OC1_TYPE_ALL,
        OC2_TYPE_2,
        COLSHAPE_CYLINDER,
    },
    {
        ELEMTYPE_UNK0,
        { 0x00000000, 0x00, 0x00 },
        { 0x00000000, 0x00, 0x00 },
        TOUCH_NONE,
        BUMP_NONE,
        OCELEM_ON,
    },
    { 0, 0, 0, { 0, 0, 0 } },
};

constexpr s8 kTalkTargetMode = 6;       // 100-unit targeting range, as vanilla NPCs
constexpr s16 kLookTrackingPreset = 0;  // sNpcTrackingPresets: 60 degrees of head yaw
constexpr f32 kVanillaNpcScale = 0.01f; // the scale `shadow` is given at

// ---- assets -------------------------------------------------------------------------------------------------------

std::shared_ptr<Ship::IResource> LoadAsset(const std::string& otrPath) {
    return ResourceMgr_GetResourceByNameHandlingMQ(otrPath.c_str() + DeclaredActorType::kOtrPrefixLength);
}

uint32_t AssetType(const std::shared_ptr<Ship::IResource>& resource) {
    return resource != nullptr ? resource->GetInitData()->Type : 0;
}

// The resource at `path` if it is a skeleton the driver can animate, else nullptr: normal or flex, with standard or
// LOD limbs. Skin limbs (Epona's) have no display list where the skeleton drawer reads one.
std::shared_ptr<SOH::Skeleton> LoadSkeleton(const std::string& path) {
    auto resource = LoadAsset(path);
    if (AssetType(resource) != (uint32_t)SOH::ResourceType::SOH_Skeleton) {
        return nullptr;
    }
    auto skeleton = std::static_pointer_cast<SOH::Skeleton>(resource);
    bool supportedType = skeleton->type == SOH::SkeletonType::Normal || skeleton->type == SOH::SkeletonType::Flex;
    bool supportedLimbs = skeleton->limbType == SOH::LimbType::Standard || skeleton->limbType == SOH::LimbType::LOD;
    return supportedType && supportedLimbs ? skeleton : nullptr;
}

// The resource at `path` if it is a normal (not Link's) animation, else nullptr.
std::shared_ptr<SOH::Animation> LoadAnimation(const std::string& path) {
    auto resource = LoadAsset(path);
    if (AssetType(resource) != (uint32_t)SOH::ResourceType::SOH_Animation) {
        return nullptr;
    }
    auto animation = std::static_pointer_cast<SOH::Animation>(resource);
    return animation->type == SOH::AnimationType::Normal ? animation : nullptr;
}

bool IsDisplayList(const std::string& path) {
    return AssetType(LoadAsset(path)) == (uint32_t)Fast::ResourceType::DisplayList;
}

bool IsTexture(const std::string& path) {
    return AssetType(LoadAsset(path)) == (uint32_t)Fast::ResourceType::Texture;
}

// ---- pure rules ---------------------------------------------------------------------------------------------------

// A placement's message: its params when they name one, otherwise the type's default (0 = cannot talk).
u16 PlacementMessage(s16 params, const DeclaredActorType& type) {
    u16 fromParams = (u16)params;
    return fromParams != 0 && fromParams != 0xFFFF ? fromParams : type.message;
}

// A looping animation's playback rate. The skeleton code wraps the frame once per update, so a step longer than
// the animation would carry it out of the animation's data.
f32 LoopSpeed(f32 speed, f32 length) {
    return CLAMP(speed, -length, length);
}

// The shadow scale that draws `shadow` at the same size whatever the model's scale: vanilla scales a shadow by the
// actor's scale, and `shadow` is given at the scale of vanilla NPCs.
f32 ShadowScale(const DeclaredActorType& type) {
    return type.scale > 0.0f ? type.shadow * kVanillaNpcScale / type.scale : 0.0f;
}

// The height of the focus point above the actor's position when there is no head to put it on.
f32 FocusHeight(const DeclaredActorType& type) {
    return type.HasCollision() ? (f32)(type.yShift + type.height) : 0.0f;
}

// ---- init ---------------------------------------------------------------------------------------------------------

bool Fail(DeclaredActor* self, const char* what, const std::string& path) {
    SPDLOG_ERROR("[Unbound] actor type '{}': {} '{}'; this actor does not spawn", self->type->name, what,
                 path.substr(DeclaredActorType::kOtrPrefixLength));
    Actor_Kill(&self->actor);
    return false;
}

bool ResolveType(DeclaredActor* self) {
    self->type = SOH::Unbound::GetDeclaredActorType(self->actor.id);
    if (self->type == nullptr) {
        SPDLOG_ERROR("[Unbound] actor id {:#x} is not a declared actor type", self->actor.id);
        Actor_Kill(&self->actor);
        return false;
    }
    return true;
}

void InitShape(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    f32 shadowScale = ShadowScale(type);
    ActorShape_Init(&self->actor.shape, type.yOffset, shadowScale > 0.0f ? ActorShadow_DrawCircle : NULL, shadowScale);
    Actor_SetScale(&self->actor, type.scale);
}

// The ground the round shadow is drawn on: the shadow draws only over a known floor. The actor never moves, so one
// raycast at spawn serves, and it records the floor without moving the actor onto it.
void InitFloor(DeclaredActor* self, PlayState* play) {
    if (self->actor.shape.shadowDraw == NULL) {
        return;
    }
    Vec3f checkPos = self->actor.world.pos;
    checkPos.y += 50.0f; // as the vanilla floor check, so a floor at the actor's feet is found
    s32 bgId = BGCHECK_SCENE;
    self->actor.floorHeight =
        BgCheck_EntityRaycastFloor5(play, &play->colCtx, &self->actor.floorPoly, &bgId, &self->actor, &checkPos);
    self->actor.floorBgId = bgId;
}

// Vanilla's culling zone fits an NPC: a model reaching further from its origin widens it, and a draw distance moves
// its far edge.
void InitCulling(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    self->actor.uncullZoneScale = std::max(self->actor.uncullZoneScale, type.cullRadius);
    self->actor.uncullZoneDownward = std::max(self->actor.uncullZoneDownward, type.cullRadius);
    if (type.drawDistance > 0.0f) {
        self->actor.uncullZoneForward = type.drawDistance;
    }
}

bool InitAnimation(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    auto resource = LoadAnimation(type.animation);
    if (resource == nullptr) {
        return Fail(self, "no animation at", type.animation);
    }
    // One entry per limb plus the root position, as the skeleton's joint table: fewer would be read past the end.
    if (resource->rotationIndices.size() < self->skelAnime.limbCount) {
        return Fail(self, "an animation for fewer limbs than the skeleton at", type.animation);
    }
    AnimationHeader* animation = (AnimationHeader*)type.animation.c_str();
    f32 lastFrame = Animation_GetLastFrame(animation);
    if (type.holdFrame) {
        f32 frame = CLAMP(type.frame, 0.0f, lastFrame);
        Animation_Change(&self->skelAnime, animation, 0.0f, frame, frame, ANIMMODE_ONCE, 0.0f);
    } else {
        f32 speed = LoopSpeed(type.speed, Animation_GetLength(animation));
        Animation_Change(&self->skelAnime, animation, speed, 0.0f, lastFrame, ANIMMODE_LOOP, 0.0f);
    }
    self->hasAnimation = true;
    return true;
}

bool InitSkeleton(DeclaredActor* self, PlayState* play) {
    const DeclaredActorType& type = *self->type;
    auto skeleton = LoadSkeleton(type.skeleton);
    if (skeleton == nullptr) {
        return Fail(self, "no normal or flex skeleton with standard or LOD limbs at", type.skeleton);
    }
    if (skeleton->type == SOH::SkeletonType::Flex) {
        SkelAnime_InitFlex(play, &self->skelAnime, (FlexSkeletonHeader*)type.skeleton.c_str(), NULL, NULL, NULL, 0);
    } else {
        SkelAnime_Init(play, &self->skelAnime, (SkeletonHeader*)type.skeleton.c_str(), NULL, NULL, NULL, 0);
    }
    self->hasSkeleton = true;
    return InitAnimation(self);
}

// Every segment texture must load: an unresolved path would reach the renderer as raw bytes.
bool InitSegments(DeclaredActor* self) {
    for (const auto& [segment, texture] : self->type->segments) {
        if (!IsTexture(texture)) {
            return Fail(self, "no texture at", texture);
        }
    }
    return true;
}

bool InitMesh(DeclaredActor* self, PlayState* play) {
    if (!self->type->skeleton.empty()) {
        return InitSkeleton(self, play);
    }
    return IsDisplayList(self->type->displayList) || Fail(self, "no display list at", self->type->displayList);
}

bool InitModel(DeclaredActor* self, PlayState* play) {
    InitShape(self);
    InitFloor(self, play);
    InitCulling(self);
    return InitSegments(self) && InitMesh(self, play);
}

void InitCollision(DeclaredActor* self, PlayState* play) {
    const DeclaredActorType& type = *self->type;
    if (!type.HasCollision()) {
        return;
    }
    Collider_InitCylinder(play, &self->collider);
    Collider_SetCylinder(play, &self->collider, &self->actor, &sCylinderInit);
    self->collider.dim.radius = type.radius;
    self->collider.dim.height = type.height;
    self->collider.dim.yShift = type.yShift;
    self->actor.colChkInfo.mass = MASS_IMMOVABLE;
    self->actor.colChkInfo.cylRadius = type.radius;
    self->actor.colChkInfo.cylHeight = type.height;
    self->hasCollision = true;
}

void InitTalk(DeclaredActor* self) {
    u16 message = self->type->talks ? PlacementMessage(self->actor.params, *self->type) : 0;
    self->talkable = message != 0;
    if (self->talkable) {
        self->actor.textId = message;
        self->actor.targetMode = kTalkTargetMode;
    } else {
        // The type's flags make every placement targetable; this one has nothing to say.
        self->actor.flags &= ~(ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY);
    }
}

void InitLook(DeclaredActor* self) {
    const DeclaredActorType& type = *self->type;
    if (!type.looks || !self->hasSkeleton) {
        return;
    }
    // Limb-draw numbering: the root is 1 and the last limb is limbCount - 1 (jointTable[0] is the root position).
    if (type.limb < 1 || type.limb >= self->skelAnime.limbCount) {
        SPDLOG_ERROR("[Unbound] actor type '{}': look limb {} is not a limb of its skeleton (1-{}); the head will not "
                     "turn",
                     type.name, type.limb, self->skelAnime.limbCount - 1);
        return;
    }
    self->looking = true;
}

void InitFocus(DeclaredActor* self) {
    self->actor.focus.pos = self->actor.world.pos;
    self->actor.focus.pos.y += FocusHeight(*self->type);
}

// ---- update -------------------------------------------------------------------------------------------------------

// Whether the player is talking to this actor. Read from the Player every frame rather than latched: the actor does
// not update while culled, so a latch could miss the one frame the conversation closes on and stay set.
bool IsTalkingTo(DeclaredActor* self, PlayState* play) {
    Player* player = GET_PLAYER(play);
    return (player->stateFlags1 & PLAYER_STATE1_TALKING) && player->talkActor == &self->actor;
}

// A box that ends in an event waits for its actor to close it, as vanilla actors do in their talk state. With no
// behavior to run, the driver closes it when the player advances, so no message can hold the player in the textbox.
void CloseEventBox(PlayState* play) {
    if (Message_GetState(&play->msgCtx) == TEXT_STATE_EVENT && Message_ShouldAdvance(play)) {
        Message_CloseTextbox(play);
    }
}

void UpdateTalk(DeclaredActor* self, PlayState* play) {
    if (!self->talkable) {
        return;
    }
    self->talking = IsTalkingTo(self, play);
    if (self->talking) {
        CloseEventBox(play);
    } else if (!Actor_ProcessTalkRequest(&self->actor, play)) {
        func_8002F2CC(&self->actor, play, self->type->range); // offer to talk
    }
}

void UpdateLook(DeclaredActor* self, PlayState* play) {
    if (!self->looking) {
        return;
    }
    Player* player = GET_PLAYER(play);
    f32 range = self->type->lookRange;
    bool follow = self->talking || self->actor.xyzDistToPlayerSq < SQ(range);
    self->interactInfo.trackPos = player->actor.focus.pos;
    self->interactInfo.yOffset = self->actor.focus.pos.y - self->actor.world.pos.y;
    Npc_TrackPoint(&self->actor, &self->interactInfo, kLookTrackingPreset,
                   follow ? NPC_TRACKING_HEAD : NPC_TRACKING_NONE);
}

void UpdateCollision(DeclaredActor* self, PlayState* play) {
    if (!self->hasCollision) {
        return;
    }
    Collider_UpdateCylinder(&self->actor, &self->collider);
    CollisionCheck_SetOC(play, &play->colChkCtx, &self->collider.base);
}

void UpdateAnimation(DeclaredActor* self) {
    if (self->hasAnimation && !self->type->holdFrame) {
        SkelAnime_Update(&self->skelAnime);
    }
}

// ---- draw ---------------------------------------------------------------------------------------------------------

// Turns the head about the head limb's own axes, around the point `pivot` along its X axis: left and right about X,
// up and down about Z, as vanilla character rigs are built. The limb's transform is applied here and zeroed so the
// skeleton drawer applies nothing further.
void TurnHead(DeclaredActor* self, s32 limbIndex, Vec3f* pos, Vec3s* rot) {
    if (!self->looking || limbIndex != self->type->limb) {
        return;
    }
    f32 pivot = self->type->pivot;
    Matrix_TranslateRotateZYX(pos, rot);
    Matrix_Translate(pivot, 0.0f, 0.0f, MTXMODE_APPLY);
    Matrix_RotateX(BINANG_TO_RAD(self->interactInfo.headRot.y), MTXMODE_APPLY);
    Matrix_RotateZ(BINANG_TO_RAD(self->interactInfo.headRot.x), MTXMODE_APPLY);
    Matrix_Translate(-pivot, 0.0f, 0.0f, MTXMODE_APPLY);
    *pos = { 0.0f, 0.0f, 0.0f };
    *rot = { 0, 0, 0 };
}

void RecordHeadFocus(DeclaredActor* self, s32 limbIndex) {
    if (!self->looking || limbIndex != self->type->limb) {
        return;
    }
    Vec3f pivot = { self->type->pivot, 0.0f, 0.0f };
    Matrix_MultVec3f(&pivot, &self->actor.focus.pos);
}

void HideLimb(DeclaredActor* self, s32 limbIndex, Gfx** dList) {
    const std::vector<s32>& hidden = self->type->hideLimbs;
    if (std::find(hidden.begin(), hidden.end(), limbIndex) != hidden.end()) {
        *dList = NULL; // children still draw
    }
}

// The limb callbacks, shared by both passes; the per-pass signatures below only forward to them.
s32 OverrideLimb(void* arg, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot) {
    HideLimb((DeclaredActor*)arg, limbIndex, dList);
    TurnHead((DeclaredActor*)arg, limbIndex, pos, rot);
    return false;
}

s32 OverrideLimbOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg) {
    return OverrideLimb(arg, limbIndex, dList, pos, rot);
}

void PostLimbOpa(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    RecordHeadFocus((DeclaredActor*)arg, limbIndex);
}

s32 OverrideLimbXlu(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg, Gfx** gfx) {
    return OverrideLimb(arg, limbIndex, dList, pos, rot);
}

void PostLimbXlu(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg, Gfx** gfx) {
    RecordHeadFocus((DeclaredActor*)arg, limbIndex);
}

// The draw state vanilla character draws set before their skeleton. Every segment 8-12 the type does not name gets
// an empty display list: character models call one of them to set their render mode (vanilla binds
// &D_80116280[2], an end-of-list, there), and an unbound segment would be whatever the last actor left in it. The
// env colour is opaque black, which models that fade through env alpha read as fully visible.
Gfx* BindSegments(const DeclaredActorType& type, Gfx* gfx) {
    for (u8 segment = DeclaredActorType::kSegmentMin; segment <= DeclaredActorType::kSegmentMax; segment++) {
        gSPSegment(gfx++, segment, (uintptr_t)gEmptyDL);
    }
    for (const auto& [segment, texture] : type.segments) {
        gSPSegment(gfx++, segment, (uintptr_t)texture.c_str());
    }
    gDPSetEnvColor(gfx++, 0, 0, 0, 255);
    return gfx;
}

void DrawDisplayList(DeclaredActor* self, PlayState* play) {
    Gfx* dList = (Gfx*)self->type->displayList.c_str();
    if (self->type->translucent) {
        Gfx_DrawDListXlu(play, dList);
    } else {
        Gfx_DrawDListOpa(play, dList);
    }
}

} // namespace

// OPEN_DISPS declares the frame-interpolation hooks at block scope. Inside an anonymous namespace that declaration
// names a function of the namespace, which nothing defines, so the functions that open the display lists live
// outside it.
static void DrawSkeleton(DeclaredActor* self, PlayState* play) {
    SkelAnime* skel = &self->skelAnime;
    if (!self->type->translucent) {
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        SkelAnime_DrawSkeletonOpa(play, skel, OverrideLimbOpa, PostLimbOpa, self);
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Xlu(play->state.gfxCtx);
    if (skel->skeletonHeader->skeletonType == SKELANIME_TYPE_FLEX) {
        POLY_XLU_DISP = SkelAnime_DrawFlex(play, skel->skeleton, skel->jointTable, skel->dListCount, OverrideLimbXlu,
                                           PostLimbXlu, self, POLY_XLU_DISP);
    } else {
        POLY_XLU_DISP =
            SkelAnime_Draw(play, skel->skeleton, skel->jointTable, OverrideLimbXlu, PostLimbXlu, self, POLY_XLU_DISP);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawSegments(DeclaredActor* self, PlayState* play) {
    OPEN_DISPS(play->state.gfxCtx);
    if (self->type->translucent) {
        POLY_XLU_DISP = BindSegments(*self->type, POLY_XLU_DISP);
    } else {
        POLY_OPA_DISP = BindSegments(*self->type, POLY_OPA_DISP);
    }
    CLOSE_DISPS(play->state.gfxCtx);
}

namespace {

// ---- ActorDB functions --------------------------------------------------------------------------------------------

void DeclaredActor_Init(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    if (!ResolveType(self) || !InitModel(self, play)) {
        return;
    }
    InitCollision(self, play);
    InitTalk(self);
    InitLook(self);
    InitFocus(self);
}

void DeclaredActor_Destroy(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    if (self->hasSkeleton) {
        SkelAnime_Free(&self->skelAnime, play);
    }
    if (self->hasCollision) {
        Collider_DestroyCylinder(play, &self->collider);
    }
}

void DeclaredActor_Update(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    UpdateTalk(self, play);
    UpdateLook(self, play);
    UpdateCollision(self, play);
    UpdateAnimation(self);
}

void DeclaredActor_Draw(Actor* thisx, PlayState* play) {
    DeclaredActor* self = (DeclaredActor*)thisx;
    DrawSegments(self, play);
    if (self->hasSkeleton) {
        DrawSkeleton(self, play);
    } else {
        DrawDisplayList(self, play);
    }
}

} // namespace

ActorDBInit DeclaredActor_DBInit(const DeclaredActorType& type) {
    ActorDBInit init;
    init.name = type.name;
    init.desc = type.displayName;
    init.category = type.talks ? ACTORCAT_NPC : ACTORCAT_PROP;
    init.flags = type.talks ? (ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY) : 0;
    init.objectId = OBJECT_GAMEPLAY_KEEP;
    init.instanceSize = sizeof(DeclaredActor);
    init.init = DeclaredActor_Init;
    init.destroy = DeclaredActor_Destroy;
    init.update = DeclaredActor_Update;
    init.draw = DeclaredActor_Draw;
    return init;
}
