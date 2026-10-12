"""Read-only Skyrim SE BSA (v105) reader, for dev tooling (inspecting vanilla NIFs). Nothing from the game ships."""
import struct
import sys

import lz4.frame


class BSA:
    def __init__(self, path):
        self.f = open(path, "rb")
        h = self.f.read(36)
        magic, ver, off, flags, nfold, nfile, lfold, lfile, fflags = struct.unpack("<4sIIIIIIII", h)
        assert magic == b"BSA\0" and ver == 105, (magic, ver)
        self.compressed = bool(flags & 0x4)
        self.embed_names = bool(flags & 0x100)
        folders = [struct.unpack("<QIIQ", self.f.read(24)) for _ in range(nfold)]
        self.files = {}
        for _, count, _, _ in folders:
            nlen = self.f.read(1)[0]
            folder = self.f.read(nlen)[:-1].decode("latin1")
            recs = [struct.unpack("<QII", self.f.read(16)) for _ in range(count)]
            self.files.setdefault(folder, []).extend(recs)
        names = self.f.read(lfile).split(b"\0")
        i = 0
        self.index = {}
        for folder, recs in self.files.items():
            for (_, size, offset) in recs:
                self.index[(folder + "\\" + names[i].decode("latin1")).lower()] = (size, offset)
                i += 1

    def read(self, name):
        size, offset = self.index[name.lower()]
        comp = self.compressed != bool(size & 0x40000000)
        size &= 0x3FFFFFFF
        self.f.seek(offset)
        if self.embed_names:
            n = self.f.read(1)[0]
            self.f.read(n)
            size -= n + 1
        if comp:
            raw = self.f.read(4)
            orig = struct.unpack("<I", raw)[0]
            return lz4.frame.decompress(self.f.read(size - 4))
        return self.f.read(size)


if __name__ == "__main__":
    b = BSA(sys.argv[1])
    for n in sorted(b.index):
        if len(sys.argv) < 3 or sys.argv[2] in n:
            print(n)
