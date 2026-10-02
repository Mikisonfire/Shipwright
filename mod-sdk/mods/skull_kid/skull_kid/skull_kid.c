// Skull Kid form: Majora's Mask turns Link into Majora's Mask's Skull Kid.
// Link stays the base (LINK kind): his actions, collision and camera keep running. His own limbs are hidden and
// Skull Kid's MM skeleton is drawn on top, the same way garo.c draws its hybrid body. The skeleton and animations
// come from the player's own mm.o2r (see "requires" in manifest.json); nothing from MM is shipped in this package.
//
// STATUS: first draft, written against garo.c and the SDK docs. Not compiled into a game yet; the numbers marked
// "tune" need in-game adjustment.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "z64items.h"

#include "align_asset_macro.h"
#include "functions.h"
#include "macros.h"
#include "variables.h"
#include "soh/ResourceManagerHelpers.h"

#define SKJ_MASK_KEY "nei.skull_kid_mask"
#define SKJ_FORM_KEY "nei.skull_kid"
// Extra pause page 2, first cell: page 1 is shared with the other MM masks.
#define SKJ_MASK_PAGE 2
#define SKJ_MASK_SLOT 0

#define BG_ON_GROUND 1

// Skull Kid's skeleton has 21 limbs, so 22 joints (joint 0 is the root translation).
#define SKJ_MAX_JOINTS 32

// tune: how the body is drawn
#define SKJ_SCALE 1.0f          // 1.0 = the size MM draws him at (actor scale 0.01)
// Measured from MM's float animation: the feet hang 16.4 to 20.7 units below the origin; the sole is 2.1 below the foot joint.
#define SKJ_FLOAT_LIFT 22.0f    // origin height so the feet clear the ground while floating
#define SKJ_BOB_HEIGHT 0.0f     // the float animation already bobs him by about 3.7 units
#define SKJ_BOB_SPEED 0.12f     // radians per frame
// tune: gameplay
#define SKJ_MOTION_SCALE 1.15f  // a little quicker than Link
#define SKJ_WALK_MIN_SPEED 1.0f // below this he floats instead of walking
#define SKJ_WALK_SPEED_DIV 3.0f // walk animation rate = speed / this
#define SKJ_HOVER_FRAMES 90     // frames of slow fall per jump while A is held
#define SKJ_HOVER_FALL_CAP 1.2f // maximum fall speed while hovering
#define SKJ_HEIGHT 52.0f        // camera and targeting height (Link child is about 44, adult about 68)

static const ALIGN_ASSET(2) char sSkelPath[] = "__OTR__objects/object_stk/gSkullKidSkel";
static const ALIGN_ASSET(2) char sWalkPath[] = "__OTR__objects/object_stk/gSkullKidWalkAnim";
static const ALIGN_ASSET(2) char sFloatPath[] = "__OTR__objects/object_stk2/gSkullKidFloatingArmsCrossedAnim";
// Attack poses: all of them are Skull Kid's own MM animations (no flute, no ocarina).
static const ALIGN_ASSET(2) char sSpankPath[] = "__OTR__objects/object_stk/gSkullKidSpankAnim";
static const ALIGN_ASSET(2) char sHipShakePath[] = "__OTR__objects/object_stk/gSkullKidHipShakeAndJumpAnim";
static const ALIGN_ASSET(2) char sCallMoonPath[] = "__OTR__objects/object_stk/gSkullKidCallDownMoonStartAnim";
static const ALIGN_ASSET(2) char sCartwheelPath[] = "__OTR__objects/object_stk2/gSkullKidCartwheelAnim";
static const ALIGN_ASSET(2) char sKickPath[] = "__OTR__objects/object_stk2/gSkullKidKickOverLinkAnim";
static const ALIGN_ASSET(2) char sSmackPath[] = "__OTR__objects/object_stk2/gSkullKidSmackFairyStartAnim";
static const ALIGN_ASSET(2) char sDeflectPath[] = "__OTR__objects/object_stk2/gSkullKidDeflectAttackAnim";
static const ALIGN_ASSET(2) char sCurseStartPath[] = "__OTR__objects/object_stk2/gSkullKidCurseStartAnim";
static const ALIGN_ASSET(2) char sCurseLoopPath[] = "__OTR__objects/object_stk2/gSkullKidCurseLoopAnim";
static const ALIGN_ASSET(2) char sScreamPath[] = "__OTR__objects/object_stk2/gSkullKidHoldHeadAndScreamStartAnim";
static const ALIGN_ASSET(2) char sBubblePath[] = "__OTR__objects/object_stk2/gSkullKidHitByBubbleAnim";
static const ALIGN_ASSET(2) char sTurnPath[] = "__OTR__objects/object_stk2/gSkullKidFloatingTurnAroundAnim";

// Icon: assets/textures/skull_kid/gItemIconMajorasMaskTex.rgba32.png (32x32). The one shipped is a plain placeholder: replace the file with your own icon, same name.
// The mask on Link's face during the transformation is vanilla OoT's Skull Mask.
static const ALIGN_ASSET(2) char sMaskIconTex[] = "__OTR__textures/skull_kid/gItemIconMajorasMaskTex";
static const ALIGN_ASSET(2) char sMaskNameTex[] = "__OTR__textures/skull_kid/gMajorasMaskNameTex";
static const ALIGN_ASSET(2) char sMaskFaceDL[] = "__OTR__objects/object_link_child/gLinkChildSkullMaskDL";

// Majora's Mask as Skull Kid wears it: three display lists from MM's object_stk, drawn on his head limb.
static const ALIGN_ASSET(2) char sMajoraMask1DL[] = "__OTR__objects/object_stk/gSkullKidMajorasMask1DL";
static const ALIGN_ASSET(2) char sMajoraMask2DL[] = "__OTR__objects/object_stk/gSkullKidMajorasMask2DL";
static const ALIGN_ASSET(2) char sNormalHeadDL[] = "__OTR__objects/object_stk/gSkullKidNormalHeadDL";
static const ALIGN_ASSET(2) char sNormalEyesDL[] = "__OTR__objects/object_stk/gSkullKidNormalEyesDL";
static const ALIGN_ASSET(2) char sMajoraMaskEyesDL[] = "__OTR__objects/object_stk/gSkullKidMajorasMaskEyesDL";

// SkelAnime limb indices are 1-based: object_stk's head is limb 16, so 17 here.
#define SKJ_LIMB_HEAD 17
// tune: where the mask sits relative to the head limb (limb units, before the 0.01 actor scale), and its size.
#define SKJ_MASK_OFFSET_X 0.0f
#define SKJ_MASK_OFFSET_Y 0.0f
#define SKJ_MASK_OFFSET_Z 0.0f
#define SKJ_MASK_SCALE 1.0f // MM draws the mask at its own size on the head matrix

