#!/usr/bin/env python3
"""Checks the AVI files a native/tools/rec_sim.cpp run left in <root>/ms0/VIDEO/TH10.
  python3 native/tools/rec_sim_check.py <root> [max_av_offset_s=0.13]
Per file: our RIFF/idx1 walk (rec_check.py walk), ffprobe streams, a clean
ffmpeg decode, and A/V sync: the game marks the picture it presents right
after every whole second (white box) and the audio thread starts a 1 kHz beep
at every whole second of the same clock, so each marker picture's time
(frame / fps) must be within the 100 ms the box stays lit plus one audio block of a beep.
Build of the simulation (32-bit: the recorder passes addresses as u32):
  S=~/opt/i386-sysroot; g++ -m32 -O2 -std=gnu++17 -isystem $S/usr/include/x86_64-linux-gnu/c++/13/32 \\
    -isystem $S/usr/include/x86_64-linux-gnu -isystem /usr/include/x86_64-linux-gnu -I native/tools/rec_sim \\
    -B$S/usr/lib/gcc/x86_64-linux-gnu/13/32 -B$S/usr/lib32 -L$S/usr/lib/gcc/x86_64-linux-gnu/13/32 -L$S/usr/lib32 \\
    -static -pthread -o rec_sim native/tools/rec_sim.cpp
"""
import glob, os, subprocess, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import rec_check

def beeps(path):
    a = np.frombuffer(subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:a', '-f', 's16le', '-'], capture_output=True).stdout, '<i2').astype(np.int32)
    loud = np.abs(a) > 4000; on = []; i = 0
    while i < len(a):
        if loud[i]: on.append(i / 22050.0); i += 2205
        else: i += 1
    return on, len(a)

def markers(path, w, h):
    v = subprocess.run(['ffmpeg', '-v', 'error', '-i', path, '-map', '0:v', '-f', 'rawvideo', '-pix_fmt', 'gray', '-'], capture_output=True).stdout
    fr = np.frombuffer(v, np.uint8).reshape(-1, h, w)
    # the 64x64 box at the top left of the 480x272 picture; 320x240 is 3:2 below a 30-line black bar
    box = fr[:, 8:56, 8:56] if w == 480 else fr[:, 36:64, 6:34]
    lit = [box[i].mean() > 200 for i in range(fr.shape[0])]
    return [i for i in range(len(lit)) if lit[i] and (i == 0 or not lit[i - 1])], fr.shape[0]

def main():
    root = sys.argv[1]; tol = float(sys.argv[2]) if len(sys.argv) > 2 else 0.13   # marker lit 100 ms + one audio block
    files = sorted(glob.glob(os.path.join(root, 'ms0', 'VIDEO', 'TH10', '*.AVI')))
    if not files: print('no AVI files'); return 1
    for f in files:
        d = open(f, 'rb').read(); fps = rec_check.u32(d, 132); spf = 22050 // fps
        if rec_check.u32(d, 4) == 0:
            rec_check.check(False, '%s is unfinished (RIFF size 0)' % os.path.basename(f)); continue
        frames, vids, samples = rec_check.walk(f, fps, spf, False)
        probe, err = rec_check.ffcheck(f)
        rec_check.check(not err, 'ffmpeg decodes %s cleanly: %s' % (os.path.basename(f), err[:200]))
        rec_check.check(samples == frames * spf, 'audio samples = frames x spf')
        m, nv = markers(f, rec_check.u32(d, 64), rec_check.u32(d, 68)); b, na = beeps(f)
        offs = [min((abs(i / fps - t) for t in b), default=9.9) for i in m]
        bad = [o for o in offs if o > tol]
        # Every marker picture is at its beep (a dropped slot can lose a marker, never move it).
        rec_check.check(not bad and (len(m) >= len(b) // 2), 'markers at their beeps (%d markers, %d beeps, worst %.3f s)' % (len(m), len(b), max(offs or [0])))
        print('  %-28s frames=%5d (%.1f s) audio=%.1f s jpeg p50=%d max=%d B | markers=%d beeps=%d worst A/V offset=%.3f s mean=%.3f s | %s' % (
            os.path.basename(f), frames, frames / fps, samples / 22050.0, int(np.median(vids)), max(vids), len(m), len(b), max(offs or [0]), float(np.mean(offs)) if offs else 0, probe))
    print('REC_SIM_CHECK OK' if not rec_check.bad else 'REC_SIM_CHECK FAILED %d' % rec_check.bad)
    return 1 if rec_check.bad else 0

if __name__ == '__main__':
    sys.exit(main())
