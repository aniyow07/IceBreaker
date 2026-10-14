"""Copy a binary AndroidManifest.xml with two targeted changes and nothing else:
  --target-sdk N        set android:targetSdkVersion (integer value in place)
  --min-sdk N           set android:minSdkVersion (integer value in place)
  --uses-library NAME   add <uses-library android:name="NAME"/> as the first child of
                        <application> (needed for org.apache.http.legacy when targetSdk >= 28)
Strings are appended to the end of the string pool, so every existing string index,
resource-map entry and attribute stays unchanged."""
import argparse, struct, sys

ap = argparse.ArgumentParser()
ap.add_argument('src'); ap.add_argument('dst')
ap.add_argument('--target-sdk', type=int)
ap.add_argument('--min-sdk', type=int)
ap.add_argument('--uses-library')
a = ap.parse_args()
data = bytearray(open(a.src, 'rb').read())
TARGET_SDK, MIN_SDK, NAME = 0x01010270, 0x0101020c, 0x01010003

def chunks(buf):
    pos = 8
    while pos < len(buf):
        t, hs, sz = struct.unpack_from('<HHI', buf, pos)
        yield pos, t, hs, sz
        pos += sz

# --- string pool ----------------------------------------------------------------
sp = next(p for p, t, hs, sz in chunks(data) if t == 0x0001)
_, sp_hs, sp_sz = struct.unpack_from('<HHI', data, sp)
count, styles, flags, str_start, style_start = struct.unpack_from('<IIIII', data, sp + 8)
utf8 = bool(flags & 0x100)
if styles: sys.exit('string pools with styles are not supported')
offsets = list(struct.unpack_from('<%dI' % count, data, sp + sp_hs))
blob = bytes(data[sp + str_start: sp + sp_sz])

def read_str(i):
    p = offsets[i]
    if utf8:
        n = blob[p]; p += 1
        if n & 0x80: p += 1
        n = blob[p]; p += 1
        if n & 0x80: n = ((n & 0x7f) << 8) | blob[p]; p += 1
        return blob[p:p + n].decode('utf8')
    n = struct.unpack_from('<H', blob, p)[0]
    return blob[p + 2:p + 2 + 2 * n].decode('utf-16le')

strings = [read_str(i) for i in range(count)]
new_strings = []
def string_index(s):
    if s in strings: return strings.index(s)
    strings.append(s); new_strings.append(s); return len(strings) - 1

def enc(s):
    if utf8:
        b = s.encode('utf8')
        assert len(s) < 128 and len(b) < 128
        return bytes([len(s), len(b)]) + b + b'\0'
    return struct.pack('<H', len(s)) + s.encode('utf-16le') + b'\0\0'

# --- targetSdk (in place) -----------------------------------------------------------
resids = []
for p, t, hs, sz in chunks(data):
    if t == 0x0180:
        resids = list(struct.unpack_from('<%dI' % ((sz - hs) // 4), data, p + hs))
def attrs(p, hs):
    _, _, ast, asz, acnt = struct.unpack_from('<iiHHH', data, p + 16)
    for i in range(acnt):
        yield p + 16 + ast + i * asz
def set_int_attr(resid, value, label):
    hits = 0
    for p, t, hs, sz in chunks(data):
        if t != 0x0102: continue
        for at in attrs(p, hs):
            ns, an, raw, vs, _, vt, vd = struct.unpack_from('<iiiHBBI', data, at)
            if an < len(resids) and resids[an] == resid:
                assert vt == 0x10 and raw == -1
                print('%s %d -> %d' % (label, vd, value))
                struct.pack_into('<I', data, at + 16, value); hits += 1
    if hits != 1: sys.exit('expected one %s attribute, found %d' % (label, hits))
if a.min_sdk is not None:
    set_int_attr(MIN_SDK, a.min_sdk, 'minSdkVersion')
if False:
    hits = 0
    for p, t, hs, sz in chunks(data):
        if t != 0x0102: continue
        for at in attrs(p, hs):
            ns, an, raw, vs, _, vt, vd = struct.unpack_from('<iiiHBBI', data, at)
            if an < len(resids) and resids[an] == TARGET_SDK:
                assert vt == 0x10 and raw == -1
                print('targetSdkVersion %d -> %d' % (vd, a.target_sdk))
                struct.pack_into('<I', data, at + 16, a.target_sdk); hits += 1
    if hits != 1: sys.exit('expected one targetSdkVersion attribute, found %d' % hits)

if a.target_sdk is not None:
    set_int_attr(TARGET_SDK, a.target_sdk, 'targetSdkVersion')

# --- <uses-library> element -------------------------------------------------------------
insert_at, insert_bytes = None, b''
if a.uses_library:
    name_attr = next(i for i, r in enumerate(resids) if r == NAME)
    android_ns = None
    for p, t, hs, sz in chunks(data):
        if t == 0x0100:
            android_ns = struct.unpack_from('<i', data, p + 20)[0]  # namespace URI string index
            break
    el = string_index('uses-library')
    val = string_index(a.uses_library)
    for p, t, hs, sz in chunks(data):
        if t == 0x0102 and strings[struct.unpack_from('<i', data, p + 20)[0]] == 'application':
            line = struct.unpack_from('<I', data, p + 8)[0]
            insert_at = p + sz  # right after <application ...> start tag
            break
    if insert_at is None: sys.exit('no <application> element')
    attr = struct.pack('<iiiHBBI', android_ns, name_attr, val, 8, 0, 0x03, val)
    start = struct.pack('<HHIIi', 0x0102, 16, 16 + 20 + len(attr), line, -1) + \
        struct.pack('<iiHHHHHH', -1, el, 20, 20, 1, 0, 0, 0) + attr
    end = struct.pack('<HHIIi', 0x0103, 16, 24, line, -1) + struct.pack('<ii', -1, el)
    insert_bytes = start + end

# --- rebuild string pool if strings were added ---------------------------------------
out = data
if new_strings:
    new_blob = bytearray(blob.rstrip(b'\0') if False else blob)
    # strings data may be padded to 4 bytes at the end; append after the last real string
    end_of_strings = max(offsets[i] + len(enc(strings[i])) for i in range(count))
    new_blob = bytearray(blob[:end_of_strings])
    new_offsets = offsets[:]
    for s in new_strings:
        new_offsets.append(len(new_blob)); new_blob += enc(s)
    while len(new_blob) % 4: new_blob += b'\0'
    n = len(new_offsets)
    header = bytearray(data[sp:sp + sp_hs])
    new_str_start = sp_hs + 4 * n
    struct.pack_into('<IIIII', header, 8, n, 0, flags, new_str_start, 0)
    pool = header + struct.pack('<%dI' % n, *new_offsets) + new_blob
    struct.pack_into('<I', pool, 4, len(pool))
    delta = len(pool) - sp_sz
    out = data[:sp] + pool + data[sp + sp_sz:]
    if insert_at is not None and insert_at > sp: insert_at += delta
if insert_bytes:
    out = out[:insert_at] + insert_bytes + out[insert_at:]
struct.pack_into('<I', out, 4, len(out))
open(a.dst, 'wb').write(out)
print('wrote', a.dst, '(%d new strings)' % len(new_strings))
