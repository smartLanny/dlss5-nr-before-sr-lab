"""Independent CPU image oracle for WARP comparisons, NOT execution of the HLSL.

Arrays: HxWx4 float64. Validity: HxW bool. No model is implemented by this file.
"""
import numpy as np

LIMIT = 65504.0

def finite(v):
    return np.isfinite(v).all() and (np.abs(v) <= LIMIT).all()

def decode(v, domain):
    if domain == 0:
        return np.array(v, dtype=np.float64, copy=True)
    v = np.asarray(v, dtype=np.float64)
    out = v / 12.92
    high = v > 0.04045
    out[high] = ((v[high] + 0.055) / 1.055) ** 2.4
    return out

def encode(v, domain):
    if domain == 0:
        return np.array(v, dtype=np.float64, copy=True)
    v = np.asarray(v, dtype=np.float64)
    out = v * 12.92
    high = v > 0.0031308
    out[high] = 1.055 * v[high] ** (1.0 / 2.4) - 0.055
    return out

def downsample(primary, width, height, domain):
    h, w, _ = primary.shape
    low = np.zeros((height, width, 4), dtype=np.float64)
    low[..., 3] = 1.0
    valid = np.ones((height, width), dtype=bool)
    for y in range(height):
        for x in range(width):
            a = np.array([x*w/width, y*h/height])
            b = np.array([(x+1)*w/width, (y+1)*h/height])
            total = 0.0
            accum = np.zeros(3)
            for iy in range(int(np.floor(a[1])), min(int(np.ceil(b[1])), h)):
                for ix in range(int(np.floor(a[0])), min(int(np.ceil(b[0])), w)):
                    weight = max(0., min(b[0],ix+1)-max(a[0],ix)) * max(0., min(b[1],iy+1)-max(a[1],iy))
                    if weight <= 0:
                        continue
                    pixel = primary[iy, ix, :3]
                    if not finite(pixel):
                        valid[y,x] = False
                        continue
                    accum += decode(pixel, domain) * weight
                    total += weight
            if valid[y,x] and total > 0:
                result = encode(accum / total, domain)
                if finite(result):
                    low[y,x,:3] = result
                else:
                    valid[y,x] = False
    return low, valid

def resolve(primary, low_input, low_answer, valid, domain):
    h,w,_ = primary.shape
    lh,lw,_ = low_input.shape
    result = primary.copy()
    for y in range(h):
        for x in range(w):
            p = np.array([(x+.5)*lw/w-.5, (y+.5)*lh/h-.5])
            base = np.floor(p).astype(int)
            f = p - base
            delta = np.zeros(3)
            ok = finite(primary[y,x,:3])
            for dy in range(2):
                for dx in range(2):
                    weight = (1-f[0] if dx==0 else f[0]) * (1-f[1] if dy==0 else f[1])
                    if weight <= 0:
                        continue
                    ix = np.clip(base[0]+dx,0,lw-1)
                    iy = np.clip(base[1]+dy,0,lh-1)
                    a,b = low_input[iy,ix,:3],low_answer[iy,ix,:3]
                    if not valid[iy,ix] or not finite(a) or not finite(b):
                        ok=False
                        continue
                    delta += weight * (decode(b,domain)-decode(a,domain))
            if ok and np.any(delta != 0):
                candidate = encode(decode(primary[y,x,:3],domain)+delta,domain)
                if finite(candidate):
                    result[y,x,:3]=candidate
    return result
