"""List native method declarations (class, name, JNI signature, static?) in a classes.dex. Read-only."""
import struct, sys
sys.stdout.reconfigure(encoding='utf-8')
d = open(sys.argv[1], 'rb').read()
def uleb(p):
    r = s = 0
    while True:
        b = d[p]; p += 1; r |= (b & 0x7f) << s; s += 7
        if b < 0x80: return r, p
hdr = struct.unpack_from('<8I', d, 56)
sid_size, sid_off, tid_size, tid_off, pid_size, pid_off, fid_size, fid_off = hdr
mid_size, mid_off, cdef_size, cdef_off = struct.unpack_from('<4I', d, 88)
def S(i):
    o = struct.unpack_from('<I', d, sid_off + 4 * i)[0]; n, p = uleb(o); return d[p:d.index(b'\0', p)].decode('utf8', 'replace')
T = lambda i: S(struct.unpack_from('<I', d, tid_off + 4 * i)[0])
def proto(i):
    shorty, ret, poff = struct.unpack_from('<3I', d, pid_off + 12 * i)
    params = ''
    if poff:
        n = struct.unpack_from('<I', d, poff)[0]
        params = ''.join(T(struct.unpack_from('<H', d, poff + 4 + 2 * k)[0]) for k in range(n))
    return '(' + params + ')' + T(ret)
for c in range(cdef_size):
    cls, flags, sup, ifs, src, ann, cdata, sv = struct.unpack_from('<8I', d, cdef_off + 32 * c)
    if not cdata: continue
    p = cdata
    sf, p = uleb(p); inf, p = uleb(p); dm, p = uleb(p); vm, p = uleb(p)
    for _ in range(sf + inf): _, p = uleb(p); _, p = uleb(p)
    for cnt in (dm, vm):
        idx = 0
        for _ in range(cnt):
            diff, p = uleb(p); acc, p = uleb(p); code, p = uleb(p); idx += diff
            if acc & 0x100:
                mcls, mproto, mname = struct.unpack_from('<HHI', d, mid_off + 8 * idx)
                print('%s\t%s\t%s\t%s' % (T(mcls), S(mname), proto(mproto), 'static' if acc & 8 else 'instance'))
