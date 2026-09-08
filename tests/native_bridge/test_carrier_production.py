"""Production carrier invariants against the actual generated source."""
import os
import unittest
from pathlib import Path


@unittest.skipUnless(os.environ.get('CARRIER_COMPLETION_SOURCE'), 'requires actual staged carrier')
class ProductionCarrierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(os.environ['CARRIER_COMPLETION_SOURCE'])
        cls.host = (root / 'carrier/src/dlss5-bridge.cpp').read_text(encoding='utf-8')
        cls.bridge = (root / 'carrier/src/bridge.inc').read_text(encoding='utf-8')

    def test_synchronous_addon_init_and_minimal_dllmain(self):
        self.assertIn('extern "C" __declspec(dllexport) bool AddonInit(HMODULE module, HMODULE reshade)', self.host)
        init = self.host[self.host.index('extern "C" __declspec(dllexport) bool AddonInit('):]
        dllmain = self.host[self.host.index('BOOL APIENTRY DllMain('):]
        self.assertIn('RegisterWithReShade(module)', init)
        self.assertIn('CarrierRegisterEvents()', init)
        self.assertIn('g_addon_init_reshade = reshade', init)
        self.assertIn('MH_Initialize()', init)
        self.assertIn('g_unregister(module)', init)
        for forbidden in ('RegisterWithReShade(', 'CarrierRegisterEvents(', 'CreateThread(',
                          'StartWatchingForNgx(', 'GetProcAddress(', 'Log('):
            self.assertNotIn(forbidden, dllmain)

    def test_trampoline_publication_and_enabled_hook_retention(self):
        install = self.host[self.host.index('static bool HookInstall('):]
        install = install[:install.index('static bool HookUninstall(')]
        self.assertLess(install.index('h.target = static_cast<BYTE *>(trampoline)'),
                        install.index('MH_EnableHook(target)'))
        self.assertLess(install.index('h.module_lease = held'), install.index('MH_EnableHook(target)'))
        self.assertIn('GET_MODULE_HANDLE_EX_FLAG_PIN |', install)
        uninstall = self.host[self.host.index('static bool HookUninstall('):]
        uninstall = uninstall[:uninstall.index('// Forwarders call')]
        self.assertIn('if (h.active || InterlockedCompareExchange(&h.callbacks, 0, 0) != 0)', uninstall)
        self.assertNotIn('MH_DisableHook', uninstall)
        self.assertIn('MH_RemoveHook(h.entry)', uninstall)
        self.assertIn('static void HookRemove(Hook &) {}', self.host)
        self.assertLess(self.host.index('HookInstall(L.release, release'),
                        self.host.index('HookInstall(L.create, create'))
        self.assertIn('!layer.create.active || !layer.release.active', self.host)

    def test_native_create_release_evaluate_share_operation_lock(self):
        for signature in ('static NVSDK_NGX_Result ForwardCreate(',
                          'static NVSDK_NGX_Result ForwardEvaluate(',
                          'static NVSDK_NGX_Result ForwardRelease('):
            body = self.host[self.host.index(signature):]
            body = body[:body.index('\n}\n')]
            self.assertIn('HookCallGuard callback(h);', body)
            self.assertIn('CarrierOperationGuard operation;', body)
        self.assertIn('g_carrier_registry.StillCurrent(admitted)', self.host)
        self.assertIn('if (retire) CarrierRetireLocked(device);', self.host)
        self.assertIn('if (g_carrier_bridge_owner != device) return;', self.host)
        self.assertIn('const bool released = BridgeReleaseSession();', self.host)
        create = self.host[self.host.index('static NVSDK_NGX_Result ForwardCreate('):]
        outer = create.index('Breadcrumb("forwarding the game\'s DLSS create")')
        self.assertLess(create.index('carrier_create_flags_known'), outer)
        self.assertLess(outer, create.index(
            'reinterpret_cast<PFN_Create>(h.target)(ctx, feature_id, p, out)', outer))

    def test_real_swapchain_identity_and_unknown_create_feedback(self):
        self.assertIn('CarrierNativeSwapchain(swapchain), CarrierNativeDevice(device), resize', self.host)
        self.assertIn('CarrierNativeSwapchain(swapchain), native, resize', self.host)
        self.assertIn('incomplete native Create/device/swapchain history', self.host)
        self.assertIn('Use F8 BUG feedback', self.host)

    def test_loader_notification_and_layer_mutation_are_synchronized(self):
        callback = self.host[self.host.index('static void CALLBACK OnDllLoaded('):]
        callback = callback[:callback.index('static void CarrierPumpModuleEvents()')]
        self.assertIn('g_carrier_modules.Record(', callback)
        self.assertIn('g_scan_pending.store(true, std::memory_order_release)', callback)
        for forbidden in ('Log(', 'GetProcAddress(', 'TryInstallHooks(', 'BridgeDrainGpu('):
            self.assertNotIn(forbidden, callback)
        self.assertIn('static std::atomic<bool> g_scan_pending{false};', self.host)
        install = self.host[self.host.index('static void TryInstallHooks()'):]
        install = install[:install.index('static void ReportIdle()')]
        self.assertLess(install.index('TryEnterCriticalSection(&g_hook_cs)'),
                        install.index('PromoteObservedLayers();'))
        self.assertLess(install.index('PromoteObservedLayers();'),
                        install.index('HookNewNgxModules();'))

    def test_complete_normal_session_teardown_is_generated(self):
        release = self.bridge[self.bridge.index('static bool BridgeReleaseSession()\n{'):]
        release = release[:release.index('static UINT64 EndCommands()')]
        for token in ('g_nr_destroy_parameters(g_bridge.params)',
                      'g_nr_destroy_parameters(g_nr_capability_parameters)',
                      'g_nr_shutdown1(g_bridge.dev12)', 'g_bridge.ctx4->Release()',
                      'g_bridge.mt->Release()', 'g_bridge.queue->Release()',
                      'g_bridge.dev12->Release()', 'CloseHandle(g_bridge.fence_event)'):
            self.assertIn(token, release)
        self.assertIn('if (g_nr_carrier_quarantined) return false;', release)
        self.assertIn('g_nr_ngx_initialized = true;', self.bridge)
        self.assertIn('required NGX lifecycle exports missing', self.bridge)


if __name__ == '__main__':
    unittest.main()
