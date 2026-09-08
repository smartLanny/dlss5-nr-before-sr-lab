from __future__ import annotations

import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/configure-carrier-exclusion.py"


def load_module():
    spec = importlib.util.spec_from_file_location("carrier_exclusion", SCRIPT)
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


tool = load_module()


class CarrierExclusionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="nr-carrier-exclusion-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.ini = self.root / "ReShade.ini"

    def write(self, text: str = "[GENERAL]\nEffectSearchPaths=.\n", bom: bool = False) -> bytes:
        data = text.encode("utf-8")
        if bom:
            data = b"\xef\xbb\xbf" + data
        self.ini.write_bytes(data)
        return data

    def carrier(self, name: str = "dlss5-native-carrier-current.addon64", directory: Path | None = None) -> Path:
        destination = (directory or self.root) / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(b"fixture")
        return destination

    def test_d3d12_and_vulkan_preview_then_exclude_actual_files(self):
        for api in ("d3d12", "vulkan"):
            with self.subTest(api=api):
                original = self.write()
                self.carrier()
                self.carrier("dlss5-bridge.addon64")
                (self.root / "nrchain_nvngx.dll").write_bytes(b"keep")
                (self.root / "DLSS5-core.addon64").write_bytes(b"keep")
                plan = tool.make_plan(self.ini, api)
                self.assertEqual(plan.status, "would-apply")
                self.assertEqual(self.ini.read_bytes(), original, "preview must not write")
                result = tool.apply_plan(plan)
                self.assertEqual(result["status"], "applied")
                appended = self.ini.read_text(encoding="utf-8")
                self.assertIn("@dlss5-native-carrier-current.addon64", appended)
                self.assertIn("@dlss5-bridge.addon64", appended)
                self.assertNotIn("nrchain_nvngx.dll", appended)
                self.assertNotIn("DLSS5-core.addon64", appended)
                tool.restore(self.ini, result["receipt"])
                (self.root / "dlss5-native-carrier-current.addon64").unlink()
                (self.root / "dlss5-bridge.addon64").unlink()
                (self.root / "nrchain_nvngx.dll").unlink()
                (self.root / "DLSS5-core.addon64").unlink()

    def test_d3d11_and_unknown_never_change_configuration(self):
        for api in ("d3d11", "unknown"):
            with self.subTest(api=api):
                original = self.write()
                self.carrier()
                (self.root / "d3d11.dll").write_bytes(b"helper device must not infer API")
                plan = tool.make_plan(self.ini, api)
                self.assertEqual(plan.status, "no-change-api")
                result = tool.apply_plan(plan)
                self.assertEqual(result["status"], "no-change-api")
                self.assertEqual(self.ini.read_bytes(), original)
                self.assertEqual(list(self.root.glob("*.carrier-exclusion.*")), [])
                (self.root / "dlss5-native-carrier-current.addon64").unlink()
                (self.root / "d3d11.dll").unlink()

    def test_duplicate_keys_escaped_comma_existing_entries_and_name_only(self):
        actual = "dlss5-native-carrier-one,two.addon64"
        original = self.write(
            "[ADDON]\nDisabledAddons=Core,Custom,,Name,@dlss5-bridge.addon64\n"
            "[OTHER]\nDisabledAddons=@ignored.addon64\n"
            "[ADDON]\nDisabledAddons=dlss5-native-carrier-one,,two.addon64\n")
        self.carrier(actual)
        self.carrier("dlss5-bridge.addon64")
        plan = tool.make_plan(self.ini, "d3d12")
        self.assertEqual(plan.entries, ("@" + actual,))
        result = tool.apply_plan(plan)
        data = self.ini.read_bytes()
        self.assertTrue(data.startswith(original), "existing INI bytes must be an exact prefix")
        self.assertIn(("@" + actual.replace(",", ",,")).encode(), data)
        self.assertEqual(data.count(b"@dlss5-bridge.addon64"), 1)
        self.assertEqual(tool.addon_values(data.decode())["DisabledAddons"],
                         ["Core", "Custom,Name", "@dlss5-bridge.addon64", actual, "@" + actual])
        self.assertEqual(tool.make_plan(self.ini, "d3d12").status, "already-excluded")
        self.assertEqual(tool.apply_plan(tool.make_plan(self.ini, "d3d12"))["status"], "already-excluded")
        tool.restore(self.ini, result["receipt"])

    def test_unicode_utf8_bom_crlf_and_actual_filename_case_are_preserved(self):
        original = self.write("[GENERAL]\r\n说明=保留\r\n", bom=True)
        name = "DLSS5-Native-Carrier-Ünicode.addon64"
        self.carrier(name)
        result = tool.apply_plan(tool.make_plan(self.ini, "vulkan"))
        data = self.ini.read_bytes()
        self.assertTrue(data.startswith(original))
        self.assertEqual(data[:3], b"\xef\xbb\xbf")
        self.assertIn(("DisabledAddons=@" + name + "\r\n").encode("utf-8"), data)
        self.assertNotIn(b"\n", data.replace(b"\r\n", b""))
        tool.restore(self.ini, result["receipt"])

    def test_addon_path_uses_first_value_and_does_not_scan_recursively(self):
        first = self.root / "mods"
        second = self.root / "ignored"
        self.write("[ADDON]\nAddonPath=mods\nAddonPath=ignored\n")
        self.carrier("dlss5-native-carrier-first.addon64", first)
        self.carrier("dlss5-native-carrier-second.addon64", second)
        nested = first / "nested"
        self.carrier("dlss5-native-carrier-nested.addon64", nested)
        plan = tool.make_plan(self.ini, "d3d12")
        self.assertEqual(plan.addon_path, first)
        self.assertEqual(plan.carriers, ("dlss5-native-carrier-first.addon64",))

    def test_direct_load_refuses_apply_without_editing_or_evidence_files(self):
        original = self.write(
            "[ADDON]\nLoadFromDllMain=dlss5-native-carrier-direct.addon64\n")
        self.carrier("dlss5-native-carrier-direct.addon64")
        plan = tool.make_plan(self.ini, "d3d12")
        self.assertEqual(plan.status, "unsupported-direct-load")
        with self.assertRaisesRegex(tool.CarrierConfigError, "LoadFromDllMain"):
            tool.apply_plan(plan)
        self.assertEqual(self.ini.read_bytes(), original)
        self.assertEqual(list(self.root.glob("*.carrier-exclusion.*")), [])

    def test_apply_backup_receipt_repeat_restore_and_changed_current_refusal(self):
        original = self.write()
        self.carrier()
        result = tool.apply_plan(tool.make_plan(self.ini, "d3d12"))
        backup = self.root / result["backup"]
        receipt = self.root / result["receipt"]
        self.assertEqual(backup.read_bytes(), original)
        info = json.loads(receipt.read_text(encoding="utf-8"))
        self.assertEqual(info["beforeSha256"], tool.digest(original))
        self.assertEqual(info["afterSha256"], tool.digest(self.ini.read_bytes()))
        evidence = set(self.root.glob("*.carrier-exclusion.*"))
        repeated = tool.apply_plan(tool.make_plan(self.ini, "d3d12"))
        self.assertEqual(repeated["status"], "already-excluded")
        self.assertEqual(set(self.root.glob("*.carrier-exclusion.*")), evidence)

        self.ini.write_bytes(self.ini.read_bytes() + b"; user edit\n")
        with self.assertRaisesRegex(tool.CarrierConfigError, "current INI differs"):
            tool.restore(self.ini, receipt.name)
        self.ini.write_bytes(tool.append_exclusion(original, "utf-8", ["@dlss5-native-carrier-current.addon64"]))
        restored = tool.restore(self.ini, receipt.name)
        self.assertEqual(restored["status"], "restored")
        self.assertEqual(self.ini.read_bytes(), original)
        with self.assertRaisesRegex(tool.CarrierConfigError, "current INI differs"):
            tool.restore(self.ini, receipt.name)
        self.assertTrue(backup.exists() and receipt.exists(), "restore keeps evidence")

    def test_plan_detects_concurrent_modification_without_backup_or_overwrite(self):
        self.write()
        self.carrier()
        plan = tool.make_plan(self.ini, "d3d12")
        changed = b"[GENERAL]\nUserChanged=1\n"
        self.ini.write_bytes(changed)
        with self.assertRaisesRegex(tool.CarrierConfigError, "changed after planning"):
            tool.apply_plan(plan)
        self.assertEqual(self.ini.read_bytes(), changed)
        self.assertEqual(list(self.root.glob("*.carrier-exclusion.*")), [])

    def test_restore_rejects_path_receipt_tamper_backup_tamper_and_symlink(self):
        self.write()
        self.carrier()
        result = tool.apply_plan(tool.make_plan(self.ini, "d3d12"))
        with self.assertRaisesRegex(tool.CarrierConfigError, "basename"):
            tool.restore(self.ini, str(self.root / result["receipt"]))
        receipt = self.root / result["receipt"]
        valid_receipt = receipt.read_bytes()
        receipt.write_text("[]\n", encoding="utf-8")
        with self.assertRaisesRegex(tool.CarrierConfigError, "JSON object"):
            tool.restore(self.ini, receipt.name)
        receipt.write_bytes(valid_receipt)
        backup = self.root / result["backup"]
        backup.write_bytes(b"tampered")
        with self.assertRaisesRegex(tool.CarrierConfigError, "backup no longer matches"):
            tool.restore(self.ini, result["receipt"])

        target = self.root / "real.ini"
        target.write_text("[GENERAL]\n", encoding="utf-8")
        link = self.root / "linked.ini"
        try:
            os.symlink(target, link)
        except (OSError, NotImplementedError):
            return
        with self.assertRaisesRegex(tool.CarrierConfigError, "symlink"):
            tool.make_plan(link, "d3d12")

    def test_unsupported_encoding_large_file_and_cli_preview(self):
        self.ini.write_bytes("[ADDON]\n说明=坏\n".encode("utf-16"))
        with self.assertRaisesRegex(tool.CarrierConfigError, "UTF-8"):
            tool.make_plan(self.ini, "d3d12")
        self.ini.write_bytes(b"[ADDON]\nDisabledAddons=old\x1a\n")
        with self.assertRaisesRegex(tool.CarrierConfigError, "control character"):
            tool.make_plan(self.ini, "d3d12")
        self.ini.write_bytes(b"x" * (tool.MAX_INI_BYTES + 1))
        with self.assertRaisesRegex(tool.CarrierConfigError, "4 MiB"):
            tool.make_plan(self.ini, "d3d12")

        self.write()
        self.carrier()
        result = subprocess.run(
            [sys.executable, str(SCRIPT), "--ini", str(self.ini), "--api", "d3d12"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        preview = json.loads(result.stdout)
        self.assertEqual(preview["status"], "would-apply")
        self.assertEqual(self.ini.read_bytes(), b"[GENERAL]\nEffectSearchPaths=.\n")

    def test_unicode_line_separator_is_data_not_a_new_ini_line(self):
        self.write("[ADDON]\nDisabledAddons=Custom\u2028Name\n")
        values = tool.addon_values(self.ini.read_text(encoding="utf-8"))
        self.assertEqual(values["DisabledAddons"], ["Custom\u2028Name"])

    def test_cli_apply_restore_and_direct_load_error_contract(self):
        original = self.write()
        self.carrier()
        applied = subprocess.run(
            [sys.executable, str(SCRIPT), "--ini", str(self.ini), "--api", "d3d12", "--apply"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(applied.returncode, 0, applied.stderr)
        applied_json = json.loads(applied.stdout)
        self.assertEqual(applied_json["status"], "applied")
        restored = subprocess.run(
            [sys.executable, str(SCRIPT), "--ini", str(self.ini),
             "--restore", applied_json["receipt"]],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(restored.returncode, 0, restored.stderr)
        self.assertEqual(json.loads(restored.stdout)["status"], "restored")
        self.assertEqual(self.ini.read_bytes(), original)

        direct = b"[ADDON]\nLoadFromDllMain=dlss5-native-carrier-direct.addon64\n"
        self.ini.write_bytes(direct)
        refused = subprocess.run(
            [sys.executable, str(SCRIPT), "--ini", str(self.ini), "--api", "d3d12", "--apply"],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(refused.returncode, 2)
        refused_json = json.loads(refused.stdout)
        self.assertEqual(refused_json["error"], "unsupported-direct-load")
        self.assertEqual(self.ini.read_bytes(), direct)


if __name__ == "__main__":
    unittest.main()
