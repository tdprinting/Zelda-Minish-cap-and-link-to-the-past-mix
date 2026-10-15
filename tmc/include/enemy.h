#ifndef ENEMY_H
#define ENEMY_H

#include "global.h"
#include "entity.h"

#define EM_FLAG_BOSS (1 << 0)
#define EM_FLAG_BOSS_KILLED (1 << 1)
#define EM_FLAG_HAS_HOME (1 << 2)
#define EM_FLAG_NO_DEATH_FX (1 << 3)
#define EM_FLAG_SUPPORT (1 << 4)
#define EM_FLAG_CAPTAIN (1 << 5)
#define EM_FLAG_MONITORED (1 << 6)

typedef struct {
    Entity base;
    Entity* child;
    u8 idx;
    u8 enemyFlags;
    u8 rangeX;
    u8 rangeY;
    s16 homeX;
    s16 homeY;
    /*0x74*/ union SplitHWord field_0x74;
    /*0x76*/ union SplitHWord field_0x76;
    /*0x78*/ union SplitHWord field_0x78;
    /*0x7a*/ union SplitHWord field_0x7a;
    /*0x7c*/ union SplitWord field_0x7c;
    /*0x80*/ union SplitHWord field_0x80;
    /*0x82*/ union SplitHWord field_0x82;
    /*0x84*/ union SplitHWord cutsceneBeh;
    /*0x86*/ union SplitHWord field_0x86;
} Enemy;

bool32 EnemyInit(Enemy* this);
/**
 * 0: _OnTick
 * 1: _OnCollision
 * 2: _OnKnockback
 * 3: _OnDeath
 * 4: _OnConfused
 * 5: _OnGrabbed
 */
u32 GetNextFunction(Entity*);
void EnemyFunctionHandler(Entity*, EntityActionArray);
void EnemyFunctionHandlerAfterCollision(Entity*, void (*const[])());
void GenericKnockback(Entity*);
void EnemyCreateDeathFX(Enemy*, u32, u32);
void sub_0804A720(Entity*);
bool32 sub_08049FDC(Entity*, u32);

Entity* EnemyCreateFX(Entity*, u32);
void EnemySetFXOffset(Entity*, s32, s32, s32);
Entity* EnemyCreateProjectile(Entity*, u32, u32);

void GenericDeath(Entity*);
void sub_080026C4(u8*, u8*, u8*, u32);
void sub_080026F2(u8*, void*, u8*, u32);
bool32 sub_08049FA0(Entity*);
u32 sub_08049EE4(Entity*);
bool32 sub_08049F84(Entity*, s32);
Entity* sub_08049DF4(u32);
u32 sub_0804A044(Entity*, Entity*, u32);
s32 sub_080012DC(Entity*);

void EnemyDetachFX(Entity*);
bool32 sub_08049F1C(Entity*, Entity*, s32);
bool32 PlayerInRange(Entity*, u32, s32);
void EnemyCopyParams(Entity*, Entity*);
void GenericKnockback2(Entity*);
extern void GenericConfused(struct Entity_*);

extern void sub_08001318(Entity*);
extern void sub_08001290(Entity*, u32);
extern u32 sub_0800132C(struct Entity_*, struct Entity_*);

extern void StealRupees(Entity*);
extern void EnemyDisableRespawn(Enemy*);

