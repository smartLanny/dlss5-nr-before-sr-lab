#!/usr/bin/env python3
"""Validate/inspect complete typed NR replay inputs. Never calls a model implicitly.

The in-game capture is an explicit diagnostic. A normal F7 image or five-plane
ROI bundle is not accepted as a complete model replay. Use mmap for plane access
so validation does not duplicate a multi-gigabyte sequence in memory.
"""
from __future__ import annotations
import argparse
import ctypes as c
import hashlib
import json
import math
import mmap
import re
from pathlib import Path
import struct

FRAME_COUNT = 16
FRAME_LIMIT = 512 << 20
SEQUENCE_LIMIT = 8 << 30
ROLES = ("source", "proxy", "neural", "depth", "motion", "exposure", "composite")

class Plane(c.LittleEndianStructure):
    _fields_ = [(name, c.c_uint32) for name in (
        "role", "width", "height", "channels", "storage_format", "view_format", "encoding", "reserved",
        "x", "y", "resource_width", "resource_height")]+[("offset", c.c_uint64), ("bytes", c.c_uint64)]

class Scalar(c.LittleEndianStructure):
    _fields_ = [("name", c.c_char*64)]+[(name,c.c_uint32) for name in ("type","present","value","reserved")]

class Header(c.LittleEndianStructure):
    _fields_ = [("magic",c.c_char*8),("version",c.c_uint32),("header_bytes",c.c_uint32)]+[
        (name,c.c_uint64) for name in ("sequence","instance","frame")]+[
        (name,c.c_uint32) for name in (
            "index","target_frames","completion","mode","config","evaluation","since_reset","reset",
            "source_x","source_y","source_width","source_height","work_width","work_height","codec_flags","color_mode",
            "parameter_count","plane_count","control_mask_before","control_mask_after","auto_mask_before","auto_mask_after")]+[
        (name,c.c_float) for name in ("pre_exposure","exposure_scale","proxy_scale")]+[
        ("source_dirty",c.c_uint32),("source_commit",c.c_char*48),("build",c.c_char*48),
        ("planes",Plane*7),("parameters",Scalar*64),("codec_constant_count",c.c_uint32),("codec_pass",c.c_uint32),
        ("codec_constants",c.c_uint32*64),("payload_bytes",c.c_uint64)]

assert c.sizeof(Plane) == 64 and c.sizeof(Scalar) == 80 and c.sizeof(Header) == 6080

def fail(condition: bool, message: str):
    if not condition:
        raise ValueError(message)

def scalars(header: Header) -> dict:
    result = {}
    for scalar in header.parameters[:header.parameter_count]:
        name = bytes(scalar.name).decode("ascii")
        fail(0 < len(name) < 64 and name not in result, "invalid/duplicate scalar name")
        fail(scalar.present in (0,1) and scalar.type in (1,2,3), "invalid scalar type/presence")
        value = scalar.value if scalar.type == 1 else struct.unpack(
            "<i" if scalar.type == 2 else "<f", struct.pack("<I",scalar.value))[0]
        fail(not scalar.present or scalar.type != 3 or math.isfinite(value), f"non-finite scalar: {name}")
        result[name] = {"type": scalar.type, "present": bool(scalar.present), "value":value if scalar.present else None}
    return result

def validate_header(header: Header, file_bytes: int):
    fail(bytes(header.magic) == b"NRRPLY1" and header.version == 1 and header.header_bytes == c.sizeof(Header), "wrong replay ABI")
    fail(header.sequence != 0 and header.instance != 0 and header.frame != 0, "missing frame identity")
    fail(header.mode in (1,2) and header.completion == 1, "capture is incomplete/invalidated")
    fail(header.target_frames == FRAME_COUNT and header.index < FRAME_COUNT, "wrong sequence shape")
    fail(header.plane_count == 7 and header.parameter_count <= 64, "wrong plane/scalar count")
    fail(header.codec_constant_count <= 64 and header.codec_pass <= 2, "invalid compositor metadata")
    fail(0 < header.payload_bytes <= FRAME_LIMIT and file_bytes == c.sizeof(Header)+header.payload_bytes, "truncated or oversized frame")
    fail(header.source_dirty in (0,1), "invalid source identity marker")
    end = 0
    for i,p in enumerate(header.planes):
        if not p.bytes:
            fail(i == 5, f"missing required {ROLES[i]} plane")
            continue
        fail(p.role == i and 0 < p.width <= 16384 and 0 < p.height <= 16384, "invalid plane extent/role")
        fail(p.encoding in (1,2) and p.channels in (1,2,4), "invalid plane encoding")
        fail(p.encoding != 2 or p.channels != 1, "unsupported scalar half packing")
        expected = (p.width*p.height*p.channels*(2 if p.encoding == 2 else 4)+3)&~3
        fail(p.bytes == expected and p.offset >= end and p.offset % 16 == 0 and
             p.offset+p.bytes <= header.payload_bytes, "overlapping or invalid plane ranges")
        fail(p.x+p.width <= p.resource_width <= 16384 and
             p.y+p.height <= p.resource_height <= 16384, "invalid physical subrect")
        end=p.offset+p.bytes
    fail(end == header.payload_bytes, "unclaimed trailing payload")
    for i in (1,2):
        p=header.planes[i]
        fail((p.width,p.height)==(header.work_width,header.work_height), "model grid mismatch")
        fail(p.x==0 and p.y==0 and p.channels==4 and p.view_format==10 and p.encoding==2,
             "model input/output is not complete FP16 RGBA")
    fail((header.planes[0].width,header.planes[0].height)==(header.source_width,header.source_height), "source grid mismatch")
    fail((header.planes[0].x,header.planes[0].y)==(header.source_x,header.source_y), "source origin mismatch")
    scalars(header)

