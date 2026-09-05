import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare", ROOT / "scripts/prepare-native-bridge.py")
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)

class PrepareTests(unittest.TestCase):
    def test_unique_anchors(self):
        self.assertEqual(m.replace_once("abc", "b", "x"), "axc")
        for value in ("ac", "abbc"):
            with self.assertRaises(ValueError): m.replace_once(value, "b", "x")

    def test_versioned_core(self):
        source = 'constexpr auto kAddonVersion = "beta0.4.2";\nconstexpr auto kBuildIdentity = "beta0.4.2";\n'
        result = m.patch_core(source)
        self.assertEqual(result.count(m.EXPERIMENT), 2)
        self.assertIn('interop/nr_native_bridge_consumer.inl', result)
        with self.assertRaises(ValueError): m.patch_core(result)

    def test_identity(self):
        self.assertEqual(m.blob(b"test content\n"), "d670460b4b4aece5915caf5c68d12f560a9fe3e4")

    def test_forward_patch_and_native_only(self):
        source = '''prefix
    Breadcrumb("forwarding the game's DLSS evaluate");
    bool old_prediction = true;
    return r;
}

static NVSDK_NGX_Result ForwardCreate(Hook &h, ID3D11DeviceContext *ctx, int feature_id,
    dummy);
g_vk_mirror = CfgKeyInt("vk_mirror", 1) != 0 ? 1 : 0;
'''
        forward = (ROOT / "src/interop/nr_native_bridge_forward.inl").read_text()
        result = m.patch_host(source, forward)
        self.assertNotIn("old_prediction", result)
        self.assertIn("g_nr_native_delivery && !g_bridge.partial_output", result)
        self.assertIn('writable->Set("Reset", original_reset)', result)
        self.assertIn("g_vk_mirror = 0", result)
        self.assertLess(result.index("BridgeFrame(ctx, p)"), result.index("if (g_nr_native_delivery"))
        self.assertIn("if (!fast) r = native();", result)
        with self.assertRaises(ValueError): m.patch_host(result, forward)

    def test_wrong_forward_boundary_rejected(self):
        with self.assertRaises(ValueError): m.patch_host("wrong version", "new code")

if __name__ == "__main__": unittest.main()