// TEST SWITCH (v0.5): the cloth (fringe, skirt, legs) rendered flat black in v0.4. Press R while Skull Kid to cycle
// the lighting used for his body: 0 = the game's own lights, 1 = full bright, 2 = medium bright.
#define SKJ_LIGHT_MODES 3
#define SKJ_LIGHT_MODE_START 1
// MM has two versions of the mask (Mask1 and Mask2, same size, different vertex data). Drawing both at once put two
// masks on him in v0.3. 0 = draw Mask1 only, 1 = draw Mask2 only. If the mask looks wrong, flip this.
#define SKJ_MASK_USE_SECOND 0

// ---- moveset (v0.7) ----
// Skull Kid's Hyrule Warriors Ocarina moveset, rebuilt for Unbound. No flute is played or shown.
//   B            weak attack, a 7-hit string
//   R            strong attack; which one depends on how many weak hits came first (0 = C1 ... 5+ = C6)
//   R while B    special attack (recharges)
//   L            tests the body lighting (3 modes), see SKJ_LIGHT_MODE_START
//   C1 puppet that holds enemies   C2 upward punch   C3 long laser   C4 ring of dark spheres
//   C5 tornado, then a moon tear   C6 Tatl and Tael pull enemies in, then an explosion
#define SKJ_WEAK_HITS 7
#define SKJ_CHAIN_WINDOW 34             // frames after a weak hit to keep the string going
#define SKJ_SPECIAL_COOLDOWN 900        // frames until the special can be used again
#define SKJ_DMG_FLAGS 0x00000100
#define SKJ_FLAME_BLUE 2                // En_Light params: colour of the glow
#define SKJ_FLAME_ORANGE 0
#define SKJ_MAX_SHOTS 12
#define SKJ_ORBS 5
#define SKJ_ZONES 3
#define SKJ_PUPPET_LIFE 160
#define SKJ_PUPPET_RADIUS 120.0f
#define SKJ_TWO_PI 6.2831853f

// ---- feel (v0.8) ----
#define SKJ_BLEND_FRAMES 6.0f           // frames spent blending from one animation into the next
#define SKJ_ROLL_TICKS 18               // how long the roll pose lasts after Link starts rolling
#define SKJ_HOP_TICKS 12                // side hop and backflip
#define SKJ_HURT_TICKS 14               // flinch when hurt
#define SKJ_LEAN_MAX 0.16f              // radians of forward lean at full run
#define SKJ_LEAN_STEP 0.02f             // how fast the lean follows
#define SKJ_CHAIN_FROM 60               // percent of a weak hit after which the next press chains at once
#define SKJ_QUEUE_FROM 25               // percent of a hit after which a press is remembered
#define SKJ_FACE_TURN 0x1800            // how fast he turns toward the target when he attacks

static const char* const sRequiredHooks[] = { "OnPlayerActionHandler", "OnPlayerResolveLimbDraw", "OnSceneInit",
                                              "OnPlayerResolveAnim", "OnPlayerResolveAnimSite" };
static const SOHModRequirements sRequirements = { sizeof(SOHModRequirements), sRequiredHooks,
                                                  ARRAY_COUNT(sRequiredHooks) };

typedef enum {
    SKJ_ANIM_NONE,
    SKJ_ANIM_FLOAT,
    SKJ_ANIM_WALK,
    SKJ_ANIM_ATTACK,
} SkjAnim;

static const SOHModApi* sApi;

static SkelAnime sSkelAnime;
static Vec3s sJoints[SKJ_MAX_JOINTS];
static Vec3s sMorph[SKJ_MAX_JOINTS];
static bool sIsBodyReady;
static SkjAnim sAnim;
static f32 sBobPhase;
static s16 sHoverFramesLeft;
static bool sIsHovering;
static const char* sAnimPath;
static const char* sAttackPath;
static f32 sAttackRate;
static s16 sAttackTimer;
static bool sRestartAnim;
static s16 sRollTicks;
static s16 sHopTicks;
static s32 sHopDir;
static s16 sHurtTicks;
static f32 sLean;
static bool sIsQueuedB;
static bool sIsQueuedR;
static s32 sLightMode = SKJ_LIGHT_MODE_START;

static bool IsSkullKid(void) {
    return sApi != NULL && sApi->IsFormActive(SKJ_FORM_KEY);
}

static bool IsOnGround(Player* player) {
    return (player->actor.bgCheckFlags & BG_ON_GROUND) != 0;
}

// ---- body ----

static bool PrepareBody(PlayState* play) {
    if (sIsBodyReady) {
        return true;
    }
    SkeletonHeader* header = ResourceMgr_LoadSkeletonByName(sSkelPath, NULL);
    if (header == NULL || header->limbCount + 1 > SKJ_MAX_JOINTS) {
        return false;
    }
    SkelAnime_InitFlex(play, &sSkelAnime, (FlexSkeletonHeader*)sSkelPath, (AnimationHeader*)sFloatPath, sJoints,
                       sMorph, header->limbCount + 1);
    Animation_PlayLoop(&sSkelAnime, (AnimationHeader*)sFloatPath);
    sAnim = SKJ_ANIM_FLOAT;
    sAnimPath = sFloatPath;
    sIsBodyReady = true;
    return true;
}

static void SetAnim(SkjAnim anim, const char* path, f32 rate, bool isLoop) {
    if (path != sAnimPath || sRestartAnim) {
        // Blend from the pose he is in now, so changing animation never snaps.
        Animation_Change(&sSkelAnime, (AnimationHeader*)path, rate, 0.0f, Animation_GetLastFrame((void*)path),
                         isLoop ? ANIMMODE_LOOP : ANIMMODE_ONCE, SKJ_BLEND_FRAMES);
        sAnimPath = path;
        sRestartAnim = false;
    }
    sAnim = anim;
    sSkelAnime.playSpeed = rate;
}

static bool IsFloating(void) {
    return sAnim == SKJ_ANIM_FLOAT;
}

