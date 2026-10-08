#!/usr/bin/env python3
"""TH10 recording: disassembly gate for the code the Media Engine runs
(from the G0b tool me_check.py, G0b; the recorder adds the copy ring).
  me_check.py <TH10GE.elf> <obj_dir>/psp/MeAudio.o
Walks every function reachable from meLibOnProcess (the ME entry, MeAudio.cpp)
and fails on what the ME must never execute:
  - FPU / VFPU instructions (any cop1 op, any dotted mnemonic, v* ops),
  - indirect jumps (jr other than $ra, jalr): jump tables / function pointers
    hold link-time 0x08... addresses while the ME runs the kseg0 0x88... alias,
  - traps and syscalls (teq/tne/... from integer division, break, syscall),
  - calls into anything but the program's own functions and MECC's meLib*
    (memset/memcpy/libgcc/sce* stubs),
  - data references to .rodata/.data/.bss other than the allow-listed rings
    (tables must come in through job arguments, read through kseg0).
Two levels: the linked ELF (what runs, after COMDAT selection) and the
object's relocations (symbol names of every data reference).
"""
import re, subprocess, sys
from collections import deque

ALLOW_DATA = ('th10_rec_box', 'th10_rec_jobs', 'th10_rec_wbox', 'th10_rec_wjobs', 'mailbox')      # uncached-alias rings (by design)
ALLOW_CALL_PREFIX = ('meLib',)
FPU_PLAIN = {'lwc1', 'swc1', 'ldc1', 'sdc1', 'mtc1', 'mfc1', 'ctc1', 'cfc1', 'mthc1', 'mfhc1', 'bc1t', 'bc1f', 'bc1tl', 'bc1fl',
             'mtv', 'mfv', 'mtvc', 'mfvc', 'lv', 'sv'}
TRAPS = {'teq', 'tne', 'tge', 'tgeu', 'tlt', 'tltu', 'teqi', 'tnei', 'tgei', 'tgeiu', 'tlti', 'tltiu', 'break', 'syscall'}

def run(*a):
    return subprocess.run(a, capture_output=True, text=True, check=True).stdout

def bad_insn(mn, ops):
    if '.' in mn or mn in FPU_PLAIN or mn.startswith('v'):
        return 'FPU/VFPU'
    if mn == 'jalr' or (mn == 'jr' and ops.strip() != 'ra'):
        return 'indirect jump'
    if mn in TRAPS:
        return 'trap/syscall'
    return None

# The audio mixer was on the ME before the recorder (device-proven since the
# TH10 MeAudio self-test): it is walked and reported, but its findings are
# notes, not failures (the rule is for the recording code).
EXISTING = ('th10::mix::mix_job',)
BRANCH = re.compile(r'^(b\w*|j)$')

