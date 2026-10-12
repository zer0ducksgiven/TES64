// Exports for the SKSE plugin: loading Mario's model from the player's ROM (mario_rom_*.c, from NG64, MIT).
#include <stddef.h>
#include <stdint.h>
#include "libsm64.h"

int ng64_rom_normalize(uint8_t *rom, size_t len);
int ng64_load_mario_from_rom(const uint8_t *rom, size_t romLen);
const char *ng64_mario_rom_error(void);

SM64_LIB_FN int tes64_rom_normalize(uint8_t *rom, size_t len) { return ng64_rom_normalize(rom, len); }
SM64_LIB_FN int tes64_load_mario_model(const uint8_t *rom, size_t len) { return ng64_load_mario_from_rom(rom, len); }
SM64_LIB_FN const char *tes64_rom_error(void) { return ng64_mario_rom_error(); }

// Mario's body parts (NG64's parts patch, gfx_adapter.c): the matrix each part was drawn with this tick. TES64 hangs
// Skyrim's weapons and spell effects off his hands.
#define NG64_MAX_PARTS 64
extern int g_ng64PartCount;
extern float g_ng64PartMtx[NG64_MAX_PARTS][4][4];

SM64_LIB_FN int tes64_part_count(void) { return g_ng64PartCount; }
SM64_LIB_FN const float *tes64_part_matrix(int part) { return (part >= 0 && part < g_ng64PartCount) ? &g_ng64PartMtx[part][0][0] : 0; }

// SM64's HUD graphics from the ROM (hud.c, from NG64): digits, x, coin, Mario's head, star, power meter
int ng64_hud_extract(const uint8_t *rom, size_t romLen);
int ng64_hud_image(const char *name, const uint8_t **rgba, int *w, int *h);

SM64_LIB_FN int tes64_hud_extract(const uint8_t *rom, size_t len) { return ng64_hud_extract(rom, len); }
SM64_LIB_FN int tes64_hud_image(const char *name, const uint8_t **rgba, int *w, int *h) { return ng64_hud_image(name, rgba, w, h); }

// Mario's sounds from the ROM (audio.c, from NG64): libsm64's audio thread feeding waveOut. libsm64's audio state is
// shared with mario_tick (it starts sounds), so the plugin ticks Mario under tes64_lock/unlock.
int ng64_audio_start(const uint8_t *rom);
void ng64_audio_stop(void);
void ng64_audio_set_volume(int pct);
void ng64_sm64_lock(void);
void ng64_sm64_unlock(void);

SM64_LIB_FN int tes64_audio_start(const uint8_t *rom) { return ng64_audio_start(rom); }
SM64_LIB_FN void tes64_audio_stop(void) { ng64_audio_stop(); }
SM64_LIB_FN void tes64_audio_volume(int pct) { ng64_audio_set_volume(pct); }
SM64_LIB_FN void tes64_lock(void) { ng64_sm64_lock(); }
SM64_LIB_FN void tes64_unlock(void) { ng64_sm64_unlock(); }
