"""Check extracted files against META-INF/MANIFEST.MF per-entry digests (integrity only)."""
import base64,hashlib,os,re,sys
root=sys.argv[1]
txt=open(os.path.join(root,'META-INF','MANIFEST.MF'),encoding='utf8').read().replace('\r\n','\n').replace('\n ','')
ok=bad=miss=0;algs=set();badl=[]
for block in txt.split('\n\n'):
    m=re.search(r'^Name: (.*)$',block,re.M)
    if not m: continue
    name=m.group(1)
    d=re.search(r'^(SHA1|SHA-256)-Digest: (.*)$',block,re.M)
    alg,val=d.group(1),d.group(2);algs.add(alg)
    p=os.path.join(root,*name.split('/'))
    if not os.path.exists(p): miss+=1;badl.append('MISSING '+name);continue
    h=hashlib.sha1 if alg=='SHA1' else hashlib.sha256
    if base64.b64encode(h(open(p,'rb').read()).digest()).decode()==val: ok+=1
    else: bad+=1;badl.append('MISMATCH '+name)
extra=[]
for dp,_,fs in os.walk(root):
    for f in fs:
        rel=os.path.relpath(os.path.join(dp,f),root).replace(os.sep,'/')
        if not rel.startswith('META-INF/') and ('Name: '+rel+'\n') not in txt+'\n': extra.append(rel)
print(f'algs={algs} ok={ok} mismatch={bad} missing={miss} extra_not_in_manifest={len(extra)}')
for l in badl[:30]: print(l)
for e in extra[:30]: print('EXTRA',e)