class Frame:
    def __init__(self,path: Path):
        self.path=Path(path)
        self.file=self.path.open("rb")
        try:
            data=self.file.read(c.sizeof(Header))
            fail(len(data)==c.sizeof(Header), "truncated header")
            self.header=Header.from_buffer_copy(data)
            validate_header(self.header,self.path.stat().st_size)
            self.mapping=mmap.mmap(self.file.fileno(),0,access=mmap.ACCESS_READ)
        except Exception:
            self.file.close()
            raise
    def close(self):
        self.mapping.close();self.file.close()
    def __enter__(self):return self
    def __exit__(self,*_):self.close()
    def plane(self,role: str):
        import numpy as np
        p=self.header.planes[ROLES.index(role)]
        if not p.bytes:return None
        # A copy gives the caller independent lifetime; no hidden full-sequence copy.
        return np.frombuffer(self.mapping,dtype="<f2" if p.encoding==2 else "<f4",
            count=p.width*p.height*p.channels,offset=c.sizeof(Header)+p.offset).reshape(p.height,p.width,p.channels).copy()
    def summary(self):
        h=self.header
        return {"file":self.path.name,"sha256":hashlib.sha256(self.mapping).hexdigest(),
            "sequence":h.sequence,"index":h.index,"instance":h.instance,"frame":h.frame,"mode":h.mode,
            "config":h.config,"evaluation":h.evaluation,"since_reset":h.since_reset,"reset":bool(h.reset),
            "source_commit":bytes(h.source_commit).decode("ascii"),"source_dirty":bool(h.source_dirty),
            "build":bytes(h.build).decode("ascii"),"source_extent":[h.source_width,h.source_height],
            "work_extent":[h.work_width,h.work_height],"source_origin":[h.source_x,h.source_y],
            "pre_exposure":h.pre_exposure,"exposure_scale":h.exposure_scale,"proxy_scale":h.proxy_scale,
            "color_mode":h.color_mode,"control_mask_before":h.control_mask_before,"control_mask_after":h.control_mask_after,
            "auto_mask_before":h.auto_mask_before,"auto_mask_after":h.auto_mask_after,
            "parameters":scalars(h),"payload_bytes":h.payload_bytes,
            "codec_pass":h.codec_pass,"codec_constants":list(h.codec_constants[:h.codec_constant_count]),
            "planes":[{name:getattr(p,name) for name,_ in Plane._fields_} | {"name":ROLES[i]}
                      for i,p in enumerate(h.planes)]}

def validate_sequence(directory: Path) -> dict:
    runtime=json.loads((Path(directory)/"runtime.json").read_text(encoding="utf-8"))
    fail(runtime.get("schema")=="nr-replay-runtime-v1", "runtime identity missing")
    for name in ("nr","bridge","core"):
        fail(re.fullmatch(r"[0-9a-f]{64}",runtime.get(name+"_sha256","")) is not None and
             isinstance(runtime.get(name+"_bytes"),int) and 0<runtime[name+"_bytes"]<=512<<20,"invalid runtime identity")
    files=sorted(Path(directory).glob("frame-*.nrf"))
    fail(len(files)==FRAME_COUNT, f"need exactly {FRAME_COUNT} complete frames; found {len(files)}")
    summaries=[];total=0
    for index,path in enumerate(files):
        with Frame(path) as frame:
            h=frame.header
            fail(path.name==f"frame-{index:02d}.nrf" and h.index==index, "missing/reordered frame")
            fail(h.evaluation==index+1 and h.since_reset==index+1 and h.reset==(1 if index==0 else 0),
                 "history did not start fresh or reset inside sequence")
            summary=frame.summary()
            if summaries:
                for key in ("sequence","instance","mode","config","source_commit","source_dirty","build",
                            "source_extent","work_extent","source_origin","color_mode"):
                    fail(summary[key]==summaries[0][key], f"contract changed inside sequence: {key}")
                fail(summary["planes"][:6]==summaries[0]["planes"][:6], "input plane contract changed")
                fail(summary["frame"]>summaries[-1]["frame"], "frame identity did not advance")
            total+=h.payload_bytes;fail(total<=SEQUENCE_LIMIT,"sequence budget exceeded")
            summaries.append(summary)
    return {"schema":"nr-replay-sequence-v1","complete":True,"frames":summaries,"payload_bytes":total,"runtime":runtime,
            "scope":"Complete captured typed values from fresh NR history; validation is not independent NVIDIA replay or visual acceptance."}

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sequence",type=Path)
    parser.add_argument("--output",type=Path,help="write validation summary (refuses overwrite)")
    args=parser.parse_args()
    result=validate_sequence(args.sequence)
    if args.output:
        with args.output.open("x",encoding="utf-8") as out:json.dump(result,out,ensure_ascii=False,indent=2,allow_nan=False)
    print(f"VALID: {len(result['frames'])} complete frames, {result['payload_bytes']} payload bytes; NVIDIA replay not yet performed")
if __name__=="__main__":main()