// Picks the animation from what Link's own engine is doing: his roll, hops and flinches arrive through the animation
// hooks, so Skull Kid moves like Link does, only with his own poses.
static void ChooseAnim(Player* player) {
    if (sAttackTimer > 0) {
        SetAnim(SKJ_ANIM_ATTACK, sAttackPath, sAttackRate, false);
    } else if (sHurtTicks > 0) {
        SetAnim(SKJ_ANIM_ATTACK, sBubblePath, 18.0f / SKJ_HURT_TICKS, false);
    } else if (sRollTicks > 0) {
        SetAnim(SKJ_ANIM_ATTACK, sCartwheelPath, 40.0f / SKJ_ROLL_TICKS, false);
    } else if (sHopTicks > 0) {
        if (sHopDir == PLAYER_STICK_DIR_BACKWARD) {
            SetAnim(SKJ_ANIM_ATTACK, sHipShakePath, 47.0f / SKJ_HOP_TICKS, false);
        } else {
            SetAnim(SKJ_ANIM_ATTACK, sDeflectPath, 26.0f / SKJ_HOP_TICKS, false);
        }
    } else if (IsOnGround(player) && player->linearVelocity > SKJ_WALK_MIN_SPEED) {
        SetAnim(SKJ_ANIM_WALK, sWalkPath, player->linearVelocity / SKJ_WALK_SPEED_DIV, true);
    } else {
        SetAnim(SKJ_ANIM_FLOAT, sFloatPath, 1.0f, true);
    }
}

// The animation hooks only listen; Link keeps playing his own animation underneath.
static void NoteLinkAnim(int32_t group, int32_t animType, LinkAnimationHeader** anim) {
    (void)animType;
    (void)anim;
    if (IsSkullKid() && group == PLAYER_ANIMGROUP_landing_roll) {
        sRollTicks = SKJ_ROLL_TICKS;
    }
}

static void NoteLinkAnimSite(int32_t site, int32_t index, LinkAnimationHeader** anim) {
    (void)anim;
    if (!IsSkullKid()) {
        return;
    }
    if (site == SOH_PLAYER_ANIM_SITE_HOP && (index % 3) == 0) {
        sHopTicks = SKJ_HOP_TICKS;
        sHopDir = index / 3;
    } else if (site == SOH_PLAYER_ANIM_SITE_DAMAGE) {
        sHurtTicks = SKJ_HURT_TICKS;
    }
}

// While the body draws, remember where the head is. The mask is drawn after the whole body: the mask's display lists
// leave their combiner settings on the pipe, and drawing it in the middle blacked out every limb after the head
// (hat, fringe) in v0.4.
static MtxF sHeadMtx;
static bool sHasHeadMtx;

// Like MM's DmStk: the skeleton's own head display list is skipped, and the head is drawn from the head limb's matrix.
static s32 OverrideSkullKidLimb(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* arg) {
    (void)play;
    (void)pos;
    (void)rot;
    (void)arg;
    if (limbIndex == SKJ_LIMB_HEAD) {
        *dList = NULL;
    }
    return false;
}

static void PostSkullKidLimb(PlayState* play, s32 limbIndex, Gfx** dList, Vec3s* rot, void* arg) {
    (void)dList;
    (void)rot;
    (void)arg;
    if (limbIndex == SKJ_LIMB_HEAD) {
        Matrix_Get(&sHeadMtx);
        sHasHeadMtx = true;
        OPEN_DISPS(play->state.gfxCtx);
        gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sNormalHeadDL);
        gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sNormalEyesDL);
        CLOSE_DISPS(play->state.gfxCtx);
    }
}

// Put Majora's Mask on the head position remembered above.
static void DrawMask(PlayState* play) {
    if (!sHasHeadMtx) {
        return;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    Matrix_Push();
    Matrix_Put(&sHeadMtx);
    Matrix_Translate(SKJ_MASK_OFFSET_X, SKJ_MASK_OFFSET_Y, SKJ_MASK_OFFSET_Z, MTXMODE_APPLY);
    Matrix_Scale(SKJ_MASK_SCALE, SKJ_MASK_SCALE, SKJ_MASK_SCALE, MTXMODE_APPLY);
    gSPMatrix(POLY_OPA_DISP++, MATRIX_NEWMTX(play->state.gfxCtx), G_MTX_NOPUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
#if SKJ_MASK_USE_SECOND
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sMajoraMask2DL);
#else
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sMajoraMask1DL);
#endif
    gSPDisplayList(POLY_OPA_DISP++, (Gfx*)sMajoraMaskEyesDL);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

static void DrawBody(PlayState* play, Player* player) {
    if (!sIsBodyReady) {
        return;
    }
    f32 lift = 0.0f;
    if (IsFloating()) {
        lift = SKJ_FLOAT_LIFT + sinf(sBobPhase) * SKJ_BOB_HEIGHT;
    }
    OPEN_DISPS(play->state.gfxCtx);
    Gfx_SetupDL_25Opa(play->state.gfxCtx);
    // MM-format display lists index the backface-cull list through segment 0x0C.
    gSPSegment(POLY_OPA_DISP++, 0x0C, (uintptr_t)gCullBackDList);
    if (sLightMode == 1) {
        Lights_NewAndDraw(play->state.gfxCtx, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0);
    } else if (sLightMode == 2) {
        Lights_NewAndDraw(play->state.gfxCtx, 140, 140, 140, 0, 0, 0, 0, 0, 0, 0);
    }
    Matrix_Push();
    Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y + lift, player->actor.world.pos.z,
                     MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y), MTXMODE_APPLY);
    Matrix_RotateX(sLean, MTXMODE_APPLY);
    Matrix_Scale(player->actor.scale.x * SKJ_SCALE, player->actor.scale.y * SKJ_SCALE, player->actor.scale.z * SKJ_SCALE,
                 MTXMODE_APPLY);
    sHasHeadMtx = false;
    // MM sets this before drawing him; his cutout materials read the environment alpha, so a leftover 0 hides the body.
    gDPPipeSync(POLY_OPA_DISP++);
    gDPSetEnvColor(POLY_OPA_DISP++, 255, 255, 255, 255);
    SkelAnime_DrawFlexOpa(play, sSkelAnime.skeleton, sSkelAnime.jointTable, sSkelAnime.dListCount,
                          OverrideSkullKidLimb, PostSkullKidLimb, player);
    DrawMask(play);
    Matrix_Pop();
    CLOSE_DISPS(play->state.gfxCtx);
}

// Link's own limbs stay hidden, but their post-draw still runs: feet, hands and focus keep their positions.
static void HideLinkBody(Player* player, int32_t limbIndex, Gfx** dList, Gfx* limbDList, Vec3f* pos) {
    (void)player;
    (void)limbIndex;
    (void)limbDList;
    (void)pos;
    if (IsSkullKid() && sIsBodyReady) {
        *dList = NULL;
    }
}

// ---- hover ----

