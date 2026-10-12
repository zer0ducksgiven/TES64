// TES64: SM64's objects (for now the Power Star) from the player's ROM, through NG64's object loader (objrom.c, MIT),
// built in here so its atlas and models can be handed to the plugin as plain triangles. Nothing of the game's is
// compiled in: every model, texture and colour is read from the ROM at runtime.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "libsm64.h"

// objrom.c's PNG writer (unused here: the plugin writes its own DDS)
int png_write_rgba(const char *path, const uint8_t *rgba, int w, int h) { (void)path; (void)rgba; (void)w; (void)h; return 0; }

#include "../../extern/NG64/helper/src/objrom.c"

SM64_LIB_FN int tes64_obj_load(const uint8_t *rom, size_t len) { return objrom_load(rom, len, NULL); }
SM64_LIB_FN const char *tes64_obj_error(void) { return objrom_error(); }

SM64_LIB_FN int tes64_obj_atlas(const uint8_t **rgba, int *w, int *h)
{
    if (!s_atlas) return 0;
    *rgba = s_atlas; *w = ATLAS_W; *h = s_atlasH;
    return 1;
}

// a model posed at the origin, unturned, as triangles: per corner a position (SM64 units), a normal and an atlas uv
// (0..1). Returns the triangle count (at most a_maxTris).
SM64_LIB_FN int tes64_obj_mesh(int model, int animState, float *pos, float *nrm, float *uv, int maxTris)
{
    ObjPose pose;
    memset(&pose, 0, sizeof(pose));
    pose.scale[0] = pose.scale[1] = pose.scale[2] = 1.0f;
    pose.animState = animState;
    ObjPart parts[64];
    int np = objrom_pose(model, &pose, parts, 64), n = 0;
    for (int p = 0; p < np; p++) {
        const ObjPiece *pc = objrom_piece(parts[p].piece);
        if (!pc) continue;
        // the part's place back from NG64's BeamNG axes: quaternion (stored conjugated) -> rotation, metres -> SM64
        float x = -parts[p].quat[0], y = -parts[p].quat[1], z = -parts[p].quat[2], w = parts[p].quat[3];
        float Rb[3][3] = { { 1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w) },
                           { 2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w) },
                           { 2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y) } };
        const float tb[3] = { parts[p].pos[0] / SM64_SCALE, parts[p].pos[1] / SM64_SCALE, parts[p].pos[2] / SM64_SCALE };
        for (int i = 0; i + 2 < pc->ni && n < maxTris; i += 3, n++) {
            for (int k = 0; k < 3; k++) {
                const ObjVert *v = &pc->v[pc->idx[i + k]];
                // SM64 -> BeamNG: (x, -z, y); rotate and place; back: (x, z, -y)
                const float s = parts[p].scale;
                const float vb[3] = { v->p[0] * s, -v->p[2] * s, v->p[1] * s };
                const float nb[3] = { v->n[0] / 127.0f, -v->n[2] / 127.0f, v->n[1] / 127.0f };
                float rb[3], rn[3];
                for (int r = 0; r < 3; r++) {
                    rb[r] = Rb[r][0] * vb[0] + Rb[r][1] * vb[1] + Rb[r][2] * vb[2] + tb[r];
                    rn[r] = Rb[r][0] * nb[0] + Rb[r][1] * nb[1] + Rb[r][2] * nb[2];
                }
                float *o = pos + (n * 3 + k) * 3, *on = nrm + (n * 3 + k) * 3, *ou = uv + (n * 3 + k) * 2;
                o[0] = rb[0]; o[1] = rb[2]; o[2] = -rb[1];
                on[0] = rn[0]; on[1] = rn[2]; on[2] = -rn[1];
                ou[0] = v->uv[0] / 65535.0f; ou[1] = v->uv[1] / 65535.0f;
            }
        }
    }
    return n;
}