enum {
    /*0x00*/ OCTOROK,
    /*0x01*/ CHUCHU,
    /*0x02*/ LEEVER,
    /*0x03*/ PEAHAT,
    /*0x04*/ ROLLOBITE,
    /*0x05*/ DARK_NUT,
    /*0x06*/ HANGING_SEED,
    /*0x07*/ BEETLE,
    /*0x08*/ KEESE,
    /*0x09*/ DOOR_MIMIC,
    /*0x0a*/ ROCK_CHUCHU,
    /*0x0b*/ SPINY_CHUCHU,
    /*0x0c*/ CUCCO_CHICK_AGGR,
    /*0x0d*/ MOLDORM,
    /*0x0e*/ ENEMY_E,
    /*0x0f*/ MOLDWORM,
    /*0x10*/ SLUGGULA,
    /*0x11*/ PESTO,
    /*0x12*/ PUFFSTOOL,
    /*0x13*/ CHUCHU_BOSS,
    /*0x14*/ LIKE_LIKE,
    /*0x15*/ SPEAR_MOBLIN,
    /*0x16*/ BUSINESS_SCRUB,
    /*0x17*/ RUPEE_LIKE,
    /*0x18*/ MADDERPILLAR,
    /*0x19*/ WATER_DROP,
    /*0x1a*/ WALL_MASTER,
    /*0x1b*/ BOMB_PEAHAT,
    /*0x1c*/ SPARK,
    /*0x1d*/ CHASER,
    /*0x1e*/ SPIKED_BEETLE,
    /*0x1f*/ SENSOR_BLADE_TRAP,
    /*0x20*/ HELMASAUR,
    /*0x21*/ FALLING_BOULDER,
    /*0x22*/ BOBOMB,
    /*0x23*/ WALL_MASTER_2,
    /*0x24*/ GLEEROK,
    /*0x25*/ VAATI_EYES_MACRO,
    /*0x26*/ TEKTITE,
    /*0x27*/ WIZZROBE_WIND,
    /*0x28*/ WIZZROBE_FIRE,
    /*0x29*/ WIZZROBE_ICE,
    /*0x2a*/ ARMOS,
    /*0x2b*/ EYEGORE,
    /*0x2c*/ ROPE,
    /*0x2d*/ SMALL_PESTO,
    /*0x2e*/ ACRO_BANDIT,
    /*0x2f*/ BLADE_TRAP,
    /*0x30*/ KEATON,
    /*0x31*/ CROW,
    /*0x32*/ MULLDOZER,
    /*0x33*/ BOMBAROSSA,
    /*0x34*/ WISP,
    /*0x35*/ SPINY_BEETLE,
    /*0x36*/ MAZAAL_HEAD,
    /*0x37*/ MAZAAL_MACRO,
    /*0x38*/ MAZAAL_HAND,
    /*0x39*/ OCTOROK_BOSS,
    /*0x3a*/ FLYING_POT,
    /*0x3b*/ GOBDO,
    /*0x3c*/ OCTOROK_GOLDEN,
    /*0x3d*/ TEKTITE_GOLDEN,
    /*0x3e*/ ROPE_GOLDEN,
    /*0x3f*/ CLOUD_PIRANHA,
    /*0x40*/ SCISSORS_BEETLE,
    /*0x41*/ CUCCO_AGGR,
    /*0x42*/ STALFOS,
    /*0x43*/ FLYING_SKULL,
    /*0x44*/ MAZAAL_BRACELET,
    /*0x45*/ TAKKURI,
    /*0x46*/ BOW_MOBLIN,
    /*0x47*/ LAKITU,
    /*0x48*/ LAKITU_CLOUD,
    /*0x49*/ TORCH_TRAP,
    /*0x4a*/ VAATI_REBORN_ENEMY,
    /*0x4b*/ VAATI_PROJECTILE,
    /*0x4c*/ BALL_CHAIN_SOLIDER,
    /*0x4d*/ ENEMY_4D,
    /*0x4e*/ GHINI,
    /*0x4f*/ VAATI_TRANSFIGURED,
    /*0x50*/ ENEMY_50,
    /*0x51*/ VAATI_WRATH,
    /*0x52*/ VAATI_ARM,
    /*0x53*/ DUST,
    /*0x54*/ VAATI_BALL,
    /*0x55*/ OCTOROK2,
    /*0x56*/ SLIME,
    /*0x57*/ MINI_SLIME,
    /*0x58*/ FIREBALL_GUY,
    /*0x59*/ MINI_FIREBALL_GUY,
    /*0x5a*/ VAATI_TRANSFIGURED_EYE,
    /*0x5b*/ BUSINESS_SCRUB_PROLOGUE,
    /*0x5c*/ GYORG_FEMALE,
    /*0x5d*/ GYORG_MALE,
    /*0x5e*/ CURTAIN,
    /*0x5f*/ VAATI_WRATH_EYE,
    /*0x60*/ GYORG_CHILD,
    /*0x61*/ GYORG_FEMALE_EYE,
    /*0x62*/ GYORG_MALE_EYE,
    /*0x63*/ GYORG_FEMALE_MOUTH,
    /*0x64*/ ENEMY_64,
    /*0x65*/ TREE_ITEM,
    /*0x66*/ ENEMY_66,
    /* A Link to the Past's enemies (src/enemy/alttpEnemies.c) */
    /*0x67*/ ALTTP_STALFOS,
    /*0x68*/ ALTTP_EYEGORE,
    /*0x69*/ ALTTP_POPO,
    /*0x6a*/ ALTTP_ANTIFAIRY,
    /*0x6b*/ ARMOS_KNIGHT,
    /*0x6c*/ ALTTP_ROPE,
    /*0x6d*/ ALTTP_MOBLIN,
    /*0x6e*/ ALTTP_SPEAR,
    /*0x6f*/ ALTTP_HINOX,
    /*0x70*/ ALTTP_BOMB,
    /*0x71*/ LANMOLA,
    /*0x72*/ LANMOLA_SEGMENT,
    /*0x73*/ ALTTP_LEEVER,
    /*0x74*/ ALTTP_BEAMOS,
    /*0x75*/ BEAMOS_LASER,
    /*0x76*/ ALTTP_POKEY,
    /*0x77*/ ALTTP_GIBDO,
    /*0x78*/ GIANT_MOLDORM,
    /*0x79*/ MOLDORM_SEGMENT,
    /*0x7a*/ ALTTP_BARI,
    /*0x7b*/ ALTTP_HARDHAT,
    /*0x7c*/ ALTTP_BUMPER,
    /*0x7d*/ ALTTP_DEBIRANDO_PIT,
    /*0x7e*/ ALTTP_DEBIRANDO,
    /*0x7f*/ ALTTP_FIREBALL,
    /*0x80*/ ALTTP_KODONGO,
    /*0x81*/ MINI_MOLDORM,
    /*0x82*/ MINI_MOLDORM_SEGMENT,
    /*0x83*/ HELMASAUR_KING,
    /*0x84*/ BOSS_PART,
    /*0x85*/ HELMASAUR_FIREBALL,
    /*0x86*/ ARRGHUS,
    /*0x87*/ ARRGHI,
    /*0x88*/ MOTHULA,
    /*0x89*/ MOTHULA_BEAM,
    /*0x8a*/ ALTTP_SPIKE_BLOCK,
    /*0x8b*/ MINI_HELMASAUR,
    /*0x8c*/ TERRORPIN,
    /*0x8d*/ HOVER,
    /*0x8e*/ KYAMERON,
    /*0x8f*/ ZOL,
    /*0x90*/ FIRESNAKE,
    /*0x91*/ ALTTP_SHOOTER,
    /*0x92*/ BLIND,
    /*0x93*/ KHOLDSTARE,
    /*0x94*/ KHOLDSTARE_SHELL,
    /*0x95*/ VITREOUS,
    /*0x96*/ VITREOUS_EYE,
    /*0x97*/ TRINEXX,
    /*0x98*/ TRINEXX_HEAD,
    /*0x99*/ BOSS_PIECE,
    /*0x9a*/ ALTTP_BOLT,
    /*0x9b*/ FREEZOR,
    /*0x9c*/ PENGATOR,
    /*0x9d*/ STALFOS_KNIGHT,
    /*0x9e*/ WIZZROBE,
    /*0x9f*/ LASER_EYE,
    /*0xa0*/ CHAIN_CHOMP,
    /*0xa1*/ ZAZAK,
    /*0xa2*/ GIBO,
    /*0xa3*/ FIRE_BAR,
    /*0xa4*/ STALFOS_HEAD,
    /*0xa5*/ AGAHNIM,
    /*0xa6*/ AGAHNIM_BALL,
    /*0xa7*/ GANON,
    /*0xa8*/ GANON_PART,
};

