"""List class names and strings from a classes.dex (read-only, no execution)."""
import struct,sys,collections
sys.stdout.reconfigure(encoding="utf-8")
d=open(sys.argv[1],'rb').read()
def uleb(p):
    r=s=0
    while True:
        b=d[p];p+=1;r|=(b&0x7f)<<s;s+=7
        if b<0x80:return r,p
sid_size,sid_off,tid_size,tid_off=struct.unpack_from('<IIII',d,56)
cdef_size,cdef_off=struct.unpack_from('<II',d,96)
def S(i):
    o=struct.unpack_from('<I',d,sid_off+4*i)[0];n,p=uleb(o);e=d.index(b'\0',p);return d[p:e].decode('utf8','replace')
strings=[S(i) for i in range(sid_size)]
types=[strings[struct.unpack_from('<I',d,tid_off+4*i)[0]] for i in range(tid_size)]
classes=[types[struct.unpack_from('<I',d,cdef_off+32*i)[0]] for i in range(cdef_size)]
mode=sys.argv[2] if len(sys.argv)>2 else 'pkgs'
if mode=='pkgs':
    c=collections.Counter('.'.join(x[1:-1].split('/')[:3]) for x in classes)
    print('dex version',d[4:7],'classes',len(classes),'strings',len(strings))
    for k,v in c.most_common(): print(v,k)
elif mode=='classes':
    print('\n'.join(sorted(classes)))
else:
    print('\n'.join(strings))
