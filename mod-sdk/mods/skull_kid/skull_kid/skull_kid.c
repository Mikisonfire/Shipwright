// Skull Kid form: the Skull Mask turns Link into Majora's Mask's Skull Kid.
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
#define SKJ_FLOAT_LIFT 14.0f    // how high he hangs above the ground while floating
#define SKJ_BOB_HEIGHT 3.0f     // sine bob while floating
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
static const ALIGN_ASSET(2) char sFlutePath[] = "__OTR__objects/object_stk2/gSkullKidPlayFluteAnim";

// Vanilla OoT Skull Mask: icon, name and the mask on Link's face during the transformation.
static const ALIGN_ASSET(2) char sMaskIconTex[] = "__OTR__textures/icon_item_static/gItemIconMaskSkullTex";
static const ALIGN_ASSET(2) char sMaskNameTex[] = "__OTR__textures/item_name_static/gSkullMaskItemNameENGTex";
static const ALIGN_ASSET(2) char sMaskFaceDL[] = "__OTR__objects/object_link_child/gLinkChildSkullMaskDL";

static const char* const sRequiredHooks[] = { "OnPlayerActionHandler", "OnPlayerResolveLimbDraw", "OnSceneInit" };
static const SOHModRequirements sRequirements = { sizeof(SOHModRequirements), sRequiredHooks,
                                                  ARRAY_COUNT(sRequiredHooks) };

typedef enum {
    SKJ_ANIM_NONE,
    SKJ_ANIM_FLOAT,
    SKJ_ANIM_WALK,
    SKJ_ANIM_FLUTE,
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
static bool sIsFluting;

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
    sIsBodyReady = true;
    return true;
}

static void SetAnim(SkjAnim anim, const char* path, f32 rate, bool isLoop) {
    if (anim != sAnim) {
        if (isLoop) {
            Animation_PlayLoop(&sSkelAnime, (AnimationHeader*)path);
        } else {
            Animation_PlayOnce(&sSkelAnime, (AnimationHeader*)path);
        }
        sAnim = anim;
    }
    sSkelAnime.playSpeed = rate;
}

static bool IsFloating(void) {
    return sAnim == SKJ_ANIM_FLOAT;
}

// Picks the animation from what Link's own movement is doing.
static void ChooseAnim(Player* player) {
    if (sIsFluting) {
        SetAnim(SKJ_ANIM_FLUTE, sFlutePath, 1.0f, false);
    } else if (IsOnGround(player) && player->linearVelocity > SKJ_WALK_MIN_SPEED) {
        SetAnim(SKJ_ANIM_WALK, sWalkPath, player->linearVelocity / SKJ_WALK_SPEED_DIV, true);
    } else {
        SetAnim(SKJ_ANIM_FLOAT, sFloatPath, 1.0f, true);
    }
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
    Matrix_Push();
    Matrix_Translate(player->actor.world.pos.x, player->actor.world.pos.y + lift, player->actor.world.pos.z,
                     MTXMODE_NEW);
    Matrix_RotateY(BINANG_TO_RAD(player->actor.shape.rot.y), MTXMODE_APPLY);
    Matrix_Scale(player->actor.scale.x * SKJ_SCALE, player->actor.scale.y * SKJ_SCALE, player->actor.scale.z * SKJ_SCALE,
                 MTXMODE_APPLY);
    SkelAnime_DrawFlexOpa(play, sSkelAnime.skeleton, sSkelAnime.jointTable, sSkelAnime.dListCount, NULL, NULL, player);
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

// ---- flute (B) ----

// B plays the flute pose. Link has no sword here, so B would do nothing anyway.
// TODO: the projectile (a magic note) goes where the pose reaches its release frame.
static int32_t PlayFlute(PlayState* play, Player* player) {
    if (!IsSkullKid() || !CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B) || sIsFluting ||
        !IsOnGround(player)) {
        return SOH_FORM_ACTION_VANILLA;
    }
    sIsFluting = true;
    return SOH_FORM_ACTION_BLOCKED;
}

static const SOHFormActionOverride sActions[] = {
    { SOH_PLAYER_ACTION_MELEE, PlayFlute },
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
    sBobPhase = 0.0f;
    sHoverFramesLeft = SKJ_HOVER_FRAMES;
    sIsHovering = false;
    sIsFluting = false;
}

static void EnterSkullKid(PlayState* play, Player* player) {
    (void)player;
    ResetState();
    PrepareBody(play);
}

static void ExitSkullKid(PlayState* play, Player* player) {
    (void)play;
    (void)player;
    ResetState();
}

static void UpdateSkullKid(PlayState* play, Player* player) {
    if (!PrepareBody(play)) {
        return;
    }
    sBobPhase += SKJ_BOB_SPEED;
    UpdateHover(play, player);
    ChooseAnim(player);
    if (SkelAnime_Update(&sSkelAnime) && sIsFluting) {
        sIsFluting = false;
    }
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
    Z64Items_SetTextbox(&mask, "You got the %rSkull Mask%w!&Wear it with %y\xA1%w to become %rSkull Kid%w:&he floats, "
                               "and plays his flute with %y\xA0%w.");
    Z64Items_SetPauseText(&mask, "%rSkull Mask&%wPress %y\xA1%w to become Skull Kid.&Hold %y\x9F%w in the air to "
                                 "hover.  %y\xA0%w: flute&No sword or shield.");
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
}
