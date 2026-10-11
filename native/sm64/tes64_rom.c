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