// Hold A in the air to fall slowly, for a limited time per jump.
static void UpdateHover(PlayState* play, Player* player) {
    bool isAHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_A);

    if (IsOnGround(player)) {
        sHoverFramesLeft = SKJ_HOVER_FRAMES;
        sIsHovering = false;
        return;
    }
    sIsHovering = false;
    if (isAHeld && sHoverFramesLeft > 0 && player->actor.velocity.y < -SKJ_HOVER_FALL_CAP &&
        !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_CLIMBING_LEDGE | PLAYER_STATE1_ON_HORSE))) {
        player->actor.velocity.y = -SKJ_HOVER_FALL_CAP;
        sHoverFramesLeft--;
        sIsHovering = true;
    }
}

// ---- moves ----

typedef struct {
    const char* path;
    f32 frames;
    s16 lock;
} SkjMoveAnim;

static const SkjMoveAnim sWeakAnims[SKJ_WEAK_HITS] = {
    { sSpankPath, 16.0f, 11 },     { sCartwheelPath, 40.0f, 11 }, { sDeflectPath, 26.0f, 11 },
    { sSpankPath, 16.0f, 11 },     { sSmackPath, 39.0f, 11 },     { sCurseStartPath, 15.0f, 11 },
    { sHipShakePath, 47.0f, 18 },
};

typedef enum {
    SKJ_MOVE_NONE,
    SKJ_MOVE_WEAK,
    SKJ_MOVE_C1,
    SKJ_MOVE_C2,
    SKJ_MOVE_C3,
    SKJ_MOVE_C4,
    SKJ_MOVE_C5,
    SKJ_MOVE_C6,
    SKJ_MOVE_SPECIAL,
} SkjMove;

static const SkjMoveAnim sStrongAnims[6] = {
    { sDeflectPath, 26.0f, 30 }, // C1 puppet
    { sSmackPath, 39.0f, 22 },   // C2 upward punch
    { sCurseLoopPath, 40.0f, 34 }, // C3 laser
    { sScreamPath, 32.0f, 44 },  // C4 dark spheres
    { sCallMoonPath, 50.0f, 60 }, // C5 tornado and moon tear
    { sTurnPath, 40.0f, 84 },    // C6 Tatl and Tael
};
static const SkjMoveAnim sSpecialAnim = { sKickPath, 110.0f, 108 };

typedef struct {
    Actor* actor;
    u8 category;
    s8 color;
} SkjOrb;

typedef struct {
    bool isActive;
    bool isPierce;
    Vec3f pos;
    Vec3f step;
    f32 travelled;
    f32 range;
    s16 radius;
    u8 damage;
    f32 scale;
    s8 color;
    SkjOrb orb;
    bool isColliderReady;
    ColliderCylinder collider;
} SkjShot;

static ColliderCylinderInit sShotInit = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_TYPE_PLAYER, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2,
      { SKJ_DMG_FLAGS, 0x00, 1 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_NEAREST | TOUCH_SFX_NORMAL,
      BUMP_NONE,
      OCELEM_NONE },
    { 30, 60, 0, { 0, 0, 0 } },
};

static SkjShot sShots[SKJ_MAX_SHOTS];
static SkjOrb sOrbs[SKJ_ORBS]; // 0 puppet, 1 tornado and moon tear, 2 Tatl, 3 Tael, 4 explosion
static ColliderCylinder sZones[SKJ_ZONES];
static bool sIsZoneReady[SKJ_ZONES];

static s16 sMove;
static s16 sMoveFrame;
static s16 sMoveLength;
static s16 sWeakCount;
static s16 sChainTimer;
static s16 sSpecialCooldown;
static s16 sStrongIndex;

static s16 sPuppetLife;
static Vec3f sPuppetPos;
static s16 sMoonPhase; // 0 none, 1 tornado, 2 falling, 3 impact
static s16 sMoonFrame;
static Vec3f sMoonPos;
static s16 sSpinFrame;
static s16 sBlastFrames;
static Vec3f sBlastPos;
static s16 sBlastRadius;
static u8 sBlastDamage;

// Actor_Kill only clears update(); the actor is freed at the end of the frame, so a saved pointer can dangle.
// Walking the list is what makes it safe to follow between frames.
static Actor* ResolveOrb(PlayState* play, SkjOrb* orb) {
    if (orb->actor == NULL) {
        return NULL;
    }
    for (Actor* it = play->actorCtx.actorLists[orb->category].head; it != NULL; it = it->next) {
        if (it == orb->actor) {
            return it->update != NULL ? it : NULL;
        }
    }
    orb->actor = NULL;
    return NULL;
}

static void HideOrb(PlayState* play, SkjOrb* orb) {
    Actor* actor = ResolveOrb(play, orb);

    if (actor != NULL) {
        Actor_Kill(actor);
    }
    orb->actor = NULL;
}

// The glow of a magic attack: a light actor that follows 'pos'. Colour is baked into its params, so a colour
// change replaces the actor.
static void ShowOrb(PlayState* play, SkjOrb* orb, Vec3f* pos, f32 scale, s8 color) {
    Actor* actor = ResolveOrb(play, orb);

    if (actor != NULL && orb->color != color) {
        HideOrb(play, orb);
        actor = NULL;
    }
    if (actor == NULL) {
        actor = Actor_Spawn(&play->actorCtx, play, ACTOR_EN_LIGHT, pos->x, pos->y, pos->z, 0, 0, 0x4000, color);
        orb->actor = actor;
        orb->color = color;
        if (actor != NULL) {
            orb->category = actor->category;
        }
    }
    if (actor != NULL) {
        actor->world.pos = *pos;
        Actor_SetScale(actor, scale);
    }
}

static void KillAllEffects(PlayState* play) {
    for (s32 i = 0; i < SKJ_MAX_SHOTS; i++) {
        sShots[i].collider.base.atFlags &= ~(AT_ON | AT_HIT);
        sShots[i].isActive = false;
        HideOrb(play, &sShots[i].orb);
    }
    for (s32 i = 0; i < SKJ_ORBS; i++) {
        HideOrb(play, &sOrbs[i]);
    }
    sPuppetLife = 0;
    sMoonPhase = 0;
    sSpinFrame = 0;
    sBlastFrames = 0;
}

static void ForgetEffects(void) {
    // A scene change frees every actor, so the saved pointers are just dropped.
    for (s32 i = 0; i < SKJ_MAX_SHOTS; i++) {
        sShots[i].isActive = false;
        sShots[i].orb.actor = NULL;
        sShots[i].isColliderReady = false;
    }
    for (s32 i = 0; i < SKJ_ORBS; i++) {
        sOrbs[i].actor = NULL;
    }
    for (s32 i = 0; i < SKJ_ZONES; i++) {
        sIsZoneReady[i] = false;
    }
    sPuppetLife = 0;
    sMoonPhase = 0;
    sSpinFrame = 0;
    sBlastFrames = 0;
}

