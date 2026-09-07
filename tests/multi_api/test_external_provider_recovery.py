"""Recovery transformer checks against real pinned source, not invented stubs."""
import importlib.util
import os
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'scripts' / (name + '.py'))
    obj = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(obj)
    return obj


class RecoverySourceTests(unittest.TestCase):
    def test_actual_core_transform_and_repeat_rejection(self):
        transform = module('prepare-external-provider-recovery')
        original = (ROOT / 'src/nr_before_sr.cpp').read_text(encoding='utf-8')
        patched = transform.patch_core(original,
            (ROOT / 'src/nr_feedback_core.inl').read_text(encoding='utf-8'))
        self.assertEqual(patched.count('owner_provider_id.load(\n'), 2)
        self.assertIn('return EnsureNativeCommandListResetHookRaw(native_command_list);', patched)
        self.assertIn('external.recordedFrames=%llu', patched)
        self.assertIn('external.completion=producer-owned; not observed by this snapshot', patched)
        with self.assertRaises(ValueError):
            transform.patch_core(patched)

    def test_pinned_feeder_d3d12_preflight_and_legacy_preservation(self):
        source = os.environ.get('NR_FEEDER_SOURCE')
        if not source:
            self.skipTest('NR_FEEDER_SOURCE must point to the pinned read-only checkout')
        transform = module('patch-feeder-external-nr')
        feeder = Path(source)
        transform.assert_feeder_commit(feeder)
        original = (feeder / 'src/dlss5-feed.cpp').read_text(encoding='utf-8')
        patched = transform.patch_feed_cpp(original)
        start = patched.index('static void FeedFrame12(')
        end = patched.index('static void FeedFrameVk(', start)
        frame = patched[start:end]
        self.assertLess(frame.index('CanPrepareFrame()'), frame.index('get_texture_binding('))
        self.assertLess(frame.index('CanPrepareFrame()'), frame.index('copy_resource('))
        self.assertIn('FeatureMissingForMode() && !nr_external_provider_client::ShouldDisableFeederNr()', frame)
        self.assertIn('const UINT64 submitted = EndCommands();', frame)
        self.assertIn('nr_external_provider_completion::Wait(g.fence12, g.fence_event,', frame)
        self.assertIn('external_delegated = submitted != 0 &&', frame)
        self.assertIn('feed_in.epoch = nr_external_resource_epoch;', frame)
        self.assertNotIn('__has_include', patched)
        # All Vk/GL/D3D11 frame implementations remain byte-for-byte unchanged.
        old_tail = original[original.index('static void FeedFrameVk('):]
        self.assertEqual(patched[end:], old_tail)
        release = patched[patched.index('static bool ReleaseFrameResources()'):]
        self.assertLess(release.index('if (!DrainGpu()) return false;'),
                        release.index('nr_external_provider_client::Release();'))
        self.assertIn('if (!ReleaseFrameResources()) return;', patched)
        with self.assertRaises(ValueError):
            transform.patch_feed_cpp(patched)


if __name__ == '__main__':
    unittest.main()
