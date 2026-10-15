	.include "asm/macros.inc"
	.include "constants/constants.inc"

	.section .rodata
    .align 2

gFrameObjLists:: @ 082F3D74
	@ the extracted table plus the blend's own sprites (blend/anim/objlists.py)
	.include "custom/gen/frame_obj_lists.inc"
