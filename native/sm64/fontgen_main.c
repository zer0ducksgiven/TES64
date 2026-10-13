// tes64_fontgen.exe <rom> <out.swf>: the installer's helper. Makes Skyrim's SM64 font from the player's own ROM.
// Exit code 0: written; 1: bad arguments; 2: the file isn't a US Super Mario 64 ROM or couldn't be read/written.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int fontgen_make(const uint8_t *rom, size_t len, const char *outPath);
const char *fontgen_error(void);

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: tes64_fontgen <Super Mario 64 (US) ROM> <out.swf>\n"); return 1; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { fprintf(stderr, "can't open %s\n", argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len != 8 * 1024 * 1024) { fclose(f); fprintf(stderr, "that isn't an 8 MB ROM\n"); return 2; }
    uint8_t *rom = malloc(len);
    if (!rom || fread(rom, 1, len, f) != (size_t)len) { fclose(f); fprintf(stderr, "can't read the ROM\n"); return 2; }
    fclose(f);
    // byte order: z64 as is; v64 swapped in pairs; n64 reversed in fours
    if (rom[0] == 0x37 && rom[1] == 0x80) for (long i = 0; i + 1 < len; i += 2) { uint8_t t = rom[i]; rom[i] = rom[i + 1]; rom[i + 1] = t; }
    else if (rom[0] == 0x40 && rom[1] == 0x12) for (long i = 0; i + 3 < len; i += 4) { uint8_t a = rom[i], b = rom[i + 1]; rom[i] = rom[i + 3]; rom[i + 1] = rom[i + 2]; rom[i + 2] = b; rom[i + 3] = a; }
    else if (!(rom[0] == 0x80 && rom[1] == 0x37)) { fprintf(stderr, "that isn't an N64 ROM\n"); return 2; }
    if (!fontgen_make(rom, len, argv[2])) { fprintf(stderr, "%s\n", fontgen_error()); return 2; }
    printf("wrote %s\n", argv[2]);
    return 0;
}
