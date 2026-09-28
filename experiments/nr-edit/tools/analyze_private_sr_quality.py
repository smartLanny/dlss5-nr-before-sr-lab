"""Analyze exact-frame post SR transport captures. No model or game inference.

Input planes are tight RGBA FP16 except N-grid tail, which is RGBA FP32.
The output keeps the original planes and adds exact spatial and U(D(Z-B))
references plus a machine-readable attribution report.
"""
import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

OW, OH, NW, NH = 640, 384, 320, 192
PLANES = {"B": (OW, OH, "e"), "Z": (OW, OH, "e"),
          "carrier": (NW, NH, "e"), "tail": (NW, NH, "f"),
          "reconstructed": (OW, OH, "e"), "final": (OW, OH, "e")}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_plane(directory, frame, name):
    w, h, code = PLANES[name]
    path = directory / f"frame-{frame}-{name}.fp{16 if code == 'e' else 32}"
    data = path.read_bytes()
    expected = w * h * 4 * (2 if code == "e" else 4)
    if len(data) != expected:
        raise ValueError(f"{path}: got {len(data)} bytes; expected {expected}")
    return struct.unpack(f"<{w*h*4}{code}", data), digest(path)


def half(value):
    return struct.unpack("<e", struct.pack("<e", value))[0]


def taps(x, full, work):
    numerator = (2*x+1)*work+full
    denominator = 2*full
    origin = numerator//denominator-1
    fraction = (numerator % denominator)/denominator
    return ((max(0, min(work-1, origin)), 1-fraction),
            (max(0, min(work-1, origin+1)), fraction))


def region(x, y):
    return {"all": True, "neutral": x < 318,
            "support_boundary": 316 <= x <= 326,
            "glyph": 468 <= x <= 500 and 78 <= y <= 143,
            "slanted_line": 348 <= x <= 552 and 68 <= y <= 125,
            "smooth": 330 <= x <= 468 and 180 <= y <= 300}


