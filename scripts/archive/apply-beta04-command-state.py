#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "src" / "nr_before_sr.cpp"


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return text.replace(old, new, 1)


def main() -> None:
    text = SOURCE.read_text(encoding="utf-8")
    marker = '#include "nr_command_state.inl"'
    if marker in text:
        print("beta0.4 command-state envelope is already integrated")
        return

    text = replace_once(
        text,
        """    ModuleObserverInstalling,\n    ModuleObserverUncertain,\n};""",
        """    ModuleObserverInstalling,\n    ModuleObserverUncertain,\n    CommandStateUnverified,\n};""",
        "command-state bypass enum",
    )
    text = replace_once(
        text,
        """    case BypassReason::ModuleObserverUncertain:\n        return \"module-observer-uncertain\";\n    default: return \"unknown\";""",
        """    case BypassReason::ModuleObserverUncertain:\n        return \"module-observer-uncertain\";\n    case BypassReason::CommandStateUnverified:\n        return \"command-state-unverified\";\n    default: return \"unknown\";""",
        "command-state bypass name",
    )
    text = replace_once(
        text,
        """std::size_t g_evaluate_hook_target_count = 0;\nSRWLOCK g_evaluate_hook_lock = SRWLOCK_INIT;\nstd::atomic<bool> g_evaluate_hooks_ready{false};""",
        """std::size_t g_evaluate_hook_target_count = 0;\nSRWLOCK g_evaluate_hook_lock = SRWLOCK_INIT;\n#include \"nr_command_state.inl\"\nstd::atomic<bool> g_evaluate_hooks_ready{false};""",
        "command-state implementation include",
    )
    text = replace_once(
        text,
        """    ReadLifecycleToken(command_list, token);\n    const HRESULT result = original(command_list, allocator, initial_state);\n    if (SUCCEEDED(result) && token != 0) {""",
        """    ReadLifecycleToken(command_list, token);\n    const HRESULT result = original(command_list, allocator, initial_state);\n    OnCommandStateResetResult(command_list, result, initial_state);\n    if (SUCCEEDED(result) && token != 0) {""",
        "native Reset state commit",
    )
    text = replace_once(
        text,
        """    Substitution before_sr = PrepareBeforeSr(\n        command_list, handle, params, feature);\n    PostSrSubstitution after_sr = PrepareAfterSr(\n        command_list, handle, params, feature);\n    if (!before_sr.active && !after_sr.active) {""",
        """    NativeCommandStateScope before_state(command_list);\n    if (!before_state.active) {\n        LeaveCriticalSection(&g_chain_cs);\n        rr_transaction.Release();\n        return original(command_list, handle, params, callback);\n    }\n    Substitution before_sr = PrepareBeforeSr(\n        before_state.command_list, handle, params, feature);\n    PostSrSubstitution after_sr = PrepareAfterSr(\n        before_state.command_list, handle, params, feature);\n    // Feature 18 warm-up, Create or Evaluate failure may still have changed\n    // the game's compute bindings. Restore the exact entry state before SR.\n    before_state.Restore();\n    if (!before_sr.active && !after_sr.active) {""",
        "pre-SR command-state scope",
    )
    text = replace_once(
        text,
        """    if (used_after_sr) {\n        if (result == kNgxSuccess) {\n            FinishAfterSr(command_list, params, feature, after_sr);\n        } else if (after_sr.redirected_output &&""",
        """    if (used_after_sr) {\n        if (result == kNgxSuccess) {\n            // Native SR is allowed to leave a different state from entry.\n            // Snapshot that state and restore it after the post-SR experiment.\n            NativeCommandStateScope after_state(command_list);\n            if (after_state.active) {\n                FinishAfterSr(after_state.command_list, params, feature, after_sr);\n                after_state.Restore();\n            } else if (after_sr.redirected_output) {\n                Log(\"[command-state] post-SR state unavailable; native SR is \"\n                    \"re-evaluated to its original Output without NR work.\");\n                result = original(command_list, handle, params, callback);\n            }\n        } else if (after_sr.redirected_output &&""",
        "post-SR command-state scope",
    )
    text = replace_once(
        text,
        """void OnResetCommandList(reshade::api::command_list *command_list) {\n    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0)""",
        """void OnResetCommandList(reshade::api::command_list *command_list) {\n    OnCommandStateBeforeReset(command_list);\n    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0)""",
        "ReShade Reset state invalidation",
    )
    text = replace_once(
        text,
        """void OnDestroyCommandList(reshade::api::command_list *command_list) {\n    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0)""",
        """void OnDestroyCommandList(reshade::api::command_list *command_list) {\n    OnCommandStateDestroyList(command_list);\n    if (g_lifecycle_active_command_lists.load(std::memory_order_acquire) == 0)""",
        "ReShade destroy state cleanup",
    )
    text = replace_once(
        text,
        """        reshade::register_event<reshade::addon_event::destroy_command_queue>(\n            OnDestroyCommandQueue);\n        reshade::register_event<reshade::addon_event::execute_command_list>(""",
        """        reshade::register_event<reshade::addon_event::destroy_command_queue>(\n            OnDestroyCommandQueue);\n        reshade::register_event<reshade::addon_event::init_device>(\n            OnCommandStateInitDevice);\n        reshade::register_event<reshade::addon_event::init_command_list>(\n            OnCommandStateInitList);\n        reshade::register_event<reshade::addon_event::execute_command_list>(""",
        "command-state event registration",
    )

    unregister = """            reshade::unregister_overlay(nullptr, DrawSettings);"""
    replacement = """            reshade::unregister_event<reshade::addon_event::init_command_list>(\n                OnCommandStateInitList);\n            reshade::unregister_event<reshade::addon_event::init_device>(\n                OnCommandStateInitDevice);\n            reshade::unregister_overlay(nullptr, DrawSettings);"""
    if text.count(unregister) != 1:
        raise RuntimeError(
            f"worker-failure command-state unregister: expected one match, found {text.count(unregister)}"
        )
    text = text.replace(unregister, replacement, 1)

    detach = """    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {\n        reshade::unregister_overlay(nullptr, DrawSettings);"""
    detach_replacement = """    } else if (reason == DLL_PROCESS_DETACH && reserved == nullptr) {\n        reshade::unregister_event<reshade::addon_event::init_command_list>(\n            OnCommandStateInitList);\n        reshade::unregister_event<reshade::addon_event::init_device>(\n            OnCommandStateInitDevice);\n        reshade::unregister_overlay(nullptr, DrawSettings);"""
    text = replace_once(
        text, detach, detach_replacement, "detach command-state unregister"
    )

    SOURCE.write_text(text, encoding="utf-8", newline="\n")
    print("integrated native D3D12 command-state envelope into beta0.4")


if __name__ == "__main__":
    main()
