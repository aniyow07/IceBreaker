"""Build an unsigned APK from an extracted tree, skipping the old signature files
and substituting replacement files. Usage: pack_apk.py <tree> <out.apk> [arc=localfile ...] [!excluded/prefix ...]"""
import os, sys, zipfile
tree, out = sys.argv[1], sys.argv[2]
repl = dict(a.split('=', 1) for a in sys.argv[3:] if not a.startswith('!'))
exclude = [a[1:] for a in sys.argv[3:] if a.startswith('!')]  # "!prefix" drops tree members
STORE = ('.png', '.ogg', '.m4a', '.gif', '.zip', '.jpg')
with zipfile.ZipFile(out, 'w') as z:
    names = []
    for dp, _, fs in os.walk(tree):
        for f in fs:
            names.append(os.path.relpath(os.path.join(dp, f), tree).replace(os.sep, '/'))
    names.sort(key=lambda n: (n != 'AndroidManifest.xml', n))
    for n in names:
        if any(n.startswith(x) for x in exclude):
            continue
        if n.startswith('META-INF/') and n.split('/')[-1].upper().endswith(('.MF', '.SF', '.RSA', '.DSA', '.EC')):
            continue
        src = repl.pop(n, os.path.join(tree, *n.split('/')))
        comp = zipfile.ZIP_STORED if (n == 'resources.arsc' or n.lower().endswith(STORE)) else zipfile.ZIP_DEFLATED
        z.write(src, n, compress_type=comp)
    for n, src in repl.items():  # new files not present in the tree
        z.write(src, n, compress_type=zipfile.ZIP_DEFLATED)
print('wrote', out)
