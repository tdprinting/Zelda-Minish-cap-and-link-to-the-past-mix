#include "entity.h"
#include "enemy.h"
#include "definitions.h"
#include "hitbox.h"

const EnemyDefinition gEnemyDefinitions[];
const EnemyDefinition gEnemyDefinition_1[];
const EnemyDefinition gEnemyDefinition_2[];
const EnemyDefinition gEnemyDefinition_9[];
const EnemyDefinition gEnemyDefinition_D[];
const EnemyDefinition gEnemyDefinition_D[];
const EnemyDefinition gEnemyDefinition_F[];
const EnemyDefinition gEnemyDefinition_10[];
const EnemyDefinition gEnemyDefinition_11[];
const EnemyDefinition gEnemyDefinition_12[];
const EnemyDefinition gEnemyDefinition_0[];
const EnemyDefinition gEnemyDefinition_13[];
const EnemyDefinition gEnemyDefinition_1B[];
const EnemyDefinition gEnemyDefinition_1B[];
const EnemyDefinition gEnemyDefinition_18[];
const EnemyDefinition gEnemyDefinition_24[];
const EnemyDefinition gEnemyDefinition_26[];
const EnemyDefinition gEnemyDefinition_27[];
const EnemyDefinition gEnemyDefinition_2D[];
const EnemyDefinition gEnemyDefinition_32[];
const EnemyDefinition gEnemyDefinition_34[];
const EnemyDefinition gEnemyDefinition_36[];
const EnemyDefinition gEnemyDefinition_38[];
const EnemyDefinition gEnemyDefinition_37[];
const EnemyDefinition gEnemyDefinition_39[];
const EnemyDefinition gEnemyDefinition_41[];
const EnemyDefinition gEnemyDefinition_44[];
const EnemyDefinition gEnemyDefinition_42[];
const EnemyDefinition gEnemyDefinition_4A[];
const EnemyDefinition gEnemyDefinition_4F[];
const EnemyDefinition gEnemyDefinition_51[];
const EnemyDefinition gEnemyDefinition_52[];
const EnemyDefinition gEnemyDefinition_56[];
const EnemyDefinition gEnemyDefinition_57[];
const EnemyDefinition gEnemyDefinition_7[];
const EnemyDefinition gEnemyDefinition_25[];
const EnemyDefinition gEnemyDefinition_5A[];
const EnemyDefinition gEnemyDefinition_5[];
const EnemyDefinition gEnemyDefinition_30[];
const EnemyDefinition gEnemyDefinition_15[];
const EnemyDefinition gEnemyDefinition_46[];
const EnemyDefinition gEnemyDefinition_AlttpEyegore[];
extern const Hitbox gHitbox_ArmosKnight;
extern const Hitbox gHitbox_AlttpHinox;
extern const Hitbox gHitbox_AlttpTall;
extern const Hitbox gHitbox_AlttpCentered;
extern const Hitbox gHitbox_AlttpLaser;
extern const Hitbox gHitbox_GiantMoldorm;
extern const Hitbox gHitbox_HelmasaurKing;
extern const Hitbox gHitbox_Arrghus;
extern const Hitbox gHitbox_Mothula;
extern const EnemyDefinition gEnemyDefinition_BossPart[];
extern const Hitbox gHitbox_Blind;
extern const Hitbox gHitbox_Kholdstare;
extern const Hitbox gHitbox_KholdShell;
extern const Hitbox gHitbox_Vitreous;
extern const Hitbox gHitbox_Trinexx;
extern const Hitbox gHitbox_TrinexxShell;
extern const EnemyDefinition gEnemyDefinition_Kholdstare[];
extern const EnemyDefinition gEnemyDefinition_BossPiece[];
extern const EnemyDefinition gEnemyDefinition_AlttpBolt[];
extern const EnemyDefinition gEnemyDefinition_Zazak[];
extern const EnemyDefinition gEnemyDefinition_Gibo[];
extern const EnemyDefinition gEnemyDefinition_FireBar[];
extern const Hitbox gHitbox_Agahnim;
extern const EnemyDefinition gEnemyDefinition_AgahnimBall[];
extern const Hitbox gHitbox_Ganon;
extern const Hitbox gHitbox_GanonBat;
extern const EnemyDefinition gEnemyDefinition_GanonPart[];
extern const EnemyDefinition gEnemyDefinition_AlttpSoldier[];
extern const EnemyDefinition gEnemyDefinition_WallCannon[];
extern const EnemyDefinition gEnemyDefinition_FlyingTile[];
#include "../custom/gen/alttp_enemies.h"
/** Swapped gfx: the enemy's frames are copied into this many 16-tile slots as it animates. */
#define ALTTP_GFX(slots) (0x4000 | ((slots) << 4))

#define MULTI_FORM(definition_ptr) \
    { 0xffff, 0, { .definition = definition_ptr }, 0, { 0, 0, 0, 0 }, 0, 0, 0, 0 }