def elf_check(elf):
    syms = {}
    for line in run('psp-nm', '-C', '--defined-only', '-S', elf).splitlines():
        m = re.match(r'([0-9a-f]+) ([0-9a-f]+) (\w) (.*)', line)
        if m:
            a, size, t, name = int(m.group(1), 16), int(m.group(2), 16), m.group(3), m.group(4)
            syms.setdefault(a, []).append((name, size, t))
    data_syms = sorted((a, sz, n) for a, l in syms.items() for (n, sz, t) in l if t in 'dDbBrRvVgGsS')
    def data_name(addr):
        for a, sz, n in data_syms:
            if a <= addr < a + max(sz, 1): return n
        return None
    funcs = {a: (n, sz) for a, l in syms.items() for (n, sz, t) in l if t in 'tTwW' and sz}
    def func_at(addr):
        for a, (n, sz) in funcs.items():
            if a <= addr < a + sz: return n
        return None
    entry = [a for a, (n, sz) in funcs.items() if n == 'meLibOnProcess']
    assert entry, 'meLibOnProcess not in ELF'
    seen = set(); todo = deque(entry); problems = []; notes = []; called_meLib = set(); refs = set()
    while todo:
        a = todo.popleft()
        if a in seen: continue
        seen.add(a); name, size = funcs[a]
        existing = name.startswith(EXISTING)
        report = notes if existing else problems
        dis = run('psp-objdump', '-d', '--no-show-raw-insn', '--start-address=0x%x' % a, '--stop-address=0x%x' % (a + size), elf)
        insns = []
        for line in dis.splitlines():
            m = re.match(r'\s*([0-9a-f]+):\s+(\S+)\s*(.*)', line)
            if m: insns.append((int(m.group(1), 16), m.group(2), m.group(3).split('<')[0].strip()))
        targets = set()
        for pc, mn, ops in insns:
            if BRANCH.match(mn) and mn not in ('break',):
                last = ops.split(',')[-1].strip()
                if re.match(r'^[0-9a-f]+$', last): targets.add(int(last, 16))
        val = {}
        for i, (pc, mn, ops) in enumerate(insns):
            if pc in targets: val = {}                         # control flow merges here: forget tracked values
            why = bad_insn(mn, ops)
            if why == 'trap/syscall' and mn == 'break' and i >= 2:
                # GCC's divide-by-zero guard: li rX,N (N != 0) ... bnez rX,skip ; div ; break 7
                prev = insns[max(0, i - 4):i]
                br = [o for (_, m2, o) in prev if m2 == 'bnez']
                li = [o for (_, m2, o) in prev if m2 == 'li']
                if br and li and any(l.split(',')[0] == br[0].split(',')[0] and int(l.split(',')[1], 0) != 0 for l in li):
                    notes.append('%s: divide by the constant %s (GCC zero-divisor guard at 0x%x is jumped over)' % (name, [l.split(',')[1] for l in li][0], pc)); why = None
            if why: report.append('%s: %s at 0x%x: %s %s' % (name, why, pc, mn, ops))
            if mn in ('div', 'divu'): notes.append('%s: integer %s at 0x%x' % (name, mn, pc))
            if mn == 'jal':
                t = int(ops.split()[0], 16)
                tname = funcs.get(t, ('?', 0))[0]
                if tname.startswith(ALLOW_CALL_PREFIX): called_meLib.add(tname); continue
                if t not in funcs: report.append('%s: call to non-function 0x%x' % (name, t)); continue
                if re.match(r'(mem|str|__|sce|_|std::|operator)', tname) and not tname.startswith('th10'):
                    report.append('%s: library call %s' % (name, tname)); continue
                todo.append(t); continue
            o = [x.strip() for x in ops.split(',')] if ops else []
            dest = o[0] if o and mn not in ('sw', 'sh', 'sb', 'swl', 'swr', 'cache', 'sync', 'nop', 'jr', 'mtc0', 'mult', 'multu', 'div', 'divu', 'madd', 'maddu', 'msub', 'msubu') and not BRANCH.match(mn) else None
            # effective addresses of loads/stores with a tracked base
            if len(o) == 2:
                mm = re.match(r'(-?\d+)\((\w+)\)$', o[1])
                if mm and mm.group(2) in val:
                    ea = val[mm.group(2)] + int(mm.group(1))
                    if 0x08800000 <= ea < 0x0c000000:   # a direct (link-address) data access, not through an alias
                        dn = data_name(ea); refs.add(dn or hex(ea))
                        if not (dn and any(x in dn for x in ALLOW_DATA)): report.append('%s: direct data access 0x%x (%s) at 0x%x' % (name, ea, dn, pc))
            new = None
            if mn == 'lui': new = int(o[1], 0) << 16
            elif mn == 'addiu' and o[1] in val: new = (val[o[1]] + int(o[2], 0)) & 0xffffffff
            elif mn == 'ori' and o[1] in val: new = val[o[1]] | int(o[2], 0)
            elif mn == 'or' and o[1] in val and o[2] in val: new = val[o[1]] | val[o[2]]
            elif mn == 'move' and o[1] in val: new = val[o[1]]
            if dest:
                val.pop(dest, None)
                if new is not None:
                    val[dest] = new
                    if mn in ('addiu', 'ori') and 0x08800000 <= new < 0x0c000000:   # an address formed
                        dn = data_name(new); fn = func_at(new)
                        refs.add(dn or (fn and 'code:' + fn) or hex(new))
                        if fn: notes.append('%s: takes the address of %s at 0x%x (function pointer)' % (name, fn, pc)) if existing or name == 'meLibOnProcess' else report.append('%s: function pointer %s at 0x%x' % (name, fn, pc))
                        elif not (dn and any(x in dn for x in ALLOW_DATA)): report.append('%s: address of %s formed at 0x%x' % (name, dn or hex(new), pc))
    names = sorted(funcs[a][0] for a in seen)
    return names, sorted(called_meLib), sorted(refs), problems, notes