// A hit zone that lasts for the frame it is submitted: a cylinder that hurts what it touches.
static void SubmitZone(PlayState* play, Player* player, s32 slot, Vec3f* pos, s16 radius, s16 height, s16 yShift,
                       u8 damage) {
    ColliderCylinder* zone = &sZones[slot];

    if (!sIsZoneReady[slot]) {
        Collider_InitCylinder(play, zone);
        Collider_SetCylinder(play, zone, &player->actor, &sShotInit);
        sIsZoneReady[slot] = true;
    }
    zone->dim.radius = radius;
    zone->dim.height = height;
    zone->dim.yShift = yShift;
    zone->info.toucher.damage = damage;
    zone->base.atFlags |= AT_ON;
    zone->dim.pos.x = (s16)pos->x;
    zone->dim.pos.y = (s16)pos->y;
    zone->dim.pos.z = (s16)pos->z;
    CollisionCheck_SetAT(play, &play->colChkCtx, &zone->base);
}

static void PointAhead(Player* player, f32 distance, Vec3f* out) {
    *out = player->actor.world.pos;
    out->x += Math_SinS(player->actor.shape.rot.y) * distance;
    out->z += Math_CosS(player->actor.shape.rot.y) * distance;
}

// ---- enemies ----

static void PullFoes(PlayState* play, Vec3f* center, f32 radius, f32 step) {
    for (Actor* it = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; it != NULL; it = it->next) {
        if (it->update == NULL) {
            continue;
        }
        Vec3f toCenter;
        f32 distance = Math_Vec3f_DistXYZAndStoreDiff(center, &it->world.pos, &toCenter);
        if (distance > radius || distance < 1.0f) {
            continue;
        }
        f32 scale = step / distance;
        it->world.pos.x += toCenter.x * scale;
        it->world.pos.z += toCenter.z * scale;
    }
}

// An actor does not update while its freeze timer is set, which is what holds an enemy in place.
static void HoldFoes(PlayState* play, Vec3f* center, f32 radius) {
    for (Actor* it = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; it != NULL; it = it->next) {
        if (it->update != NULL && Math_Vec3f_DistXYZ(center, &it->world.pos) <= radius) {
            it->freezeTimer = 2;
        }
    }
}

static void LaunchFoes(PlayState* play, Vec3f* center, f32 radius, f32 speedY) {
    for (Actor* it = play->actorCtx.actorLists[ACTORCAT_ENEMY].head; it != NULL; it = it->next) {
        if (it->update != NULL && Math_Vec3f_DistXYZ(center, &it->world.pos) <= radius) {
            it->velocity.y = speedY;
        }
    }
}

// ---- shots (laser, dark spheres) ----

// Straight at what he has targeted; otherwise where he faces, turned by 'yawOffset'.
static void AimShot(Player* player, SkjShot* shot, f32 speed, s16 yawOffset) {
    Actor* target = player->focusActor;
    bool isLockedOnFoe = target != NULL && target->update != NULL &&
                         (target->category == ACTORCAT_ENEMY || target->category == ACTORCAT_BOSS);

    if (isLockedOnFoe && yawOffset == 0) {
        Vec3f toFoe;
        f32 distance = Math_Vec3f_DistXYZAndStoreDiff(&target->focus.pos, &shot->pos, &toFoe);
        if (distance > 1.0f) {
            f32 scale = speed / distance;
            shot->step.x = toFoe.x * scale;
            shot->step.y = toFoe.y * scale;
            shot->step.z = toFoe.z * scale;
            return;
        }
    }
    s16 yaw = player->actor.shape.rot.y + yawOffset;
    shot->step.x = Math_SinS(yaw) * speed;
    shot->step.y = 0.0f;
    shot->step.z = Math_CosS(yaw) * speed;
}

static void LaunchShot(PlayState* play, Player* player, s16 yawOffset, f32 speed, f32 range, s16 radius, u8 damage,
                       f32 scale, s8 color, bool isPierce) {
    SkjShot* shot = NULL;

    for (s32 i = 0; i < SKJ_MAX_SHOTS; i++) {
        if (!sShots[i].isActive) {
            shot = &sShots[i];
            break;
        }
    }
    if (shot == NULL) {
        return;
    }
    shot->pos = player->actor.world.pos;
    shot->pos.y += SKJ_HEIGHT * 0.6f;
    shot->pos.x += Math_SinS(player->actor.shape.rot.y) * 14.0f;
    shot->pos.z += Math_CosS(player->actor.shape.rot.y) * 14.0f;
    AimShot(player, shot, speed, yawOffset);
    shot->travelled = 0.0f;
    shot->range = range;
    shot->radius = radius;
    shot->damage = damage;
    shot->scale = scale;
    shot->color = color;
    shot->isPierce = isPierce;
    shot->isActive = true;
    HideOrb(play, &shot->orb);
    ShowOrb(play, &shot->orb, &shot->pos, scale, color);
}

static void UpdateShots(PlayState* play, Player* player) {
    for (s32 i = 0; i < SKJ_MAX_SHOTS; i++) {
        SkjShot* shot = &sShots[i];

        if (!shot->isActive) {
            continue;
        }
        shot->pos.x += shot->step.x;
        shot->pos.y += shot->step.y;
        shot->pos.z += shot->step.z;
        shot->travelled += sqrtf(SQ(shot->step.x) + SQ(shot->step.y) + SQ(shot->step.z));
        ShowOrb(play, &shot->orb, &shot->pos, shot->scale, shot->color);

        if (!shot->isColliderReady) {
            Collider_InitCylinder(play, &shot->collider);
            Collider_SetCylinder(play, &shot->collider, &player->actor, &sShotInit);
            shot->isColliderReady = true;
        }
        shot->collider.dim.radius = shot->radius;
        shot->collider.dim.height = shot->radius * 2;
        shot->collider.dim.yShift = -shot->radius;
        shot->collider.info.toucher.damage = shot->damage;
        shot->collider.base.atFlags |= AT_ON;
        shot->collider.dim.pos.x = (s16)shot->pos.x;
        shot->collider.dim.pos.y = (s16)shot->pos.y;
        shot->collider.dim.pos.z = (s16)shot->pos.z;
        CollisionCheck_SetAT(play, &play->colChkCtx, &shot->collider.base);

        bool isHit = (shot->collider.base.atFlags & AT_HIT) != 0;
        if (isHit) {
            shot->collider.base.atFlags &= ~AT_HIT;
        }
        if (shot->travelled >= shot->range || (isHit && !shot->isPierce)) {
            shot->collider.base.atFlags &= ~(AT_ON | AT_HIT);
            shot->isActive = false;
            HideOrb(play, &shot->orb);
        }
    }
}

