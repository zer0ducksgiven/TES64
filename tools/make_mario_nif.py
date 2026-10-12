"""Generates TES64's Mario template mesh and its two generic textures into plugin/data/. Nothing of Nintendo's: the
mesh is 3072 placeholder vertices the plugin overwrites every frame with libsm64's output, and the textures are flat
white / flat normal. Mario's real texture atlas is written at runtime from the player's ROM (Render.cpp).

Two shapes, because SM64 blends its textures over the vertex colour (mix(color, tex, tex.a)) and Skyrim's lighting
shader multiplies: MarioBody draws every triangle in vertex colour, MarioDecals draws the textured ones again on top
with the atlas, alpha-blended."""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import nif

MAX_TRIS = 1024
NV = MAX_TRIS * 3
FLAGS = nif.VF_VERTEX | nif.VF_UV | nif.VF_NORMAL | nif.VF_TANGENT | nif.VF_COLORS   # = vanilla clutter's 0x3B
DESC = nif.make_desc(FLAGS)
STRIDE, _ = nif.vertex_layout(FLAGS)

SLSF1_RECEIVE_SHADOWS, SLSF1_CAST_SHADOWS, SLSF1_OWN_EMIT = 1 << 8, 1 << 9, 1 << 22
SLSF1_DECAL, SLSF1_DYNAMIC_DECAL, SLSF1_ZTEST = 1 << 26, 1 << 27, 1 << 31
SLSF2_ZWRITE, SLSF2_VERTEX_COLORS, SLSF2_ENVLIGHTFADE = 1 << 0, 1 << 5, 1 << 15


def trishape(name_idx, shader_idx, alpha_idx, max_tris=MAX_TRIS, bound=(0, 0, 40, 220)):
    nv = max_tris * 3
    w = nif.W()
    nif.avobject(w, name_idx)
    w.fs(list(bound))                      # Mario: ~80 units tall at 0.48
    w.i32(-1); w.i32(shader_idx); w.i32(alpha_idx)
    w.u64(DESC)
    w.u16(max_tris); w.u16(nv)
    w.u32(STRIDE * nv + 6 * max_tris)
    vert = bytearray(STRIDE)
    struct.pack_into("<4f", vert, 0, 0, 0, 0, 0)
    struct.pack_into("<4B", vert, 20, 128, 128, 255, 128)   # normal +Z
    struct.pack_into("<4B", vert, 24, 255, 128, 128, 128)   # tangent +X
    struct.pack_into("<4B", vert, 28, 255, 255, 255, 255)   # white
    w.raw(bytes(vert) * nv)
    for t in range(max_tris):
        w.u16(3 * t); w.u16(3 * t + 1); w.u16(3 * t + 2)
    w.u32(0)                               # particle data
    return w.data()


def lighting(name_idx, texset_idx, f1, f2, emissive=(0, 0, 0)):
    w = nif.W()
    w.u32(0)                               # default shader
    w.i32(name_idx); w.u32(0); w.i32(-1)
    w.u32(f1); w.u32(f2)
    w.fs([0, 0]); w.fs([1, 1])
    w.i32(texset_idx)
    w.fs(list(emissive)); w.f32(1.0)       # emissive
    w.u32(3)                               # wrap s/t
    w.f32(1.0); w.f32(0.0); w.f32(80.0)    # alpha, refraction, glossiness
    w.fs([1, 1, 1]); w.f32(0.0)            # specular off
    w.f32(0.3); w.f32(2.0)                 # lighting effects (vanilla defaults)
    return w.data()


def texset(diffuse, normal):
    w = nif.W()
    w.i32(9)
    for t in [diffuse, normal] + [""] * 7:
        w.sized(t)
    return w.data()


def alpha_blend(name_idx):
    w = nif.W()
    w.i32(name_idx); w.u32(0); w.i32(-1)
    w.u16(0x00ED)                          # blend: src alpha / inv src alpha
    w.u8(0)
    return w.data()


def alpha_test(name_idx):
    w = nif.W()
    w.i32(name_idx); w.u32(0); w.i32(-1)
    w.u16(0x1200)                          # test: alpha greater than the threshold, no blending
    w.u8(128)
    return w.data()