def obj_check(obj):
    """Relocations of the functions reachable from meLibOnProcess inside the object."""
    dis = run('psp-objdump', '-dr', '--no-show-raw-insn', obj)
    sec = None; fn_of_sec = {}; relocs = {}
    for line in dis.splitlines():
        m = re.match(r'Disassembly of section (\S+):', line)
        if m: sec = m.group(1); relocs.setdefault(sec, []); continue
        m = re.match(r'[0-9a-f]+ <(.+)>:', line)
        if m and sec: fn_of_sec.setdefault(sec, m.group(1)); continue
        m = re.match(r'\s*[0-9a-f]+: (R_MIPS_\w+)\s+(\S+)', line)
        if m and sec: relocs[sec].append((m.group(1), m.group(2)))
    sec_of_fn = {v: k for k, v in fn_of_sec.items()}
    start = sec_of_fn.get('meLibOnProcess')
    assert start, 'meLibOnProcess not in object'
    seen = set(); todo = deque([start]); problems = []; external = set(); data = set()
    while todo:
        s = todo.popleft()
        if s in seen: continue
        seen.add(s)
        for kind, target in relocs.get(s, []):
            tsec = target if target in relocs else sec_of_fn.get(target)
            if kind == 'R_MIPS_26':
                if tsec: todo.append(tsec)
                elif target.startswith(ALLOW_CALL_PREFIX): external.add(target)
                elif 'mix7mix_job' in fn_of_sec.get(s, s): pass   # the pre-existing audio mixer (reported by the ELF pass)
                else: problems.append('%s: call to %s (not in this object, not meLib)' % (fn_of_sec.get(s, s), target))
            elif kind in ('R_MIPS_HI16', 'R_MIPS_LO16'):
                data.add(target)
                fn = fn_of_sec.get(s, s)
                if not any(x in target for x in ALLOW_DATA) and not (target.startswith('.text.') and fn == 'meLibOnProcess') and 'mix_job' not in fn and 'mix7mix_job' not in fn:
                    problems.append('%s: %s against %s' % (fn, kind, target))
            else:
                problems.append('%s: relocation %s %s' % (fn_of_sec.get(s, s), kind, target))
    return sorted(fn_of_sec.get(s, s) for s in seen), sorted(external), sorted(data), problems

def main():
    elf, obj = sys.argv[1], sys.argv[2]
    names, meLib, refs, p1, notes = elf_check(elf)
    print('ELF: %d functions reachable from meLibOnProcess:' % len(names))
    for n in names: print('   ', n)
    print('ELF: meLib calls:', ', '.join(meLib))
    print('ELF: data addresses formed:', ', '.join(refs))
    onames, ext, data, p2 = obj_check(obj)
    print('OBJ: %d functions reachable in %s; external calls: %s; data relocations: %s' % (len(onames), obj.split('/')[-1], ', '.join(ext), ', '.join(data)))
    for n in sorted(set(notes)): print('NOTE', n)
    for p in p1 + p2: print('PROBLEM', p)
    print('ME_CHECK_OK' if not (p1 or p2) else 'ME_CHECK_FAILED %d' % len(p1 + p2))
    return 1 if (p1 or p2) else 0

if __name__ == '__main__':
    sys.exit(main())