// ---- starting moves ----

static void StartBlast(Vec3f* pos, s16 radius, u8 damage) {
    sBlastPos = *pos;
    sBlastRadius = radius;
    sBlastDamage = damage;
    sBlastFrames = 6;
}

// OoT lock-on: when he has a target he turns to face it as the attack starts, then steps into the hit.
static void FaceTarget(Player* player) {
    Actor* target = player->focusActor;

    if (target != NULL && target->update != NULL) {
        s16 yaw = Math_Vec3f_Yaw(&player->actor.world.pos, &target->world.pos);

        player->actor.shape.rot.y = yaw;
        player->yaw = yaw;
    }
}

static void BeginMove(Player* player, SkjMove move, const SkjMoveAnim* anim, bool isStrong, f32 lunge) {
    sMove = move;
    sMoveFrame = 0;
    sMoveLength = anim->lock;
    sAttackPath = anim->path;
    sAttackRate = anim->frames / (f32)anim->lock;
    sAttackTimer = anim->lock;
    sRestartAnim = true;
    sIsQueuedB = false;
    sIsQueuedR = false;
    FaceTarget(player);
    player->linearVelocity = lunge;
    if (isStrong) {
        Player_PlayVoiceSfx(player, NA_SE_VO_LI_MAGIC_ATTACK);
    } else {
        Player_PlayVoiceSfx(player, NA_SE_VO_LI_SWORD_N);
    }
}

static void StartWeak(Player* player) {
    s32 index = sWeakCount % SKJ_WEAK_HITS;

    BeginMove(player, SKJ_MOVE_WEAK, &sWeakAnims[index], false, index < 4 ? 5.0f : (index < 6 ? 8.0f : 11.0f));
    sWeakCount++;
    sChainTimer = sWeakAnims[index].lock + SKJ_CHAIN_WINDOW;
}

static void StartStrong(Player* player) {
    s32 index = sWeakCount > 5 ? 5 : sWeakCount;

    sStrongIndex = index;
    BeginMove(player, (SkjMove)(SKJ_MOVE_C1 + index), &sStrongAnims[index], true, index == 1 ? 8.0f : 0.0f);
    sWeakCount = 0;
    sChainTimer = 0;
}

static void StartSpecial(Player* player) {
    BeginMove(player, SKJ_MOVE_SPECIAL, &sSpecialAnim, true, 0.0f);
    sSpecialCooldown = SKJ_SPECIAL_COOLDOWN;
    sWeakCount = 0;
    sChainTimer = 0;
}

// ---- the moves while they play ----

// Weak string: a short hit zone in front of him. Hits 5 and 6 reach further, the last one hits hardest.
static void RunWeak(PlayState* play, Player* player) {
    s32 index = (sWeakCount - 1) % SKJ_WEAK_HITS;
    bool isLong = index == 4 || index == 5;
    u8 damage = index < 4 ? 1 : (index < 6 ? 2 : 3);

    if (sMoveFrame >= 2 && sMoveFrame <= 7) {
        Vec3f at;
        PointAhead(player, isLong ? 62.0f : 38.0f, &at);
        SubmitZone(play, player, 0, &at, isLong ? 52 : 34, 80, -10, damage);
    }
}

// C1: a puppet is summoned in front of him and holds the enemies around it in place.
static void RunC1(PlayState* play, Player* player) {
    if (sMoveFrame == 8) {
        PointAhead(player, 110.0f, &sPuppetPos);
        sPuppetLife = SKJ_PUPPET_LIFE;
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_MAGIC_ARROW_SHOT);
    }
}

// C2: an upward punch that sends enemies into the air.
static void RunC2(PlayState* play, Player* player) {
    if (sMoveFrame >= 6 && sMoveFrame <= 10) {
        Vec3f at;
        PointAhead(player, 36.0f, &at);
        SubmitZone(play, player, 0, &at, 40, 100, -10, 6);
        if (sMoveFrame == 6) {
            LaunchFoes(play, &at, 60.0f, 13.0f);
        }
    }
}

// C3: a laser that goes a long way and passes through everything.
static void RunC3(PlayState* play, Player* player) {
    if (sMoveFrame == 8) {
        LaunchShot(play, player, 0, 30.0f, 750.0f, 30, 5, 0.0075f * 0.22f, SKJ_FLAME_ORANGE, true);
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_MAGIC_ARROW_SHOT);
    }
}

// C4: dark spheres, one every few frames, each turned a bit further round.
static void RunC4(PlayState* play, Player* player) {
    if (sMoveFrame >= 6 && sMoveFrame <= 42 && (sMoveFrame % 4) == 2) {
        s16 turn = (s16)((sMoveFrame - 6) / 4 * 0x1800);

        LaunchShot(play, player, turn, 6.5f, 430.0f, 24, 2, 0.0075f * 0.10f, SKJ_FLAME_BLUE, false);
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_MAGIC_ARROW_SHOT);
    }
}

// C5: a tornado where he points, then a moon tear falls on it.
static void RunC5(PlayState* play, Player* player) {
    if (sMoveFrame == 14) {
        Actor* target = player->focusActor;

        if (target != NULL && target->update != NULL && target->category == ACTORCAT_ENEMY) {
            sMoonPos = target->world.pos;
        } else {
            PointAhead(player, 230.0f, &sMoonPos);
        }
        sMoonPhase = 1;
        sMoonFrame = 0;
    }
}

// C6: Tatl and Tael spin round him and pull enemies in, then he lets go of the dark energy.
static void RunC6(PlayState* play, Player* player) {
    if (sMoveFrame == 10) {
        sSpinFrame = 1;
    }
}

// Special: a flurry of strikes, then a dark explosion.
static void RunSpecial(PlayState* play, Player* player) {
    if (sMoveFrame >= 10 && sMoveFrame < 82 && ((sMoveFrame - 10) % 12) < 4) {
        Vec3f at;
        PointAhead(player, 40.0f, &at);
        SubmitZone(play, player, 0, &at, 60, 100, -10, 3);
    }
    if (sMoveFrame == 92) {
        StartBlast(&player->actor.world.pos, 210, 10);
        Audio_PlayActorSound2(&player->actor, NA_SE_IT_MAGIC_ARROW_SHOT);
    }
}

// ---- effects that outlive the pose ----