def node(name_idx, children):
    w = nif.W()
    nif.avobject(w, name_idx)
    w.u32(len(children))
    for c in children:
        w.i32(c)
    w.u32(0)
    return w.data()


def build():
    strings = ["TES64Mario", "MarioBody", "MarioDecals", "MarioBodyShader", "MarioDecalShader", "MarioDecalAlpha"]
    body_f1 = SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_OWN_EMIT | SLSF1_ZTEST
    decal_f1 = SLSF1_RECEIVE_SHADOWS | SLSF1_DECAL | SLSF1_DYNAMIC_DECAL | SLSF1_ZTEST
    blocks = [
        ("NiNode", node(0, [1, 4])),        # not BSFadeNode: no ref drives its fade
        ("BSTriShape", trishape(1, 2, -1)),
        ("BSLightingShaderProperty", lighting(3, 3, body_f1, SLSF2_ZWRITE | SLSF2_VERTEX_COLORS | SLSF2_ENVLIGHTFADE)),
        ("BSShaderTextureSet", texset("textures\\TES64\\white.dds", "textures\\TES64\\flat_n.dds")),
        ("BSTriShape", trishape(2, 5, 7)),
        ("BSLightingShaderProperty", lighting(4, 6, decal_f1, SLSF2_VERTEX_COLORS | SLSF2_ENVLIGHTFADE)),
        ("BSShaderTextureSet", texset("textures\\TES64\\mario.dds", "textures\\TES64\\flat_n.dds")),
        ("NiAlphaProperty", alpha_blend(5)),
    ]
    return nif.write(blocks, strings)


OBJECT_TRIS = 512


def build_objects():
    """SM64's objects' template (the Power Star, coins): OBJECT_TRIS placeholder triangles the plugin fills from the ROM's
    models (Objects.cpp), textured with the objects atlas it writes at runtime, glowing a little."""
    strings = ["TES64Objects", "Objects", "ObjectsShader", "ObjectsAlpha"]
    f1 = SLSF1_RECEIVE_SHADOWS | SLSF1_CAST_SHADOWS | SLSF1_OWN_EMIT | SLSF1_ZTEST
    blocks = [
        ("NiNode", node(0, [1])),
        ("BSTriShape", trishape(1, 2, 4, OBJECT_TRIS, (0, 0, 0, 2000))),
        ("BSLightingShaderProperty", lighting(2, 3, f1, SLSF2_ZWRITE | SLSF2_VERTEX_COLORS | SLSF2_ENVLIGHTFADE, (0.35, 0.3, 0.08))),
        ("BSShaderTextureSet", texset("textures\\TES64\\objects.dds", "textures\\TES64\\flat_n.dds")),
        ("NiAlphaProperty", alpha_test(3)),
    ]
    return nif.write(blocks, strings)


def dds(w, h, bgra):
    hdr = struct.pack("<4sIIIIIII44sIIIIIIIIIIIII", b"DDS ", 124, 0x1 | 0x2 | 0x4 | 0x1000 | 0x8, h, w, w * 4, 0, 1,
                      b"\0" * 44, 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000, 0x1000, 0, 0, 0, 0)
    return hdr + bytes(bgra)


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "plugin", "data")
    os.makedirs(os.path.join(out, "meshes", "TES64"), exist_ok=True)
    os.makedirs(os.path.join(out, "textures", "TES64"), exist_ok=True)
    data = build()
    check = nif.read(data)
    assert all(b.get("checked") for b in check["blocks"]), "generated NIF didn't read back"
    open(os.path.join(out, "meshes", "TES64", "mario.nif"), "wb").write(data)
    objects = build_objects()
    assert all(b.get("checked") for b in nif.read(objects)["blocks"]), "generated objects NIF didn't read back"
    open(os.path.join(out, "meshes", "TES64", "objects.nif"), "wb").write(objects)
    open(os.path.join(out, "textures", "TES64", "white.dds"), "wb").write(dds(4, 4, b"\xff\xff\xff\xff" * 16))
    open(os.path.join(out, "textures", "TES64", "flat_n.dds"), "wb").write(dds(4, 4, b"\xff\x80\x80\xff" * 16))
    print(f"mario.nif {len(data)} bytes, desc {DESC:016X}, stride {STRIDE}")
