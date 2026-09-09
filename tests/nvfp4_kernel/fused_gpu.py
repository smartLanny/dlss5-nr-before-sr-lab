"""Isolated fused contraction, FP8 residual, and counter-array GPU validation.

Uses only the public helper ABI and its queried runtime-binding region. The
CPU epilogue uses a tensor-axis permutation, independent of the device's bit
offset expression. CUDA stream ordering here is not an NVAPI UAV-barrier or
NR/image-quality acceptance test. --cpu-self-test never initializes CUDA.
"""
from __future__ import annotations

import argparse
import ctypes as ct
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import time

import numpy as np

from benchmark import Cuda, Input, Info, fixture, ptr, u32, u64
from abi_cpu import Bindings, FusedInfo, FusedInput

ROOT = Path(__file__).resolve().parents[2]
N = 1024
GUARD = bytes([0xA5]) * 256


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fp8_values():
    """All finite positive E4M3 values, including the subnormal interval."""
    b = np.arange(127, dtype=np.int32)
    exponent, fraction = b // 8, b % 8
    return np.where(exponent == 0, fraction * 2.0**-9,
                    (8 + fraction) * np.exp2(exponent - 10)).astype(np.float32)


def decode_fp8(encoded):
    magnitude = (encoded & 127).astype(np.int32)
    require(bool(np.all(magnitude < 127)), "Fixture contains E4M3 NaN")
    return np.copysign(fp8_values()[magnitude],
                       np.where(encoded & 128, -1.0, 1.0)).astype(np.float16)


def encode_fp8(value):
    """Round-to-nearest, ties-to-even, finite-saturating E4M3 conversion."""
    x = np.asarray(value, dtype=np.float32)
    require(not bool(np.isnan(x).any()), "Unexpected NaN in CPU epilogue")
    table = fp8_values()
    magnitude = np.minimum(np.abs(x), table[-1])
    upper = np.searchsorted(table, magnitude, side="left").clip(0, 126)
    lower = np.maximum(upper - 1, 0)
    low_distance = magnitude - table[lower]
    high_distance = table[upper] - magnitude
    choose_lower = (low_distance < high_distance) | (
        (low_distance == high_distance) & ((lower % 2) == 0))
    code = np.where(choose_lower, lower, upper).astype(np.uint8)
    return code | (np.signbit(x).astype(np.uint8) << 7)


def original_bytes(logical, m):
    """Permute 16-row tiles, then take the original packed byte prefix.

    Logical axes: tile, row[3], row[2:0], col[9:5], col[4], col[3],
    col[2:1], col[0]. Physical storage permutes those axes. Padding up to
    sixteen rows is essential: a partial prefix can reference rows >= M.
    """
    padded = (m + 15) // 16 * 16
    require(logical.shape == (padded, N), "CPU permutation shape mismatch")
    return logical.reshape(-1, 2, 8, 32, 2, 2, 4, 2).transpose(
        0, 3, 2, 6, 4, 1, 5, 7).reshape(-1)[:m * N]


def cpu_epilogue(gemm, skip_bytes, cosine):
    m = gemm.shape[0]
    padded = (m + 15) // 16 * 16
    logical = np.zeros((padded, N), dtype=np.float16)
    logical[:m] = gemm
    packed_gemm = original_bytes(logical, m)
    packed_cos = original_bytes(np.broadcast_to(cosine, (padded, N)), m)
    skip = decode_fp8(np.frombuffer(skip_bytes, np.uint8))
    with np.errstate(over="ignore", under="ignore", invalid="raise"):
        exact_product = skip.astype(np.float32) * packed_cos.astype(np.float32)
        half_product = exact_product.astype(np.float16)
        half_sum = (packed_gemm.astype(np.float32) +
                    half_product.astype(np.float32)).astype(np.float16)
        expected = encode_fp8(half_sum)
        one_round = encode_fp8((packed_gemm.astype(np.float32) +
                               exact_product).astype(np.float16))
        no_add_round = encode_fp8(packed_gemm.astype(np.float32) +
                                 half_product.astype(np.float32))
    return expected.tobytes(), {
        "nonzero_skip_bytes": int(np.count_nonzero(skip)),
        "nonzero_cosine_columns": int(np.count_nonzero(cosine)),
        "half_product_rounding_sensitive_bytes": int(np.count_nonzero(expected != one_round)),
        "half_add_rounding_sensitive_bytes": int(np.count_nonzero(expected != no_add_round)),
        "saturated_bytes": int(np.count_nonzero((expected & 127) == 126)),
    }