def summary(values):
    if not values:
        return {"count": 0}
    values.sort()
    return {"count": len(values), "mae": sum(values)/len(values),
            "p95": values[(len(values)*95+99)//100-1], "max": values[-1]}


def analyze_frame(directory, frame, mode, carrier_range):
    planes, hashes = {}, {}
    for name in PLANES:
        planes[name], hashes[name] = read_plane(directory, frame, name)
    depth_path = directory/f"frame-{frame}-depth.fp32"
    depth_data = depth_path.read_bytes()
    if len(depth_data) != NW*NH*4:
        raise ValueError("Depth plane byte extent mismatch")
    depth = struct.unpack(f"<{NW*NH}f", depth_data)
    if any(depth[y*NW+x] != (.25 if x < NW//2 else .75)
           for y in range(NH) for x in range(NW)):
        raise ValueError("Expected two constant depth planes")
    hashes["depth"] = digest(depth_path)
    b, z, carrier, tail, reconstructed, final = (planes[n] for n in PLANES)
    # Independent N-grid area average. Here O/N is exactly two in each axis.
    down = [0.0]*(NW*NH*3)
    for ny in range(NH):
        for nx in range(NW):
            for c in range(3):
                down[(ny*NW+nx)*3+c] = sum(
                    z[((ny*2+dy)*OW+nx*2+dx)*4+c]-
                    b[((ny*2+dy)*OW+nx*2+dx)*4+c]
                    for dy in range(2) for dx in range(2))/4
    errors = {stage: {name: [] for name in
              ("all", "neutral", "support_boundary", "glyph", "slanted_line", "smooth")}
              for stage in ("downsample_loss", "transport_error", "reconstruction_fp16_loss",
                            "reconstruction_quantization", "compose_error",
                            "feature1_increment", "feature1_vs_z",
                            "fixture_vs_spatial")}
    references = {"exact_spatial": bytearray(), "exact_spatial_fp16": bytearray(),
                  "area_average": bytearray()}
    invalid = 0
    for y in range(OH):
        yt = taps(y, OH, NH)
        for x in range(OW):
            xt = taps(x, OW, NW)
            for c in range(3):
                interpolation = edit = quantization = 0.0
                support = False
                for ny, wy in yt:
                    for nx, wx in xt:
                        weight = wx*wy
                        offset = (ny*NW+nx)*4
                        interpolation += carrier[offset+c]*weight
                        quantization += tail[offset+c]*weight
                        edit += down[(ny*NW+nx)*3+c]*weight
                        if weight > 0:
                            if tail[offset+3] < .5 or not math.isfinite(tail[offset+c]):
                                invalid += 1
                            support |= tail[offset+3] >= 1.5
                pixel = (y*OW+x)*4+c
                stored_interpolation = half(interpolation)
                spatial = half(b[pixel]+((2*max(0, min(1, interpolation))-1)*carrier_range
                                  if support else 0)+quantization)
                spatial_fp16 = half(b[pixel]+((2*max(0, min(1, stored_interpolation))-1)*carrier_range
                                       if support else 0)+quantization)
                area = half(b[pixel]+edit)
                references["exact_spatial"].extend(struct.pack("<e", spatial))
                references["exact_spatial_fp16"].extend(struct.pack("<e", spatial_fp16))
                references["area_average"].extend(struct.pack("<e", area))
                observed = final[pixel]
                actual_carrier = reconstructed[pixel]
                composed = half(b[pixel]+((2*max(0, min(1, actual_carrier))-1)*carrier_range
                                  if support else 0)+quantization)
                if not all(math.isfinite(v) for v in (spatial, area, z[pixel], observed)):
                    invalid += 1
                    continue
                differences = {
                    "downsample_loss": abs(area-z[pixel]),
                    "transport_error": abs(spatial-area),
                    "reconstruction_fp16_loss": abs(stored_interpolation-interpolation),
                    "reconstruction_quantization": abs(actual_carrier-stored_interpolation),
                    "compose_error": abs(observed-composed),
                    "feature1_increment": abs(observed-spatial_fp16),
                    "feature1_vs_z": abs(observed-z[pixel]),
                    "fixture_vs_spatial": abs(observed-spatial_fp16),
                }
                for name, enabled in region(x, y).items():
                    if enabled:
                        for stage, value in differences.items():
                            if (mode == "fixture") != (stage == "fixture_vs_spatial") and stage in (
                                    "feature1_increment", "feature1_vs_z", "fixture_vs_spatial"):
                                continue
                            errors[stage][name].append(value)
            for ref in references.values():
                ref.extend(struct.pack("<e", b[(y*OW+x)*4+3]))
    for name, data in references.items():
        path = directory/f"frame-{frame}-{name}.fp16"
        path.write_bytes(data)
        hashes[name] = digest(path)
    return {"frame": frame, "plane_sha256": hashes,
            "invalid_or_nonfinite_samples": invalid,
            "errors": {stage: {mask: summary(values) for mask, values in masks.items()}
                       for stage, masks in errors.items()}}, final


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    directory = args.directory.resolve(strict=True)
    identities = [json.loads(line) for line in (directory/"frames.jsonl").read_text().splitlines()]
    if len(identities) != 16 or [entry["frame"] for entry in identities] != list(range(16)):
        raise ValueError("Expected exactly 16 ordered, adopted frames")
    modes = {entry["mode"] for entry in identities}
    if len(modes) != 1 or modes.pop() not in ("feature1", "fixture"):
        raise ValueError("Mixed or unknown inference identity")
    mode = identities[0]["mode"]
    carrier_range = identities[0]["carrier_range"]
    if not isinstance(carrier_range, (int, float)) or not math.isfinite(carrier_range) or not 1e-6 < carrier_range <= 1e6:
        raise ValueError("Invalid carrier range")
    if any(not entry["adopted"] or entry["output"] != [OW, OH] or
           entry["work"] != [NW, NH] or entry["carrier_range"] != carrier_range
           or entry["depth"] != "two_planes_0.25_0.75_split_160"
           for entry in identities):
        raise ValueError("Mixed extent, range or non-adopted frame")
    if len({entry["source_sequence"] for entry in identities}) != 16:
        raise ValueError("Repeated source identity")
    results, first = [], None
    for frame in range(16):
        result, output = analyze_frame(directory, frame, mode, carrier_range)
        if first is None:
            first = output
        result["first_frame_change"] = summary(
            [abs(a-b) for a, b in zip(first, output)])
        results.append(result)
    report = {"schema": "private-sr-controlled-quality-v1", "mode": mode,
              "reference": "half-pixel bilinear carrier, ideal and FP16-stored, plus N-tail; independent area-average D(Z-B)",
              "carrier_range": carrier_range,
              "identity": identities, "frames": results,
              "quality_acceptance": "unjudged; compare against spatial baseline and user imagery"}
    (directory/"analysis.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"frames": len(results), "mode": mode,
                      "first": results[0]["errors"], "report": str(directory/"analysis.json")}))


if __name__ == "__main__":
    main()