void Octorok();
void Chuchu();
void Leever();
void Peahat();
void Rollobite();
void DarkNut();
void HangingSeed();
void Beetle();
void Keese();
void DoorMimic();
void RockChuchu();
void SpinyChuchu();
void CuccoChickAggr();
void Moldorm();
void EnemyE();
void Moldworm();
void Sluggula();
void Pesto();
void Puffstool();
void ChuchuBoss();
void LikeLike();
void SpearMoblin();
void BusinessScrub();
void RupeeLike();
void Madderpillar();
void WaterDrop();
void WallMaster();
void BombPeahat();
void Spark();
void Chaser();
void SpikedBeetle();
void SensorBladeTrap();
void Helmasaur();
void FallingBoulder();
void Bobomb();
void WallMaster2();
void Gleerok();
void VaatiEyesMacro();
void Tektite();
void WizzrobeWind();
void WizzrobeFire();
void WizzrobeIce();
void Armos();
void Eyegore();
void Rope();
void SmallPesto();
void AcroBandit();
void BladeTrap();
void Keaton();
void Crow();
void Mulldozer();
void Bombarossa();
void Wisp();
void SpinyBeetle();
void MazaalHead();
void MazaalMacro();
void MazaalHand();
void OctorokBoss();
void FlyingPot();
void Gibdo();
void OctorokGolden();
void TektiteGolden();
void RopeGolden();
void CloudPiranha();
void ScissorsBeetle();
void CuccoAggr();
void Stalfos();
void FlyingSkull();
void MazaalBracelet();
void Takkuri();
void BowMoblin();
void Lakitu();
void LakituCloud();
void TorchTrap();
void VaatiRebornEnemy();
void VaatiProjectile();
void BallChainSoldier();
void Enemy4D();
void Ghini();
void VaatiTransfigured();
void Enemy50();
void VaatiWrath();
void VaatiArm();
void Dust();
void VaatiBall();
void Slime();
void MiniSlime();
void FireballGuy();
void MiniFireballGuy();
void VaatiTransfiguredEye();
void BusinessScrubPrologue();
void GyorgFemale();
void GyorgMale();
void Curtain();
void VaatiWrathEye();
void GyorgChild();
void GyorgFemaleEye();
void GyorgMaleEye();
void GyorgFemaleMouth();
void Enemy64();
void TreeItem();
void Enemy66();
void AlttpStalfos();
void AlttpEyegore();
void AlttpPopo();
void AlttpAntifairy();
void ArmosKnight();
void AlttpRope();
void AlttpMoblin();
void AlttpSpear();
void AlttpHinox();
void AlttpBomb();
void Lanmola();
void LanmolaSegment();
void AlttpLeever();
void AlttpBeamos();
void BeamosLaser();
void AlttpPokey();
void AlttpGibdo();
void GiantMoldorm();
void MoldormSegment();
void AlttpBari();
void AlttpHardhat();
void AlttpBumper();
void AlttpDebirandoPit();
void AlttpDebirando();
void AlttpFireball();
void AlttpKodongo();
void MiniMoldorm();
void MiniMoldormSegment();
void HelmasaurKing();
void BossPart();
void HelmasaurFireball();
void Arrghus();
void Arrghi();
void Mothula();
void MothulaBeam();
void AlttpSpikeBlock();
void MiniHelmasaur();
void Terrorpin();
void Hover();
void Kyameron();
void Zol();
void Firesnake();
void AlttpShooter();
void Blind();
void Kholdstare();
void KholdstareShell();
void Vitreous();
void VitreousEye();
void Trinexx();
void TrinexxHead();
void BossPiece();
void AlttpBolt();
void Freezor();
void Pengator();
void StalfosKnight();
void Wizzrobe();
void LaserEye();
void ChainChomp();
void Zazak();
void Gibo();
void FireBar();
void StalfosHead();
void Agahnim();
void AgahnimBall();
void Ganon();
void GanonPart();

extern void (*const gUnk_080012C8[])(Entity*);

/**
 * The Entity targeted by enemies.
 * This is usually the player.
 */
extern Entity* gEnemyTarget;

#endif // ENEMY_H
