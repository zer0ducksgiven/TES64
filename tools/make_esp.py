"""Writes TES64.esp (ESL-flagged): one quest that MCM Helper turns into TES64's Mod Configuration Menu
(MCM_ConfigBase on the quest, SKI_PlayerLoadGameAlias on a PlayerRef alias - MCM Helper's documented setup).
Needs SkyUI + MCM Helper in game; without them the quest just sits there. No Nintendo content."""
import os
import struct
import sys

QUEST_ID = 0x01000800      # light plugin: own forms 0x800+, file index 01 (one master)


def sub(tag, data):
    return tag.encode() + struct.pack("<H", len(data)) + data


def zstr(s):
    return s.encode("latin1") + b"\0"


def wstr(s):
    b = s.encode("latin1")
    return struct.pack("<H", len(b)) + b


def record(tag, form_id, payload, flags=0):
    # SSE record header: type, size, flags, formid, timestamp/vc (u32), version u16 (44), unknown u16
    return tag.encode() + struct.pack("<IIIIHH", len(payload), flags, form_id, 0, 44, 0) + payload


def group(label, records):
    body = b"".join(records)
    return b"GRUP" + struct.pack("<I", 24 + len(body)) + label.encode() + struct.pack("<iIHH", 0, 0, 0, 0)[:4] + struct.pack("<IHH", 0, 0, 0) + body


def script(name):
    return wstr(name) + struct.pack("<BH", 0, 0)     # status, no properties


def quest_vmad():
    v = struct.pack("<hhH", 5, 2, 1) + script("MCM_ConfigBase")
    # quest fragment section: unknown (2), no fragments, empty file name; then aliases
    v += struct.pack("<bH", 2, 0) + wstr("")
    v += struct.pack("<H", 1)
    v += struct.pack("<HhI", 0, 0, 0)                  # object: alias 0 of this quest (format 2: unused, alias, formid)
    v += struct.pack("<hhH", 5, 2, 1) + script("SKI_PlayerLoadGameAlias")
    return v


def build():
    q = b""
    q += sub("EDID", zstr("TES64_MCM"))
    q += sub("VMAD", quest_vmad())
    q += sub("FULL", zstr("TES64"))
    q += sub("DNAM", struct.pack("<HBBII", 0x0001 | 0x0010, 0, 0, 0, 0))   # start game enabled, run once
    q += sub("NEXT", b"")
    q += sub("ANAM", struct.pack("<I", 1))
    q += sub("ALST", struct.pack("<I", 0))
    q += sub("ALID", zstr("PlayerRef"))
    q += sub("FNAM", struct.pack("<I", 0))
    q += sub("ALFR", struct.pack("<I", 0x00000014))   # forced: the player
    q += sub("VTCK", struct.pack("<I", 0))
    q += sub("ALED", b"")
    quest = record("QUST", QUEST_ID, q)

    hdr = sub("HEDR", struct.pack("<fII", 1.71, 1, 0x801))
    hdr += sub("CNAM", zstr("TES64"))
    hdr += sub("MAST", zstr("Skyrim.esm")) + sub("DATA", struct.pack("<Q", 0))
    tes4 = record("TES4", 0, hdr, flags=0x200)        # ESL
    return tes4 + group("QUST", [quest])


def check(data):
    """Walk it back: every record/group size must land exactly on the next one."""
    p, n = 0, 0
    while p < len(data):
        tag, size = data[p:p + 4], struct.unpack_from("<I", data, p + 4)[0]
        if tag == b"GRUP":
            p += 24
            continue
        q, end = p + 24, p + 24 + size
        while q < end:
            ss = struct.unpack_from("<H", data, q + 4)[0]
            q += 6 + ss
        assert q == end, f"{tag} subrecords overrun ({q} vs {end})"
        p, n = end, n + 1
    assert p == len(data)
    return n


if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "plugin", "data")
    data = build()
    print(f"TES64.esp {len(data)} bytes, {check(data)} records")
    open(os.path.join(out, "TES64.esp"), "wb").write(data)
