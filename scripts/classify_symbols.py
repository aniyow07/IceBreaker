import re,collections,sys
rows=[l.split() for l in open(sys.argv[1]) if re.match(r'\s*\d+:',l)]
names=[r[7] for r in rows if len(r)>=8 and r[3]=='FUNC' and r[6]!='UND']
und=sorted({r[7] for r in rows if len(r)>=8 and r[6]=='UND'})
c=collections.Counter()
for n in names:
    m=re.match(r'_ZN(K)?(\d+)',n)
    if m:
        L=int(m.group(2)); s=n[m.end():m.end()+L]; rest=n[m.end()+L:]
        if s in('cocos2d','CocosDenshion'):
            m2=re.match(r'(\d+)',rest); s=s+'::'+(rest[len(m2.group(1)):len(m2.group(1))+int(m2.group(1))] if m2 else '?')
        c[s]+=1
    elif n.startswith('_Z'): c['<free C++>']+=1
    else: c['<C>']+=1
print('defined funcs',len(names),'undefined syms',len(und))
for k,v in sorted(c.items(),key=lambda x:-x[1]): print(f'{v:5} {k}')
print('\nUNDEFINED:'); print(' '.join(und))
