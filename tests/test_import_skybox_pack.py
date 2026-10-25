"""Workspace-only synthetic import fixtures; no game install or network needed."""
import importlib.util
import io
from pathlib import Path
import stat
import struct
import tempfile
import unittest
import zipfile
import zlib

SPEC = importlib.util.spec_from_file_location("import_skybox_pack", Path(__file__).resolve().parents[1] / "tools/import_skybox_pack.py")
pack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(pack)


def resource():
    out = bytearray(128)
    struct.pack_into("<IHHII", out, 0, 128, 12, 1, 8, 1)
    struct.pack_into("<4sII", out, 16, b"DATA", 12, 40)
    struct.pack_into("<HH", out, 32, 1, 16)
    struct.pack_into("<HHHBB", out, 52, 512, 512, 1, 19, 8)
    return bytes(out)


class ImportChecks(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cs2-skybox-import-checks-")
        self.root = Path(self.temp.name)
        self.destination = self.root / "destination"

    def tearDown(self):
        self.temp.cleanup()

    def archive(self, entries):
        p = self.root / "pack.zip"
        with zipfile.ZipFile(p, "w") as out:
            for name, data in entries:
                out.writestr(name, data)
        return p

    def test_zip_preserves_dependencies_and_ignores_executable(self):
        source = self.archive([("LynX/materials/skies/Sky.vtex_c", resource()),
                               ("LynX/materials/skies/sky.vmat_c", resource()), ("installer.exe", b"not executed")])
        names = pack.import_pack(source, self.destination)
        self.assertEqual(names, ["materials/skies/sky.vmat_c", "materials/skies/sky.vtex_c"])
        self.assertEqual((self.destination / names[1]).read_bytes(), resource())
        self.assertFalse((self.destination / "installer.exe").exists())

    def test_overwrite_rejected_without_partial_import(self):
        source = self.archive([("materials/skies/first.vtex_c", resource()), ("materials/skies/last.vtex_c", resource())])
        conflict = self.destination / "materials/skies/last.vtex_c"
        conflict.parent.mkdir(parents=True)
        conflict.write_bytes(b"existing user content")
        with self.assertRaisesRegex(ValueError, "overwrite"):
            pack.import_pack(source, self.destination)
        self.assertEqual(conflict.read_bytes(), b"existing user content")
        self.assertFalse((conflict.parent / "first.vtex_c").exists())

    def test_traversal_is_rejected_before_any_install(self):
        source = self.archive([("materials/skies/ok.vtex_c", resource()), ("../materials/skies/out.vtex_c", resource())])
        with self.assertRaisesRegex(ValueError, "Unsafe"):
            pack.import_pack(source, self.destination)
        self.assertFalse(self.destination.exists())

    def test_case_collision_rejected(self):
        source = self.archive([("materials/skies/Test.vtex_c", resource()), ("materials/skies/test.vtex_c", resource())])
        with self.assertRaisesRegex(ValueError, "colliding"):
            pack.import_pack(source, self.destination)

    def test_bad_resource_has_no_side_effects(self):
        source = self.archive([("materials/skies/ok.vtex_c", resource()), ("materials/skies/bad.vtex_c", b"garbage" * 20)])
        with self.assertRaisesRegex(ValueError, "header"):
            pack.import_pack(source, self.destination)
        self.assertFalse(self.destination.exists())

    def test_zip_symlink_rejected(self):
        source = self.root / "symlink.zip"
        entry = zipfile.ZipInfo("materials/skies/link.vtex_c")
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(source, "w") as out:
            out.writestr(entry, "elsewhere")
        with self.assertRaisesRegex(ValueError, "nonregular"):
            pack.import_pack(source, self.destination)

    def test_extracted_directory(self):
        source = self.root / "source"
        file = source / "materials/skies/test.vtex_c"
        file.parent.mkdir(parents=True)
        file.write_bytes(resource())
        self.assertEqual(pack.import_pack(source, self.destination), ["materials/skies/test.vtex_c"])

    def test_embedded_vpk(self):
        data = resource()
        tree = b"vtex_c\0materials/skies\0test\0" + struct.pack("<IHHIIH", zlib.crc32(data), 0, 0x7FFF, 0, len(data), 0xFFFF) + b"\0\0\0"
        source = self.root / "pack.vpk"
        source.write_bytes(struct.pack("<III", 0x55AA1234, 1, len(tree)) + tree + data)
        self.assertEqual(pack.import_pack(source, self.destination), ["materials/skies/test.vtex_c"])

    def test_vpk_crc_failure(self):
        data = resource()
        tree = b"vtex_c\0materials/skies\0test\0" + struct.pack("<IHHIIH", 0, 0, 0x7FFF, 0, len(data), 0xFFFF) + b"\0\0\0"
        source = self.root / "bad.vpk"
        source.write_bytes(struct.pack("<III", 0x55AA1234, 1, len(tree)) + tree + data)
        with self.assertRaisesRegex(ValueError, "CRC"):
            pack.import_pack(source, self.destination)

    def test_ambiguous_windows_paths(self):
        for name in ("materials/CON/test.vtex_c", "materials/skies/test.vtex_c.", "C:/materials/skies/test.vtex_c"):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    pack.resource_name(name)


if __name__ == "__main__":
    unittest.main()