const EnemyDefinition gEnemyDefinitions[] = {
    MULTI_FORM(gEnemyDefinition_0),
    MULTI_FORM(gEnemyDefinition_1),
    MULTI_FORM(gEnemyDefinition_2),
    { 5, 32, { &gHitbox_0 }, SPRITE_PEAHAT, { 1, 1, 1, 1 }, 4, 128, 112, 15 },
    { 8, 42, { &gHitbox_5 }, SPRITE_ROLLOBITE, { 1, 1, 1, 1 }, 255, 256, 34, 143 },
    MULTI_FORM(gEnemyDefinition_5),
#ifdef EU
    { 259, 5, { &gHitbox_0 }, 344, { 0, 1, 1, 1 }, 255, 0, 64, 139 }, // TODO sprite index too high
#else
    { 259, 5, { &gHitbox_0 }, 345, { 0, 1, 1, 1 }, 255, 0, 64, 139 },     // TODO sprite index too high
#endif
    MULTI_FORM(gEnemyDefinition_7),
    { 7, 112, { &gHitbox_8 }, SPRITE_KEESE, { 0, 1, 1, 0 }, 2, 256, 32, 15 },
    MULTI_FORM(gEnemyDefinition_9),
    { 16, 30, { &gHitbox_23 }, SPRITE_ROCKCHUCHU, { 1, 1, 1, 0 }, 4, 64, 148, 139 },
    { 17, 35, { &gHitbox_23 }, SPRITE_SPINYCHUCHU, { 1, 1, 1, 0 }, 4, 32, 92, 139 },
#ifdef EU
    { 463, 0, { &gHitbox_27 }, 494, { 1, 1, 1, 0 }, 255, 256, 4, 1 }, // TODO sprite index too high
#else
    { 463, 0, { &gHitbox_27 }, 495, { 1, 1, 1, 0 }, 255, 256, 4, 1 },     // TODO sprite index too high
#endif
    MULTI_FORM(gEnemyDefinition_D),
    { 0, 0, { &gHitbox_0 }, SPRITE_0, { 0, 0, 0, 1 }, 255, 256, 0, 139 },
    MULTI_FORM(gEnemyDefinition_F),
    MULTI_FORM(gEnemyDefinition_10),
    MULTI_FORM(gEnemyDefinition_11),
    MULTI_FORM(gEnemyDefinition_12),
    MULTI_FORM(gEnemyDefinition_13),
    { 69, 111, { &gHitbox_9 }, SPRITE_LIKELIKE, { 1, 1, 1, 1 }, 20, 64, 140, 139 },
    MULTI_FORM(gEnemyDefinition_15),
    { 114, 110, { &gHitbox_0 }, SPRITE_BUSINESSSCRUB, { 0, 0, 1, 1 }, 255, 0, 1, 139 },
    { 115, 4, { &gHitbox_10 }, SPRITE_323, { 0, 0, 0, 1 }, 16, 96, 142, 139 },
    MULTI_FORM(gEnemyDefinition_18),
    { 134, 1, { &gHitbox_0 }, SPRITE_WATERDROP, { 0, 1, 1, 1 }, 255, 256, 122, 1 },
    { 135, 0, { &gHitbox_11 }, SPRITE_WALLMASTER, { 0, 0, 0, 1 }, 10, 256, 116, 139 },
    MULTI_FORM(gEnemyDefinition_1B),
    { 141, 0, { &gHitbox_17 }, SPRITE_SPARK, { 0, 1, 1, 1 }, 255, 256, 72, 139 },
    { 143, 112, { &gHitbox_13 }, SPRITE_CHASER, { 0, 1, 1, 1 }, 255, 256, 89, 139 },
    { 144, 113, { &gHitbox_0 }, SPRITE_SPIKEDBEETLE, { 1, 1, 1, 1 }, 4, 96, 99, 139 },
#ifdef EU
    { 145, 1, { &gHitbox_13 }, 482, { 0, 1, 1, 1 }, 255, 256, 102, 139 }, // TODO sprite index too high
#else
    { 145, 1, { &gHitbox_13 }, 483, { 0, 1, 1, 1 }, 255, 256, 102, 139 }, // TODO sprite index too high
#endif
    { 146, 112, { &gHitbox_14 }, SPRITE_HELMASAUR, { 1, 1, 1, 1 }, 4, 256, 111, 15 },
    { 168, 0, { &gHitbox_0 }, SPRITE_FALLINGBOULDER, { 3, 0, 0, 2 }, 255, 256, 147, 139 },
    { 169, 112, { &gHitbox_0 }, SPRITE_BOBOMB, { 1, 1, 1, 0 }, 255, 128, 128, 15 },
    { 135, 1, { &gHitbox_12 }, SPRITE_WALLMASTER, { 3, 1, 1, 1 }, 10, 96, 116, 139 },
    MULTI_FORM(gEnemyDefinition_24),
    MULTI_FORM(gEnemyDefinition_25),
    MULTI_FORM(gEnemyDefinition_26),
    MULTI_FORM(gEnemyDefinition_27),
    { 216, 0, { &gHitbox_0 }, SPRITE_WIZZROBEFIRE, { 0, 0, 0, 1 }, 8, 192, 12, 139 },
    { 217, 113, { &gHitbox_0 }, SPRITE_WIZZROBEICE, { 0, 0, 0, 1 }, 8, 192, 6, 139 },
    { 222, 170, { &gHitbox_15 }, SPRITE_ARMOS, { 1, 1, 1, 1 }, 10, 160, 1, 1 },
    { 225, 178, { &gHitbox_16 }, SPRITE_EYEGORE, { 1, 1, 1, 2 }, 3, 256, 5, 139 },
    { 224, 111, { &gHitbox_24 }, SPRITE_ROPE, { 1, 1, 1, 1 }, 4, 128, 25, 139 },
    MULTI_FORM(gEnemyDefinition_2D),
    { 228, 179, { &gHitbox_0 }, SPRITE_ACROBANDITS, { 0, 0, 0, 1 }, 1, 128, 25, 139 },
#ifdef EU
    { 145, 2, { &gHitbox_13 }, 482, { 0, 1, 1, 1 }, 255, 256, 102, 139 }, // TODO sprite index too high
#else
    { 145, 2, { &gHitbox_13 }, 483, { 0, 1, 1, 1 }, 255, 256, 102, 139 }, // TODO sprite index too high
#endif
    MULTI_FORM(gEnemyDefinition_30),
    { 236, 1, { (Hitbox*)&gHitbox_19 }, SPRITE_CROW, { 1, 0, 1, 1 }, 2, 320, 41, 15 },
    MULTI_FORM(gEnemyDefinition_32),
    { 253, 112, { &gHitbox_0 }, SPRITE_BOMBAROSSA, { 1, 1, 1, 1 }, 255, 256, 1, 139 },
    MULTI_FORM(gEnemyDefinition_34),
    { 255, 112, { &gHitbox_29 }, SPRITE_SPINYBEETLE, { 0, 0, 0, 1 }, 4, 352, 25, 139 },
    MULTI_FORM(gEnemyDefinition_36),
    MULTI_FORM(gEnemyDefinition_37),
    MULTI_FORM(gEnemyDefinition_38),
    MULTI_FORM(gEnemyDefinition_39),
    { 33008, 1, { &gHitbox_18 }, SPRITE_POT, { 0, 1, 1, 0 }, 255, 640, 110, 132 },
    { 16416, 1, { &gHitbox_0 }, SPRITE_GIBDO, { 1, 1, 1, 1 }, 20, 96, 38, 139 },
    { 1, 378, { &gHitbox_0 }, SPRITE_OCTOROK, { 3, 1, 1, 1 }, 64, 96, 144, 139 },
    { 199, 378, { &gHitbox_0 }, SPRITE_TEKTITE, { 1, 1, 1, 1 }, 64, 576, 144, 139 },
    { 224, 378, { &gHitbox_0 }, SPRITE_ROPE, { 1, 1, 1, 1 }, 64, 128, 144, 139 },
    { 353, 112, { &gHitbox_0 }, SPRITE_CLOUDPIRANHA, { 0, 0, 1, 1 }, 4, 256, 90, 139 },
    { 16400, 288, { &gHitbox_2 }, SPRITE_SCISSORSBEETLE, { 1, 0, 0, 1 }, 20, 128, 2, 139 },
    MULTI_FORM(gEnemyDefinition_41),
    MULTI_FORM(gEnemyDefinition_42),
    { 366, 0, { &gHitbox_18 }, SPRITE_FLYINGSKULL, { 0, 1, 1, 0 }, 255, 640, 110, 132 },
    MULTI_FORM(gEnemyDefinition_44),
    { 236, 0, { (Hitbox*)&gHitbox_19 }, SPRITE_CROW, { 1, 0, 1, 1 }, 4, 320, 87, 15 },
    MULTI_FORM(gEnemyDefinition_46),
    { 16400, 299, { &gHitbox_0 }, SPRITE_LAKITU, { 0, 1, 0, 1 }, 6, 128, 67, 143 },
    { 16400, 0, { &gHitbox_0 }, SPRITE_LAKITUCLOUDPROJECTILE, { 1, 1, 1, 2 }, 255, 0, 166, 143 },
    { 0, 0, { &gHitbox_0 }, SPRITE_0, { 0, 0, 0, 1 }, 255, 256, 0, 139 },
    MULTI_FORM(gEnemyDefinition_4A),
    { 393, 308, { &gHitbox_0 }, SPRITE_VAATIPROJECTILE, { 1, 1, 1, 1 }, 255, 256, 14, 139 },
    { 16400, 112, { &gHitbox_0 }, SPRITE_BALLCHAINSOLDIER, { 1, 0, 0, 1 }, 16, 128, 163, 139 },
    { 16400, 112, { &gHitbox_0 }, SPRITE_BALLCHAINSOLDIER, { 1, 0, 0, 1 }, 16, 128, 163, 139 },
    { 406, 314, { &gHitbox_0 }, SPRITE_GHINI, { 1, 1, 1, 1 }, 9, 256, 36, 143 },
    MULTI_FORM(gEnemyDefinition_4F),
    { 406, 314, { &gHitbox_0 }, SPRITE_GHINI, { 1, 0, 0, 1 }, 9, 256, 36, 143 },
    MULTI_FORM(gEnemyDefinition_51),
    MULTI_FORM(gEnemyDefinition_52),
#ifdef EU
    { 423, 0, { &gHitbox_4 }, 490, { 0, 1, 1, 1 }, 255, 256, 110, 4 }, // TODO sprite index too high
#else
    { 423, 0, { &gHitbox_4 }, 491, { 0, 1, 1, 1 }, 255, 256, 110, 4 },    // TODO sprite index too high
#endif
    { 16400, 313, { &gHitbox_6 }, SPRITE_VAATIBALL, { 0, 0, 0, 0 }, 255, 1280, 43, 139 },
    MULTI_FORM(gEnemyDefinition_0),
    MULTI_FORM(gEnemyDefinition_56),
    MULTI_FORM(gEnemyDefinition_57),
    { 444, 0, { (Hitbox*)&gHitbox_25 }, SPRITE_FIREBALLGUY, { 1, 1, 1, 1 }, 4, 64, 25, 139 },
    { 444, 0, { (Hitbox*)&gHitbox_26 }, SPRITE_FIREBALLGUY, { 1, 1, 1, 0 }, 1, 64, 25, 139 },
    MULTI_FORM(gEnemyDefinition_5A),
    { 114, 110, { &gHitbox_0 }, SPRITE_BUSINESSSCRUB, { 0, 0, 1, 1 }, 255, 0, 1, 139 },
    { 0, 0, { &gHitbox_0 }, SPRITE_0, { 0, 0, 0, 1 }, 24, 256, 0, 139 },
    { 16448, 338, { (Hitbox*)&gHitbox_28 }, SPRITE_GYORGMALE, { 0, 0, 1, 1 }, 12, 256, 27, 139 },
    { 485, 352, { &gHitbox_0 }, SPRITE_CURTAIN, { 0, 1, 1, 1 }, 255, 256, 154, 139 },
    { 418, 320, { (Hitbox*)&gHitbox_20 }, SPRITE_WRATHEYE, { 1, 0, 1, 0 }, 255, 256, 57, 139 },
    { 498, 0, { (Hitbox*)&gHitbox_20 }, SPRITE_GYORGCHILD, { 0, 0, 1, 1 }, 3, 256, 32, 15 },
#ifdef EU
    { 499, 377, { &gHitbox_22 }, SPRITE_GYORGFEMALEEYE, { 0, 0, 1, 1 }, 255, 256, 30, 139 },
#else
    { 499, 377, { &gHitbox_21 }, SPRITE_GYORGFEMALEEYE, { 0, 0, 1, 1 }, 255, 256, 30, 139 },
#endif
    { 500, 376, { &gHitbox_0 }, SPRITE_ENEMY62, { 0, 0, 1, 1 }, 255, 256, 30, 139 },
    { 504, 372, { &gHitbox_0 }, SPRITE_GYORGFEMALEMOUTH, { 0, 0, 1, 1 }, 255, 256, 136, 139 },
    { 16448, 338, { (Hitbox*)&gHitbox_28 }, SPRITE_GYORGMALE, { 0, 0, 1, 1 }, 36, 256, 0, 139 },
    { 0, 0, { &gHitbox_0 }, SPRITE_0, { 0, 0, 0, 1 }, 255, 256, 0, 139 },
    // ENEMY_66 had no entry and read the next table's first (gEnemyDefinition_1's); keep that
    { 3, 28, { &gHitbox_23 }, SPRITE_CHUCHU, { 1, 0, 0, 0 }, 4, 32, 92, 15 },
    // A Link to the Past's enemies: swapped gfx from sprite 155 (blend/enemies/alttp_enemies.py).
    // Hit types 25 and 100 take sword damage and hurt Link on touch (100 is not pulled by
    // the Gust Jar); 81 takes it too but is only solid to touch; 72 (a Spark's) shocks
    // Link and his sword. Not 86 (a Darknut's): it guards the front and writes the blow
    // into byte 0x79, which here is AlttpEnemy.shield.
    { ALTTP_GFX(AS_STALFOS), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 1 }, 4, 256, 25, 139 },
    MULTI_FORM(gEnemyDefinition_AlttpEyegore),
    { ALTTP_GFX(AS_POPO), 0, { &gHitbox_23 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 1 }, 2, 192, 25, 139 },
    { ALTTP_GFX(AS_ANTIFAIRY), 0, { &gHitbox_14 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 0 }, 255, 256, 72, 139 },
    { ALTTP_GFX(AS_ARMOS), 0, { &gHitbox_ArmosKnight }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 2 }, 8, 256, 100, 139 },
    { ALTTP_GFX(AS_ROPE), 0, { &gHitbox_24 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 1 }, 2, 128, 25, 139 },
    { ALTTP_GFX(AS_MOBLIN), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 2 }, 6, 256, 100, 139 },
    { ALTTP_GFX(AS_SPEAR), 0, { &gHitbox_22 }, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 0 }, 255, 512, 72, 139 },
    // the Desert Palace, Tower of Hera and Hinox set: sprite 329
    { ALTTP_GFX(AS_HINOX), 0, { &gHitbox_AlttpHinox }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 2 }, 12, 128, 100, 139 },
    { ALTTP_GFX(AS_BOMB), 0, { &gHitbox_22 }, ALTTP_ENEMY_SPRITE_2, { 3, 0, 1, 1 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_LANMOLA), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 2 }, 12, 256, 100, 139 },
    { ALTTP_GFX(AS_LANMOLA), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_2, { 2, 0, 1, 1 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_LEEVER), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 4, 256, 25, 139 },
    { ALTTP_GFX(AS_BEAMOS), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 255, 0, 81, 139 },
    { ALTTP_GFX(AS_BEAMLASER), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 0 }, 255, 0x500, 72, 139 },
    { ALTTP_GFX(AS_POKEY), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 6, 256, 25, 139 },
    { ALTTP_GFX(AS_GIBDO), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 10, 128, 25, 139 },
    { ALTTP_GFX(AS_MOLDORM), 0, { &gHitbox_GiantMoldorm }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 0 }, 16, 0x180, 100, 139 },
    { ALTTP_GFX(AS_MOLDORM), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_2, { 2, 1, 1, 0 }, 0, 0, 25, 139 },
    { ALTTP_GFX(AS_BARI), 0, { &gHitbox_23 }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 4, 64, 25, 139 },
    { ALTTP_GFX(AS_HARDHAT), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_2, { 1, 1, 1, 1 }, 200, 256, 25, 139 },
    { ALTTP_GFX(AS_BUMPER), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_2, { 3, 0, 1, 0 }, 0, 0, 72, 139 },
    // Debirandos, Kodongos, mini Moldorms, fireballs: sprite 330
    { ALTTP_GFX(AS_DEBPIT), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_3, { 3, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_DEBIRANDO), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 0 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_KFIRE), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 0 }, 255, 0x200, 72, 139 },
    { ALTTP_GFX(AS_KODONGO), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 1 }, 8, 0x80, 25, 139 },
    { ALTTP_GFX(AS_MINIMOLD), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 0 }, 3, 0x180, 25, 139 },
    { ALTTP_GFX(AS_MINIMOLD), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_3, { 2, 0, 1, 0 }, 0, 0, 72, 139 },
    // the Dark World dungeons' bosses: sprite 332
    { ALTTP_GFX(AS_HKUP), 0, { &gHitbox_HelmasaurKing }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 0 }, 24, 0, 100, 139 },
    MULTI_FORM(gEnemyDefinition_BossPart),
    { ALTTP_GFX(AS_HKFIRE), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 0 }, 255, 0, 72, 139 },
    { ALTTP_GFX(AS_ARRGHUS), 0, { &gHitbox_Arrghus }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 2 }, 24, 0, 100, 139 },
    { ALTTP_GFX(AS_ARRGHI), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 0 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_MOTHULA), 0, { &gHitbox_Mothula }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 2 }, 24, 0, 100, 139 },
    { ALTTP_GFX(AS_MBEAM), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_5, { 1, 1, 1, 0 }, 255, 0, 72, 139 },
    // the Dark World dungeons' enemies: sprite 331
    { ALTTP_GFX(AS_SPIKEBLOCK), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_4, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_MINIHELM), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_4, { 1, 1, 1, 1 }, 4, 0x100, 25, 139 },
    { ALTTP_GFX(AS_TERRORPIN), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_4, { 1, 1, 1, 1 }, 8, 0, 25, 139 },
    { ALTTP_GFX(AS_HOVER), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_4, { 1, 1, 1, 1 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_KYAM), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_4, { 1, 0, 1, 0 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_ZOL), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_4, { 1, 0, 0, 0 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_FIRESNAKE), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_4, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBALL), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_3, { 1, 0, 0, 0 }, 0, 0, 72, 139 },    // the Ice Palace, Misery Mire, Turtle Rock and Thieves' Town bosses: sprites 334 and 335
    { ALTTP_GFX(AS_BLIND), 0, { &gHitbox_Blind }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 2 }, 255, 0, 100, 139 },
    MULTI_FORM(gEnemyDefinition_Kholdstare),
    { ALTTP_GFX(AS_KSHELL), 0, { &gHitbox_KholdShell }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 0 }, 255, 0, 81, 139 },
    { ALTTP_GFX(AS_VITREOUS), 0, { &gHitbox_Vitreous }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 2 }, 24, 0, 100, 139 },
    { ALTTP_GFX(AS_VEYE), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 2, 0, 25, 139 },
    { ALTTP_GFX(AS_TRINEXX), 0, { &gHitbox_Trinexx }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 255, 0, 100, 139 },
    { ALTTP_GFX(AS_THEAD), 0, { &gHitbox_Trinexx }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 8, 0, 100, 139 },
    MULTI_FORM(gEnemyDefinition_BossPiece),
    MULTI_FORM(gEnemyDefinition_AlttpBolt),
    // their dungeons' enemies: sprite 333
    { ALTTP_GFX(AS_FREEZOR), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 6, 0, 25, 139 },
    { ALTTP_GFX(AS_PENGATOR), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_KNIGHT), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 2 }, 12, 0, 100, 139 },
    { ALTTP_GFX(AS_WIZZROBE), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 4, 0, 25, 139 },
    { ALTTP_GFX(AS_LASEREYE), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CHOMP), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 0, 0, 72, 139 },
    MULTI_FORM(gEnemyDefinition_Zazak),
    MULTI_FORM(gEnemyDefinition_Gibo),
    MULTI_FORM(gEnemyDefinition_FireBar),
    { ALTTP_GFX(AS_SKULL), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 4, 0, 25, 139 },
    // Agahnim and his energy balls: sprite 336
    { ALTTP_GFX(AS_AGAHNIM), 0, { &gHitbox_Agahnim }, ALTTP_ENEMY_SPRITE_9, { 1, 1, 1, 2 }, 255, 0, 100, 139 },
    MULTI_FORM(gEnemyDefinition_AgahnimBall),
    // Ganon, and his trident, fire bats, flames and torches: sprite 337
    { ALTTP_GFX(AS_GANON), 0, { &gHitbox_Ganon }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 2 }, 255, 0, 100, 139 },
    MULTI_FORM(gEnemyDefinition_GanonPart),
    // Ganon's Tower's soldiers, wall cannons, flying floor tiles and Sparks: sprite 340
    MULTI_FORM(gEnemyDefinition_AlttpSoldier),
    MULTI_FORM(gEnemyDefinition_WallCannon),
    MULTI_FORM(gEnemyDefinition_FlyingTile),
    { ALTTP_GFX(AS_SPARK), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_11, { 1, 1, 1, 0 }, 255, 0, 72, 139 },
};