static void UpdateEffects(PlayState* play, Player* player) {
    // Puppet
    if (sPuppetLife > 0) {
        sPuppetLife--;
        Vec3f at = sPuppetPos;
        at.y += 22.0f + sinf(sPuppetLife * 0.3f) * 4.0f;
        HoldFoes(play, &sPuppetPos, SKJ_PUPPET_RADIUS);
        if (sPuppetLife > 0) {
            ShowOrb(play, &sOrbs[0], &at, 0.0075f * 0.12f, SKJ_FLAME_BLUE);
            if ((sPuppetLife % 25) == 0) {
                SubmitZone(play, player, 1, &sPuppetPos, 80, 90, -10, 1);
            }
        } else {
            HideOrb(play, &sOrbs[0]);
        }
    }

    // Tornado, then the moon tear
    if (sMoonPhase != 0) {
        sMoonFrame++;
        Vec3f at = sMoonPos;
        if (sMoonPhase == 1) {
            at.y += 30.0f;
            ShowOrb(play, &sOrbs[1], &at, 0.0075f * (0.08f + sMoonFrame * 0.0015f), SKJ_FLAME_BLUE);
            PullFoes(play, &sMoonPos, 170.0f, 3.0f);
            if ((sMoonFrame % 20) == 0) {
                SubmitZone(play, player, 1, &sMoonPos, 70, 100, -10, 1);
            }
            if (sMoonFrame >= 60) {
                sMoonPhase = 2;
                sMoonFrame = 0;
            }
        } else if (sMoonPhase == 2) {
            at.y += 420.0f * (1.0f - sMoonFrame / 20.0f);
            ShowOrb(play, &sOrbs[1], &at, 0.0075f * 0.3f, SKJ_FLAME_ORANGE);
            if (sMoonFrame >= 20) {
                sMoonPhase = 3;
                sMoonFrame = 0;
            }
        } else {
            at.y += 20.0f;
            ShowOrb(play, &sOrbs[1], &at, 0.0075f * 0.4f, SKJ_FLAME_ORANGE);
            SubmitZone(play, player, 1, &sMoonPos, 110, 130, -10, 9);
            if (sMoonFrame >= 6) {
                sMoonPhase = 0;
                HideOrb(play, &sOrbs[1]);
            }
        }
    }

    // Tatl and Tael
    if (sSpinFrame > 0) {
        f32 angle = sSpinFrame * 0.4f;
        Vec3f tatl = player->actor.world.pos;
        Vec3f tael = player->actor.world.pos;

        tatl.x += cosf(angle) * 80.0f;
        tatl.z += sinf(angle) * 80.0f;
        tatl.y += 34.0f;
        tael.x -= cosf(angle) * 80.0f;
        tael.z -= sinf(angle) * 80.0f;
        tael.y += 34.0f;
        ShowOrb(play, &sOrbs[2], &tatl, 0.0075f * 0.08f, SKJ_FLAME_BLUE);
        ShowOrb(play, &sOrbs[3], &tael, 0.0075f * 0.08f, SKJ_FLAME_BLUE);
        PullFoes(play, &player->actor.world.pos, 250.0f, 4.0f);
        sSpinFrame++;
        if (sSpinFrame >= 66) {
            sSpinFrame = 0;
            HideOrb(play, &sOrbs[2]);
            HideOrb(play, &sOrbs[3]);
            StartBlast(&player->actor.world.pos, 140, 7);
        }
    }

    // Explosion
    if (sBlastFrames > 0) {
        Vec3f at = sBlastPos;

        sBlastFrames--;
        at.y += 30.0f;
        SubmitZone(play, player, 2, &sBlastPos, sBlastRadius, 150, -20, sBlastDamage);
        if (sBlastFrames > 0) {
            ShowOrb(play, &sOrbs[4], &at, 0.0075f * (sBlastRadius / 340.0f), SKJ_FLAME_ORANGE);
        } else {
            HideOrb(play, &sOrbs[4]);
        }
    }
}

static void UpdateMoves(PlayState* play, Player* player) {
    bool isOnGround = IsOnGround(player);
    bool isBPressed = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B);
    bool isRPressed = CHECK_BTN_ALL(play->state.input[0].press.button, BTN_R);
    bool isBHeld = CHECK_BTN_ALL(play->state.input[0].cur.button, BTN_B);

    if (sSpecialCooldown > 0) {
        sSpecialCooldown--;
    }
    if (sAttackTimer > 0) {
        sAttackTimer--;
    }
    if (sMove == SKJ_MOVE_NONE && sChainTimer > 0 && --sChainTimer == 0) {
        sWeakCount = 0;
    }

    if (sMove != SKJ_MOVE_NONE) {
        sMoveFrame++;
        switch (sMove) {
            case SKJ_MOVE_WEAK:
                RunWeak(play, player);
                break;
            case SKJ_MOVE_C1:
                RunC1(play, player);
                break;
            case SKJ_MOVE_C2:
                RunC2(play, player);
                break;
            case SKJ_MOVE_C3:
                RunC3(play, player);
                break;
            case SKJ_MOVE_C4:
                RunC4(play, player);
                break;
            case SKJ_MOVE_C5:
                RunC5(play, player);
                break;
            case SKJ_MOVE_C6:
                RunC6(play, player);
                break;
            case SKJ_MOVE_SPECIAL:
                RunSpecial(play, player);
                break;
            default:
                break;
        }
        if (sMoveFrame >= sMoveLength) {
            sMove = SKJ_MOVE_NONE;
        }
    }

    // He slides forward into each hit and stops: the step decays instead of cutting off.
    if (sMove != SKJ_MOVE_NONE && player->linearVelocity > 0.0f) {
        Math_StepToF(&player->linearVelocity, 0.0f, 2.5f);
    }

    // Like OoT: a press during a hit is remembered, and a weak hit chains into the next one as soon as it has
    // swung, without waiting for the whole pose to finish.
    bool isChainable = sMove == SKJ_MOVE_WEAK && sMoveFrame >= (sMoveLength * SKJ_CHAIN_FROM) / 100;
    bool isQueueable = sMove != SKJ_MOVE_NONE && sMoveFrame >= (sMoveLength * SKJ_QUEUE_FROM) / 100;

    if (isQueueable) {
        sIsQueuedB = sIsQueuedB || isBPressed;
        sIsQueuedR = sIsQueuedR || isRPressed;
    }
    bool wantsB = isBPressed || sIsQueuedB;
    bool wantsR = isRPressed || sIsQueuedR;

    if ((sMove == SKJ_MOVE_NONE || isChainable) && isOnGround &&
        !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_FIRST_PERSON))) {
        if (wantsR && isBHeld) {
            if (sSpecialCooldown == 0) {
                StartSpecial(player);
            }
        } else if (wantsR) {
            StartStrong(player);
        } else if (wantsB) {
            StartWeak(player);
        }
    }
    UpdateShots(play, player);
    UpdateEffects(play, player);
}

