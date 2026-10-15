#ifndef ALTTPBOSSCAM_H
#define ALTTPBOSSCAM_H

#include "global.h"

/**
 * The boss camera (A Link to the Past's boss rooms are taller than the GBA screen):
 * each frame the boss adds where it is, and Scroll1 leans the camera from Link
 * toward the middle of the fight so both stay on screen. Lives at the end of
 * gAlttpEnemyState.
 */
typedef struct {
    s32 sumX, sumY; /**< this frame's boss positions, added up */
    u8 n;           /**< how many were added */
    u8 hold;        /**< frames to keep leaning after the boss stops adding */
    s8 wantX, wantY;
    s8 biasX, biasY; /**< the lean now, eased one pixel a frame */
    u8 area, room;   /**< where the lean was made: a new room starts straight */
} AlttpBossCam;

extern u8 gAlttpEnemyState[];
#define gAlttpBossCam (*(AlttpBossCam*)(gAlttpEnemyState + 0xff0))

void AlttpBossCamUpdate(s32 targetX, s32 targetY);

#endif
