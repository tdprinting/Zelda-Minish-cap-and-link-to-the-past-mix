	.include "asm/macros.inc"
	.include "constants/constants.inc"

	.section .rodata
    .align 2

gFrameObjLists:: @ 082F3D74
	@ blend: the game's table with new lists spliced in (blend/anim/objlists.py)
	.include "custom/gen/frame_obj_lists.inc"
