"""Fixed controlled P/N sampling comparison; not Feature18 or a GPU compositor run."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def guide_weight(reference, proxy):
    scale = max(*(abs(value) for value in (*reference, *proxy)), 1 / 4096)
    distance = max(abs(a / scale - b / scale) for a, b in zip(reference, proxy))
    return 1 / (1 + 64 * distance * distance)


def cubic(distance):
    x = abs(distance)
    if x < 1:
        return (4 + x*x*(3*x - 6)) / 6
    if x < 2:
        return (2 - x)**3 / 6
    return 0


def sample(plane, proxy_plane, reference, x, y, full_width, full_height, work_width, work_height, mode):
    px = (x + .5) * work_width / full_width - .5
    py = (y + .5) * work_height / full_height - .5
    bx, by = math.floor(px), math.floor(py)
    fx, fy = px - bx, py - by
    acc = total = 0.0
    offsets = range(-1, 3) if mode == 'fine16' else range(2)
    for dy in offsets:
        for dx in offsets:
            if mode == 'fine16':
                spatial = cubic(dx - fx) * cubic(dy - fy)
            else:
                spatial = (fx if dx else 1-fx) * (fy if dy else 1-fy)
            xx = min(work_width - 1, max(0, bx + dx))
            yy = min(work_height - 1, max(0, by + dy))
            value = plane[yy*work_width + xx]
            proxy = proxy_plane[yy*work_width + xx]
            weight = spatial * (guide_weight((reference,)*3, (proxy,)*3)
                                if mode != 'off' else 1)
            acc += value * weight
            total += weight
    return acc / total


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    width, height, nw, nh = 34, 18, 17, 9
    base = [0.08 if x < 16 else 0.5 for y in range(height) for x in range(width)]
    # P approximates an already captured, fixed controlled model plane; N is
    # prescribed by the fixture, never described as an NGX result.
    p = [base[(2*y)*width + 2*x] for y in range(nh) for x in range(nw)]
    n = [value - .04 if x >= 8 else value for y in range(nh)
         for x, value in enumerate(p[y*nw:(y+1)*nw])]
    results = {}
    for mode in ('off', 'balanced4', 'fine16', 'old_edge'):
        sampler = 'balanced4' if mode == 'old_edge' else mode
        reconstructed = []
        for y in range(height):
            for x in range(width):
                reference = base[y*width+x]
                ps = sample(p, p, reference, x, y, width, height, nw, nh, sampler)
                ns = sample(n, p, reference, x, y, width, height, nw, nh, sampler)
                reconstructed.append(ns - ps)
        left = [abs(reconstructed[y*width+x]) for y in range(height) for x in (14,15)]
        right = [reconstructed[y*width+x] for y in range(height) for x in (17,18)]
        results[mode] = {"left_boundary_abs_edit": sum(left)/len(left),
                         "right_boundary_mean_edit": sum(right)/len(right)}
    source_hashes = {name: hashlib.sha256((Path(__file__).parent/name).read_bytes()).hexdigest()
                     for name in ('stage1_pair.py','spatial_delta.py')}
    result = {"identity": "fixed synthetic controlled P/N, CPU sampling equations only; not actual shader, Feature18, complete color/guard or game",
              "size": [width,height], "work": [nw,nh], "requested_modes": list(results),
              "effective_pair_sampling": {"off":"bilinear", "balanced4":"guided bilinear",
                                          "fine16":"guided cubic B-spline", "old_edge":"guided bilinear"},
              "results": results, "source_sha256": source_hashes}
    path = args.output
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(path)


if __name__ == '__main__':
    main()
