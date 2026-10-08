#!/usr/bin/env python3
# Folds th10_samples.bin (native/main.cpp, TH10_SAMPLER) into per-callback
# exclusive/inclusive function profiles.
#   samples.py BINARY SAMPLES [--top N] [--callbacks A,B,...] [--tree FUNC]
import bisect,re,struct,subprocess,sys,collections,argparse
ap=argparse.ArgumentParser()
ap.add_argument('binary');ap.add_argument('samples')
ap.add_argument('--top',type=int,default=14);ap.add_argument('--callbacks',default='')
ap.add_argument('--min-share',type=float,default=1.5)
a=ap.parse_args()
names_src=open(__file__.rsplit('/',2)[0]+'/CallbackNames.inc').read()
cb=re.findall(r'"([^"]+)"',names_src.split('=',1)[1])
def token_name(t):
    if t<0:return 'outside'
    d,i=divmod(t,128);return (cb[i] if i<len(cb) else '?%d'%i)+('' if d==0 else '')
syms=[]
for line in subprocess.run(['nm','-n','--defined-only','-C',a.binary],capture_output=True,text=True).stdout.splitlines():
    parts=line.split(' ',2)
    if len(parts)==3 and parts[1] in 'tTwW':syms.append((int(parts[0],16),parts[2]))
addrs=[s[0] for s in syms]
cache={}
def fn(pc):
    if pc in cache:return cache[pc]
    i=bisect.bisect_right(addrs,pc)-1;n=syms[i][1] if i>=0 else '?'
    n=n.replace('(anonymous namespace)::','~');n=re.sub(r'\(.*$','',n);cache[pc]=n;return n
raw=open(a.samples,'rb').read();count=struct.unpack_from('<I',raw,0)[0];size=8+30*4
per=collections.defaultdict(lambda:[0,collections.Counter(),collections.Counter()])
total=0
for k in range(count):
    token,depth=struct.unpack_from('<iI',raw,4+k*size);pcs=struct.unpack_from('<%dI'%depth,raw,4+k*size+8)
    frames=[fn(pcs[0])]+[fn(p-1) for p in pcs[1:]]
    e=per[token];e[0]+=1;e[1][frames[0]]+=1
    for f in set(frames):e[2][f]+=1
    total+=1
print('samples=%d'%total)
want=[w for w in a.callbacks.split(',') if w]
order=sorted(per.items(),key=lambda kv:-kv[1][0])
for token,(n,exc,inc) in order:
    name=token_name(token)+(' (draw)' if token>=128 else '')
    if want and token_name(token) not in want:continue
    share=100.0*n/total
    if not want and share<a.min_share:continue
    print('\n== %s  %.1f%% (%d samples)'%(name,share,n))
    print('  exclusive:');[print('    %5.1f%%  %s'%(100.0*c/n,f[:110])) for f,c in exc.most_common(a.top)]
    print('  inclusive:');[print('    %5.1f%%  %s'%(100.0*c/n,f[:110])) for f,c in inc.most_common(a.top+6)]
