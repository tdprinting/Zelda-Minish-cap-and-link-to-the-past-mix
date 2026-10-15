	.include "asm/macros.inc"
	.include "constants/constants.inc"

	.section .rodata
	.align 2

@ A Link to the Past's enemies: graphics, frames, animations and palettes, made from
@ your own cartridge by blend/enemies/alttp_enemies.py
	.include "custom/gen/alttp_enemies.inc"
