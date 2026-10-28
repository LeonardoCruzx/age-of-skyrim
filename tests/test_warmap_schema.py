"""M11: schema do snapshot do mapa tático (docs/warmap-schema.json).

- Os dados de exemplo (tools/warmap_sample.py) seguem o schema — com 60 e com 300 territórios.
- O DLL (src/UI/WarMap.cpp) escreve exatamente os campos do schema (checagem estática do código-fonte).
- Todas as chaves de tradução usadas pela view e pelos menus existem em en.json (e pt-br, pelo check_translations).
"""
import json
import pathlib
import re
import sys
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import warmap_sample  # noqa: E402

SCHEMA = json.loads((ROOT / "docs/warmap-schema.json").read_text(encoding="utf-8"))
CPP = (ROOT / "src/UI/WarMap.cpp").read_text(encoding="utf-8")
VIEW = ROOT / "data/PrismaUI/views/AgeOfSkyrim"
TYPES = {"string": str, "boolean": bool, "object": dict, "array": list, "null": type(None)}


def _type_ok(value, t):
    if t == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if t == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    return isinstance(value, TYPES[t])


def validate(value, schema, path="$", root=SCHEMA):
    """Validador do subconjunto de JSON Schema usado em docs/warmap-schema.json. Devolve lista de erros."""
    if "$ref" in schema:
        node = root
        for part in schema["$ref"].lstrip("#/").split("/"):
            node = node[part]
        return validate(value, node, path, root)
    errors = []
    types = schema.get("type")
    if types is not None:
        types = types if isinstance(types, list) else [types]
        if not any(_type_ok(value, t) for t in types):
            return [f"{path}: tipo {type(value).__name__}, esperado {types}"]
    if value is None:
        return errors
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: {value!r} fora de {schema['enum']}")
    if "pattern" in schema and isinstance(value, str) and not re.search(schema["pattern"], value):
        errors.append(f"{path}: {value!r} não casa {schema['pattern']}")
    if "minimum" in schema and isinstance(value, (int, float)) and value < schema["minimum"]:
        errors.append(f"{path}: {value} < {schema['minimum']}")
    if isinstance(value, dict):
        for key in schema.get("required", []):
            if key not in value:
                errors.append(f"{path}: falta '{key}'")
        props = schema.get("properties", {})
        extra = schema.get("additionalProperties", True)
        for key, v in value.items():
            if key in props:
                errors += validate(v, props[key], f"{path}.{key}", root)
            elif extra is False:
                errors.append(f"{path}: campo extra '{key}'")
            elif isinstance(extra, dict):
                errors += validate(v, extra, f"{path}.{key}", root)
    if isinstance(value, list) and "items" in schema:
        for i, v in enumerate(value):
            errors += validate(v, schema["items"], f"{path}[{i}]", root)
    return errors


def load_tr(lang):
    text = (ROOT / f"data/SKSE/Plugins/AgeOfSkyrim/Translations/{lang}.json").read_text(encoding="utf-8")
    return json.loads(re.sub(r"^\s*//.*$", "", text, flags=re.MULTILINE))


class SampleTest(unittest.TestCase):
    def test_sample_60_valido(self):
        self.assertEqual(validate(warmap_sample.build(60), SCHEMA), [])

    def test_sample_300_valido_e_tamanho(self):
        data = warmap_sample.build(300)
        self.assertEqual(validate(data, SCHEMA), [])
        self.assertEqual(len(data["territories"]), 300)
        # InteropCall leva uma string: com 300 territórios o snapshot fica bem abaixo de 1 MB.
        self.assertLess(len(json.dumps(data)), 1_000_000)

    def test_referencias_consistentes(self):
        data = warmap_sample.build(60)
        ids = {t["id"] for t in data["territories"]}
        for t in data["territories"]:
            self.assertTrue(set(t["neighbors"]) <= ids)
        for r in data["routes"]:
            self.assertIn(r["a"], ids)
            self.assertIn(r["b"], ids)
        self.assertIn(data["seat"]["territory"], ids)

    def test_validador_rejeita(self):
        data = warmap_sample.build(10)
        data["territories"][0]["id"] = "xyz"
        data["territories"][0]["type"] = "castle"
        data["extra"] = 1
        del data["routes"]
        errors = validate(data, SCHEMA)
        self.assertEqual(len(errors), 4, errors)

    def test_make_id_igual_ao_dll(self):
        # FNV-1a de "" = offset basis; e case-insensitive como MakeId.
        self.assertEqual(warmap_sample.make_id(""), 2166136261)
        self.assertEqual(warmap_sample.make_id("Imperial"), warmap_sample.make_id("imperial"))


class DllSourceTest(unittest.TestCase):
    """O código que monta o snapshot escreve os mesmos campos do schema."""

    def test_campos_raiz(self):
        props = set(SCHEMA["properties"])
        written = set(re.findall(r'root\["(\w+)"\]', CPP))
        self.assertEqual(written, props)

    def test_campos_territorio(self):
        props = set(SCHEMA["properties"]["territories"]["items"]["properties"])
        written = set(re.findall(r'\bj\["(\w+)"\]', CPP))
        self.assertEqual(written, props)

    def test_campos_aninhados(self):
        for section in ("factions", "provinces", "worldspaces", "routes", "layers"):
            for key in SCHEMA["properties"][section]["items"]["properties"]:
                self.assertRegex(CPP, r'\{\s*"%s",' % key, f"{section}.{key} não aparece no WarMap.cpp")
        for key in SCHEMA["properties"]["seat"]["properties"]:
            self.assertIn(f'seat["{key}"]', CPP)
        for key in ("maxRoutes", "maxTargets"):
            self.assertIn(f'"{key}"', CPP)


class TranslationKeysTest(unittest.TestCase):
    def setUp(self):
        self.en = load_tr("en")
        self.pt = load_tr("pt-br")

    def check(self, keys, origin):
        for k in keys:
            if k.endswith("."):  # prefixo de chave dinâmica ('warmap.type.' + tipo): conferido à parte
                continue
            self.assertTrue(k in self.en, f"{origin}: chave '{k}' falta em en.json")
            self.assertTrue(k in self.pt, f"{origin}: chave '{k}' falta em pt-br.json")

    def test_view(self):
        js = (VIEW / "app.js").read_text(encoding="utf-8")
        html = (VIEW / "index.html").read_text(encoding="utf-8")
        keys = set(re.findall(r"""\bt\(\s*'(warmap\.[\w.]+)'""", js))
        keys |= set(re.findall(r'data-i18n(?:-placeholder)?="([\w.]+)"', html))
        self.assertGreater(len(keys), 20)
        self.check(keys, "view")
        self.check({f"warmap.type.{t}" for t in
                    SCHEMA["properties"]["territories"]["items"]["properties"]["type"]["enum"]}, "tipos")

    def test_dll(self):
        keys = set()
        for f in (ROOT / "src/UI").glob("WarMap*.cpp"):
            keys |= set(re.findall(r'"((?:warmap|route|seat|stock)\.[\w.]+)"', f.read_text(encoding="utf-8")))
        self.assertGreater(len(keys), 30)
        self.check(keys, "DLL")


if __name__ == "__main__":
    unittest.main()
