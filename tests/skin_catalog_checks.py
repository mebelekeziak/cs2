"""Offline parser/catalog regressions. No installed game or process is needed."""
import importlib.util
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("skin_catalog_generator", ROOT / "tools/generate_skin_catalog.py")
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)

ITEMS = r'''
"items_game" {
    "prefabs" {
        "base" { "flexible_loadout_category" "rifle" "model_player" "weapons/base.vmdl"
                 "used_by_classes" { "terrorists" "1" "counter-terrorists" "1" } }
        "gun" { "prefab" "base" "capabilities" { "paintable" "1" } }
    }
    "items" { "16" { "name" "weapon_m4a1" "prefab" "gun" "item_name" "#m4" } }
    // Repeated top-level sections must be merged; current Valve files use many.
    "prefabs" { "silencer" { "prefab" "gun" "used_by_classes" { "terrorists" "0" } } }
    "items" { "60" { "name" "weapon_m4a1_silencer" "prefab" "silencer" "item_name" "#m4s" } }
    "paint_kits" {
        "0" { "name" "default" }
        "10" { "name" "cu_phase1" "description_tag" "#phase" "use_legacy_model" "1" }
    }
    "paint_kits" {
        "11" { "name" "cu_phase2" "description_tag" "#phase" "wear_remap_min" "0.00" "wear_remap_max" "0.08" }
        "9001" { "name" "workshop_default" }
    }
    "client_loot_lists" { "test" { "[cu_phase2]weapon_m4a1_silencer" "1" } }
}
'''
LABELS = r'''"lang" { "Tokens" { "m4" "M4A4" "M4s" "M4A1-S" "PHASE" "Doppler" } }'''
ICONS = {"panorama/images/econ/default_generated/weapon_m4a1_cu_phase1_light_png.vtex_c",
         "panorama/images/econ/default_generated/weapon_m4a1_silencer_cu_phase1_medium_png.vtex_c",
         "panorama/images/econ/default_generated/weapon_m4a1_silencer_cu_phase1_heavy_png.vtex_c"}


class ParserChecks(unittest.TestCase):
    def fixture(self):
        return catalog.build_catalog(ITEMS.encode(), LABELS.encode("utf-16"), ICONS)

    def test_repeat_sections_and_prefab_team_overrides(self):
        items, paints = self.fixture()
        self.assertEqual([item["id"] for item in items], [16, 60])
        self.assertEqual([item["label"] for item in items], ["M4A4", "M4A1-S"])
        self.assertEqual([item["team_mask"] for item in items], [3, 2])
        self.assertTrue(all(item["model"] == "weapons/base.vmdl" for item in items))
        self.assertEqual([paint["id"] for paint in paints], [0, 10, 11])

    def test_exact_item_prefix_and_icon_duplicates(self):
        items, _ = self.fixture()
        self.assertEqual(items[0]["compatibility"], [0, 10])
        self.assertEqual(items[1]["compatibility"], [0, 10, 11])

    def test_duplicate_finish_labels_preserve_phase_ids(self):
        _, paints = self.fixture()
        self.assertEqual(paints[1]["label"], paints[2]["label"])
        self.assertNotEqual(paints[1]["name"], paints[2]["name"])
        self.assertTrue(paints[1]["legacy"])
        self.assertEqual((paints[2]["min"], paints[2]["max"]), (0.0, 0.08))

    def test_comments_escapes_and_quoted_braces(self):
        result = catalog.keyvalues('/*ignored*/ "a" { "key" "a\\\\b\\\"c" "brace" "}" } //tail\n')
        self.assertEqual(result, [("a", [("key", 'a\\b"c'), ("brace", "}")])])

    def test_invalid_input_and_prefab_cycles_reject(self):
        for malformed in ('"a" { "b" "c"', '}', '"a"', '"a" { "b" }'):
            with self.assertRaises(ValueError):
                catalog.keyvalues(malformed)
        with self.assertRaisesRegex(ValueError, "Cyclic"):
            catalog.build_catalog(ITEMS.replace('"prefab" "base"', '"prefab" "gun"').encode(), LABELS.encode(), ICONS)

    def test_utf8_cpp_strings_do_not_depend_on_codepage(self):
        self.assertEqual(catalog.cpp_string('a"\\\u2605'), '"a\\"\\\\\\342\\230\\205"')


class GeneratedCatalogChecks(unittest.TestCase):
    def test_generated_metadata_is_complete_and_self_consistent(self):
        source = (ROOT / "src/skin_catalog_data.inc").read_text(encoding="utf-8")
        self.assertIn("scripts/items/items_game.txt SHA-256:", source)
        self.assertIn("resource/csgo_english.txt SHA-256:", source)
        items, remaining = source.split("constexpr Item kItems[] = {\n", 1)[1].split("};", 1)
        paints, remaining = remaining.split("constexpr Paint kPaints[] = {\n", 1)[1].split("};", 1)
        compatible, remaining = remaining.split("constexpr int kCompatiblePaints[] = {\n", 1)[1].split("};", 1)
        ranges = remaining.split("constexpr PaintRange kPaintRanges[] = {\n", 1)[1].split("};", 1)[0]
        item_ids = [int(value) for value in re.findall(r"^    \{(\d+),", items, re.M)]
        paint_ids = [int(value) for value in re.findall(r"^    \{(\d+),", paints, re.M)]
        pairs = [int(value) for value in re.findall(r"\d+", compatible)]
        rows = [tuple(map(int, row)) for row in re.findall(r"\{(\d+), (\d+), (\d+)\}", ranges)]
        self.assertEqual(item_ids, sorted(set(item_ids)))
        self.assertEqual(paint_ids, sorted(set(paint_ids)))
        self.assertEqual(len(item_ids), 68)
        self.assertEqual(len(paint_ids), 1480)
        self.assertEqual([row[0] for row in rows], item_ids)
        visited = set()
        offset = 0
        for _, begin, count in rows:
            self.assertEqual(begin, offset)
            supported = pairs[begin:begin + count]
            self.assertEqual(supported, sorted(set(supported)))
            self.assertEqual(supported[0], 0)
            self.assertTrue(set(supported) <= set(paint_ids))
            visited.update(supported)
            offset += count
        self.assertEqual(offset, len(pairs))
        self.assertEqual(visited, set(paint_ids))
        self.assertEqual(len(pairs) - len(item_ids), 2128)


if __name__ == "__main__":
    unittest.main()