/** The bosses' parts, by type: the Helmasaur King's lower body, a leg, its face, a
 * ball of its tail, the mace, a piece of the mask. */
const EnemyDefinition gEnemyDefinition_BossPart[] = {
    { ALTTP_GFX(AS_HKLOW), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_HKLEG), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_HKFACE), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_HKTAIL), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_HKTAIL), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_HKBIT), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_5, { 1, 0, 1, 1 }, 0, 0, 72, 139 },
};

/** Kholdstare: type 0 the eye inside the shell, type 1 the three it splits into. */
const EnemyDefinition gEnemyDefinition_Kholdstare[] = {
    { ALTTP_GFX(AS_KHOLD), 0, { &gHitbox_Kholdstare }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 0 }, 255, 0, 100, 139 },
    { ALTTP_GFX(AS_KHOLD), 0, { &gHitbox_Kholdstare }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 0 }, 10, 0, 100, 139 },
};

/** The later bosses' pieces, by type: Blind's head, his flying head, Kholdstare's pupil,
 * Trinexx's legs, a half of its shell, a part of a neck, a part of the snake, and a
 * Chain Chomp's chain link. */
const EnemyDefinition gEnemyDefinition_BossPiece[] = {
    { ALTTP_GFX(AS_BLINDHEAD), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_7, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_BLINDHEAD), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 1 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_KPUPIL), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_7, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_TLEGS), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_8, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_TSHELL), 0, { &gHitbox_TrinexxShell }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_TNECK), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_TRINEXX), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CHOMPLINK), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_6, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
};

