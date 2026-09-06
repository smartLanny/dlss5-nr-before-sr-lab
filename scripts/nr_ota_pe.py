"""Bounded PE reader for the four owned companion exports; never executes DLLs."""
from __future__ import annotations
import struct

REQUIRED={'NRChain_Init','NRChain_Create','NRChain_Evaluate','NRChain_Release'}

def companion_check(data:bytes)->None:
    def take(offset:int,size:int)->bytes:
        if offset<0 or size<0 or offset>len(data) or size>len(data)-offset:
            raise ValueError('Companion PE range out of file')
        return data[offset:offset+size]
    def u16(offset:int)->int:return struct.unpack('<H',take(offset,2))[0]
    def u32(offset:int)->int:return struct.unpack('<I',take(offset,4))[0]
    if take(0,2)!=b'MZ':raise ValueError('Companion is not PE')
    pe=u32(0x3c)
    if take(pe,4)!=b'PE\0\0' or u16(pe+4)!=0x8664 or not (u16(pe+22)&0x2000):
        raise ValueError('Expected an x64 companion DLL')
    optional=pe+24;optional_bytes=u16(pe+20)
    if optional_bytes<120 or u16(optional)!=0x20b or u32(optional+108)<1:
        raise ValueError('Companion PE32+ export directory missing')
    take(optional,optional_bytes)
    sections=[];count=u16(pe+6)
    if not 1<=count<=96:raise ValueError('Invalid PE sections')
    for i in range(count):
        p=optional+optional_bytes+40*i;take(p,40)
        sections.append((u32(p+12),u32(p+16),u32(p+20),u32(p+36)))
    def map_rva(address:int,size:int)->tuple[int,int]:
        for va,raw_bytes,raw,flags in sections:
            if address>=va and address-va<=raw_bytes and size<=raw_bytes-(address-va):
                offset=raw+address-va;take(offset,size);return offset,flags
        raise ValueError('Export RVA is not backed by a section')
    export,size=u32(optional+112),u32(optional+116)
    if not export or size<40:raise ValueError('Companion export directory missing')
    table,_=map_rva(export,40)
    funcs,names=u32(table+20),u32(table+24)
    if not 1<=names<=4096 or not 1<=funcs<=4096:raise ValueError('Unbounded export count')
    functions,_=map_rva(u32(table+28),4*funcs)
    strings,_=map_rva(u32(table+32),4*names)
    ordinals,_=map_rva(u32(table+36),2*names)
    found=set()
    for i in range(names):
        name_rva=u32(strings+i*4);name=bytearray()
        for j in range(128):
            pos,_=map_rva(name_rva+j,1);value=data[pos]
            if not value:break
            name.append(value)
        else:raise ValueError('Unbounded export name')
        try:label=name.decode('ascii')
        except UnicodeDecodeError:raise ValueError('Non-ASCII companion export') from None
        if label not in REQUIRED:continue
        if label in found:raise ValueError('Duplicate companion export')
        ordinal=u16(ordinals+i*2)
        if ordinal>=funcs:raise ValueError('Invalid export ordinal')
        function=u32(functions+ordinal*4)
        if export<=function<export+size:raise ValueError('Forwarded companion function refused')
        _,flags=map_rva(function,1)
        if not flags&0x20000000:raise ValueError('Companion export is not executable code')
        found.add(label)
    if found!=REQUIRED:raise ValueError('Required companion exports incomplete')
