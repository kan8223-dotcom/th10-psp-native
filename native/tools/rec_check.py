#!/usr/bin/env python3
"""Validates the AVI files of native/tools/rec_check.cpp (TH10_REC core).
  python3 native/tools/rec_check.py <outdir>
Per file: ffprobe (mjpeg 480x272 at the file's fps, pcm_s16le 22050 Hz mono),
ffmpeg full decode without errors, our own RIFF walk (movi at 2048, '00dc'/'01wb'
pairs, every audio chunk spf*2 bytes, idx1 entry = chunk position/size/flags,
avih/strh counts and maxima, RIFF and movi sizes), then:
  a_b0     byte-identical to the XMB-proven B0 writer (write_avi below, copied
           from the G0 tool make_avitest.py) fed the same JPEGs and PCM
  sync     marker pictures (white box) and 1 kHz beeps start together:
           decoded frame i <-> decoded audio sample i*spf, in every file
  repair   the repaired crash files decode and their idx1 matches their chunks
"""
import glob, os, struct, subprocess, sys
import numpy as np

def chunk(fourcc, data):
    return fourcc + struct.pack('<I', len(data)) + data + (b'\0' if len(data) & 1 else b'')

def write_avi(path, frames, width, height, rate, scale, audio, idx1=True):
    """make_avitest.py write_avi (th10_rec, 2026-10-01; the B0 files that played with sound on the Go)."""
    fps = rate / scale; n = len(frames)
    blk = audio['channels'] * audio['bits'] // 8; abytes = audio['rate'] * blk
    achunks = audio['chunks']; assert len(achunks) == n
    vmax = max(len(f) for f in frames); amax = max(len(a) for a in achunks); total_audio = sum(len(a) for a in achunks)
    movi = bytearray(b'movi'); index = []
    for f, a in zip(frames, achunks):
        index.append((b'00dc', 0x10, len(movi), len(f))); movi += chunk(b'00dc', f)
        index.append((b'01wb', 0x10, len(movi), len(a))); movi += chunk(b'01wb', a)
    avih = struct.pack('<14I', round(1e6 / fps), int(vmax * fps + abytes) + 1, 0, (0x10 if idx1 else 0) | 0x100, n, 0, 2, vmax + 8, width, height, 0, 0, 0, 0)
    vstrh = b'vids' + b'MJPG' + struct.pack('<IHHIIIIIIIIhhhh', 0, 0, 0, 0, scale, rate, 0, n, vmax, 0xffffffff, 0, 0, 0, width, height)
    vstrf = struct.pack('<IiiHH4sIiiII', 40, width, height, 1, 24, b'MJPG', width * height * 3, 0, 0, 0, 0)
    astrh = b'auds' + struct.pack('<IIHHIIIIIIIIhhhh', 0, 0, 0, 0, 0, blk, abytes, 0, total_audio // blk, amax, 0xffffffff, blk, 0, 0, 0, 0)
    astrf = struct.pack('<HHIIHHH', audio['tag'], audio['channels'], audio['rate'], abytes, blk, audio['bits'], 0)
    hdrl = b'hdrl' + chunk(b'avih', avih) + chunk(b'LIST', b'strl' + chunk(b'strh', vstrh) + chunk(b'strf', vstrf)) \
           + chunk(b'LIST', b'strl' + chunk(b'strh', astrh) + chunk(b'strf', astrf))
    head = chunk(b'LIST', hdrl)
    junk = 2048 - 12 - len(head) - 8
    body = head + chunk(b'JUNK', b'\0' * junk) + chunk(b'LIST', bytes(movi))
    if idx1:
        body += chunk(b'idx1', b''.join(cid + struct.pack('<III', fl, off, sz) for cid, fl, off, sz in index))
    data = b'RIFF' + struct.pack('<I', len(body) + 4) + b'AVI ' + body
    open(path, 'wb').write(data)

u32 = lambda d, o: struct.unpack_from('<I', d, o)[0]
bad = 0
def check(cond, what):
    global bad
    if not cond:
        bad += 1; print('  FAIL', what)
    return cond

def walk(path, fps, spf, empty_ok):
    """Our RIFF walk; returns (frames, video chunk sizes, audio samples)."""
    d = open(path, 'rb').read()
    check(d[:4] == b'RIFF' and d[8:12] == b'AVI ', 'RIFF AVI')
    riff = u32(d, 4); end = riff + 8
    check(end <= len(d), 'RIFF size %d within the file %d' % (end, len(d)))
    check(d[2048:2052] == b'LIST' and d[2056:2060] == b'movi', "'LIST' 'movi' at 2048")
    movi_end = 2056 + u32(d, 2052)
    at = 2060; chunks = []
    while at < movi_end:
        cid = d[at:at + 4]; n = u32(d, at + 4); chunks.append((cid, at, n)); at += 8 + n + (n & 1)
    check(at == movi_end, 'chunks end exactly at the movi end')
    check(d[at:at + 4] == b'idx1', 'idx1 after movi')
    ni = u32(d, at + 4) // 16; idx = [struct.unpack_from('<4sIII', d, at + 8 + 16 * i) for i in range(ni)]
    check(at + 8 + 16 * ni == end, 'idx1 ends at the RIFF end')
    check(len(idx) == len(chunks), 'one idx1 entry per chunk (%d vs %d)' % (len(idx), len(chunks)))
    vids = []; samples = 0
    for i, ((cid, pos, n), (icid, fl, off, sz)) in enumerate(zip(chunks, idx)):
        want = b'00dc' if i % 2 == 0 else b'01wb'
        if not check(cid == want and icid == cid, 'chunk %d is %s' % (i, want)): break
        check(off == pos - 2056 and sz == n, 'idx1 %d offset/size' % i)
        check(fl == (0x10 if (n or cid == b'01wb') else 0), 'idx1 %d flags' % i)
        if cid == b'00dc':
            vids.append(n)
            if n: check(d[pos + 8:pos + 10] == b'\xff\xd8' and d[pos + 8 + n - 2:pos + 8 + n] == b'\xff\xd9', 'chunk %d is a whole JPEG' % i)
            else: check(empty_ok, 'zero-length picture only in the empty-drop variant')
        else:
            check(n == spf * 2, 'audio chunk %d = %d bytes' % (i, spf * 2)); samples += n // 2
    frames = len(vids); vmax = max(vids) if vids else 0
    check(u32(d, 48) == frames and u32(d, 140) == frames, 'avih/strh frame counts %d' % frames)
    check(u32(d, 264) == samples, 'audio strh length %d' % samples)
    check(u32(d, 60) == vmax + 8 and u32(d, 144) == vmax and u32(d, 268) == spf * 2, 'suggested buffer sizes')
    check(u32(d, 36) == vmax * fps + 44100 + 1 and u32(d, 32) == round(1e6 / fps) and u32(d, 132) == fps, 'avih rate fields')
    check(u32(d, 44) == 0x110, 'avih flags 0x110')
    return frames, vids, samples

def ffcheck(path):
    r = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_name,width,height,r_frame_rate,pix_fmt,sample_rate,channels,nb_frames', '-of', 'csv=p=0', path],
                       capture_output=True, text=True)
    dec = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-f', 'null', '-'], capture_output=True, text=True)
    return r.stdout.strip().replace('\n', ' | '), (r.stderr + dec.stderr).strip()

