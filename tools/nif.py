"""Minimal Skyrim SE NIF (20.2.0.7, user 12, BS 100) reader/writer for the blocks TES64 needs: NiNode, BSTriShape,
BSLightingShaderProperty, BSEffectShaderProperty, BSShaderTextureSet. The reader checks every parsed block consumes
exactly its recorded size, which is how the layout here was validated against vanilla meshes."""
import io
import struct
import sys

# ---- vertex descriptor (BSVertexDesc) ----------------------------------------------------------------------------
VF_VERTEX, VF_UV, VF_UV2, VF_NORMAL, VF_TANGENT, VF_COLORS, VF_SKINNED, VF_LANDDATA, VF_EYEDATA, VF_INSTANCE, VF_FULLPREC = \
    [1 << i for i in range(11)]


def vertex_layout(flags):
    """-> (size, {attr: offset}) for SSE BSVertexDataSSE."""
    off, o = {}, 0
    if flags & VF_VERTEX:
        off["pos"] = o
        o += 16                                    # SSE: float xyz + float bitangentX, always
    if flags & VF_UV:
        off["uv"] = o
        o += 4
    if flags & VF_UV2:
        off["uv2"] = o
        o += 4
    if flags & VF_NORMAL:
        off["nrm"] = o
        o += 4                                     # normbyte xyz + bitangentY
    if flags & VF_TANGENT:
        off["tan"] = o
        o += 4
    if flags & VF_COLORS:
        off["col"] = o
        o += 4
    if flags & VF_SKINNED:
        off["skin"] = o
        o += 12
    if flags & VF_EYEDATA:
        off["eye"] = o
        o += 4
    return o, off