/** Shots and trails, by type (see AlttpBolt). */
const EnemyDefinition gEnemyDefinition_AlttpBolt[] = {
    { ALTTP_GFX(AS_BLINDLASER), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_BLINDLASER), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_7, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_BOLT), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_BOLT), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_8, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_ICEBALL), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_ICEBALL), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_KFIRE), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_KFLAME), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_3, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_WIZZBEAM), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_LASER), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_LASER), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_PHLEGM), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
};

/** Zazaks: type 0 the blue, 1 the red (which breathes fire). */
const EnemyDefinition gEnemyDefinition_Zazak[] = {
    { ALTTP_GFX(AS_ZAZAK), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 6, 0, 25, 139 },
    { ALTTP_GFX(AS_ZAZAK), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 8, 0, 25, 139 },
};

/** Gibo: type 0 the shell, 1 the nucleus. */
const EnemyDefinition gEnemyDefinition_Gibo[] = {
    { ALTTP_GFX(AS_GIBO), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 1 }, 255, 0, 72, 139 },
    { ALTTP_GFX(AS_GIBOCORE), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 8, 0, 25, 139 },
};

/** The fire bar: types 0 and 1 its post (turning either way), 2 a fireball. */
const EnemyDefinition gEnemyDefinition_FireBar[] = {
    { ALTTP_GFX(AS_FIREBAR), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 0, 0, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBAR), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 0, 0, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBAR), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_6, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
};

/** Agahnim's balls, by type: the energy ball, the seeker, a spark of a burst seeker,
 * the trail a ball leaves, and the spell glowing between his hands. */
const EnemyDefinition gEnemyDefinition_AgahnimBall[] = {
    { ALTTP_GFX(AS_AGBALL), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_9, { 1, 1, 1, 0 }, 255, 0, 100, 139 },
    { ALTTP_GFX(AS_AGBALL), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_9, { 1, 1, 1, 0 }, 255, 0, 100, 139 },
    { ALTTP_GFX(AS_AGBALL), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_9, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_AGBALL), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_9, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_AGGLOW), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_9, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
};

/** Ganon's pieces, by type: the trident, a bat of his ring, a spiralling bat, a bat
 * flung at Link, a flame a bat leaves, a torch, a torch's flame, his afterimage. */
const EnemyDefinition gEnemyDefinition_GanonPart[] = {
    { ALTTP_GFX(AS_TRIDENT), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBAT), 0, { &gHitbox_GanonBat }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBAT), 0, { &gHitbox_GanonBat }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FIREBAT), 0, { &gHitbox_GanonBat }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_GFLAME), 0, { &gHitbox_AlttpLaser }, ALTTP_ENEMY_SPRITE_10, { 1, 1, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_GTORCH), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_10, { 4, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_GFLAME), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_10, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_GANON), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_10, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
};

/** Soldiers: type 0 the blue sword soldier, 1 the red spearman. */
const EnemyDefinition gEnemyDefinition_AlttpSoldier[] = {
    { ALTTP_GFX(AS_SOLDIER), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_11, { 1, 1, 1, 1 }, 4, 0, 100, 139 },
    { ALTTP_GFX(AS_SPEARMAN), 0, { &gHitbox_AlttpTall }, ALTTP_ENEMY_SPRITE_11, { 1, 1, 1, 1 }, 6, 0, 100, 139 },
};

/** Wall cannons, by the way they fire (types 0 right, 1 left, 2 down, 3 up), and
 * (type 4) a cannonball. The cannons sit in the wall and can't be touched. */
const EnemyDefinition gEnemyDefinition_WallCannon[] = {
    { ALTTP_GFX(AS_CANNON), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_11, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CANNON), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_11, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CANNON), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_11, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CANNON), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_11, { 1, 0, 1, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_CANNONBALL), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_11, { 1, 1, 1, 1 }, 0, 0, 72, 139 },
};

/** Flying floor tiles: type 0 the room's (invisible) tile spawner, 1 a tile. */
const EnemyDefinition gEnemyDefinition_FlyingTile[] = {
    { ALTTP_GFX(AS_FTILE), 0, { &gHitbox_0 }, ALTTP_ENEMY_SPRITE_11, { 1, 0, 0, 0 }, 0, 0, 72, 139 },
    { ALTTP_GFX(AS_FTILE), 0, { &gHitbox_AlttpCentered }, ALTTP_ENEMY_SPRITE_11, { 1, 1, 1, 1 }, 1, 0, 25, 139 },
};

const EnemyDefinition gEnemyDefinition_AlttpEyegore[] = {
    { ALTTP_GFX(AS_EYEGORE), 0, &gHitbox_16, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 2 }, 8, 256, 100, 139 },
    { ALTTP_GFX(AS_EYEGORE), 0, &gHitbox_16, ALTTP_ENEMY_SPRITE, { 1, 1, 1, 2 }, 16, 256, 100, 139 },
};

