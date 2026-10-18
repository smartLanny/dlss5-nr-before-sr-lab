#!/usr/bin/env python3
"""Portable network policy and the actual production Feature 18 Create boundary.

These checks exercise CPU binding, identity, and failure behavior. The bridge
observer does not load a DLL, submit GPU work, or claim game compatibility.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def definition(source: str, marker: str, semicolon: bool = False) -> str:
    start = source.index(marker)
    end = source.index("{", start) + 1
    depth = 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end + int(semicolon)] + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", default="g++")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--layer-config-directory", type=Path,
                        help="Optional independently staged layer settings headers for integration checks")
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    core = (ROOT / "src/nr_before_sr.cpp").read_text(encoding="utf-8-sig")
    receipt = {"schema": "nr-network-cpu-v1", "source_sha256": hashlib.sha256(core.encode()).hexdigest(),
               "scope": "CPU production Create body with observation stubs; no Windows, NGX, GPU or game validation", "runs": []}
    receipt["sources"] = {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in [
        "src/nr_before_sr.cpp", "src/nr_network_policy.h", "src/nr_network_runtime.inl",
        "src/nr_network_identity_worker.inl", "src/nr_diagnostic_bundle.inl",
        "tests/nr_network_policy_test.cpp", "tests/nr_network_create_test.cpp",
        "tests/nr_network_runtime_test.cpp", "tests/aux_history_reuse.cpp",
        "src/nr_dual_runtime.inl", "src/nr_fg_consumer.inl", "src/nr_compat_present_events.inl",
        "src/nr_compat_present_models.inl", "src/nr_compat_present_policy.hpp",
        "src/nr_full_runtime.inl", "src/nr_replay_runtime.inl", "src/nr_replay_format.h",
        "scripts/nr_replay.py", "tests/nr_full_runtime_test.cpp",
        "tests/nr_network_caller_test.cpp", "scripts/validate-nr-network.py"]}
    layer_directory = args.layer_config_directory.resolve() if args.layer_config_directory else ROOT / "src"
    receipt["layer_configuration"] = {"path": str(layer_directory / "nr_layer_config.h"),
        "sha256": hashlib.sha256((layer_directory / "nr_layer_config.h").read_bytes()).hexdigest()}

    def run(command: list[str], label: str, expected: int = 0) -> None:
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        (out / f"{label}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        receipt["runs"].append({"label": label, "exit": result.returncode, "stdout": result.stdout.strip()})
        print(label, result.returncode, result.stdout.strip(), flush=True)
        if result.returncode != expected:
            raise RuntimeError(f"{label}: expected {expected}; {result.stderr[-6000:]}")

    include = ["-I" + str(args.layer_config_directory.resolve())] if args.layer_config_directory else []
    compiler_flags = [args.compiler, "-std=c++20", "-O2", "-Wall", "-Wextra", "-Werror"]
    flags = compiler_flags + ["-I" + str(ROOT / "src")]
    try:
        run([args.compiler, "--version"], "compiler")
        policy = out / "network-policy"
        run(flags + [str(ROOT / "tests/nr_network_policy_test.cpp"), "-o", str(policy)], "policy-build")
        run([str(policy)], "policy")
        runtime_folder = out / "runtime-identity"
        runtime_folder.mkdir()
        bundle = (ROOT / "src/nr_diagnostic_bundle.inl").read_text(encoding="utf-8-sig")
        (runtime_folder / "network-sha.inl").write_text(definition(bundle, "struct Sha256", True), encoding="utf-8")
        runtime_executable = runtime_folder / "runtime-identity"
        run(flags + ["-I" + str(runtime_folder), str(ROOT / "tests/nr_network_runtime_test.cpp"),
                     "-o", str(runtime_executable)], "runtime-identity-build")
        run([str(runtime_executable)], "runtime-identity")
        create = definition(core, "NgxResult CreateSignedNrFeature(")
        tuning = definition(core, "struct NrTuningSignature", True)
        variants = [("candidate", create, 0)]
        token = "nr_network_policy::WriteCreateParameter(tuning.network_preset,"
        if token not in create:
            raise AssertionError("actual Create no longer uses the reviewed typed network binder")
        # Compile the same function with only its new setter disabled. It must
        # fail at the observed bridge boundary, not merely at a source regex.
        negative = create
        setter = "SetUInt(context.params, key, value);"
        if create.count(setter) != 1:
            raise AssertionError("review actual network setter before updating this negative control")
        negative = negative.replace(setter, "(void)key; (void)value;", 1)
        variants.append(("negative-missing-create-binding", negative, 1))
        for label, body, expected in variants:
            folder = out / label
            folder.mkdir()
            (folder / "network-tuning.inl").write_text(tuning, encoding="utf-8")
            (folder / "network-create.inl").write_text(body, encoding="utf-8")
            executable = folder / "create-boundary"
            run(flags + ["-I" + str(folder), str(ROOT / "tests/nr_network_create_test.cpp"), "-o", str(executable)], label + "-build")
            run([str(executable)], label, expected)

        # Execute the actual Create sections of every production owner. The
        # copied sections retain their return/failure/retirement decisions;
        # only preceding GPU preparation is replaced by boundary observations.
        before = definition(core, "Substitution PrepareBeforeSr(")
        post = definition(core, "bool FinishAfterSr(")
        prepare_post = definition(core, "PostSrSubstitution PrepareAfterSr(")
        dual = (ROOT / "src/nr_dual_runtime.inl").read_text(encoding="utf-8-sig")
        fg = (ROOT / "src/nr_fg_consumer.inl").read_text(encoding="utf-8-sig")
        present = (ROOT / "src/nr_compat_present_events.inl").read_text(encoding="utf-8-sig")
        present_models = (ROOT / "src/nr_compat_present_models.inl").read_text(encoding="utf-8-sig")
        network_runtime = (ROOT / "src/nr_network_runtime.inl").read_text(encoding="utf-8-sig")
        replay = (ROOT / "src/nr_replay_runtime.inl").read_text(encoding="utf-8-sig")
        def consumer_boundary(function: str) -> str:
            start = function.index("substitution.fatal_recording=nr_dual_runtime::Run(")
            end = function.index("nr_dual_runtime::Stamp(", start)
            return function[start:end]
        fg_create = definition(definition(fg, "bool ConsumeDetailed("), "if(!context->nr_handle){")
        fg_create = fg_create[fg_create.index("void* handle=nullptr;bool api_called=false;"):-2]
        caller_files = {
            "network-tuning.inl": tuning, "network-create.inl": create,
            "network-admission.inl": definition(network_runtime, "bool ValidateNrActiveNetworkRequests("),
            "network-before-create.inl": definition(before, "if (first) {"),
            "network-post-create.inl": definition(post, "if (first) {"),
            "network-before-no-context.inl": definition(before, "if (context == nullptr) {"),
            "network-post-no-context.inl": definition(prepare_post, "if (context == nullptr) {"),
            "network-aux-create.inl": definition(definition(dual, "NgxResult RunOne("), "if(!aux.nr_handle) {"),
            "network-fg-create.inl": fg_create,
            "network-present-submit.inl": definition(present, "bool Submit("),
            "network-present-retire.inl": definition(present_models, "bool RetireProof("),
            "network-present-layer.inl": definition(present, "bool CreateLayer("),
            "network-present-bootstrap.inl": definition(present, "bool Bootstrap("),
            "network-present-later.inl": definition(definition(present, "bool Frame("), "if(!l.context.nr_handle){"),
            "network-before-consumer.inl": consumer_boundary(before),
            "network-post-consumer.inl": consumer_boundary(post),
            "network-post-timing.inl": definition(post, "struct TimingEnd").strip()+";\nTimingEnd timing{command_list,slot};\n",
            "network-replay-attempt.inl": definition(replay, "struct Attempt", True),
            "network-replay-scalars.inl": definition(replay, "void SnapshotScalars("),
        }
        caller_variants = [("callers", caller_files, "all", 0)]
        removals = [
            ("before", "network-before-create.inl", "if (!api_called) return substitution;"),
            ("post", "network-post-create.inl", definition(caller_files["network-post-create.inl"], "if (!api_called) {")),
            ("aux", "network-aux-create.inl", definition(caller_files["network-aux-create.inl"], "if(!api_called){")),
            ("fg", "network-fg-create.inl", "if(!api_called)return true;"),
            ("present-primary", "network-present-layer.inl", definition(caller_files["network-present-layer.inl"], "if(!api_called){")),
            ("present-later", "network-present-layer.inl", definition(caller_files["network-present-layer.inl"], "if(!api_called){")),
            ("before-consumer", "network-before-consumer.inl", definition(caller_files["network-before-consumer.inl"], "if(dual_prepared.create_admission_refused){")),
            ("post-consumer", "network-post-consumer.inl", definition(caller_files["network-post-consumer.inl"], "if(substitution.dual_prepared.create_admission_refused){")),
        ]
        for route, filename, guard in removals:
            if caller_files[filename].count(guard) != 1:
                raise AssertionError(f"review {route} admission negative-control boundary")
            changed = dict(caller_files)
            changed[filename] = changed[filename].replace(guard, "", 1)
            caller_variants.append(("negative-admission-" + route, changed, route, 1))
        for route, filename in [("present-primary", "network-present-bootstrap.inl"),
                                ("present-later", "network-present-later.inl")]:
            changed = dict(caller_files)
            token = "return Submit(c,slot,source,!admission_refused,false);"
            if caller_files[filename].count(token) != 1:
                raise AssertionError(f"review {route} exact-fence negative control")
            changed[filename] = changed[filename].replace(token,
                "if(admission_refused){return true;} " + token, 1)
            caller_variants.append(("negative-deferred-fence-" + route, changed, route, 1))
        for route, filename, anchor, call in [
            ("before-consumer", "network-before-consumer.inl", "nr_dual_runtime::ResolveTiming(command_list,*slot,false);",
             "replay.Finish(command_list,*context,*slot,true);"),
            ("post-consumer", "network-post-consumer.inl", "PreservePostSrOnFailure(command_list,substitution);",
             "replay.Finish(command_list,context,slot,true);")]:
            changed = dict(caller_files)
            if changed[filename].count(anchor) != 1:
                raise AssertionError(f"review {route} incomplete-replay boundary")
            changed[filename] = changed[filename].replace(anchor, call + anchor, 1)
            caller_variants.append(("negative-unwritten-replay-" + route, changed, route, 1))
        changed = dict(caller_files)
        changed["network-replay-scalars.inl"] = changed["network-replay-scalars.inl"].replace('"DLSSNR.Hint.Render.Preset",', '', 1)
        caller_variants.append(("negative-replay-network-scalar", changed, "replay", 1))
        for label, files, route, expected in caller_variants:
            folder = out / label
            folder.mkdir()
            for name, body in files.items():
                (folder / name).write_text(body, encoding="utf-8")
            executable = folder / "create-callers"
            run(flags + ["-I" + str(folder), str(ROOT / "tests/nr_network_caller_test.cpp"),
                         "-o", str(executable)], label + "-build")
            run([str(executable), route], label, expected)
            if label == "callers":
                header_file = folder / "network-scalars.bin"
                run([str(executable), "replay", str(header_file)], "replay-scalar-writer")
                spec = importlib.util.spec_from_file_location("network_replay_reader", ROOT / "scripts/nr_replay.py")
                reader = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(reader)
                header = reader.Header.from_buffer_copy(header_file.read_bytes())
                observed = reader.scalars(header)["DLSSNR.Hint.Render.Preset"]
                if ctypes.sizeof(header) != 6080 or header.parameter_count > 64 or observed != {"type": 1, "present": True, "value": 2}:
                    raise AssertionError("independent Python reader did not retain the real UInt network metadata")
                receipt["runs"].append({"label": "replay-scalar-reader", "exit": 0, "stdout": "production Python reader retained UInt 2 from actual C++ Header; metadata only"})
                print("replay-scalar-reader 0", observed, flush=True)

        # The complete production full-chain adapter propagates a refusal at
        # every auxiliary position, through both two-layer and 3-5-layer paths.
        full = (ROOT / "src/nr_full_runtime.inl").read_text(encoding="utf-8-sig")
        full_fixture = (ROOT / "tests/nr_full_runtime_test.cpp").read_text(encoding="utf-8-sig")
        full_fixture = full_fixture.replace('#include "../src/nr_full_pass.hpp"', '#include "nr_full_pass.hpp"')
        full_fixture = full_fixture.replace('#include "../src/nr_full_runtime.inl"', '#include "network-full-runtime.inl"')
        propagation = "prepared.create_admission_refused|=stage.create_admission_refused;"
        if full.count(propagation) != 1:
            raise AssertionError("review full-chain admission receipt propagation")
        for label, body, expected in [
            ("full-chain-refusal", full, 0),
            ("negative-full-chain-refusal", full.replace(propagation, "", 1), 1)]:
            folder = out / label
            folder.mkdir()
            (folder / "network-full-runtime.inl").write_text(body, encoding="utf-8")
            fixture = folder / "full-runtime.cpp"
            fixture.write_text(full_fixture, encoding="utf-8")
            executable = folder / "full-runtime"
            run(flags + ["-I" + str(folder), str(fixture), "-o", str(executable)], label + "-build")
            run([str(executable)], label, expected)

        folder = out / "context-identity"
        folder.mkdir()
        tuning_definitions = "".join(definition(core, marker, semi) for marker, semi in [
            ("struct NrTuningSignature", True), ("NrTuningSignature CurrentNrTuningSignature(", False),
            ("bool SameNrTuningSignature(", False)])
        (folder / "tuning.inl").write_text(tuning_definitions, encoding="utf-8")
        lookup = definition(core, "NrContext *FindOrCreateContext(")
        (folder / "lookup.inl").write_text(lookup, encoding="utf-8")
        executable = folder / "context-identity"
        run(compiler_flags + include + ["-I" + str(ROOT / "src"),
                     "-DUNIFORM_LAYERS=1", "-DNR_HAS_NETWORK_MODEL_SELECTION=1", "-I" + str(folder),
                     str(ROOT / "tests/aux_history_reuse.cpp"), "-o", str(executable)], "context-build")
        run([str(executable)], "context-identity")
        run([str(executable), "--reproduction"], "context-history-reset")
        # Reintroduce the late-evidence nested retirement: the actual lookup
        # must now fail the primary/auxiliary ownership regression above.
        gate = "layer_index == 0 && !ValidateNrActiveNetworkRequests()"
        if lookup.count(gate) != 1:
            raise AssertionError("review the primary network admission boundary")
        folder = out / "negative-auxiliary-readmission"
        folder.mkdir()
        (folder / "tuning.inl").write_text(tuning_definitions, encoding="utf-8")
        (folder / "lookup.inl").write_text(lookup.replace(gate, "!ValidateNrActiveNetworkRequests()", 1), encoding="utf-8")
        executable = folder / "context-identity"
        run(compiler_flags + include + ["-I" + str(ROOT / "src"),
                     "-DUNIFORM_LAYERS=1", "-DNR_HAS_NETWORK_MODEL_SELECTION=1", "-I" + str(folder),
                     str(ROOT / "tests/aux_history_reuse.cpp"), "-o", str(executable)], "negative-auxiliary-readmission-build")
        run([str(executable)], "negative-auxiliary-readmission", 1)
        receipt["status"] = "passed"
    except Exception as error:
        receipt.update(status="failed", error=str(error))
        raise
    finally:
        (out / "validation.json").write_text(json.dumps(receipt, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
