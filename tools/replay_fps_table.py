import struct, sys
def transform(data, key, step, block, limit):
    data = bytearray(data); length = len(data)
    tmp = bytes(data[:min(length, limit)])
    remainder = length % block
    remaining = length - (length & 1) - (remainder if remainder < block // 4 else 0)
    offset = 0
    while remaining > 0 and limit > 0:
        if block > remaining: block = remaining
        seq = offset
        for lane in range(2):
            index = block - 1 - lane
            while index >= 0:
                data[offset + index] = tmp[seq] ^ key
                key = (key + step) & 0xff
                seq += 1
                index -= 2
        offset += block; remaining -= block; limit -= block
    return bytes(data)
def lzss(src, cap):
    out = bytearray(); d = bytearray(8192); head = 1
    pos = 0; mask = 128; byte = 0
    def bit():
        nonlocal pos, mask, byte
        if mask == 128:
            byte = src[pos] if pos < len(src) else 0; pos += 1
        v = 1 if byte & mask else 0
        mask >>= 1
        if not mask: mask = 128
        return v
    def bits(n):
        v = 0
        for _ in range(n): v = (v << 1) | bit()
        return v
    while True:
        if bit():
            v = bits(8); out.append(v); d[head] = v; head = (head + 1) & 8191
        else:
            off = bits(13)
            if not off: return bytes(out)
            cnt = bits(4) + 3
            for i in range(cnt):
                v = d[(off + i) & 8191]; out.append(v); d[head] = v; head = (head + 1) & 8191
def load(path):
    raw = open(path, 'rb').read()
    sig, ver, r6, r8, user_off, vcode, r14, r18, packed, unpacked = struct.unpack_from('<IHHIIIIIII', raw, 0)
    p = raw[0x24:0x24 + packed]
    p = transform(p, 0xaa, 0xe1, 0x400, packed)
    p = transform(p, 0x3d, 0x7a, 0x80, packed)
    u = lzss(p, unpacked)
    return raw, u, unpacked
# ---- th10_port tools/replay_fps_table.py ----
# usage: replay_fps_table.py <th10_xx.rpy> <device th10_result.txt of the replay's playback> [playback label]
# Prints, per stage, the FPS when the replay was SAVED (the .rpy's FPS byte per
# 30 frames, time-weighted: frames / sum(30/fps)) beside the FPS while it was
# PLAYED BACK (the result file's "stage real_hz" line, measured). The two are
# different runs on different builds: always label which is which
import math, re
def recorded_fps(path):
    raw,u,unp=load(path); slow,stages=struct.unpack_from('<fi',u,12+8+52); off=0x64; out={}
    for i in range(min(stages,6)):
        st=u[off:off+0x1c4]; stage,seed,frames,nbytes,score=struct.unpack_from('<hHiii',st,0)
        nf=math.ceil(frames/30); fps=u[off+0x1c4+nbytes-nf:off+0x1c4+nbytes]
        t=sum((30 if k<nf-1 else frames-30*(nf-1))/max(f,1) for k,f in enumerate(fps))
        out[stage]=(frames,frames/t,t); off+=0x1c4+nbytes
    return slow,out
def playback_fps(path):
    text=open(path,errors='ignore').read().replace('\r','')
    build=re.search(r'TH10_BUILD_ID=(\S+)',text); hz=re.search(r'real_hz=([\d.]+)',text)
    line=re.search(r'^stage real_hz:(.*)$',text,re.M); per={}
    if line:
        for m in re.finditer(r'(\d+)=([\d.]+)',line.group(1)): per[int(m.group(1))]=float(m.group(2))
    miss={int(m.group(1)):float(m.group(2)) for m in re.finditer(r'^miss stage (\d) windows=\d+ misses=\d+ \(([\d.]+)%\)',text,re.M)}
    return (build.group(1) if build else '?'),(float(hz.group(1)) if hz else None),per,miss
if __name__=='__main__':
    slow,rec=recorded_fps(sys.argv[1]); build,hz,per,miss=playback_fps(sys.argv[2]); label=sys.argv[3] if len(sys.argv)>3 else build
    tf=sum(v[0] for v in rec.values()); tt=sum(v[2] for v in rec.values())
    print(f'| 面 | 保存時の FPS（.rpy の記録） | 再生中の FPS（{label}、実測） | 差（再生中−保存時） | 再生中の MISS |')
    print('|---|---|---|---|---|')
    for s in sorted(rec):
        p=per.get(s); print(f'| {s} | {rec[s][1]:.1f} | {"%.1f"%p if p else "—"} | {"%+.1f"%(p-rec[s][1]) if p else "—"} | {"%.2f%%"%miss[s] if s in miss else "—"} |')
    print(f'| 全体 | {tf/tt:.1f}（処理落ち率 {slow:.1f}% から計算すると {60*(1-slow/100):.1f}） | {"%.2f"%hz if hz else "—"} | {"%+.1f"%(hz-tf/tt) if hz else "—"} | |')
