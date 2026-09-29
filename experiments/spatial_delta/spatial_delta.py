"""Offline IEEE binary32 signed-delta arithmetic reference. No product mutation.

The captured B/Z path has no clean truth, so it reports attribution only. A
separate deterministic fixture has known truth and is the sole gate source.
"""
import argparse
import ctypes
import hashlib
import json
import math
import random
import struct
from pathlib import Path


def f32(value):
    """Round every named shader arithmetic result to IEEE binary32."""
    return ctypes.c_float(value).value


def add(a, b): return f32(f32(a) + f32(b))
def sub(a, b): return f32(f32(a) - f32(b))
def mul(a, b): return f32(f32(a) * f32(b))
def div(a, b): return f32(f32(a) / f32(b))


def peak(v):
    return max(abs(x) for x in v)


def guide_weight(reference, proxy):
    scale = max(peak(reference), peak(proxy), f32(1 / 4096))
    distance = peak([sub(div(a, scale), div(b, scale)) for a, b in zip(reference, proxy)])
    return div(1, add(1, mul(64, mul(distance, distance))))


def at(plane, width, x, y):
    return plane[y * width + x]


def area(plane, width, height, work_width, work_height):
    result = []
    for ny in range(work_height):
        y0, y1 = ny * height, (ny + 1) * height
        for nx in range(work_width):
            x0, x1 = nx * width, (nx + 1) * width
            accum = [f32(0), f32(0), f32(0)]
            total = f32(0)
            for y in range(y0 // work_height, (y1 + work_height - 1) // work_height):
                wy = min(y1, (y + 1) * work_height) - max(y0, y * work_height)
                for x in range(x0 // work_width, (x1 + work_width - 1) // work_width):
                    wx = min(x1, (x + 1) * work_width) - max(x0, x * work_width)
                    weight = f32(wx * wy)
                    pixel = at(plane, width, x, y)
                    for channel in range(3):
                        accum[channel] = add(accum[channel], mul(pixel[channel], weight))
                    total = add(total, weight)
            result.append(tuple(div(value, total) for value in accum))
    return result


def taps(position, full, work):
    numerator = (2 * position + 1) * work + full
    denominator = 2 * full
    origin = numerator // denominator - 1
    fraction = div(numerator % denominator, denominator)
    return ((max(0, min(work - 1, origin)), sub(1, fraction)),
            (max(0, min(work - 1, origin + 1)), fraction))


def reconstruct(base, observed, width, height, work_width, work_height,
                region=None, guided=False):
    base = [tuple(f32(v) for v in pixel) for pixel in base]
    observed = [tuple(f32(v) for v in pixel) for pixel in observed]
    delta = [tuple(sub(z[c], b[c]) for c in range(3))
             for b, z in zip(base, observed)]
    bn = area(base, width, height, work_width, work_height)
    dn = area(delta, width, height, work_width, work_height)
    result = []
    counts = {"invalid_fallback": 0, "zero_preserved": 0, "outside_preserved": 0}
    for y in range(height):
        for x in range(width):
            index = y * width + x
            b, z = base[index], observed[index]
            if region and not (region[0] <= x < region[2] and region[1] <= y < region[3]):
                result.append(z)
                counts["outside_preserved"] += 1
                continue
            if all(b[c] == z[c] for c in range(3)) and all(math.isfinite(v) for v in (*b, *z)):
                result.append(b)
                counts["zero_preserved"] += 1
                continue
            if not all(math.isfinite(v) for v in (*b, *z)):
                result.append(z)
                counts["invalid_fallback"] += 1
                continue
            accum = [f32(0), f32(0), f32(0)]
            total = f32(0)
            valid = True
            for ny, wy in taps(y, height, work_height):
                for nx, wx in taps(x, width, work_width):
                    spatial = mul(wx, wy)
                    if spatial == 0:
                        continue
                    sample_b = at(bn, work_width, nx, ny)
                    sample_d = at(dn, work_width, nx, ny)
                    if not all(math.isfinite(v) for v in (*sample_b, *sample_d)):
                        valid = False
                        break
                    weight = mul(spatial, (guide_weight(b[:3], sample_b) if guided else 1))
                    for channel in range(3):
                        accum[channel] = add(accum[channel], mul(sample_d[channel], weight))
                    total = add(total, weight)
            if not valid or not math.isfinite(total) or total <= 0:
                result.append(z)
                counts["invalid_fallback"] += 1
                continue
            rgb = tuple(add(b[c], div(accum[c], total)) for c in range(3))
            if not all(math.isfinite(v) and abs(v) <= 65504 for v in rgb):
                result.append(z)
                counts["invalid_fallback"] += 1
                continue
            result.append((*rgb, b[3]))
    return result, counts


def fixture(width=129, height=97, seed=73219):
    rng = random.Random(seed)
    base, truth, observed, masks = [], [], [], {"edit": [], "quiet": [], "line": [], "dark": []}
    for y in range(height):
        for x in range(width):
            dark = x >= width // 2
            tone = 0.022 if dark else 0.22
            hair = dark and 72 <= x < 119 and y == 30 + (x - 94) ** 2 // 38
            glyph = dark and 84 <= x < 99 and 68 <= y < 79 and (
                y in (68, 73, 78) or x in (84, 98))
            line = hair or glyph or (x >= 13 and x < 60 and y == 18 + (x - 13) // 5)
            b = tone + (0.026 if line else 0) + x * 0.00003
            # Low-frequency signed edit and genuine thin edit share one truth.
            edit = (0.0038 if dark else -0.0042) if x >= 5 else 0.0
            if hair:
                edit += 0.0055
            if glyph:
                edit -= 0.0065
            if x >= 13 and x < 60 and line:
                edit += 0.007
            noise = rng.gauss(0, 0.0019 if dark else 0.0013) if edit else 0.0
            bp = (b, b * 0.91, b * 1.07, 0.75)
            tp = (b + edit, b * 0.91 + edit * 0.93, b * 1.07 + edit * 1.08, 0.75)
            zp = (tp[0] + noise, tp[1] + noise, tp[2] + noise, 0.75)
            index = y * width + x
            base.append(bp)
            truth.append(tp)
            observed.append(zp)
            if edit:
                masks["edit"].append(index)
            else:
                masks["quiet"].append(index)
            if line:
                masks["line"].append(index)
            if dark and not line:
                masks["dark"].append(index)
    return ([tuple(f32(v) for v in pixel) for pixel in base],
            [tuple(f32(v) for v in pixel) for pixel in truth],
            [tuple(f32(v) for v in pixel) for pixel in observed], masks)


def rms_error(output, truth, indices):
    values = [(output[i][c] - truth[i][c]) ** 2 for i in indices for c in range(3)]
    return math.sqrt(sum(values) / len(values))


def edit_integral(output, base, indices):
    return sum(abs(output[i][0] - base[i][0]) for i in indices)


def hair_continuity(output, base, width):
    hits = []
    for x in range(72, 119):
        y = 30 + (x - 94) ** 2 // 38
        center = y * width + x
        neighbors = ((y - 2) * width + x, (y + 2) * width + x)
        contrast = (output[center][0] - base[center][0]) - sum(
            output[i][0] - base[i][0] for i in neighbors) / 2
        hits.append(contrast >= 0.00275)  # half the clean hair-specific edit
    longest = current = 0
    for hit in hits:
        current = current + 1 if hit else 0
        longest = max(longest, current)
    return {"retained": sum(hits), "total": len(hits), "longest": longest}


def read_half_rgba(path, width, height):
    data = path.read_bytes()
    if len(data) != width * height * 8:
        raise ValueError(f"bad plane extent: {path}")
    return list(struct.iter_unpack('<eeee', data))


def read_float_rgba(path, width, height):
    data = path.read_bytes()
    if len(data) != width * height * 16:
        raise ValueError(f"bad plane extent: {path}")
    return list(struct.iter_unpack('<ffff', data))


def half(value):
    return struct.unpack('<e', struct.pack('<e', value))[0]


def carrier_identity(base, observed, carrier, tail, width, height, nw, nh, carrier_range=1):
    """Captured N carrier, bilinear identity recovery, production support/tail decode.

    The sampler arithmetic is FP32; a reconstructed FP16 texture write is
    approximated by IEEE nearest-even half. GPU store parity is not asserted.
    """
    result = []
    counts = {"invalid_fallback": 0, "zero_preserved": 0}
    for y in range(height):
        for x in range(width):
            index = y * width + x
            b, z = base[index], observed[index]
            if all(math.isfinite(v) for v in (*b, *z)) and all(b[c] == z[c] for c in range(3)):
                result.append(b)
                counts["zero_preserved"] += 1
                continue
            interpolation = [f32(0), f32(0), f32(0)]
            tail_sum = [f32(0), f32(0), f32(0)]
            total = f32(0)
            support = False
            valid = all(math.isfinite(v) for v in (*b, *z))
            for ny, wy in taps(y, height, nh):
                for nx, wx in taps(x, width, nw):
                    weight = mul(wx, wy)
                    if weight == 0:
                        continue
                    c = at(carrier, nw, nx, ny)
                    t = at(tail, nw, nx, ny)
                    if not all(math.isfinite(v) for v in (*c, *t)) or t[3] < .5:
                        valid = False
                        break
                    support = support or t[3] >= 1.5
                    for channel in range(3):
                        interpolation[channel] = add(interpolation[channel], mul(c[channel], weight))
                        tail_sum[channel] = add(tail_sum[channel], mul(t[channel], weight))
                    total = add(total, weight)
            if not valid or not math.isfinite(total) or total <= 0:
                result.append(z)
                counts["invalid_fallback"] += 1
                continue
            rgb = []
            for channel in range(3):
                stored = half(interpolation[channel])
                decoded = mul(sub(mul(2, max(0, min(1, stored))), 1), carrier_range) if support else f32(0)
                delta = add(decoded, div(tail_sum[channel], total))
                value = add(b[channel], delta)
                rgb.append(half(value) if math.isfinite(value) and abs(value) <= 65504 else float('nan'))
            if any(not math.isfinite(v) for v in rgb):
                result.append(z)
                counts["invalid_fallback"] += 1
            else:
                result.append((*rgb, b[3]))
    return result, counts


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(capture):
    width, height, nw, nh = 129, 97, 64, 48
    b, truth, z, masks = fixture(width, height)
    bilinear, bilinear_counts = reconstruct(b, z, width, height, nw, nh)
    guided, guided_counts = reconstruct(b, z, width, height, nw, nh, guided=True)
    baseline = rms_error(z, truth, masks["edit"])
    guided_noise = rms_error(guided, truth, masks["edit"])
    line_baseline = edit_integral(z, b, masks["line"])
    line_guided = edit_integral(guided, b, masks["line"])
    true_integral = edit_integral(truth, b, masks["edit"])
    actual_integral = edit_integral(guided, b, masks["edit"])
    metrics = {"fixture": {"identity": "deterministic synthetic B, clean truth, noisy Z; no Feature18",
        "seed": 73219, "size": [width, height], "work": [nw, nh],
        "ordinary_rms": baseline, "area_bilinear_rms": rms_error(bilinear, truth, masks["edit"]),
        "guided_rms": guided_noise, "noise_reduction_fraction": 1 - guided_noise / baseline,
        "guided_to_truth_edit_integral": actual_integral / true_integral,
        "guided_to_ordinary_line_integral": line_guided / line_baseline,
        "hair_continuity": {"truth": hair_continuity(truth, b, width),
                            "ordinary": hair_continuity(z, b, width),
                            "area_bilinear": hair_continuity(bilinear, b, width),
                            "guided": hair_continuity(guided, b, width)},
        "guided_counts": guided_counts, "bilinear_counts": bilinear_counts}}
    checks = {}
    # Exact neutral, signed micro edit, invalid fallback, odd dimensions, and subrect.
    tiny = list(b)
    tiny[40] = (b[40][0] + 1e-7, b[40][1] - 1e-7, b[40][2] + 1e-7, b[40][3])
    tiny_output, _ = reconstruct(b, tiny, width, height, nw, nh, guided=True)
    micro_input = [sub(f32(tiny[40][c]), b[40][c]) for c in range(3)]
    micro_output = [sub(tiny_output[40][c], b[40][c]) for c in range(3)]
    metrics["near_zero"] = {
        "pixel": [40, 0], "input_domain": "binary32 B and signed micro Z; one edited pixel",
        "input_signed_delta_rgb": micro_input,
        "recovered_signed_delta_rgb": micro_output,
        "absolute_error_rgb": [abs(sub(a, b)) for a, b in zip(micro_output, micro_input)],
        "absolute_retention_rgb": [abs(a) / abs(b) if b else None
                                   for a, b in zip(micro_output, micro_input)],
        "exact_zero_rgb_pixels": len(b) - 1,
        "exact_zero_output_preserved": sum(tiny_output[i] == b[i] for i in range(len(b)) if i != 40)}
    checks["zero_rgb_exact_and_alpha"] = all(tiny_output[i] == b[i] for i in range(len(b)) if i != 40)
    checks["signed_micro_finite"] = all(math.isfinite(v) for v in tiny_output[40])
    invalid = list(z)
    invalid[50] = (float("nan"), z[50][1], z[50][2], z[50][3])
    invalid_output, invalid_counts = reconstruct(b, invalid, width, height, nw, nh, guided=True)
    checks["invalid_pixel_falls_back_to_Z"] = math.isnan(invalid_output[50][0]) and invalid_counts["invalid_fallback"] > 0
    region = (7, 9, width - 5, height - 3)
    cropped, _ = reconstruct(b, z, width, height, nw, nh, region=region, guided=True)
    checks["outside_region_exact_Z"] = all(cropped[y*width+x] == z[y*width+x]
        for y in range(height) for x in range(width) if not (region[0] <= x < region[2] and region[1] <= y < region[3]))
    checks["processed_alpha_from_B"] = all(guided[i][3] == b[i][3] for i in range(len(b)))
    hb = [(120.0 + x * 0.25, 3.0 + y * 0.1, 0.015625, 0.625)
          for y in range(5) for x in range(7)]
    hz = [(v[0] - 0.0078125, v[1] + 0.000244140625, v[2] - 0.0001220703125, v[3])
          for v in hb]
    ho, _ = reconstruct(hb, hz, 7, 5, 3, 2, guided=True)
    checks["odd_HDR_signed_finite"] = all(all(math.isfinite(v) for v in pixel)
                                           for pixel in ho)
    checks["odd_HDR_signed_direction"] = all(p[0] < q[0] and p[1] > q[1] and p[2] < q[2]
                                              for p, q in zip(ho, hb))
    metrics["contracts"] = checks
    metrics["gate"] = {"edit_95_to_105_percent": .95 <= actual_integral / true_integral <= 1.05,
        "noise_rms_reduction_at_least_15_percent": 1 - guided_noise / baseline >= .15,
        "line_integral_at_least_98_percent": line_guided / line_baseline >= .98,
        "continuity_no_worse": hair_continuity(guided, b, width)["longest"] >= hair_continuity(z, b, width)["longest"],
        "all_contracts": all(checks.values())}
    if capture:
        capture = capture.resolve(strict=True)
        cw, ch = 640, 384
        cb = read_half_rgba(capture / "frame-0-B.fp16", cw, ch)
        cz = read_half_rgba(capture / "frame-0-Z.fp16", cw, ch)
        final = read_half_rgba(capture / "frame-0-final.fp16", cw, ch)
        carrier = read_half_rgba(capture / "frame-0-carrier.fp16", 320, 192)
        tail = read_float_rgba(capture / "frame-0-tail.fp32", 320, 192)
        plain, _ = reconstruct(cb, cz, cw, ch, 320, 192)
        spatial, _ = reconstruct(cb, cz, cw, ch, 320, 192, guided=True)
        identity, identity_counts = carrier_identity(cb, cz, carrier, tail, cw, ch, 320, 192)
        roi = [y*cw+x for y in range(344, 371) for x in range(348, 502)]
        mae = lambda candidate: sum(abs(candidate[i][c]-cz[i][c]) for i in roi for c in range(3)) / (len(roi)*3)
        metrics["captured_attribution"] = {"identity": "Beta7 real Feature1 same-source captured B/Z, not clean truth or P/N",
            "paths_sha256": {name: sha(capture / f"frame-0-{name}.fp16") for name in ("B", "Z", "final", "carrier")},
            "tail_sha256": sha(capture / "frame-0-tail.fp32"),
            "frames_manifest_sha256": sha(capture / "frames.jsonl"),
            "roi": [348, 344, 502, 371], "mae_vs_Z": {"area_bilinear": mae(plain),
                "signed_delta_guided": mae(spatial), "carrier_identity_bilinear_decode": mae(identity),
                "existing_real_Feature1": mae(final)},
            "carrier_identity_counts": identity_counts,
            "carrier_identity_domain": "captured FP16 N carrier + FP32 tail, FP32 bilinear, approximate FP16 reconstructed texture write, production support/tail/zero-pixel decode; CPU oracle, no GPU store bit parity"}
    metrics["experiment_source_sha256"] = sha(Path(__file__))
    return metrics


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--capture", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    report = run(args.capture)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"gate": report["gate"], "output": str(args.output)}))


if __name__ == "__main__":
    main()
