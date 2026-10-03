"""Attribute a bounded warmed real-Feature1 capture, including arbitrary N/O.

Requires numpy/Pillow. Captured inputs remain private; outputs are developer
evidence, not game acceptance. CPU resampling is separately labelled from the
captured GPU spatial reconstruction and production Compose.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def summary(a):
    a = np.abs(a).astype(np.float32)
    return {"count": int(a.size), "mae": float(a.mean(dtype=np.float64)),
            "p95": float(np.quantile(a, .95)), "max": float(a.max())}


def taps(full, work):
    n = (2*np.arange(full, dtype=np.int64)+1)*work+full
    origin = n//(2*full)-1
    fraction = (n % (2*full))/(2*full)
    return [(np.clip(origin, 0, work-1), 1-fraction),
            (np.clip(origin+1, 0, work-1), fraction)]


def expand(a, ow, oh, support=False):
    nh, nw = a.shape[:2]
    out = np.zeros((oh, ow, *a.shape[2:]), np.float32 if not support else bool)
    for y, wy in taps(oh, nh):
        for x, wx in taps(ow, nw):
            weight = wy[:, None]*wx[None, :]
            if support:
                if a.ndim == 3:
                    weight = weight[..., None]
                out |= a[y[:, None], x[None, :]] & (weight > 0)
            else:
                out += a[y[:, None], x[None, :]] * weight[..., None]
    return out


def downsample(a, nw, nh):
    # Independent exact rational area integration; unlike the old analyzer,
    # this accepts odd N and the actual 2336x1312 -> 3840x2160 workload.
    oh, ow = a.shape[:2]
    xlo = np.arange(nw, dtype=np.int64)*ow
    xhi = xlo+ow
    xfirst = xlo//nw
    horizontal = np.zeros((oh, nw, 3), np.float32)
    for offset in range((ow+nw-1)//nw+1):
        x = xfirst+offset
        w = np.maximum(0, np.minimum(xhi, (x+1)*nw)-np.maximum(xlo, x*nw))/ow
        horizontal += a[:, np.clip(x, 0, ow-1)]*w[None, :, None]
    out = np.zeros((nh, nw, 3), np.float32)
    for y in range(nh):
        lo, hi = y*oh, (y+1)*oh
        for row in range(lo//nh, (hi+nh-1)//nh):
            w = (min(hi, (row+1)*nh)-max(lo, row*nh))/oh
            out[y] += horizontal[row]*w
    return out


def contact(directory, frame, planes, corrected, meta):
    ow, oh = meta["output"]
    # Legacy frozen scene has fixed pixel ROIs; matched-jitter uses a scaled
    # continuous scene. All columns use one common display transform.
    rects = {"dark_text_hair": (312, 298, 565, 376),
             "glyph_line": (348, 68, 553, 145)}
    if meta.get("scenario") == "matched-jitter":
        rects = {"dark_text_hair": tuple(round(v*s) for v, s in
                 zip((312, 298, 565, 376), (ow/640, oh/384, ow/640, oh/384)))}
    if meta.get("scenario") == "threshold":
        rects = {"signed_patch": (ow//3, oh//4, min(ow//3+160, ow), min(oh//4+80, oh))}
    outputs = {}
    for label, (x0, y0, x1, y1) in rects.items():
        gain = 8 if "dark" in label or "signed" in label else 1
        crop = np.s_[y0:y1, x0:x1, :3]
        if x1 <= x0 or y1 <= y0:
            continue
        tiles = [planes["Z"][crop], planes["spatial_final"][crop],
                 planes["final"][crop], corrected[crop]]
        scale = max(1, min(4, 400//max(1, x1-x0)))
        w, h = (x1-x0)*scale, (y1-y0)*scale
        sheet = Image.new("RGB", (w*4, h+50), "#151515")
        draw = ImageDraw.Draw(sheet)
        for index, (name, tile) in enumerate(zip(("Z ordinary", "matched spatial", "private Feature1", "Z + private-spatial"), tiles)):
            im = Image.fromarray(np.rint(np.clip(tile*gain, 0, 1)*255).astype(np.uint8))
            sheet.paste(im.resize((w, h), Image.Resampling.NEAREST), (index*w, 50))
            draw.text((index*w+4, 8), name, fill="white")
        draw.text((4, 29), f"frame {frame} | common encoded RGB gain x{gain} | nearest display only", fill="white")
        path = directory/f"contact-frame-{frame}-{label}.png"
        sheet.save(path)
        outputs[label] = str(path)
    return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    a = parser.parse_args()
    directory = a.directory.resolve(strict=True)
    identities = [json.loads(line) for line in (directory/"frames.jsonl").read_text().splitlines()]
    if len(identities) != 16 or [m["frame"] for m in identities] != list(range(16)):
        raise ValueError("Exactly sixteen ordered frames required")
    ow, oh = identities[0]["output"]
    nw, nh = identities[0]["work"]
    if any(m["output"] != [ow, oh] or m["work"] != [nw, nh] or m["mode"] != "feature1" or
           not m["adopted"] or not m["recording_released"] or m["completion_value"] < m["fence_value"] for m in identities):
        raise ValueError("Mixed identity or missing exact completion")
    if any(identities[f]["source_sequence"] != identities[f-1]["source_sequence"]+1 or
           identities[f]["predecessor_sequence"] != identities[f-1]["source_sequence"] or
           identities[f]["reset_causes"] for f in range(1, 16)):
        raise ValueError("Private history is not continuous")
    specs = {"B": (oh, ow, 4, "<f2"), "Z": (oh, ow, 4, "<f2"),
             "carrier": (nh, nw, 4, "<f2"), "tail": (nh, nw, 4, "<f4"),
             "reconstructed": (oh, ow, 4, "<f2"), "spatial_reconstructed": (oh, ow, 4, "<f2"),
             "final": (oh, ow, 4, "<f2"), "spatial_final": (oh, ow, 4, "<f2"),
             "depth": (nh, nw, 1, "<f4"), "motion": (nh, nw, 2, "<f4")}
    regions = {"all": np.s_[:, :, :3], "dark_text_hair": np.s_[300:371, 330:561, :3],
               "glyph": np.s_[78:144, 468:501, :3], "line": np.s_[68:126, 348:553, :3]}
    if identities[0].get("scenario") == "matched-jitter":
        regions = {"all": np.s_[:, :, :3], "dark_text_hair": np.s_[round(300*oh/384):round(371*oh/384), round(330*ow/640):round(561*ow/640), :3]}
    if identities[0].get("scenario") == "threshold":
        regions = {"all": np.s_[:, :, :3], "signed_patch": np.s_[oh//4:oh//2, ow//3:2*ow//3, :3],
                   "signed_thin": np.s_[3*oh//4:min(oh, 3*oh//4+max(16, ow//96)), ow//3:2*ow//3, :3]}
    frames, temporal, previous = [], [], None
    previous_carrier = None
    for frame, meta in enumerate(identities):
        planes, hashes = {}, {}
        for name, (h, w, channels, code) in specs.items():
            path = directory/f"frame-{frame}-{name}.fp{16 if code == '<f2' else 32}"
            data = np.fromfile(path, code)
            if data.size != h*w*channels:
                raise ValueError(f"Wrong plane extent: {path}")
            planes[name] = data.reshape(h, w, channels).astype(np.float32)
            hashes[name] = sha(path)
            if not np.isfinite(planes[name]).all():
                raise ValueError(f"Nonfinite plane: {path}")
        b, z, private, spatial = (planes[n] for n in ("B", "Z", "final", "spatial_final"))
        unchanged = (b[..., :3] == z[..., :3]).all(axis=2)
        corrected = z.copy()
        corrected[..., :3] = (z[..., :3]+(private[..., :3]-spatial[..., :3])).astype(np.float16).astype(np.float32)
        corrected[unchanged, :3] = z[unchanged, :3]
        if np.any(private[unchanged] != b[unchanged]) or np.any(corrected[unchanged] != z[unchanged]):
            raise ValueError("Exact zero-edit protection changed")
        for image in (z, private, spatial):
            if np.any(image[..., 3] != b[..., 3]):
                raise ValueError("Alpha changed")
        deltas = {"private_vs_z": private-z, "matched_spatial_vs_z": spatial-z,
                  "feature1_increment": private-spatial, "corrected_vs_z": corrected-z}
        result = {"frame": frame, "plane_sha256": hashes,
                  "errors": {name: {roi: summary(delta[sl]) for roi, sl in regions.items()}
                             for name, delta in deltas.items()},
                  "shared_support_pixels": int(np.count_nonzero(planes["tail"][..., 3] >= 1.5)),
                  "per_channel_non_neutral_carrier": np.count_nonzero(planes["carrier"][..., :3] != .5, axis=(0, 1)).tolist()}
        actual_channel_support = expand((planes["carrier"][..., :3] != .5) &
                                        (planes["tail"][..., 3:] >= 1.5), ow, oh, support=True)
        inactive_increment = (private-spatial)[..., :3][~actual_channel_support]
        result["inactive_channel_model_increment"] = summary(inactive_increment)
        result["inactive_channel_changed_samples"] = int(np.count_nonzero(inactive_increment))
        if meta.get("per_channel_support", False) and np.any(inactive_increment != 0):
            raise ValueError("Inactive channel must remain exact matched tail-only composition")
        if previous is not None:
            changes = {name: summary((private-previous)[sl]) for name, sl in regions.items()}
            changes["carrier_support_transition_pixels"] = int(np.count_nonzero((planes["carrier"][..., :3] != .5) != (previous_carrier != .5)))
            temporal.append({"frame": frame, "changes": changes})
        if frame == 0 or meta.get("scenario") == "threshold":
            en = downsample(z[..., :3]-b[..., :3], nw, nh)
            represented = (2*planes["carrier"][..., :3]-1)*meta["carrier_range"]+planes["tail"][..., :3]
            result["encoding_roundtrip_vs_E_N"] = summary(represented-en)
            if frame == 0:
                en.astype("<f4").tofile(directory/"frame-0-E_N.fp32")
                spatial_delta = expand(en, ow, oh)
                area = (b[..., :3]+spatial_delta).astype(np.float16).astype(np.float32)
                result["earliest_area_roundtrip_loss"] = {roi: summary((area-z[..., :3])[sl[:2]]) for roi, sl in regions.items()}
        if frame in (0, 7, 15):
            corrected.astype("<f2").tofile(directory/f"frame-{frame}-corrected_candidate.fp16")
            result["contacts"] = contact(directory, frame, planes, corrected, meta)
        frames.append(result)
        previous = private.copy()
        previous_carrier = planes["carrier"][..., :3].copy()
        print(f"frame {frame}: private/Z MAE={result['errors']['private_vs_z']['all']['mae']:.8f}", flush=True)
    frozen_inputs = all(frames[f]["plane_sha256"][name] == frames[0]["plane_sha256"][name]
                        for f in range(16) for name in ("B", "Z", "carrier", "tail", "depth", "motion"))
    report = {"schema": "warmed-real-feature1-attribution-v1", "output": [ow, oh], "work": [nw, nh],
              "game": "not-run", "quality_acceptance": "unjudged", "inputs_frozen": frozen_inputs,
              "reference": "captured matched FP16 spatial reconstruction plus same production Compose; candidate offline only",
              "per_channel_support": identities[0].get("per_channel_support", False),
              "history": identities, "frames": frames, "temporal": temporal,
              "tail8_consecutive": {roi: {"max": max(t["changes"][roi]["max"] for t in temporal if t["frame"] >= 9),
                                         "mean_mae": sum(t["changes"][roi]["mae"] for t in temporal if t["frame"] >= 9)/7}
                                    for roi in regions}}
    path = directory/"attribution.json"
    path.write_text(json.dumps(report, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"report": str(path), "inputs_frozen": frozen_inputs,
                      "tail8_consecutive": report["tail8_consecutive"]}, indent=2))


if __name__ == "__main__":
    main()