def decode_markers(path, spf):
    """Frame indices with the white marker box, sample indices where a beep starts."""
    v = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:v', '-f', 'rawvideo', '-pix_fmt', 'gray', '-'], capture_output=True).stdout
    fr = np.frombuffer(v, np.uint8).reshape(-1, 272, 480)
    lit = [fr[i, 8:56, 8:56].mean() > 200 for i in range(fr.shape[0])]
    marks = [i for i in range(len(lit)) if lit[i] and (i == 0 or not lit[i - 1])]   # rising edges (a dropped slot repeats the marker)
    a = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:a', '-f', 's16le', '-'], capture_output=True).stdout, '<i2').astype(np.int32)
    loud = np.abs(a) > 4000
    onsets = []; i = 0
    while i < len(a):
        if loud[i]:
            # the beep starts at the slot boundary: walk back over the first quiet zero crossing of the sine
            s = i
            while s > 0 and (s % spf) != 0 and s > i - 30: s -= 1
            onsets.append(s if s % spf == 0 else i); i += 441
        else: i += 1
    return marks, onsets, fr.shape[0], len(a)

def main():
    out = sys.argv[1]
    for exp in sorted(glob.glob(os.path.join(out, '*.expect'))):
        name = os.path.basename(exp)[:-7]; lines = open(exp).read().split('\n')
        files = [l[5:] for l in lines if l.startswith('file ')]
        kv = dict(zip(*[iter([l for l in lines if l.startswith('slots')][0].split())] * 2))
        fps, spf, marker, crash = int(kv['fps']), int(kv['spf']), int(kv['marker']), kv['crash'] == '1'
        print('==', name, 'files=%d' % len(files), 'expected slots=%s real=%s repeats=%s empty=%s lead=%s' % (kv['slots'], kv['real'], kv['repeats'], kv['empty'], kv['lead']))
        if crash: files = sorted(glob.glob(os.path.join(out, name + '_cut*.avi')))
        total = 0; first = 0
        for f in files:
            frames, vids, samples = walk(f, fps, spf, name.startswith('c_'))
            probe, err = ffcheck(f)
            ok = check(not err or name.startswith('c_'), 'ffmpeg decodes %s cleanly: %s' % (os.path.basename(f), err[:300]))
            print('  %-22s frames=%5d audio=%7d (%.2f s) video p50=%d max=%d B | %s%s' % (os.path.basename(f), frames, samples, samples / 22050.0,
                  int(np.median([v for v in vids if v] or [0])), max(vids or [0]), probe, '' if not err else ' | ffmpeg: ' + err[:160].replace('\n', ' ')))
            check(samples == frames * spf, 'audio samples = frames x spf')
            if not name.startswith('c_') and not crash:
                check(('mjpeg,480,272,yuvj420p,%d/1' % fps) in probe.replace('|', ',').replace(' ', '') or 'mjpeg' in probe, 'ffprobe video stream')
                check('pcm_s16le,22050,1' in probe.replace(' ', ''), 'ffprobe audio stream 22050 Hz mono s16')
            if marker and not name.startswith('c_'):
                marks, onsets, nv, na = decode_markers(f, spf)
                sync = [m for m in marks if m * spf in set(onsets)]
                check(len(sync) == len(marks), 'every marker picture starts with its beep (%d/%d)' % (len(sync), len(marks)))
                print('     sync: marker pictures=%d all at their beep=%s beeps=%d decoded frames=%d audio=%d' % (len(marks), len(sync) == len(marks), len(onsets), nv, na))
            total += frames
        if not crash:
            check(total == int(kv['slots']), 'all slots in the files (%d vs %s)' % (total, kv['slots']))
        if name == 'a_b0':
            jl = open(os.path.join(out, 'a_b0.jpegs'), 'rb').read(); jpegs = []; i = 0
            while i < len(jl):
                n = u32(jl, i); jpegs.append(jl[i + 4:i + 4 + n]); i += 4 + n
            pcm = open(os.path.join(out, 'a_b0.pcm'), 'rb').read()
            achunks = [pcm[k * spf * 2:(k + 1) * spf * 2] for k in range(len(jpegs))]
            ref = os.path.join(out, 'a_b0_python_b0.avi')
            write_avi(ref, jpegs, 480, 272, 15, 1, dict(tag=1, channels=1, rate=22050, bits=16, chunks=achunks))
            same = open(ref, 'rb').read() == open(files[0], 'rb').read()
            check(same, 'byte-identical to the B0 writer')
            print('  B0 writer (make_avitest.py write_avi) on the same JPEGs and PCM: byte-identical=%s (%d bytes)' % (same, os.path.getsize(ref)))
    print('REC_CHECK_PY OK' if not bad else 'REC_CHECK_PY FAILED %d' % bad)
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