// B would swing a sword, and Skull Kid has none.
static int32_t BlockSwordSwing(PlayState* play, Player* player) {
    if (IsSkullKid() && CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B)) {
        return SOH_FORM_ACTION_BLOCKED;
    }
    return SOH_FORM_ACTION_VANILLA;
}

static const SOHFormActionOverride sActions[] = {
    { SOH_PLAYER_ACTION_MELEE, BlockSwordSwing },
};

// ---- loadout ----

static uint16_t ResolveSkullKidEquipment(int32_t equipType, uint16_t value) {
    if (equipType == EQUIP_TYPE_SWORD) {
        return EQUIP_VALUE_SWORD_NONE;
    }
    if (equipType == EQUIP_TYPE_SHIELD) {
        return EQUIP_VALUE_SHIELD_NONE;
    }
    return value;
}

// ---- form lifetime ----

static void ResetState(void) {
    sAnim = SKJ_ANIM_NONE;
    sAnimPath = NULL;
    sAttackTimer = 0;
    sRestartAnim = false;
    sBobPhase = 0.0f;
    sHoverFramesLeft = SKJ_HOVER_FRAMES;
    sIsHovering = false;
    sMove = SKJ_MOVE_NONE;
    sMoveFrame = 0;
    sWeakCount = 0;
    sChainTimer = 0;
    sSpecialCooldown = 0;
    sRollTicks = 0;
    sHopTicks = 0;
    sHurtTicks = 0;
    sLean = 0.0f;
    sIsQueuedB = false;
    sIsQueuedR = false;
}

static void EnterSkullKid(PlayState* play, Player* player) {
    (void)player;
    ResetState();
    PrepareBody(play);
}

static void ExitSkullKid(PlayState* play, Player* player) {
    (void)player;
    KillAllEffects(play);
    ResetState();
}

static void UpdateSkullKid(PlayState* play, Player* player) {
    if (!PrepareBody(play)) {
        return;
    }
    sBobPhase += SKJ_BOB_SPEED;
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_L)) {
        sLightMode = (sLightMode + 1) % SKJ_LIGHT_MODES;
    }
    if (sRollTicks > 0) {
        sRollTicks--;
    }
    if (sHopTicks > 0) {
        sHopTicks--;
    }
    if (sHurtTicks > 0) {
        sHurtTicks--;
    }
    UpdateHover(play, player);
    UpdateMoves(play, player);
    ChooseAnim(player);
    {
        // Lean into a run; stand upright otherwise.
        f32 target = IsOnGround(player) && sAttackTimer == 0 ? CLAMP(player->linearVelocity / 8.0f, 0.0f, 1.0f) * SKJ_LEAN_MAX : 0.0f;
        Math_StepToF(&sLean, target, SKJ_LEAN_STEP);
    }
    SkelAnime_Update(&sSkelAnime);
}

static void DrawSkullKid(PlayState* play, Player* player) {
    bool isBodyVisible =
        !(player->stateFlags2 & PLAYER_STATE2_DISABLE_DRAW) && !(player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON);

    if (isBodyVisible) {
        DrawBody(play, player);
    }
}

static void ForgetScene(int16_t sceneNum) {
    (void)sceneNum;
    // The skeleton's joint tables live in this mod; rebuild them against the new scene.
    sIsBodyReady = false;
    ForgetEffects();
    ResetState();
}

// ---- mask ----

static bool CanWearMask(Player* player, PlayState* play) {
    return IsOnGround(player) &&
           !(player->stateFlags1 & (PLAYER_STATE1_IN_WATER | PLAYER_STATE1_FIRST_PERSON | PLAYER_STATE1_IN_CUTSCENE));
}

static void WearMask(PlayState* play, Player* player) {
    sApi->ToggleForm(SKJ_FORM_KEY);
}

static void RegisterMask(void) {
    SOHCustomItemDefinition mask = Z64Items_Define(SKJ_MASK_KEY, sMaskIconTex, sMaskNameTex);

    Z64Items_SetButtons(&mask, SOH_CUSTOM_ITEM_C_BUTTON | SOH_CUSTOM_ITEM_DPAD);
    mask.flags |= SOH_CUSTOM_ITEM_INSTANT | SOH_CUSTOM_ITEM_WEARABLE;
    Z64Items_SetPlacement(&mask, SKJ_MASK_PAGE, SKJ_MASK_SLOT, 0);
    Z64Items_SetTextbox(&mask, "You got %rMajora's Mask%w!&Wear it with %y\xA1%w to become %rSkull Kid%w:&he floats, and "
                               "fights with %y\xA0%w and %yR%w.");
    Z64Items_SetPauseText(&mask, "%rMajora's Mask&%wPress %y\xA1%w to become Skull Kid.&Hold %y\x9F%w in the air to "
                                 "hover.&%y\xA0%w: weak string  %yR%w: strong  %yR+\xA0%w: special");
    Z64Items_SetCanUse(&mask, CanWearMask);
    Z64Items_SetAction(&mask, WearMask, NULL);
    Z64Items_Register(sApi, &mask);
}

static void RegisterForm(void) {
    SOHFormDefinition skj = { 0 };

    skj.structSize = sizeof(skj);
    skj.key = SKJ_FORM_KEY;
    skj.label = "Skull Kid";
    skj.kind = SOH_FORM_KIND_LINK;
    skj.item = SKJ_MASK_KEY;
    skj.motionScale = SKJ_MOTION_SCALE;
    skj.height = SKJ_HEIGHT;
    skj.resolveEquipment = ResolveSkullKidEquipment;
    skj.transformMask = sMaskFaceDL;
    skj.transformVoiceSfx = NA_SE_VO_LI_FALL_L;
    skj.actions = sActions;
    skj.actionCount = ARRAY_COUNT(sActions);
    skj.onEnter = EnterSkullKid;
    skj.onExit = ExitSkullKid;
    skj.update = UpdateSkullKid;
    skj.draw = DrawSkullKid;
    sApi->RegisterForm(&skj);
}

SOH_MOD_EXPORT void ModSetApi(const SOHModApi* api) {
    sApi = api;
}

SOH_MOD_EXPORT const SOHModRequirements* ModGetRequirements(void) {
    return &sRequirements;
}

SOH_MOD_EXPORT void ModInit(void) {
    RegisterMask();
    RegisterForm();
    SOH_REGISTER_HOOK(sApi, OnPlayerResolveLimbDraw, HideLinkBody);
    SOH_REGISTER_HOOK(sApi, OnSceneInit, ForgetScene);
    SOH_REGISTER_HOOK(sApi, OnPlayerResolveAnim, NoteLinkAnim);
    SOH_REGISTER_HOOK(sApi, OnPlayerResolveAnimSite, NoteLinkAnimSite);
}