const EnemyDefinition gEnemyDefinition_1[] = {
    { 3, 28, &gHitbox_23, SPRITE_CHUCHU, { 1, 0, 0, 0 }, 4, 32, 92, 15 },
    { 3, 30, &gHitbox_23, SPRITE_CHUCHU, { 1, 0, 0, 0 }, 4, 32, 92, 15 },
    { 3, 31, &gHitbox_23, SPRITE_CHUCHU, { 1, 0, 0, 0 }, 4, 32, 92, 15 },
};
const EnemyDefinition gEnemyDefinition_2[] = {
    { 2, 175, &gHitbox_0, SPRITE_LEEVER, { 3, 0, 0, 1 }, 6, 128, 25, 139 },
    { 2, 176, &gHitbox_0, SPRITE_LEEVER, { 3, 0, 0, 1 }, 8, 128, 25, 139 },
};
const EnemyDefinition gEnemyDefinition_9[] = {
    { 14, 0, &gHitbox_0, SPRITE_DOORMIMIC, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 15, 0, &gHitbox_0, SPRITE_DOORMIMIC_1, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 14, 0, &gHitbox_0, SPRITE_DOORMIMIC, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 15, 0, &gHitbox_0, SPRITE_DOORMIMIC_1, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 516, 1, &gHitbox_0, SPRITE_DOORMIMIC, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 517, 1, &gHitbox_0, SPRITE_DOORMIMIC_1, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 516, 1, &gHitbox_0, SPRITE_DOORMIMIC, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
    { 517, 1, &gHitbox_0, SPRITE_DOORMIMIC_1, { 0, 1, 1, 1 }, 255, 0, 103, 139 },
};
#ifdef EU
const EnemyDefinition gEnemyDefinition_D[] = {
    { 26, 42, &gUnk_080FD1DC, 483, { 0, 0, 1, 1 }, 10, 384, 93, 139 },
    { 26, 42, &gUnk_080FD1E4, 483, { 0, 1, 1, 1 }, 255, 0, 94, 3 },
    { 26, 42, &gUnk_080FD1E4, 483, { 0, 1, 1, 1 }, 255, 0, 94, 3 },
    { 26, 42, &gUnk_080FD1EC, 483, { 0, 0, 1, 0 }, 255, 0, 0, 0 },
}; // TODO sprite index too high
#else
const EnemyDefinition gEnemyDefinition_D[] = {
    { 26, 42, &gUnk_080FD1DC, 484, { 0, 0, 1, 1 }, 10, 384, 93, 139 },
    { 26, 42, &gUnk_080FD1E4, 484, { 0, 1, 1, 1 }, 255, 0, 94, 3 },
    { 26, 42, &gUnk_080FD1E4, 484, { 0, 1, 1, 1 }, 255, 0, 94, 3 },
    { 26, 42, &gUnk_080FD1EC, 484, { 0, 0, 1, 0 }, 255, 0, 0, 0 },
}; // TODO sprite index too high
#endif
const EnemyDefinition gEnemyDefinition_F[] = {
    { 31, 34, &gUnk_080FD1FC, SPRITE_MOLDWORM, { 0, 0, 0, 1 }, 20, 128, 133, 139 },
    { 31, 34, &gUnk_080FD204, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD20C, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
    { 31, 34, &gUnk_080FD214, SPRITE_MOLDWORM, { 0, 0, 0, 0 }, 255, 0, 134, 139 },
};
const EnemyDefinition gEnemyDefinition_10[] = {
    { 33, 36, &gUnk_080FD21C, SPRITE_SLUGGULA_1, { 3, 1, 0, 1 }, 1, 0, 105, 139 },
    { 32, 36, &gUnk_080FD21C, SPRITE_SLUGGULA, { 0, 1, 1, 1 }, 3, 32, 105, 139 },
    { 32, 0, &gUnk_080FD21C, SPRITE_SLUGGULA_2, { 0, 0, 1, 0 }, 3, 0, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_11[] = {
    { 34, 38, (const Hitbox*)&gUnk_080FD22C, SPRITE_PESTO, { 3, 1, 1, 1 }, 4, 64, 119, 15 },
    { 34, 39, (const Hitbox*)&gUnk_080FD22C, SPRITE_PESTO, { 3, 1, 1, 1 }, 4, 64, 119, 15 },
    { 34, 37, (const Hitbox*)&gUnk_080FD22C, SPRITE_PESTO, { 3, 1, 1, 1 }, 4, 64, 119, 15 },
};
const EnemyDefinition gEnemyDefinition_12[] = {
    { 35, 40, &gHitbox_0, SPRITE_PUFFSTOOL, { 1, 1, 1, 0 }, 1, 64, 130, 15 },
    { 35, 40, &gHitbox_0, SPRITE_PUFFSTOOL, { 1, 1, 1, 0 }, 1, 64, 130, 15 },
};
const EnemyDefinition gEnemyDefinition_0[] = {
    { 1, 0, &gHitbox_0, SPRITE_OCTOROK, { 3, 1, 1, 1 }, 2, 96, 25, 15 },
    { 1, 1, &gHitbox_0, SPRITE_OCTOROK, { 3, 1, 1, 1 }, 3, 96, 25, 15 },
    { 1, 0, &gHitbox_0, SPRITE_OCTOROK, { 3, 1, 1, 1 }, 2, 96, 25, 15 },
};
const EnemyDefinition gEnemyDefinition_13[] = {
    { 62, 43, &gUnk_080FD238, SPRITE_CHUCHUBOSS_1, { 0, 1, 0, 3 }, 255, 80, 125, 15 },
    { 16480, 43, &gUnk_080FD240, SPRITE_CHUCHUBOSS_0, { 0, 0, 0, 0 }, 255, 0, 123, 15 },
    { 62, 43, &gUnk_080FD248, SPRITE_CHUCHUBOSS_1, { 0, 0, 0, 0 }, 255, 0, 122, 0 },
    { 62, 43, &gHitbox_0, SPRITE_CHUCHUBOSS_1, { 0, 0, 0, 0 }, 255, 0, 122, 0 },
    { 62, 44, &gUnk_080FD238, SPRITE_CHUCHUBOSS_1, { 0, 1, 0, 3 }, 255, 80, 125, 15 },
    { 16480, 44, &gUnk_080FD240, SPRITE_CHUCHUBOSS_0, { 0, 0, 0, 0 }, 255, 0, 123, 15 },
    { 62, 44, &gUnk_080FD248, SPRITE_CHUCHUBOSS_1, { 0, 0, 0, 0 }, 255, 0, 122, 0 },
    { 62, 44, &gHitbox_0, SPRITE_CHUCHUBOSS_1, { 0, 0, 0, 0 }, 255, 0, 122, 0 },
    { 62, 44, &gHitbox_0, SPRITE_CHUCHUBOSS_1, { 0, 0, 0, 0 }, 255, 0, 124, 139 },
};
#ifdef EU
const EnemyDefinition gEnemyDefinition_1B[] = {
    { 140, 125, &gHitbox_0, SPRITE_BOMBPEAHAT, { 0, 1, 0, 1 }, 4, 192, 127, 15 },
    { 140, 125, &gHitbox_0, SPRITE_BOMBPEAHAT, { 0, 1, 0, 1 }, 4, 192, 127, 15 },
    { 140, 1, &gHitbox_0, SPRITE_BOMBPEAHAT, { 0, 0, 0, 1 }, 1, 0, 169, 139 },
    { 169, 112, &gUnk_080FD21C, SPRITE_BOBOMB, { 0, 1, 0, 1 }, 255, 128, 128, 15 },
};
#else
const EnemyDefinition gEnemyDefinition_1B[] = {
    { 140, 125, &gHitbox_0, SPRITE_BOMBPEAHAT, { 1, 1, 0, 1 }, 4, 192, 127, 15 },
    { 140, 125, &gHitbox_0, SPRITE_BOMBPEAHAT, { 1, 1, 0, 1 }, 4, 192, 127, 15 },
    { 140, 1, &gHitbox_0, SPRITE_BOMBPEAHAT, { 0, 0, 0, 1 }, 1, 0, 169, 139 },
    { 169, 112, &gUnk_080FD21C, SPRITE_BOBOMB, { 0, 1, 0, 1 }, 255, 128, 128, 15 },
};
#endif
const EnemyDefinition gEnemyDefinition_18[] = {
    { 16400, 119, &gUnk_080FD290, SPRITE_MADDERPILLAR_0, { 0, 0, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD298, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD298, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD298, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD2A0, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD2A0, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 255, 224, 106, 139 },
    { 123, 119, &gUnk_080FD2A0, SPRITE_MADDERPILLAR_1, { 0, 1, 1, 1 }, 32, 224, 106, 139 },
};
const EnemyDefinition gEnemyDefinition_24[] = {
    { 173, 201, &gUnk_080FD2D8, SPRITE_GLEEROK_1, { 0, 1, 0, 1 }, 255, 256, 63, 139 },
    { 173, 201, &gUnk_080FD2E0, SPRITE_GLEEROK_1, { 0, 0, 0, 1 }, 255, 256, 1, 139 },
    { 16416, 201, &gUnk_080FD2D0, SPRITE_GLEEROK_0, { 0, 0, 0, 1 }, 255, 256, 0, 139 },
    { 173, 204, &gUnk_080FD2D0, SPRITE_GLEEROK_1, { 0, 1, 1, 1 }, 255, 256, 122, 139 },
    { 16416, 203, &gUnk_080FD2D0, SPRITE_GLEEROK_0, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
    { 16416, 204, &gUnk_080FD2D0, SPRITE_GLEEROK_0, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
    { 173, 204, &gUnk_080FD2D0, SPRITE_GLEEROK_1, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
    { 173, 204, &gUnk_080FD2D0, SPRITE_GLEEROK_1, { 0, 0, 0, 1 }, 255, 256, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_26[] = {
    { 199, 112, &gHitbox_0, SPRITE_TEKTITE, { 1, 1, 1, 1 }, 3, 256, 25, 139 },
    { 199, 113, &gHitbox_0, SPRITE_TEKTITE, { 1, 1, 1, 1 }, 6, 320, 25, 139 },
};
const EnemyDefinition gEnemyDefinition_27[] = {
    { 215, 111, &gHitbox_0, SPRITE_WIZZROBEWIND, { 0, 0, 0, 1 }, 8, 192, 12, 139 },
    { 215, 112, &gHitbox_0, SPRITE_WIZZROBEWIND, { 0, 0, 0, 1 }, 8, 192, 12, 139 },
    { 215, 113, &gHitbox_0, SPRITE_WIZZROBEWIND, { 0, 0, 0, 1 }, 8, 192, 12, 139 },
};
const EnemyDefinition gEnemyDefinition_2D[] = {
    { 227, 38, &gHitbox_0, SPRITE_PESTO_1, { 3, 1, 1, 0 }, 1, 64, 132, 15 },
    { 227, 39, &gHitbox_0, SPRITE_PESTO_1, { 3, 1, 1, 0 }, 1, 64, 132, 15 },
    { 227, 37, &gHitbox_0, SPRITE_PESTO_1, { 3, 1, 1, 0 }, 1, 64, 132, 15 },
};
const EnemyDefinition gEnemyDefinition_32[] = {
    { 237, 200, &gHitbox_0, SPRITE_MULLDOZER, { 1, 1, 1, 1 }, 6, 96, 156, 139 },
    { 237, 199, &gHitbox_0, SPRITE_MULLDOZER, { 1, 1, 1, 1 }, 8, 96, 155, 139 },
};
const EnemyDefinition gEnemyDefinition_34[] = {
    { 254, 0, &gHitbox_1, SPRITE_WISP, { 1, 1, 1, 1 }, 255, 128, 9, 15 },
    { 254, 1, &gHitbox_1, SPRITE_WISP, { 1, 1, 1, 1 }, 255, 384, 9, 15 },
};
const EnemyDefinition gEnemyDefinition_36[] = {
    { 256, 229, &gUnk_080FD354, SPRITE_MAZAALHEAD, { 0, 1, 0, 1 }, 255, 256, 18, 138 },
    { 256, 230, &gHitbox_0, SPRITE_MAZAALHEAD, { 0, 1, 1, 1 }, 255, 256, 0, 138 },
    { 256, 219, &gHitbox_0, SPRITE_MAZAALHEAD, { 0, 1, 1, 1 }, 255, 256, 0, 139 },
    { 256, 220, &gHitbox_0, SPRITE_MAZAALHEAD, { 0, 1, 1, 1 }, 255, 256, 0, 139 },
    { 256, 218, &gUnk_080FD38C, SPRITE_MAZAALHEAD, { 0, 1, 1, 1 }, 255, 256, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_38[] = {
    { 16432, 0, &gHitbox_0, SPRITE_MAZAALHAND, { 0, 1, 0, 1 }, 255, 256, 19, 139 },
    { 16432, 0, &gHitbox_0, SPRITE_MAZAALHAND, { 0, 1, 0, 1 }, 255, 256, 19, 139 },
};
const EnemyDefinition gEnemyDefinition_37[] = {
    { 257, 309, &gHitbox_0, SPRITE_MAZAALMACRO, { 0, 1, 1, 1 }, 255, 256, 15, 138 },
    { 257, 309, &gHitbox_0, SPRITE_MAZAALMACRO, { 0, 0, 1, 1 }, 255, 256, 15, 139 },
    { 257, 309, &gHitbox_0, SPRITE_MAZAALMACRO, { 0, 0, 1, 1 }, 255, 256, 15, 139 },
};
const EnemyDefinition gEnemyDefinition_39[] = {
    { 264, 239, &gUnk_080FD3CC, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 1 }, 3, 0, 95, 139 },
    { 266, 239, &gUnk_080FD3DC, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 0 }, 3, 0, 96, 139 },
    { 266, 239, &gUnk_080FD3DC, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 0 }, 3, 0, 96, 139 },
    { 266, 239, &gUnk_080FD3DC, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 0 }, 3, 0, 96, 139 },
    { 266, 239, &gUnk_080FD3DC, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 0 }, 3, 0, 96, 139 },
    { 267, 239, &gUnk_080FD3E4, SPRITE_OCTOROKBOSS_1, { 0, 1, 1, 0 }, 3, 0, 124, 139 },
    { 16400, 244, &gUnk_080FD3EC, SPRITE_OCTOROKBOSS_0, { 0, 1, 1, 0 }, 3, 0, 1, 138 },
    { 269, 239, &gUnk_080FD3EC, SPRITE_OCTOROKBOSS_1, { 0, 0, 1, 0 }, 3, 0, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_41[] = {
    { 67, 0, &gUnk_080FD580, SPRITE_CUCCOAGGR, { 1, 1, 1, 0 }, 255, 128, 25, 138 },
    { 462, 4, &gUnk_080FD580, SPRITE_CUCCOAGGR, { 1, 1, 1, 0 }, 255, 128, 25, 138 },
    { 67, 0, &gUnk_080FD580, SPRITE_CUCCOAGGR, { 1, 1, 1, 0 }, 255, 640, 167, 1 },
};
const EnemyDefinition gEnemyDefinition_44[] = {
    { 256, 218, &gUnk_080FD35C, SPRITE_MAZAALBRACELET, { 0, 0, 0, 1 }, 255, 256, 23, 139 },
    { 256, 218, &gUnk_080FD36C, SPRITE_MAZAALBRACELET, { 0, 0, 0, 1 }, 255, 256, 23, 139 },
    { 256, 219, &gUnk_080FD37C, SPRITE_MAZAALBRACELET, { 0, 1, 0, 1 }, 255, 256, 0, 139 },
    { 256, 220, &gUnk_080FD384, SPRITE_MAZAALBRACELET, { 0, 1, 0, 1 }, 255, 256, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_42[] = {
    { 16416, 1, &gHitbox_0, SPRITE_STALFOS, { 1, 1, 1, 1 }, 4, 224, 68, 143 },
    { 16416, 0, &gHitbox_0, SPRITE_STALFOS, { 1, 1, 1, 1 }, 4, 224, 68, 143 },
};
const EnemyDefinition gEnemyDefinition_4A[] = {
    { 16416, 312, &gHitbox_0, SPRITE_VAATIREBORNENEMY_0, { 1, 0, 0, 1 }, 64, 160, 138, 139 },
    { 16400, 312, &gUnk_080FD588, SPRITE_VAATIREBORNENEMY_1, { 0, 1, 1, 0 }, 255, 640, 47, 139 },
    { 16400, 312, &gHitbox_0, SPRITE_VAATIREBORNENEMY_2, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
    { 16400, 312, &gHitbox_0, SPRITE_VAATIREBORNENEMY_3, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_4F[] = {
    { 16416, 319, &gUnk_080FD530, SPRITE_VAATITRANSFIGURED_0, { 1, 1, 0, 2 }, 255, 256, 53, 139 },
    { 16480, 319, &gHitbox_7, SPRITE_VAATITRANSFIGURED_1, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
    { 16400, 319, &gHitbox_7, SPRITE_VAATITRANSFIGURED_2, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
    { 16400, 319, &gHitbox_7, SPRITE_VAATITRANSFIGURED_3, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
    { 409, 0, &gHitbox_7, SPRITE_VAATITRANSFIGURED_4, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
    { 410, 316, &gHitbox_7, SPRITE_VAATITRANSFIGURED_5, { 0, 0, 1, 0 }, 255, 640, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_51[] = {
    { 16448, 320, &gUnk_080FD544, SPRITE_VAATIWRATH, { 1, 1, 0, 2 }, 255, 256, 57, 139 },
    { 420, 320, &gHitbox_0, SPRITE_VAATIWRATH_1, { 0, 0, 1, 2 }, 255, 256, 1, 139 },
    { 420, 320, &gHitbox_0, SPRITE_VAATIWRATH_1, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
    { 417, 320, &gHitbox_0, SPRITE_VAATIWRATH_2, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_52[] = {
    { 16400, 320, (Hitbox*)&gUnk_080FD538, SPRITE_VAATIARM, { 1, 0, 1, 1 }, 255, 256, 57, 139 },
    { 419, 320, (Hitbox*)&gUnk_080FD538, SPRITE_VAATIARM_1, { 0, 0, 1, 1 }, 255, 256, 57, 139 },
    { 419, 320, (Hitbox*)&gUnk_080FD538, SPRITE_VAATIARM_1, { 1, 0, 1, 1 }, 255, 256, 57, 139 },
    { 419, 320, (Hitbox*)&gUnk_080FD538, SPRITE_VAATIARM_1, { 1, 0, 1, 1 }, 255, 256, 57, 139 },
    { 419, 320, (Hitbox*)&gUnk_080FD538, SPRITE_VAATIARM_1, { 1, 0, 1, 1 }, 255, 256, 57, 139 },
};
const EnemyDefinition gEnemyDefinition_56[] = {
    { 422, 113, &gUnk_080FD488, SPRITE_SLIME, { 3, 1, 1, 1 }, 2, 64, 25, 139 },
    { 422, 111, &gUnk_080FD488, SPRITE_SLIME, { 3, 1, 1, 1 }, 4, 64, 25, 139 },
    { 422, 112, &gUnk_080FD488, SPRITE_SLIME, { 3, 1, 1, 1 }, 4, 64, 25, 139 },
};
const EnemyDefinition gEnemyDefinition_57[] = {
    { 422, 113, &gUnk_080FD490, SPRITE_SLIME, { 3, 1, 1, 0 }, 1, 128, 25, 139 },
    { 422, 111, &gUnk_080FD490, SPRITE_SLIME, { 3, 1, 1, 0 }, 1, 128, 25, 139 },
    { 422, 112, &gUnk_080FD490, SPRITE_SLIME, { 3, 1, 1, 0 }, 1, 128, 25, 139 },
};
const EnemyDefinition gEnemyDefinition_7[] = {
    { 11, 0, &gHitbox_6, SPRITE_BEETLE, { 3, 0, 0, 0 }, 1, 256, 75, 15 },
    { 478, 0, &gHitbox_6, SPRITE_BEETLE, { 3, 0, 0, 0 }, 1, 128, 75, 15 },
};
const EnemyDefinition gEnemyDefinition_25[] = {
    { 421, 320, &gHitbox_0, SPRITE_VAATIEYESMACRO, { 1, 1, 1, 1 }, 32, 256, 25, 139 },
    { 421, 320, &gHitbox_0, SPRITE_VAATIEYESMACRO, { 0, 1, 1, 1 }, 255, 256, 25, 139 },
    { 421, 320, &gHitbox_0, SPRITE_VAATIEYESMACRO, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
    { 421, 320, &gHitbox_0, SPRITE_VAATIEYESMACRO, { 0, 0, 1, 1 }, 255, 256, 0, 139 },
};
const EnemyDefinition gEnemyDefinition_5A[] = {
    { 408, 315, &gHitbox_1, SPRITE_ENEMY5A, { 0, 0, 1, 0 }, 255, 1280, 0, 139 },
    { 408, 319, &gHitbox_1, SPRITE_ENEMY5A, { 0, 0, 1, 0 }, 255, 1280, 51, 139 },
};
const EnemyDefinition gEnemyDefinition_5[] = {
    { 16416, 33, &gUnk_080FD1B4, SPRITE_DARKNUT_0, { 1, 1, 1, 2 }, 12, 96, 86, 139 },
    { 16416, 373, &gUnk_080FD1B4, SPRITE_DARKNUT_0, { 1, 1, 1, 2 }, 12, 96, 86, 139 },
    { 16416, 374, &gUnk_080FD1B4, SPRITE_DARKNUT_0, { 1, 1, 1, 2 }, 20, 96, 86, 139 },
    { 16416, 375, &gUnk_080FD1B4, SPRITE_DARKNUT_0, { 1, 1, 1, 2 }, 26, 96, 86, 139 },
};
const EnemyDefinition gEnemyDefinition_30[] = {
    { 16400, 198, &gUnk_080FD330, SPRITE_KEATON, { 1, 1, 1, 1 }, 4, 256, 33, 139 },
    { 16400, 383, &gUnk_080FD330, SPRITE_KEATON, { 1, 1, 1, 1 }, 4, 256, 33, 139 },
};
const EnemyDefinition gEnemyDefinition_15[] = {
    { 71, 111, &gUnk_080FD258, SPRITE_SPEARMOBLIN, { 1, 1, 1, 2 }, 8, 256, 138, 139 },
    { 71, 384, &gUnk_080FD258, SPRITE_SPEARMOBLIN_1, { 1, 1, 1, 2 }, 8, 256, 138, 139 },
};
const EnemyDefinition gEnemyDefinition_46[] = {
    { 72, 111, &gUnk_080FD258, SPRITE_BOWMOBLIN, { 1, 1, 1, 2 }, 8, 256, 138, 139 },
    { 72, 384, &gUnk_080FD258, SPRITE_BOWMOBLIN_1, { 1, 1, 1, 2 }, 8, 256, 138, 139 },
};

extern Entity* sub_08049E18(void);
extern Entity* sub_08049E4C(void);
extern Entity* sub_08049E80(void);
extern Entity* sub_08049EB0(void);
Entity* (*const gUnk_080D3BE8[])(void) = {
    sub_08049E18,
    sub_08049E4C,
    sub_08049E80,
    sub_08049EB0,
}; // TODO correct signature

// clang-format off
void (*const gEnemyFunctions[])(Entity* ent) = {
    [OCTOROK] = Octorok,
    [CHUCHU] = Chuchu,
    [LEEVER] = Leever,
    [PEAHAT] = Peahat,
    [ROLLOBITE] = Rollobite,
    [DARK_NUT] = DarkNut,
    [HANGING_SEED] = HangingSeed,
    [BEETLE] = Beetle,
    [KEESE] = Keese,
    [DOOR_MIMIC] = DoorMimic,
    [ROCK_CHUCHU] = RockChuchu,
    [SPINY_CHUCHU] = SpinyChuchu,
    [CUCCO_CHICK_AGGR] = CuccoChickAggr,
    [MOLDORM] = Moldorm,
    [ENEMY_E] = EnemyE,
    [MOLDWORM] = Moldworm,
    [SLUGGULA] = Sluggula,
    [PESTO] = Pesto,
    [PUFFSTOOL] = Puffstool,
    [CHUCHU_BOSS] = ChuchuBoss,
    [LIKE_LIKE] = LikeLike,
    [SPEAR_MOBLIN] = SpearMoblin,
    [BUSINESS_SCRUB] = BusinessScrub,
    [RUPEE_LIKE] = RupeeLike,
    [MADDERPILLAR] = Madderpillar,
    [WATER_DROP] = WaterDrop,
    [WALL_MASTER] = WallMaster,
    [BOMB_PEAHAT] = BombPeahat,
    [SPARK] = Spark,
    [CHASER] = Chaser,
    [SPIKED_BEETLE] = SpikedBeetle,
    [SENSOR_BLADE_TRAP] = SensorBladeTrap,
    [HELMASAUR] = Helmasaur,
    [FALLING_BOULDER] = FallingBoulder,
    [BOBOMB] = Bobomb,
    [WALL_MASTER_2] = WallMaster2,
    [GLEEROK] = Gleerok,
    [VAATI_EYES_MACRO] = VaatiEyesMacro,
    [TEKTITE] = Tektite,
    [WIZZROBE_WIND] = WizzrobeWind,
    [WIZZROBE_FIRE] = WizzrobeFire,
    [WIZZROBE_ICE] = WizzrobeIce,
    [ARMOS] = Armos,
    [EYEGORE] = Eyegore,
    [ROPE] = Rope,
    [SMALL_PESTO] = SmallPesto,
    [ACRO_BANDIT] = AcroBandit,
    [BLADE_TRAP] = BladeTrap,
    [KEATON] = Keaton,
    [CROW] = Crow,
    [MULLDOZER] = Mulldozer,
    [BOMBAROSSA] = Bombarossa,
    [WISP] = Wisp,
    [SPINY_BEETLE] = SpinyBeetle,
    [MAZAAL_HEAD] = MazaalHead,
    [MAZAAL_MACRO] = MazaalMacro,
    [MAZAAL_HAND] = MazaalHand,
    [OCTOROK_BOSS] = OctorokBoss,
    [FLYING_POT] = FlyingPot,
    [GOBDO] = Gibdo,
    [OCTOROK_GOLDEN] = OctorokGolden,
    [TEKTITE_GOLDEN] = TektiteGolden,
    [ROPE_GOLDEN] = RopeGolden,
    [CLOUD_PIRANHA] = CloudPiranha,
    [SCISSORS_BEETLE] = ScissorsBeetle,
    [CUCCO_AGGR] = CuccoAggr,
    [STALFOS] = Stalfos,
    [FLYING_SKULL] = FlyingSkull,
    [MAZAAL_BRACELET] = MazaalBracelet,
    [TAKKURI] = Takkuri,
    [BOW_MOBLIN] = BowMoblin,
    [LAKITU] = Lakitu,
    [LAKITU_CLOUD] = LakituCloud,
    [TORCH_TRAP] = TorchTrap,
    [VAATI_REBORN_ENEMY] = VaatiRebornEnemy,
    [VAATI_PROJECTILE] = VaatiProjectile,
    [BALL_CHAIN_SOLIDER] = BallChainSoldier,
    [ENEMY_4D] = Enemy4D,
    [GHINI] = Ghini,
    [VAATI_TRANSFIGURED] = VaatiTransfigured,
    [ENEMY_50] = Enemy50,
    [VAATI_WRATH] = VaatiWrath,
    [VAATI_ARM] = VaatiArm,
    [DUST] = Dust,
    [VAATI_BALL] = VaatiBall,
    [OCTOROK2] = Octorok,
    [SLIME] = Slime,
    [MINI_SLIME] = MiniSlime,
    [FIREBALL_GUY] = FireballGuy,
    [MINI_FIREBALL_GUY] = MiniFireballGuy,
    [VAATI_TRANSFIGURED_EYE] = VaatiTransfiguredEye,
    [BUSINESS_SCRUB_PROLOGUE] = BusinessScrubPrologue,
    [GYORG_FEMALE] = GyorgFemale,
    [GYORG_MALE] = GyorgMale,
    [CURTAIN] = Curtain,
    [VAATI_WRATH_EYE] = VaatiWrathEye,
    [GYORG_CHILD] = GyorgChild,
    [GYORG_FEMALE_EYE] = GyorgFemaleEye,
    [GYORG_MALE_EYE] = GyorgMaleEye,
    [GYORG_FEMALE_MOUTH] = GyorgFemaleMouth,
    [ENEMY_64] = Enemy64,
    [TREE_ITEM] = TreeItem,
    [ENEMY_66] = Enemy66,
    [ALTTP_STALFOS] = AlttpStalfos,
    [ALTTP_EYEGORE] = AlttpEyegore,
    [ALTTP_POPO] = AlttpPopo,
    [ALTTP_ANTIFAIRY] = AlttpAntifairy,
    [ARMOS_KNIGHT] = ArmosKnight,
    [ALTTP_ROPE] = AlttpRope,
    [ALTTP_MOBLIN] = AlttpMoblin,
    [ALTTP_SPEAR] = AlttpSpear,
    [ALTTP_HINOX] = AlttpHinox,
    [ALTTP_BOMB] = AlttpBomb,
    [LANMOLA] = Lanmola,
    [LANMOLA_SEGMENT] = LanmolaSegment,
    [ALTTP_LEEVER] = AlttpLeever,
    [ALTTP_BEAMOS] = AlttpBeamos,
    [BEAMOS_LASER] = BeamosLaser,
    [ALTTP_POKEY] = AlttpPokey,
    [ALTTP_GIBDO] = AlttpGibdo,
    [GIANT_MOLDORM] = GiantMoldorm,
    [MOLDORM_SEGMENT] = MoldormSegment,
    [ALTTP_BARI] = AlttpBari,
    [ALTTP_HARDHAT] = AlttpHardhat,
    [ALTTP_BUMPER] = AlttpBumper,
    [ALTTP_DEBIRANDO_PIT] = AlttpDebirandoPit,
    [ALTTP_DEBIRANDO] = AlttpDebirando,
    [ALTTP_FIREBALL] = AlttpFireball,
    [ALTTP_KODONGO] = AlttpKodongo,
    [MINI_MOLDORM] = MiniMoldorm,
    [MINI_MOLDORM_SEGMENT] = MiniMoldormSegment,
    [HELMASAUR_KING] = HelmasaurKing,
    [BOSS_PART] = BossPart,
    [HELMASAUR_FIREBALL] = HelmasaurFireball,
    [ARRGHUS] = Arrghus,
    [ARRGHI] = Arrghi,
    [MOTHULA] = Mothula,
    [MOTHULA_BEAM] = MothulaBeam,
    [ALTTP_SPIKE_BLOCK] = AlttpSpikeBlock,
    [MINI_HELMASAUR] = MiniHelmasaur,
    [TERRORPIN] = Terrorpin,
    [HOVER] = Hover,
    [KYAMERON] = Kyameron,
    [ZOL] = Zol,
    [FIRESNAKE] = Firesnake,
    [ALTTP_SHOOTER] = AlttpShooter,
    [BLIND] = Blind,
    [KHOLDSTARE] = Kholdstare,
    [KHOLDSTARE_SHELL] = KholdstareShell,
    [VITREOUS] = Vitreous,
    [VITREOUS_EYE] = VitreousEye,
    [TRINEXX] = Trinexx,
    [TRINEXX_HEAD] = TrinexxHead,
    [BOSS_PIECE] = BossPiece,
    [ALTTP_BOLT] = AlttpBolt,
    [FREEZOR] = Freezor,
    [PENGATOR] = Pengator,
    [STALFOS_KNIGHT] = StalfosKnight,
    [WIZZROBE] = Wizzrobe,
    [LASER_EYE] = LaserEye,
    [CHAIN_CHOMP] = ChainChomp,
    [ZAZAK] = Zazak,
    [GIBO] = Gibo,
    [FIRE_BAR] = FireBar,
    [STALFOS_HEAD] = StalfosHead,
    [AGAHNIM] = Agahnim,
    [AGAHNIM_BALL] = AgahnimBall,
    [GANON] = Ganon,
    [GANON_PART] = GanonPart,
    [ALTTP_SOLDIER] = AlttpSoldier,
    [WALL_CANNON] = WallCannon,
    [FLYING_TILE] = FlyingTile,
    [ALTTP_SPARK] = AlttpSpark,
};
// clang-format on
