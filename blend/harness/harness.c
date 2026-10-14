// Headless test harness: runs the ROM in libmgba from a small script.
// Script commands (one per line):
//   wait N            run N frames with no keys
//   hold KEYS N       hold keys (A,B,L,R,S=start,E=select,U,D,<,>) for N frames
//   shot FILE.ppm     write the current frame
//   save FILE / load FILE   save state slots
//   peek ADDR LEN     print bytes
#include <mgba/core/core.h>
#include <mgba/core/serialize.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct mCore* core;
static void quiet(struct mLogger* l, int c, enum mLogLevel lv, const char* f, va_list a) { (void) l; (void) c; (void) lv; (void) f; (void) a; }
static struct mLogger quietLogger = { .log = quiet };
static color_t* buf;
static unsigned W, H;

static int keymask(const char* s) {
	int m = 0;
	for (; *s; s++) switch (*s) {
		case 'A': m |= 1; break; case 'B': m |= 2; break;
		case 'E': m |= 4; break; case 'S': m |= 8; break;
		case '>': m |= 16; break; case '<': m |= 32; break;
		case 'U': m |= 64; break; case 'D': m |= 128; break;
		case 'R': m |= 256; break; case 'L': m |= 512; break;
	}
	return m;
}

static void shot(const char* path) {
	FILE* f = fopen(path, "wb");
	fprintf(f, "P6 %u %u 255\n", W, H);
	for (unsigned i = 0; i < W * H; i++) {
		uint32_t c = buf[i];
		unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
		fwrite(px, 1, 3, f);
	}
	fclose(f);
}

int main(int argc, char** argv) {
	if (argc < 3) { fprintf(stderr, "usage: harness rom script\n"); return 1; }
	mLogSetDefaultLogger(&quietLogger);
	core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) return 2;
	mCoreInitConfig(core, NULL);
	core->desiredVideoDimensions(core, &W, &H);
	buf = calloc(W * H, sizeof(color_t));
	core->setVideoBuffer(core, buf, W);
	if (!mCoreLoadFile(core, argv[1])) return 3;
	if (argc > 3) mCoreLoadSaveFile(core, argv[3], false);
	core->reset(core);
	FILE* s = fopen(argv[2], "r");
	char line[256];
	while (fgets(line, sizeof line, s)) {
		char cmd[32] = "", a[128] = ""; int n = 0;
		sscanf(line, "%31s %127s %d", cmd, a, &n);
		if (!strcmp(cmd, "wait")) { core->setKeys(core, 0); for (int i = atoi(a); i > 0; i--) core->runFrame(core); }
		else if (!strcmp(cmd, "hold")) { core->setKeys(core, keymask(a)); for (; n > 0; n--) core->runFrame(core); core->setKeys(core, 0); core->runFrame(core); }
		else if (!strcmp(cmd, "shot")) shot(a);
		else if (!strcmp(cmd, "save")) { struct VFile* vf = VFileOpen(a, O_CREAT | O_TRUNC | O_WRONLY); mCoreSaveStateNamed(core, vf, SAVESTATE_ALL); vf->close(vf); }
		else if (!strcmp(cmd, "load")) { struct VFile* vf = VFileOpen(a, O_RDONLY); mCoreLoadStateNamed(core, vf, SAVESTATE_ALL); vf->close(vf); }
		else if (!strcmp(cmd, "pos")) printf("pos %d %d\n", (int16_t) core->busRead16(core, 0x0300118E), (int16_t) core->busRead16(core, 0x03001192));
		else if (!strcmp(cmd, "walkx") || !strcmp(cmd, "walky")) {
			// walk along one axis until the player reaches coordinate a (max 600 frames)
			int target = atoi(a), isx = cmd[4] == 'x';
			for (int i = 0; i < 600; i++) {
				int v = (int16_t) core->busRead16(core, isx ? 0x0300118E : 0x03001192);
				if (abs(v - target) <= 1) break;
				core->setKeys(core, isx ? (v < target ? 16 : 32) : (v < target ? 128 : 64));
				core->runFrame(core);
			}
			core->setKeys(core, 0); core->runFrame(core);
		}
		else if (!strcmp(cmd, "poke8")) core->busWrite8(core, strtoul(a, 0, 0), n);
		else if (!strcmp(cmd, "poke16")) core->busWrite16(core, strtoul(a, 0, 0), n);
		else if (!strcmp(cmd, "echo")) {
			// fake a co-op partner: copy our own sync block into the partner block, offset by a pixels in x,
			// while holding keys given as the third field (or none) for n frames
			int dx = atoi(a);
			core->busWrite8(core, 0x0203F004, 1);
			for (int i = 0; i < n; i++) {
				for (int k = 0; k < 0x24; k++) core->busWrite8(core, 0x0203F034 + k, core->busRead8(core, 0x0203F010 + k));
				core->busWrite16(core, 0x0203F034, core->busRead16(core, 0x0203F010) + dx);
				core->runFrame(core);
			}
		}
		else if (!strcmp(cmd, "enemies")) {
			// list live enemy entity ids (entity array follows gPlayerEntity, 0x50 entries of 0x88 bytes)
			printf("enemies:");
			for (int i = 0; i < 0x50; i++) {
				uint32_t e = 0x03001160 + i * 0x88;
				if (core->busRead32(core, e + 4) && core->busRead8(core, e + 8) == 3) printf(" %02x", core->busRead8(core, e + 9));
			}
			printf("\n");
		}
		else if (!strcmp(cmd, "ents")) {
			for (int i = 0; i < 0x50; i++) {
				uint32_t e = 0x03001160 + i * 0x88;
				if (core->busRead32(core, e + 4)) printf("[%d %08x k%d id%02x spr%d draw%d] ", i, e, core->busRead8(core, e + 8), core->busRead8(core, e + 9), core->busRead16(core, e + 0x12), core->busRead8(core, e + 0x18) & 3);
			}
			printf("\n");
		}
		else if (!strcmp(cmd, "peek")) { unsigned long ad = strtoul(a, 0, 0); for (int i = 0; i < n; i++) printf("%02x ", core->busRead8(core, ad + i)); printf("\n"); }
	}
	core->deinit(core);
	return 0;
}
