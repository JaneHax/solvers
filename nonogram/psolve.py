# portfolio nonogram solver: N boards of any size; writes hex answer JSON
import json,sys,time,subprocess,os,itertools
sys.path.insert(0,'/workspace/solvd')
BIN='/root/scripts/solvd/solvd-solver/nonobp'
def runs(line):
    out=[];n=0
    for x in line:
        if x==1: n+=1
        elif n: out.append(n); n=0
    if n: out.append(n)
    return out
CFG=[(1,0,300,3,20),(2,0,300,2,10),(3,0,300,1,1),(4,0,300,3,40),(5,0.3,300,3,20),(6,0,500,2,5),(7,0.2,300,2,20),(8,0.5,300,3,10),
     (9,0.1,300,3,20),(10,0,1000,1,10),(11,0.4,300,2,10),(12,0.2,200,3,30)]
def check(b,g):
    H,W=b['height'],b['width']
    return len(g)==H and all(len(r)==W for r in g) and all(runs(g[r])==list(b['rows'][r]) for r in range(H)) and all(runs([g[r][c] for r in range(H)])==list(b['cols'][c]) for c in range(W))
def main():
    d=json.load(open(sys.argv[1])); outp=sys.argv[2]; t=time.time()
    assert d.get('type')=='nonogram'
    boards=d['boards']; nb=len(boards); ncpu=os.cpu_count() or 4
    per=max(1,ncpu//nb)
    wd='/tmp/psolve_%d'%os.getpid(); os.makedirs(wd,exist_ok=True)
    procs={}  # bi -> list of (proc, outfile)
    for bi,b in enumerate(boards):
        inf='%s/in%d.txt'%(wd,bi)
        with open(inf,'w') as f:
            f.write('%d %d\n'%(b['height'],b['width']))
            for cl in list(b['rows'])+list(b['cols']): f.write(' '.join(map(str,[len(cl)]+list(cl)))+'\n')
        procs[bi]=[]
        for k in range(per):
            cfg=CFG[k%len(CFG)]; of='%s/out%d_%d.txt'%(wd,bi,k)
            p=subprocess.Popen([BIN]+[str(x) for x in cfg],stdin=open(inf),stdout=open(of,'w'),stderr=subprocess.DEVNULL)
            procs[bi].append((p,of,cfg))
    res={}
    deadline=d['expiresAt']-20
    while len(res)<nb:
        if time.time()>deadline: break
        time.sleep(0.2)
        for bi in range(nb):
            if bi in res: continue
            for p,of,cfg in procs[bi]:
                if p.poll()==0:
                    g=[[int(c) for c in l.strip()] for l in open(of) if l.strip()]
                    if check(boards[bi],g):
                        res[bi]=g; print('board',bi,'solved by',cfg,round(time.time()-t,1),flush=True)
                        for q,_,_ in procs[bi]:
                            if q.poll() is None: q.kill()
                        break
            if bi not in res and all(p.poll() is not None for p,_,_ in procs[bi]):
                print('board',bi,'all failed'); break
    for bi in procs:
        for q,_,_ in procs[bi]:
            if q.poll() is None: q.kill()
    if len(res)<nb: print('FAIL',len(res),'/',nb,flush=True); sys.exit(1)
    ans=[]
    for bi,b in enumerate(boards):
        H,W=b['height'],b['width']; g=res[bi]; rows=[]
        for r in range(H):
            bits=''.join(map(str,g[r])); nh=(W+3)//4; bits+='0'*(nh*4-W)
            rows.append(''.join('%x'%int(bits[i:i+4],2) for i in range(0,len(bits),4)))
        # independent decode check
        dec=[[int(c) for c in ''.join(format(int(ch,16),'04b') for ch in h)[:W]] for h in rows]
        for h in rows: assert len(h)==(W+3)//4 and ''.join(format(int(ch,16),'04b') for ch in h)[W:].strip('0')==''
        assert len(rows)==H and check(b,dec),('verify fail',bi)
        ans.append(rows)
    assert 'hex' in d['submit']['encodings']
    s=json.dumps({"challengeId":d['challengeId'],"encoding":"hex","boards":ans},separators=(',',':'))
    assert len(s)<=d['submit']['maxBodyBytes'],'too big'
    if 'expiresAt' in d and not d.get('_bench'): assert int(time.time())<d['expiresAt']-5,'too late'
    open(outp,'w').write(s)
    print('solved',round(time.time()-t,1),'bytes',len(s),flush=True)
main()
