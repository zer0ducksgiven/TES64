"""Read-only Skyrim.esm scanner: editor ID -> form ID, for dev/test tooling (placeatme in the UAT harness)."""
import struct
import sys
import zlib


def scan(path, wanted_types=None):
    data = open(path, "rb").read()
    out = {}

    def records(buf, start, end):
        p = start
        while p < end:
            typ = buf[p:p + 4]
            size = struct.unpack_from("<I", buf, p + 4)[0]
            if typ == b"GRUP":
                records(buf, p + 24, p + size)
                p += size
                continue
            flags, fid = struct.unpack_from("<II", buf, p + 8)
            body = buf[p + 24:p + 24 + size]
            p += 24 + size
            t = typ.decode("latin1")
            if wanted_types and t not in wanted_types:
                continue
            if flags & 0x40000:
                body = zlib.decompress(body[4:])
            q = 0
            while q + 6 <= len(body):
                st = body[q:q + 4]
                ss = struct.unpack_from("<H", body, q + 4)[0]
                if st == b"EDID":
                    out[body[q + 6:q + 6 + ss - 1].decode("latin1")] = (t, fid)
                    break
                q += 6 + ss

    records(data, 0, len(data))
    return out


if __name__ == "__main__":
    ids = scan(sys.argv[1], set(sys.argv[2].split(",")) if len(sys.argv) > 2 else None)
    for name in sys.argv[3:]:
        hits = [(k, v) for k, v in ids.items() if name.lower() in k.lower()]
        for k, (t, fid) in sorted(hits)[:15]:
            print(f"{t} {fid:08X} {k}")
