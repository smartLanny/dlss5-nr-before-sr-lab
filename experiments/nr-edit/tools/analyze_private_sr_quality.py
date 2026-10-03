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
          "reconstructed": (OW, OH, "e"), "spatial_reconstructed": (OW, OH, "e"),
          "final": (OW, OH, "e"), "spatial_final": (OW, OH, "e")}


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


def region(x, y, scene):
    dark = scene in ("dark_text_v1", "dark_text_hair_v2")
    hair_scene = scene == "dark_text_hair_v2"
    dark_line = dark and 340 <= x < 541 and y == 310+(x-340)//9
    hair = hair_scene and 350 <= x < 450 and y == 348+(x-400)**2//115
    text_rows = ((31, 4, 4, 4, 4, 4, 4), (31, 16, 16, 30, 16, 16, 31),
                 (17, 10, 4, 4, 4, 10, 17), (31, 4, 4, 4, 4, 4, 4))
    dark_text = False
    if dark and 470 <= x < 494 and 336 <= y < 343:
        letter, column = divmod(x-470, 6)
        dark_text = column < 5 and bool(text_rows[letter][y-336] & (1 << (4-column)))
    masks = {"all": True, "neutral": x < 318,
            "support_boundary": 316 <= x <= 326,
            "glyph": 468 <= x <= 500 and 78 <= y <= 143,
            "slanted_line": 348 <= x <= 552 and 68 <= y <= 125,
            "smooth": 330 <= x <= 468 and 180 <= y <= 300}
    if dark:
        masks.update(dark_line=dark_line, dark_text=dark_text,
                     dark_background=330 <= x < 561 and 300 <= y < 371
                                     and not dark_line and not dark_text and not hair,
                     dark_boundary_outside=316 <= x < 320 and 300 <= y < 371,
                     dark_boundary_inside=320 <= x <= 326 and 300 <= y < 371)
        if hair_scene:
            masks.update(hair=hair,
                         hair_neighborhood=348 <= x < 452 and 344 <= y < 371
                                           and not hair)
    return masks


