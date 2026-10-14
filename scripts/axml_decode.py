"""Minimal Android binary XML (AXML) to text decoder. Read-only; prints XML."""
import struct,sys
def decode(data):
    strings=[];resids=[];out=[];depth=0;ns={}
    def s(i): return strings[i] if 0<=i<len(strings) else ''
    pos=8
    while pos<len(data):
        t,hs,sz=struct.unpack_from('<HHI',data,pos)
        if t==0x0001:
            cnt,scnt,flags,so,sso=struct.unpack_from('<IIIII',data,pos+8)
            utf8=flags&0x100
            offs=struct.unpack_from('<%dI'%cnt,data,pos+hs)
            base=pos+so
            for o in offs:
                p=base+o
                if utf8:
                    n=data[p];p+=1
                    if n&0x80:p+=1
                    n=data[p];p+=1
                    if n&0x80:n=((n&0x7f)<<8)|data[p];p+=1
                    strings.append(data[p:p+n].decode('utf8','replace'))
                else:
                    n=struct.unpack_from('<H',data,p)[0];p+=2
                    if n&0x8000:n=((n&0x7fff)<<16)|struct.unpack_from('<H',data,p)[0];p+=2
                    strings.append(data[p:p+2*n].decode('utf-16le','replace'))
        elif t==0x0180:
            resids=list(struct.unpack_from('<%dI'%((sz-hs)//4),data,pos+hs))
        elif t==0x0100:
            pfx,uri=struct.unpack_from('<ii',data,pos+16); ns[s(uri)]=s(pfx)
        elif t==0x0102:
            nsi,name,ast,asz,acnt=struct.unpack_from('<iiHHH',data,pos+16)
            attrs=[]
            for i in range(acnt):
                a=pos+16+ast+i*asz
                ans,an,araw,vs,_,vt,vd=struct.unpack_from('<iiiHBBI',data,a)
                nm=s(an) or ('0x%08x'%resids[an] if an<len(resids) else '?')
                p=ns.get(s(ans),'') if ans>=0 else ''
                if vt==3: v=s(vd)
                elif vt==0x10: v=str(struct.unpack('<i',struct.pack('<I',vd))[0])
                elif vt==0x12: v='true' if vd else 'false'
                elif vt==0x11: v='0x%x'%vd
                elif vt==1: v='@0x%08x'%vd
                elif vt==2: v='?0x%08x'%vd
                else: v=s(araw) if araw>=0 else '(type%d)0x%x'%(vt,vd)
                attrs.append('%s%s="%s"'%(p+':' if p else '',nm,v))
            out.append('  '*depth+'<'+s(name)+(' '+' '.join(attrs) if attrs else '')+'>');depth+=1
        elif t==0x0103:
            depth-=1;out.append('  '*depth+'</'+s(struct.unpack_from('<i',data,pos+20)[0])+'>')
        pos+=sz
    return '\n'.join(out)
if __name__=='__main__':
    print(decode(open(sys.argv[1],'rb').read()))