def make_desc(flags):
    size, off = vertex_layout(flags)
    d = (size // 4) & 0xF
    slots = ["pos", "uv", "uv2", "nrm", "tan", "col", "skin", "landdata", "eye"]
    for i, k in enumerate(slots):
        if k in off:
            d |= ((off[k] // 4) & 0xF) << (4 * (i + 1))
    d |= flags << 44
    return d


def desc_flags(desc):
    return (desc >> 44) & 0xFFFFF


class R:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u8(self): v = self.b[self.p]; self.p += 1; return v
    def u16(self): v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v
    def u32(self): v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v
    def i32(self): v = struct.unpack_from("<i", self.b, self.p)[0]; self.p += 4; return v
    def u64(self): v = struct.unpack_from("<Q", self.b, self.p)[0]; self.p += 8; return v
    def f32(self): v = struct.unpack_from("<f", self.b, self.p)[0]; self.p += 4; return v
    def fs(self, n): v = struct.unpack_from("<%df" % n, self.b, self.p); self.p += 4 * n; return list(v)
    def raw(self, n): v = self.b[self.p:self.p + n]; self.p += n; return v
    def sized(self): return self.raw(self.u32()).decode("latin1")
    def export(self): n = self.u8(); return self.raw(n)[:-1].decode("latin1") if n else ""


def read(data):
    r = R(data)
    line = data[:data.index(b"\n") + 1]
    r.p = len(line)
    ver, endian, user, nblocks = r.u32(), r.u8(), r.u32(), r.u32()
    assert ver == 0x14020007 and user == 12, hex(ver)
    bsver = r.u32()
    author, proc, exp = r.export(), r.export(), r.export()
    types = [r.sized() for _ in range(r.u16())]
    tindex = [r.u16() for _ in range(nblocks)]
    sizes = [r.u32() for _ in range(nblocks)]
    nstrings = r.u32()
    r.u32()  # max string length
    strings = [r.sized() for _ in range(nstrings)]
    ngroups = r.u32()
    r.fs(ngroups)
    blocks = []
    for i in range(nblocks):
        t = types[tindex[i]]
        start = r.p
        body = r.raw(sizes[i])
        b = {"type": t}
        rr = R(body)
        parser = PARSERS.get(t)
        if parser:
            parser(rr, b, strings)
            assert rr.p == len(body), f"block {i} {t}: parsed {rr.p} of {len(body)} bytes"
            b["checked"] = True
        blocks.append(b)
    return {"bsver": bsver, "types": types, "strings": strings, "blocks": blocks, "author": author}


def p_objnet(r, b, s):
    b["name"] = s[r.i32()] if True else None
    b["extra"] = [r.i32() for _ in range(r.u32())]
    b["controller"] = r.i32()


def p_avobject(r, b, s):
    p_objnet(r, b, s)
    b["flags"] = r.u32()
    b["translation"] = r.fs(3)
    b["rotation"] = r.fs(9)
    b["scale"] = r.f32()
    b["collision"] = r.i32()


def p_ninode(r, b, s):
    p_avobject(r, b, s)
    b["children"] = [r.i32() for _ in range(r.u32())]
    b["effects"] = [r.i32() for _ in range(r.u32())]


def p_bstrishape(r, b, s):
    p_avobject(r, b, s)
    b["bound"] = r.fs(4)
    b["skin"], b["shader"], b["alpha"] = r.i32(), r.i32(), r.i32()
    b["desc"] = r.u64()
    ntri = r.u16()
    nvert = r.u16()
    dsize = r.u32()
    b["ntri"], b["nvert"] = ntri, nvert
    if dsize:
        size, _ = vertex_layout(desc_flags(b["desc"]))
        assert size == (b["desc"] & 0xF) * 4, (hex(b["desc"]), size, b["desc"] & 0xF)
        b["vdata"] = r.raw(size * nvert)
        b["tris"] = r.raw(6 * ntri)
        assert dsize == size * nvert + 6 * ntri, (dsize, size, nvert, ntri)
    psize = r.u32()
    if psize:
        r.raw(psize)


def p_lighting(r, b, s):
    b["shadertype"] = r.u32()
    p_objnet(r, b, s)
    b["f1"], b["f2"] = r.u32(), r.u32()
    b["uvoff"], b["uvscale"] = r.fs(2), r.fs(2)
    b["texset"] = r.i32()
    b["emissive"] = r.fs(3)
    b["emissive_mult"] = r.f32()
    b["clamp"] = r.u32()
    b["alpha_"] = r.f32()
    b["refraction"] = r.f32()
    b["gloss"] = r.f32()
    b["spec"] = r.fs(3)
    b["spec_strength"] = r.f32()
    b["light1"], b["light2"] = r.f32(), r.f32()
    t = b["shadertype"]
    if t == 1: r.f32()                                  # env map scale
    elif t == 5: r.fs(3)                                # skin tint
    elif t == 6: r.fs(3)                                # hair tint
    elif t == 7: r.fs(2)                                # parallax
    elif t == 11: r.fs(4); r.fs(2); r.fs(2); r.f32(); r.fs(4)  # multilayer parallax
    elif t == 14: r.fs(4)                               # sparkle
    elif t == 16: r.f32(); r.f32(); r.fs(3)             # eye envmap


def p_texset(r, b, s):
    b["textures"] = [r.sized() for _ in range(r.i32())]


def p_alpha(r, b, s):
    p_objnet(r, b, s)
    b["aflags"] = r.u16()
    b["threshold"] = r.u8()


PARSERS = {"NiNode": p_ninode, "BSFadeNode": p_ninode, "BSTriShape": p_bstrishape,
           "BSLightingShaderProperty": p_lighting, "BSShaderTextureSet": p_texset, "NiAlphaProperty": p_alpha}


# ---- writer ------------------------------------------------------------------------------------------------------
class W:
    def __init__(self): self.b = io.BytesIO()
    def u8(self, v): self.b.write(struct.pack("<B", v))
    def u16(self, v): self.b.write(struct.pack("<H", v))
    def u32(self, v): self.b.write(struct.pack("<I", v))
    def i32(self, v): self.b.write(struct.pack("<i", v))
    def u64(self, v): self.b.write(struct.pack("<Q", v))
    def f32(self, v): self.b.write(struct.pack("<f", v))
    def fs(self, vs): [self.f32(v) for v in vs]
    def raw(self, v): self.b.write(v)
    def sized(self, s): s = s.encode("latin1"); self.u32(len(s)); self.raw(s)
    def export(self, s): s = s.encode("latin1") + b"\0"; self.u8(len(s)); self.raw(s)
    def data(self): return self.b.getvalue()


def write(blocks, strings):
    """blocks: list of (type, bytes). Root is block 0."""
    types = []
    for t, _ in blocks:
        if t not in types:
            types.append(t)
    w = W()
    w.raw(b"Gamebryo File Format, Version 20.2.0.7\n")
    w.u32(0x14020007); w.u8(1); w.u32(12); w.u32(len(blocks))
    w.u32(100); w.export("TES64"); w.export(""); w.export("")
    w.u16(len(types))
    for t in types:
        w.sized(t)
    for t, _ in blocks:
        w.u16(types.index(t))
    for _, d in blocks:
        w.u32(len(d))
    w.u32(len(strings))
    w.u32(max([len(s) for s in strings] + [0]))
    for s in strings:
        w.sized(s)
    w.u32(0)
    for _, d in blocks:
        w.raw(d)
    w.u32(1); w.i32(0)
    return w.data()


def avobject(w, name_idx, flags=0x0E, translation=(0, 0, 0), scale=1.0):
    w.i32(name_idx); w.u32(0); w.i32(-1)
    w.u32(flags); w.fs(translation); w.fs([1, 0, 0, 0, 1, 0, 0, 0, 1]); w.f32(scale); w.i32(-1)


if __name__ == "__main__":
    from bsa import BSA
    b = BSA(sys.argv[1])
    for name in sys.argv[2:]:
        n = read(b.read(name))
        print(name, n["bsver"], n["types"])
        for i, bl in enumerate(n["blocks"]):
            extra = ""
            if bl["type"] == "BSTriShape":
                extra = f"desc={bl['desc']:016X} flags={desc_flags(bl['desc']):X} nv={bl['nvert']} nt={bl['ntri']}"
            if bl["type"] == "BSLightingShaderProperty":
                extra = f"type={bl['shadertype']} f1={bl['f1']:08X} f2={bl['f2']:08X}"
            if bl["type"] == "BSShaderTextureSet":
                extra = str(bl["textures"][:2])
            print(f"  {i:2} {bl['type']:28} {'ok' if bl.get('checked') else '--'} {extra}")
