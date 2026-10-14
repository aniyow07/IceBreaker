"""Copy a binary AndroidManifest.xml, changing only the integer value of
android:targetSdkVersion (resource id 0x01010270). Every other byte stays identical."""
import struct, sys
src, dst, new_target = sys.argv[1], sys.argv[2], int(sys.argv[3])
data = bytearray(open(src, 'rb').read())
TARGET_SDK_RES = 0x01010270
pos = 8; resids = []; hits = 0
while pos < len(data):
    t, hs, sz = struct.unpack_from('<HHI', data, pos)
    if t == 0x0180:
        resids = list(struct.unpack_from('<%dI' % ((sz - hs) // 4), data, pos + hs))
    elif t == 0x0102:
        _, name, ast, asz, acnt = struct.unpack_from('<iiHHH', data, pos + 16)
        for i in range(acnt):
            a = pos + 16 + ast + i * asz
            ans, an, araw, vs, _, vt, vd = struct.unpack_from('<iiiHBBI', data, a)
            if an < len(resids) and resids[an] == TARGET_SDK_RES:
                if vt != 0x10: sys.exit('unexpected value type %d' % vt)
                if araw != -1: sys.exit('raw string value present; refusing to patch')
                print('targetSdkVersion %d -> %d at offset 0x%x' % (vd, new_target, a + 16))
                struct.pack_into('<I', data, a + 16, new_target); hits += 1
    pos += sz
if hits != 1: sys.exit('expected exactly one targetSdkVersion attribute, found %d' % hits)
open(dst, 'wb').write(data)
