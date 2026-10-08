#!/usr/bin/env python3
# TH08 r269 の順序配置 (SORT(.text.sorted.*)) を TH10 に移植した生成器。
# -ffunction-sections でコンパイルした関数セクションを、hot_order.txt の順に
# .text の先頭 (.text.hot の手前) へ並べるリンカスクリプトを書く。
#   gen_hot_ld.py <ELF> <hot_order.txt> <base.ld> <out.ld>
# ELF は同じソースの任意のビルド (シンボル名が同じなら -ffunction-sections 無しでもよい)。
# どのパターンにも一致しない行、2 つ以上の関数に一致した行は報告する。
import re,subprocess,sys
elf,order_file,base_ld,out_ld=sys.argv[1:5]
def nm(args):
    return subprocess.run(['psp-nm']+args+[elf],capture_output=True,text=True,check=True).stdout.splitlines()
mangled=[l.split(' ',2) for l in nm([]) if re.match(r'^[0-9a-f]+ [tTwW] ',l)]
demangled=[l.split(' ',2) for l in nm(['-C']) if re.match(r'^[0-9a-f]+ [tTwW] ',l)]
assert len(mangled)==len(demangled)
symbols=[(d[2],m[2]) for m,d in zip(mangled,demangled)]
lines,seen,report=[],set(),[]
for raw in open(order_file,encoding='utf-8'):
    pattern=raw.strip()
    if not pattern or pattern.startswith('#'):continue
    rx=re.compile(pattern)
    hits=[(d,m) for d,m in symbols if rx.match(d) and m not in seen]
    if not hits:report.append('NO MATCH: '+pattern);continue
    if len({m.split('.')[0] for d,m in hits})>1:report.append('%d functions: %s'%(len(hits),pattern))
    for d,m in hits:
        seen.add(m);lines.append('    *(.text.%s)'%m)
base=open(base_ld).read()
anchor='    *(.text.hot .text.hot.*)'
assert base.count(anchor)==1,'anchor not found in '+base_ld
block='    /* th10_port: hot_order.txt (tools/gen_hot_ld.py) */\n'+'\n'.join(lines)+'\n'
open(out_ld,'w').write(base.replace(anchor,block+anchor))
print('%d sections placed'%len(lines))
for r in report:print(r)
