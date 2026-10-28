"""Relê o AgeOfSkyrim.esp gerado por tools/build_esp.py e valida a estrutura (sem depender do gerador).

Rodar: python -m unittest discover -s tests -v
"""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent


def read_subrecords(data):
    subs, pos = [], 0
    while pos < len(data):
        sig = data[pos:pos + 4].decode("ascii")
        (size,) = struct.unpack_from("<H", data, pos + 4)
        pos += 6
        subs.append((sig, data[pos:pos + size]))
        pos += size
    assert pos == len(data), "subrecords não fecham com o tamanho do record"
    return subs


def read_record(data, pos):
    sig = data[pos:pos + 4].decode("ascii")
    size, flags, fid, _ts, _vc, version, _unk = struct.unpack_from("<IIIHHHH", data, pos + 4)
    payload = data[pos + 24:pos + 24 + size]
    return {"sig": sig, "flags": flags, "fid": fid, "version": version, "subs": read_subrecords(payload)}, pos + 24 + size


class Reader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def take(self, fmt):
        values = struct.unpack_from(fmt, self.data, self.pos)
        self.pos += struct.calcsize(fmt)
        return values if len(values) > 1 else values[0]

    def wstring(self):
        n = self.take("<H")
        s = self.data[self.pos:self.pos + n].decode("cp1252")
        self.pos += n
        return s

    def script(self):
        name = self.wstring()
        flags, props = self.take("<BH")
        assert props == 0, "este teste só espera scripts sem propriedades"
        return name, flags


class BuildEspTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        out = pathlib.Path(tempfile.mkdtemp()) / "AgeOfSkyrim.esp"
        subprocess.run([sys.executable, str(ROOT / "tools/build_esp.py"), str(out)], check=True)
        cls.data = out.read_bytes()

        cls.tes4, pos = read_record(cls.data, 0)
        cls.groups = []
        while pos < len(cls.data):
            assert cls.data[pos:pos + 4] == b"GRUP"
            size, label, gtype, _ts, _vc, _unk = struct.unpack_from("<I4sIHHI", cls.data, pos + 4)
            end, inner = pos + size, pos + 24
            recs = []
            while inner < end:
                rec, inner = read_record(cls.data, inner)
                recs.append(rec)
            assert inner == end, "grupo não fecha com o tamanho declarado"
            cls.groups.append({"label": label.decode("ascii"), "type": gtype, "records": recs})
            pos = end

    def test_header(self):
        self.assertEqual(self.tes4["sig"], "TES4")
        self.assertEqual(self.tes4["fid"], 0)
        self.assertEqual(self.tes4["flags"], 0x200, "precisa ser ESL e nada mais (sem ESM/localized)")
        self.assertEqual(self.tes4["version"], 44)
        subs = self.tes4["subs"]
        self.assertEqual(subs[0][0], "HEDR")
        version, num, next_id = struct.unpack("<fiI", subs[0][1])
        self.assertAlmostEqual(version, 1.71, places=5)
        total = sum(1 + len(g["records"]) for g in self.groups)
        self.assertEqual(num, total, "HEDR deve contar records + grupos")
        local_ids = [r["fid"] & 0xFFFFFF for g in self.groups for r in g["records"]]
        self.assertGreater(next_id, max(local_ids))
        sigs = [s for s, _ in subs]
        self.assertEqual(sigs.count("MAST"), 1)
        mast = sigs.index("MAST")
        self.assertEqual(subs[mast][1], b"Skyrim.esm\x00")
        self.assertEqual(subs[mast + 1], ("DATA", b"\x00" * 8))

    def test_formids_are_light(self):
        for g in self.groups:
            self.assertEqual(g["type"], 0)
            for r in g["records"]:
                self.assertEqual(r["sig"], g["label"])
                self.assertEqual(r["version"], 44)
                self.assertEqual(r["fid"] >> 24, 1, "FormID do próprio plugin = índice 1 (depois de Skyrim.esm)")
                self.assertTrue(0x800 <= (r["fid"] & 0xFFFFFF) <= 0xFFF, "fora da faixa ESL")

    def quest(self):
        (g,) = [g for g in self.groups if g["label"] == "QUST"]
        (q,) = g["records"]
        return q

    def test_quest_layout(self):
        q = self.quest()
        sigs = [s for s, _ in q["subs"]]
        self.assertEqual(sigs, ["EDID", "VMAD", "DNAM", "NEXT", "ANAM", "ALST", "ALID", "FNAM", "ALFR", "ALED"])
        subs = dict(q["subs"])
        self.assertEqual(subs["EDID"], b"AoS_MCMQuest\x00")
        flags, _prio, _fv, _unk, qtype = struct.unpack("<HBB4sI", subs["DNAM"])
        self.assertEqual(len(subs["DNAM"]), 12)
        self.assertTrue(flags & 0x0001, "Start Game Enabled")
        self.assertEqual(qtype, 0)
        self.assertEqual(struct.unpack("<I", subs["ANAM"])[0], 1)
        self.assertEqual(struct.unpack("<I", subs["ALST"])[0], 0)
        self.assertEqual(subs["ALID"], b"PlayerAlias\x00")
        self.assertEqual(struct.unpack("<I", subs["ALFR"])[0], 0x14, "PlayerRef de Skyrim.esm")
        self.assertEqual(subs["NEXT"], b"")
        self.assertEqual(subs["ALED"], b"")

    def test_vmad(self):
        q = self.quest()
        r = Reader(dict(q["subs"])["VMAD"])
        version, obj_format, count = r.take("<hhH")
        self.assertEqual((version, obj_format, count), (5, 2, 1))
        self.assertEqual(r.script(), ("AgeOfSkyrimMCM", 0))
        bind_version, fragments = r.take("<bH")
        self.assertEqual((bind_version, fragments), (2, 0))
        self.assertEqual(r.wstring(), "")
        self.assertEqual(r.take("<H"), 1)  # aliases
        _unused, alias_id, fid = r.take("<HhI")
        self.assertEqual(alias_id, 0)
        self.assertEqual(fid, q["fid"], "alias aponta para a própria quest")
        self.assertEqual(r.take("<hhH"), (5, 2, 1))
        self.assertEqual(r.script(), ("SKI_PlayerLoadGameAlias", 0))
        self.assertEqual(r.pos, len(r.data), "bytes sobrando no VMAD")


if __name__ == "__main__":
    unittest.main()