def summary(values):
    if not values:
        return {"count": 0}
    values.sort()
    return {"count": len(values), "mae": sum(values)/len(values),
            "p95": values[(len(values)*95+99)//100-1], "max": values[-1]}


def analyze_frame(directory, frame, mode, carrier_range, scene, zero_preserve):
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
    b, z, carrier, tail, reconstructed, spatial_reconstructed, final, spatial_final = (planes[n] for n in PLANES)
    # Independent N-grid area average. Here O/N is exactly two in each axis.
    down = [0.0]*(NW*NH*3)
    for ny in range(NH):
        for nx in range(NW):
            for c in range(3):
                down[(ny*NW+nx)*3+c] = sum(
                    z[((ny*2+dy)*OW+nx*2+dx)*4+c]-
                    b[((ny*2+dy)*OW+nx*2+dx)*4+c]
                    for dy in range(2) for dx in range(2))/4
    regions = tuple(region(0, 0, scene))
    errors = {stage: {name: [] for name in regions}
              for stage in ("downsample_loss", "transport_error", "reconstruction_fp16_loss",
                            "reconstruction_quantization", "compose_error",
                            "feature1_increment", "feature1_increment_gate_aware", "feature1_vs_z",
                            "fixture_vs_spatial", "fixture_vs_gated_spatial", "source_edit",
                            "corrected_vs_z", "corrected_vs_private", "matched_vs_private",
                            "cpu_spatial_vs_gpu", "cpu_decode_vs_gpu_matched")}
    references = {"exact_spatial": bytearray(), "exact_spatial_fp16": bytearray(),
                  "gated_spatial_fp16": bytearray(), "area_average": bytearray(),
                  "matched_reference": bytearray(), "corrected_candidate": bytearray()}
    support_data = bytes(int(tail[index*4+3] >= 1.5) for index in range(NW*NH))
    support_path = directory/f"frame-{frame}-support.u8"
    support_path.write_bytes(support_data)
    hashes["support"] = digest(support_path)
    en_path = directory/f"frame-{frame}-E_N.fp32"
    en_path.write_bytes(struct.pack(f"<{len(down)}f", *down))
    hashes["E_N"] = digest(en_path)
    invalid = 0
    dark_base_min, dark_base_max = math.inf, -math.inf
    for y in range(OH):
        yt = taps(y, OH, NH)
        for x in range(OW):
            xt = taps(x, OW, NW)
            pixel_base = (y*OW+x)*4
            exact_unchanged = zero_preserve and all(z[pixel_base+j] == b[pixel_base+j] for j in range(3))
            tail_valid = True
            for ny, wy in yt:
                for nx, wx in xt:
                    if wx*wy <= 0:
                        continue
                    offset = (ny*NW+nx)*4
                    value = tail[offset:offset+4]
                    tail_valid &= len(value) == 4 and all(math.isfinite(v) for v in value) and value[3] >= .5
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
                gated_spatial = b[pixel] if exact_unchanged else spatial_fp16
                references["gated_spatial_fp16"].extend(struct.pack("<e", gated_spatial))
                references["area_average"].extend(struct.pack("<e", area))
                observed = final[pixel]
                spatial_gpu = spatial_reconstructed[pixel]
                matched_decode = half(b[pixel]+((2*max(0, min(1, spatial_gpu))-1)*carrier_range
                                        if support else 0)+quantization)
                cpu_matched = (b[pixel] if exact_unchanged else matched_decode) if tail_valid else z[pixel]
                matched = spatial_final[pixel]
                corrected = z[pixel]
                if tail_valid and not exact_unchanged and all(math.isfinite(v) for v in (z[pixel], observed, matched)):
                    proposed = z[pixel] + (observed-matched)
                    if math.isfinite(proposed) and abs(proposed) <= 65504:
                        corrected = half(proposed)
                references["matched_reference"].extend(struct.pack("<e", matched))
                references["corrected_candidate"].extend(struct.pack("<e", corrected))
                if exact_unchanged and corrected != z[pixel]:
                    raise ValueError("Zero-edit candidate changed Z")
                actual_carrier = reconstructed[pixel]
                composed = (b[pixel] if exact_unchanged else
                    half(b[pixel]+((2*max(0, min(1, actual_carrier))-1)*carrier_range
                                   if support else 0)+quantization))
                if not all(math.isfinite(v) for v in (spatial, area, z[pixel], observed)):
                    invalid += 1
                    continue
                differences = {
                    "source_edit": abs(z[pixel]-b[pixel]),
                    "downsample_loss": abs(area-z[pixel]),
                    "transport_error": abs(spatial-area),
                    "reconstruction_fp16_loss": abs(stored_interpolation-interpolation),
                    "reconstruction_quantization": abs(actual_carrier-stored_interpolation),
                    "compose_error": abs(observed-composed),
                    "feature1_increment": abs(observed-spatial_fp16),
                    "feature1_increment_gate_aware": abs(observed-gated_spatial),
                    "feature1_vs_z": abs(observed-z[pixel]),
                    "fixture_vs_spatial": abs(observed-spatial_fp16),
                    "fixture_vs_gated_spatial": abs(observed-gated_spatial),
                    "corrected_vs_z": abs(corrected-z[pixel]),
                    "corrected_vs_private": abs(corrected-observed),
                    "matched_vs_private": abs(matched-observed),
                    "cpu_spatial_vs_gpu": abs(stored_interpolation-spatial_gpu),
                    "cpu_decode_vs_gpu_matched": abs(cpu_matched-matched),
                }
                masks = region(x, y, scene)
                if (masks.get("dark_background") or masks.get("dark_line") or
                        masks.get("dark_text") or masks.get("hair")):
                    dark_base_min = min(dark_base_min, b[pixel])
                    dark_base_max = max(dark_base_max, b[pixel])
                for name, enabled in masks.items():
                    if enabled:
                        for stage, value in differences.items():
                            if ((mode == "fixture" and (stage.startswith("feature1_") or stage == "feature1_vs_z")) or
                                    (mode != "fixture" and stage.startswith("fixture_vs_"))):
                                continue
                            errors[stage][name].append(value)
            for ref in references.values():
                ref.extend(struct.pack("<e", b[(y*OW+x)*4+3]))
            if (b[pixel_base+3] != z[pixel_base+3] or final[pixel_base+3] != b[pixel_base+3]
                    or spatial_final[pixel_base+3] != b[pixel_base+3]):
                raise ValueError("Alpha contract changed in captured planes")
    for name, data in references.items():
        path = directory/f"frame-{frame}-{name}.fp16"
        path.write_bytes(data)
        hashes[name] = digest(path)
    motion_path = directory/f"frame-{frame}-motion.fp32"
    if len(motion_path.read_bytes()) != NW*NH*2*4:
        raise ValueError("Normalized motion plane byte extent mismatch")
    hashes["motion"] = digest(motion_path)
    return {"frame": frame, "plane_sha256": hashes,
            "invalid_or_nonfinite_samples": invalid,
            "dark_base_range": [dark_base_min, dark_base_max] if scene in ("dark_text_v1", "dark_text_hair_v2") else None,
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
           or entry["motion"] != "normalized_zero_work_pixels"
           for entry in identities):
        raise ValueError("Mixed extent, range or non-adopted frame")
    if any(identities[index]["source_sequence"] != identities[0]["source_sequence"]+index
           for index in range(16)):
        raise ValueError("Source identities are not consecutive")
    if any(entry["completion_value"] < entry["fence_value"] or not entry["recording_released"]
           or not entry["feature1_bind_reset"] == bool(entry["reset_causes"])
           for entry in identities):
        raise ValueError("Missing exact completion, recording release or Bind Reset evidence")
    if any(identities[index]["predecessor_sequence"] != identities[index-1]["source_sequence"]
           or identities[index]["reset_causes"] != 0 for index in range(1, 16)):
        raise ValueError("Host private Feature1 history is not continuous")
    scenes = {entry.get("scene", "legacy_v1") for entry in identities}
    if len(scenes) != 1 or scenes.pop() not in ("legacy_v1", "dark_text_v1", "dark_text_hair_v2"):
        raise ValueError("Mixed or unknown quality scene")
    scene = identities[0].get("scene", "legacy_v1")
    policies = {entry.get("encoding_policy", "shared_legacy") for entry in identities}
    zero_flags = {entry.get("zero_preserve", False) for entry in identities}
    if (len(policies) != 1 or not policies.issubset({"shared_legacy", "post_legacy_range4_neutral"}) or
            len(zero_flags) != 1 or any(type(flag) is not bool for flag in zero_flags)):
        raise ValueError("Mixed or unknown encoding/zero-preserve identity")
    policy, zero_preserve = policies.pop(), zero_flags.pop()
    if zero_preserve and policy != "post_legacy_range4_neutral":
        raise ValueError("Zero-preserve capture must name post encoding policy")
    results, first = [], None
    for frame in range(16):
        result, output = analyze_frame(directory, frame, mode, carrier_range, scene, zero_preserve)
        if mode == "fixture" and result["errors"]["corrected_vs_z"]["all"]["max"] > .002:
            raise ValueError("Spatial fixture correction did not return to Z")
        if scene in ("dark_text_v1", "dark_text_hair_v2") and result["errors"]["source_edit"]["dark_boundary_outside"]["max"] != 0:
            raise ValueError("Dark support exterior has source edit")
        if first is None:
            first = output
        result["first_frame_change"] = summary(
            [abs(a-b) for a, b in zip(first, output)])
        results.append(result)
    report = {"schema": "private-sr-controlled-quality-v1", "mode": mode,
              "scene": scene, "encoding_policy": policy, "zero_preserve": zero_preserve,
              "reference": "GPU spatial reconstruction of captured N carrier followed by the same production Compose over an independent Z copy, with identical tail/support/phase; corrected candidate is Z plus private-minus-matched-reference, offline only; CPU interpolation and decode remain separate tolerance checks",
              "carrier_range": carrier_range,
              "identity": identities, "frames": results,
              "quality_acceptance": "unjudged; compare against spatial baseline and user imagery"}
    (directory/"analysis.json").write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"frames": len(results), "mode": mode,
                      "first": results[0]["errors"], "report": str(directory/"analysis.json")}))


if __name__ == "__main__":
    main()