def residual_fixture(m, case, binding):
    rng = np.random.default_rng(902100 + m * 17 + binding)
    # Every finite FP8 encoding (including signed zero) occurs repeatedly.
    codes = np.arange(254, dtype=np.uint16)
    codes = ((codes % 127) | ((codes // 127) << 7)).astype(np.uint8)
    raw = np.resize(codes, m * N)
    rng.shuffle(raw)
    cosine = rng.uniform(-1.5, 1.5, N).astype(np.float16)
    special = [0.0, -0.0, 1.0, -1.0, 0.333251953125, -0.66650390625,
               2**-14, -(2**-14), 512.0, -512.0, 0.5, -0.5]
    cosine[:len(special)] = np.asarray(special, np.float16)
    if case == "rounding":
        # Non-power-of-two half factors exercise both explicit half roundings.
        cosine = rng.uniform(-3, 3, N).astype(np.float16)
        cosine[::31] = np.float16(512 if binding else -512)
    return raw.tobytes(), cosine


class Guarded:
    def __init__(self, cuda, label, payload):
        self.cuda, self.label, self.initial = cuda, label, bytes(payload)
        self.size = len(self.initial)
        self.base = cuda.alloc(self.size + 2 * len(GUARD), GUARD + self.initial + GUARD)
        self.address = self.base + len(GUARD)

    def read(self):
        return self.cuda.read(self.address, self.size)

    def guards(self):
        return (self.cuda.read(self.base, len(GUARD)) == GUARD and
                self.cuda.read(self.address + self.size, len(GUARD)) == GUARD)


def counters(cuda, label, m):
    tiles = (m + 127) // 128
    return [Guarded(cuda, label + "/" + name, bytes([0xBB]) * tiles * entries * 4)
            for name, entries in (("done", 8), ("counter", 8), ("expand_done", 32))]


def counter_checks(domains, m):
    tiles = (m + 127) // 128
    result = {}
    for g, name, entries, value in zip(domains, ("done", "counter", "expand_done"),
                                      (8, 8, 32), (0, 3, 0)):
        observed = np.frombuffer(g.read(), dtype=np.uint32)
        result[name] = {"entries": tiles * entries, "expected_value": value,
                        "different_entries": int(np.count_nonzero(observed != value)),
                        "guards_intact": g.guards()}
    return result


def pod(fmt, *values):
    data = struct.pack(fmt, *values)
    return ct.create_string_buffer(data, len(data))


class Prepared:
    """Own the aligned Params allocation; treat all undescribed bytes as opaque."""
    def __init__(self, helper, inp, fused=False):
        self.info = FusedInfo() if fused else Info()
        self.launch = self.info.launch if fused else self.info
        self.launch.struct_bytes, self.launch.abi_version = ct.sizeof(Info), 1
        fn = helper.nr_nvfp4_build_fused_contract if fused else helper.nr_nvfp4_build_contract
        error = ct.create_string_buffer(1024)
        start = time.perf_counter()
        rc = fn(ct.byref(inp), None, 0, ct.byref(self.info), error, len(error))
        require(rc == 0, f"Descriptor query failed ({rc}): {error.value!r}")
        size, alignment = self.launch.params_bytes, self.launch.params_alignment
        require(size > 0 and alignment > 0 and alignment & (alignment - 1) == 0,
                "Helper returned invalid Params size/alignment")
        require(self.launch.workspace_bytes == 0, "Unsupported nonzero workspace")
        self.storage = ct.create_string_buffer(size + alignment + 512)
        ct.memset(ct.addressof(self.storage), 0xA5, ct.sizeof(self.storage))
        self.address = (ct.addressof(self.storage) + 256 + alignment - 1) & ~(alignment - 1)
        self.blob = (ct.c_char * size).from_address(self.address)
        rc = fn(ct.byref(inp), self.address, size, ct.byref(self.info), error, len(error))
        require(rc == 0, f"Descriptor encode failed ({rc}): {error.value!r}")
        self.prepare_ms = (time.perf_counter() - start) * 1000
        require(self.guards(), "Helper overwrote Params host guards")
        if fused:
            offset, count = self.info.runtime_bindings_offset, self.info.runtime_bindings_bytes
            require(count == ct.sizeof(Bindings) and offset % 8 == 0 and offset + count <= size,
                    "Helper returned invalid public runtime region")
            require(bytes(self.blob)[offset:offset + count] == bytes(Bindings(
                inp.skip, inp.cosine, inp.original_output, inp.done, inp.counter, inp.expand_done)),
                "Queried runtime region does not contain the supplied public bindings")

    def guards(self):
        return (ct.string_at(self.address - 256, 256) == GUARD and
                ct.string_at(self.address + ct.sizeof(self.blob), 256) == GUARD)

    def rebind(self, bindings):
        offset, count = self.info.runtime_bindings_offset, self.info.runtime_bindings_bytes
        before = bytes(self.blob)
        ct.memmove(self.address + offset, ct.byref(bindings), count)
        after = bytes(self.blob)
        require(before[:offset] == after[:offset] and before[offset + count:] == after[offset + count:],
                "Runtime rebinding modified opaque descriptor bytes")
        require(after[offset:offset + count] == bytes(bindings) and self.guards(),
                "Runtime rebinding did not preserve its bounds")

    def launch_kernel(self, cuda, function):
        cuda.launch(function, self.blob, tuple(self.launch.grid), tuple(self.launch.block),
                    self.launch.shared_bytes)


def check_artifacts(build, io, reference):
    receipt = json.loads((build / "kernel-build.json").read_text(encoding="utf-8-sig"))
    expected = {item["file"]: item["sha256"] for item in receipt["artifacts"]}
    names = ("nr_nvfp4_contract_sm120.cubin", "nr_nvfp4_contract_fused_sm120.cubin", "nr_nvfp4_params.dll")
    artifacts = {}
    for name in names:
        observed = sha256(build / name)
        require(observed == expected.get(name), f"Build receipt mismatch: {name}")
        artifacts[name] = observed
    # Reject stale source claims as well as mismatched helper/cubin pairs.
    for name, expected_hash in receipt["source_identity"]["sources"].items():
        path = ROOT / "kernels" / (name if name.startswith("nvfp4_io/") else "nvfp4/" + name)
        require(sha256(path) == expected_hash, f"Build source receipt mismatch: {name}")
    io_receipt = json.loads((io / "io-build.json").read_text(encoding="utf-8-sig"))
    for item in io_receipt["files"]:
        path = io / item["name"] if item["name"].endswith(".cubin") else ROOT / "kernels/nvfp4_io" / item["name"]
        require(sha256(path) == item["sha256"], f"IO receipt mismatch: {item['name']}")
    artifacts["nr_original_io.cubin"] = sha256(io / "nr_original_io.cubin")
    artifacts["kernel-build.json"] = sha256(build / "kernel-build.json")
    artifacts["io-build.json"] = sha256(io / "io-build.json")
    if reference:
        artifacts["immutable_upstream_io.cubin"] = sha256(reference)
    return receipt, artifacts


def timed_sequence(cuda, sequence, iterations):
    for _ in range(2):
        sequence()
    start, end = ptr(), ptr()
    cuda.call("cuEventCreate", ct.byref(start), 0)
    cuda.call("cuEventCreate", ct.byref(end), 0)
    try:
        cuda.call("cuEventRecord", start, None)
        for _ in range(iterations):
            sequence()
        cuda.call("cuEventRecord", end, None)
        cuda.call("cuEventSynchronize", end)
        elapsed = ct.c_float()
        cuda.call("cuEventElapsedTime", ct.byref(elapsed), start, end)
        return elapsed.value / iterations
    finally:
        cuda.call("cuEventDestroy_v2", start)
        cuda.call("cuEventDestroy_v2", end)


def cpu_self_test():
    # Compare the new axis oracle with the existing independent inverse oracle.
    spec = importlib.util.spec_from_file_location("existing_io_oracle", ROOT / "tests/nvfp4_io/verify.py")
    prior = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(prior)
    checks = []
    for m in (1, 15, 16, 17, 127, 128, 129, 640, 641, 960, 961, 2160, 2173, 3840):
        padded = (m + 15) // 16 * 16
        labels = np.arange(padded * N, dtype=np.uint32).reshape(padded, N)
        packed = original_bytes(labels, m)
        i = np.arange(m * N // 2, dtype=np.uint32)
        row, col = prior.coordinates(i)
        expected = np.stack((row * N + col, row * N + col + 1), axis=1).reshape(-1)
        require(np.array_equal(packed, expected), f"CPU layout oracle mismatch for M={m}")
        checks.append({"m": m, "packed_prefix_bytes": m * N, "layout_matches_existing_oracle": True})
    table = fp8_values()
    endpoints = np.concatenate((table, -table, (table[:-1] + table[1:]) / 2,
                                -(table[:-1] + table[1:]) / 2, [np.inf, -np.inf]))
    require(np.array_equal(encode_fp8(endpoints), prior.fp8_encode(endpoints)),
            "E4M3 endpoint/tie/saturation oracle mismatch")
    sensitive = {"half_product_rounding_sensitive_bytes": 0, "half_add_rounding_sensitive_bytes": 0}
    for case in ("zero", "signed", "rounding"):
        *_, gemm = fixture(129, "signed" if case == "rounding" else case)
        raw, cosine = residual_fixture(129, case, 0)
        actual, coverage = cpu_epilogue(gemm, raw, cosine)
        logical = np.zeros((144, N), np.float16)
        logical[:129] = gemm
        i = np.arange(129 * N // 2, dtype=np.uint32)
        r, c = prior.coordinates(i)
        columns = np.stack((c, c + 1), axis=1)
        with np.errstate(over="ignore"):
            expected = prior.fp8_encode((logical[r[:, None], columns] +
                (prior.lookup_fp8()[np.frombuffer(raw, np.uint8)].reshape(-1, 2) *
                 cosine[columns]).astype(np.float16)).astype(np.float16)).tobytes()
        require(actual == expected, f"CPU epilogue oracle mismatch for {case}")
        for name in sensitive:
            sensitive[name] += coverage[name]
    require(all(value > 0 for value in sensitive.values()), "Fixtures do not distinguish required half roundings")
    return {"passed": True, "cuda_initialized": False, "gpu_launches": 0,
            "layout_checks": checks, "rounding_negative_control_bytes": sensitive}


def run_case(cuda, helper, args, functions, m, case, results):
    w, ws, x, xs, gemm_cpu = fixture(m, "signed" if case == "rounding" else case)
    operands = [Guarded(cuda, name, value) for name, value in
                (("weights", w), ("weight_scales", ws), ("activations", x), ("activation_scales", xs))]
    padded = (m + 15) // 16 * 16
    gemm = Guarded(cuda, "unfused_fp16", bytes([0xCD]) * m * N * 2 + bytes((padded - m) * N * 2))
    base = Input(ct.sizeof(Input), 1, cuda.luid.value, m, N, 4096, cuda.sms, 0, 0,
                 *(g.address for g in operands), gemm.address, 0, 0)
    normal = Prepared(helper, base)
    require(normal.launch.output_bytes == m * N * 2, "Unexpected unfused output size")
    require(normal.launch.weight_scale_bytes == len(ws) and normal.launch.activation_scale_bytes == len(xs),
            "Fixture scale capacity disagrees with helper")
    normal_function = cuda.load(args.build / "nr_nvfp4_contract_sm120.cubin",
                                normal.launch.kernel_id.decode(), normal.launch.shared_bytes)
    normal.launch_kernel(cuda, normal_function)
    cuda.call("cuCtxSynchronize")
    actual_gemm = gemm.read()
    gemm_equal = actual_gemm[:m * N * 2] == gemm_cpu.tobytes()
    zero_padding = actual_gemm[m * N * 2:] == bytes((padded - m) * N * 2)
    require(gemm_equal and zero_padding and gemm.guards(), "Unfused contraction failed CPU oracle/padding/guards")
    prepared, retired = None, []
    for binding in range(2):
        raw_skip, cosine_cpu = residual_fixture(m, case, binding)
        expected, coverage = cpu_epilogue(gemm_cpu, raw_skip, cosine_cpu)
        skip = Guarded(cuda, f"skip{binding}", raw_skip)
        cosine = Guarded(cuda, f"cosine{binding}", cosine_cpu.tobytes())
        fused_out = Guarded(cuda, "fused_fp8", bytes([0xCD]) * m * N)
        fused_domains = counters(cuda, "fused", m)
        bindings = Bindings(skip.address, cosine.address, fused_out.address,
                            *(g.address for g in fused_domains))
        if prepared is None:
            contract = Input.from_buffer_copy(base)
            contract.output = 0
            inp = FusedInput(contract, *(getattr(bindings, name) for name, _ in Bindings._fields_))
            prepared = Prepared(helper, inp, fused=True)
            require(prepared.launch.output_bytes == m * N, "Unexpected fused output size")
            fused_function = cuda.load(args.build / "nr_nvfp4_contract_fused_sm120.cubin",
                                      prepared.launch.kernel_id.decode(), prepared.launch.shared_bytes)
        else:
            prepared.rebind(bindings)
        snapshots = [Guarded(cuda, "stream_snapshot/" + g.label, bytes([0xCC]) * g.size)
                     for g in [fused_out, *fused_domains]]
        prepared.launch_kernel(cuda, fused_function)
        # No host synchronization intervenes: an ordered CUDA consumer copies
        # the complete output and all counters, including every final entry.
        for source, destination in zip([fused_out, *fused_domains], snapshots):
            cuda.launch(functions["copy"], pod("<QQQ", source.address, destination.address, source.size),
                        ((source.size + 255) // 256, 1, 1), (256, 1, 1), 0)
        cuda.call("cuCtxSynchronize")
        actual = fused_out.read()
        guard_objects = [*operands, gemm, skip, cosine, fused_out, *fused_domains, *snapshots]
        domain_result = counter_checks(fused_domains, m)
        reference_rows, epilogue_launches = {}, []
        for key in ("recovered", "upstream"):
            if key not in functions:
                continue
            output = Guarded(cuda, key + "_fp8", bytes([0xCD]) * m * N)
            domains = counters(cuda, key, m)
            params = pod("<QQQQIIQQQ", gemm.address, skip.address, cosine.address, output.address,
                         m, 0, *(g.address for g in domains))
            grid = ((m * 512 + 255) // 256, 1, 1)
            cuda.launch(functions[key], params, grid, (256, 1, 1), 0)
            cuda.call("cuCtxSynchronize")
            observed = output.read()
            observed_array = np.frombuffer(observed, np.uint8)
            reference_rows[key] = {
                "different_bytes_vs_cpu": int(np.count_nonzero(observed_array != np.frombuffer(expected, np.uint8))),
                "different_bytes_vs_fused": int(np.count_nonzero(observed_array != np.frombuffer(actual, np.uint8))),
                "counter_arrays": counter_checks(domains, m),
            }
            guard_objects.extend([output, *domains])
            epilogue_launches.append((key, params, grid))
        input_unchanged = all(g.read() == g.initial for g in [*operands, skip, cosine])
        retired_unchanged = all(g.read() == value and g.guards() for g, value in retired)
        snapshots_equal = all(a.read() == b.read() for a, b in zip([fused_out, *fused_domains], snapshots))
        guards = {g.label: g.guards() for g in guard_objects}
        diff = np.frombuffer(actual, np.uint8) != np.frombuffer(expected, np.uint8)
        row = {"m": m, "case": case, "binding_generation": binding,
               "cpu_different_bytes": int(np.count_nonzero(diff)),
               "first_different_byte": int(np.flatnonzero(diff)[0]) if diff.any() else None,
               "unfused_fp16_matches_cpu": gemm_equal, "unfused_zero_padding_intact": zero_padding,
               "runtime_bindings_offset": prepared.info.runtime_bindings_offset,
               "runtime_bindings_bytes": prepared.info.runtime_bindings_bytes,
               "opaque_params_bytes": prepared.launch.params_bytes,
               "params_alignment": prepared.launch.params_alignment,
               "params_guards_intact": normal.guards() and prepared.guards(),
               "grid": list(prepared.launch.grid), "block": list(prepared.launch.block),
               "shared_bytes": prepared.launch.shared_bytes, "coverage": coverage,
               "counter_arrays": domain_result, "ordered_cuda_consumer_matches": snapshots_equal,
               "retired_bindings_unchanged": retired_unchanged, "inputs_unchanged": input_unchanged,
               "buffer_guards": guards, "references": reference_rows}
        all_domains = [domain_result] + [r["counter_arrays"] for r in reference_rows.values()]
        row["passed"] = bool(not diff.any() and all(guards.values()) and row["params_guards_intact"]
            and snapshots_equal and retired_unchanged and input_unchanged
            and all(d["different_entries"] == 0 and d["guards_intact"] for group in all_domains for d in group.values())
            and all(r["different_bytes_vs_cpu"] == 0 and r["different_bytes_vs_fused"] == 0 for r in reference_rows.values()))
        # Timings are diagnostic and exclude input pack/expansion/model work.
        # Skip timed repetition after a failed functional result.
        results.append(row)
        if binding == 1 and args.iterations and row["passed"]:
            row["isolated_fused_ms"] = timed_sequence(cuda, lambda: prepared.launch_kernel(cuda, fused_function), args.iterations)
            for key, params, grid in epilogue_launches:
                def sequence():
                    normal.launch_kernel(cuda, normal_function)
                    cuda.launch(functions[key], params, grid, (256, 1, 1), 0)
                row["isolated_unfused_plus_" + key + "_epilogue_ms"] = timed_sequence(cuda, sequence, args.iterations)
            row["descriptor_prepare_cpu_ms"] = {"fused": prepared.prepare_ms, "unfused": normal.prepare_ms}
            row["timed_output_and_guards_intact"] = (fused_out.read() == expected and
                                                       all(g.guards() for g in guard_objects))
            row["passed"] &= row["timed_output_and_guards_intact"]
        print(json.dumps(row), flush=True)
        retired.extend((g, g.read()) for g in [skip, cosine, fused_out, *fused_domains])
        require(row["passed"], f"Fused validation failed for M={m}, case={case}, binding={binding}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=Path)
    parser.add_argument("--io", type=Path)
    parser.add_argument("--reference-io", type=Path)
    parser.add_argument("--m", default="640,960,2160,3840,1,127,128,129,641,961,2173")
    parser.add_argument("--cases", default="zero,signed,rounding")
    parser.add_argument("--iterations", type=int, default=5)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--cpu-self-test", action="store_true")
    args = parser.parse_args()
    if args.cpu_self_test:
        report = cpu_self_test()
        print(json.dumps(report), flush=True)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        return
    if not args.build or not args.io or not args.output:
        parser.error("GPU validation requires --build, --io, and --output")
    if args.output.exists():
        parser.error("--output already exists; choose a new evidence file")
    ms = [int(value) for value in args.m.split(",")]
    cases = args.cases.split(",")
    require(ms and all(0 < m <= 16384 for m in ms), "M must be in [1,16384]")
    require(cases and all(case in ("zero", "signed", "rounding", "dense") for case in cases), "Unknown fixture case")
    require(args.iterations >= 0, "Negative iteration count")
    receipt, artifacts = check_artifacts(args.build, args.io, args.reference_io)
    helper = ct.CDLL(str((args.build / "nr_nvfp4_params.dll").resolve()))
    helper.nr_nvfp4_build_id_v1.restype = ct.c_char_p
    require(helper.nr_nvfp4_build_id_v1().decode() == receipt["abi_fingerprint"], "Helper build ID disagrees with receipt")
    for name, inp, info in (("nr_nvfp4_build_contract", Input, Info),
                            ("nr_nvfp4_build_fused_contract", FusedInput, FusedInfo)):
        fn = getattr(helper, name)
        fn.argtypes, fn.restype = [ct.POINTER(inp), ptr, u64, ct.POINTER(info), ptr, u32], ct.c_int
    report = {"schema": 1, "scope": "isolated CUDA fused contraction/FP8 residual/counter arrays",
              "passed": False, "abi_fingerprint": receipt["abi_fingerprint"], "artifacts": artifacts,
              "test_script_sha256": sha256(Path(__file__)), "results": [],
              "requested_m": ms, "requested_cases": cases, "binding_generations": 2,
              "upstream_epilogue_enabled": bool(args.reference_io),
              "nvapi_uav_visibility_accepted": False, "nr_image_quality_accepted": False,
              "full_nr_performance_accepted": False, "timing_valid_for_variant_selection": False,
              "timing_note": "Isolated diagnostic only; no exclusive GPU ownership, NR/SR, or game FPS claim",
              "ordering_note": "CUDA same-stream consumer and host synchronization only; ordinary counters do not prove NVAPI global UAV dependency"}
    started, cuda = time.perf_counter(), None
    try:
        cuda = Cuda()
        report.update({"cc": cuda.cc, "sm_count": cuda.sms, "shared_optin": cuda.shared})
        functions = {"recovered": cuda.load(args.io / "nr_original_io.cubin", "nr_contract_epilogue_pair", 0),
                     "copy": cuda.load(args.io / "nr_original_io.cubin", "nr_copy_bytes", 0)}
        if args.reference_io:
            functions["upstream"] = cuda.load(args.reference_io, "contract_epilogue_unsplit_f16_pair_publish", 0)
        for m in ms:
            for case in cases:
                report["active_case"] = {"m": m, "case": case}
                try:
                    run_case(cuda, helper, args, functions, m, case, report["results"])
                finally:
                    cuda.clear()
        require(len(report["results"]) == len(ms) * len(cases) * 2, "Incomplete validation matrix")
        report.pop("active_case", None)
        report["passed"] = all(row["passed"] for row in report["results"])
    except Exception as error:
        report["error"] = f"{type(error).__name__}: {error}"
    finally:
        report["wall_seconds"] = time.perf_counter() - started
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": report["passed"], "completed_results": len(report["results"]),
                      "output": str(args.output), "error": report.get("error")}), flush=True)
    if not report["passed"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
