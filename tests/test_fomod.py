"""Confere o instalador FOMOD (fomod/*.xml) e os patches de compatibilidade (patches/) do repositório."""
import pathlib
import sys
import unittest
import xml.etree.ElementTree as ET

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import package_release as pr  # noqa: E402


class FomodTest(unittest.TestCase):
    def test_xml_bem_formado(self):
        for name in ("info.xml", "ModuleConfig.xml"):
            ET.parse(ROOT / "fomod" / name)  # lança ParseError se mal formado

    def test_info_tem_campos(self):
        info = ET.parse(ROOT / "fomod/info.xml").getroot()
        self.assertEqual(info.tag, "fomod")
        for tag in ("Name", "Author", "Version", "Website", "Description"):
            self.assertTrue((info.findtext(tag) or "").strip(), tag)

    def test_core_obrigatorio(self):
        required, _ = pr.fomod_sources(ROOT / "fomod/ModuleConfig.xml")
        self.assertIn(pr.CORE, required)

    def test_patches_batem_com_moduleconfig(self):
        self.assertGreaterEqual(pr.validate_repo(ROOT), 1)

    def test_compat_json_validos(self):
        files = list((ROOT / "patches").rglob("Compat/*.json"))
        self.assertTrue(files)
        for f in files:
            data = pr.load_json(f)
            pr.validate_compat(f, data)
            self.assertEqual(data["name"], f.stem)
            # Toda entrada documentada precisa dizer se foi conferida (ADR 0042).
            for p in data.get("plugins", []):
                self.assertIn("verified", p, f)
                self.assertIn(p["plugin"], data["whenLoaded"], f)

    def test_compat_rejeita_formref_invalido(self):
        with self.assertRaises(pr.PackageError):
            pr.validate_compat("x.json", {"conquest": {"exclude": ["Skyrim.esm:1234"]}})
        with self.assertRaises(pr.PackageError):
            pr.validate_compat("x.json", {"whenLoaded": "Mod.esp"})
        with self.assertRaises(pr.PackageError):
            pr.validate_compat("x.json", {"discovery": {"exclude": ""}})
        pr.validate_compat("x.json", {"discovery": {"exclude": ["Skyrim.esm|0x01A2B3"]}})


if __name__ == "__main__":
    unittest.main()
