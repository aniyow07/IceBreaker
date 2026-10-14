"""Static validation of a built APK. Usage: validate_apk.py <apk> <expected-abi>
Checks: zip integrity, manifest (aapt2), native libs per ABI dir and their ELF machine/class
(llvm-readelf), NEEDED libraries, signature (apksigner), original assets byte-identical to
game_data/, and absence of development paths in the native libraries."""
import hashlib, os, re, subprocess, sys, tempfile, zipfile

apk, want_abi = sys.argv[1], sys.argv[2]
SDK = r'C:\Android\Sdk'
BT = os.path.join(SDK, 'build-tools', '36.0.0')
NDKBIN = os.path.join(SDK, 'ndk', '28.2.13676358', 'toolchains', 'llvm', 'prebuilt', 'windows-x86_64', 'bin')
ORIG = 'game_data'
ok = True

def check(cond, msg):
    global ok
    print(('PASS ' if cond else 'FAIL ') + msg)
    ok = ok and cond

def run(*cmd):
    return subprocess.run(cmd, capture_output=True, text=True, errors='replace').stdout

z = zipfile.ZipFile(apk)
check(z.testzip() is None, 'zip archive integrity (all CRCs)')
badging = run(os.path.join(BT, 'aapt2.exe'), 'dump', 'badging', apk)
check("package: name='com.nitrome.icebreaker'" in badging, 'package name com.nitrome.icebreaker')
for line in badging.splitlines():
    if re.match(r"(package|sdkVersion|targetSdkVersion|native-code|launchable-activity|uses-library)", line):
        print('     ' + line)
check(f"native-code: '{want_abi}'" in badging, f'only ABI directory is {want_abi}')

machine = {'arm64-v8a': ('ELF64', 'AArch64'), 'armeabi-v7a': ('ELF32', 'ARM')}[want_abi]
libs = [n for n in z.namelist() if n.startswith('lib/')]
with tempfile.TemporaryDirectory() as td:
    for n in libs:
        check(n.startswith(f'lib/{want_abi}/'), f'{n} is under lib/{want_abi}/')
        p = z.extract(n, td)
        hdr = run(os.path.join(NDKBIN, 'llvm-readelf.exe'), '-h', p)
        cls = re.search(r'Class:\s+(\S+)', hdr).group(1)
        mach = re.search(r'Machine:\s+(.+)', hdr).group(1).strip()
        check((cls, mach) == machine, f'{n}: {cls} {mach}')
        dyn = run(os.path.join(NDKBIN, 'llvm-readelf.exe'), '-d', p)
        needed = re.findall(r'\(NEEDED\)\s+Shared library: \[(.+?)\]', dyn)
        print('     NEEDED: ' + ' '.join(needed))
        data = open(p, 'rb').read()
        dev = [s for s in (b'C:\\REA', b'C:/REA', b'/Users/ANI', b'C:\\Users') if s in data]
        check(not dev, f'{n}: no development paths embedded' + (f' (found {dev})' if dev else ''))

sig = run(os.path.join(BT, 'apksigner.bat'), 'verify', '--verbose', apk)
check('Verifies' in sig, 'apksigner verify')
for line in sig.splitlines():
    if line.startswith('Verified using v'):
        print('     ' + line)

mismatch, missing = [], []
for root, _, files in os.walk(os.path.join(ORIG, 'assets')):
    for f in files:
        rel = os.path.relpath(os.path.join(root, f), ORIG).replace(os.sep, '/')
        try:
            if hashlib.sha256(z.read(rel)).digest() != hashlib.sha256(open(os.path.join(root, f), 'rb').read()).digest():
                mismatch.append(rel)
        except KeyError:
            missing.append(rel)
check(not missing and not mismatch, f'original assets present and byte-identical ({len(missing)} missing, {len(mismatch)} changed)')
for name in ('classes.dex', 'resources.arsc'):
    check(z.read(name) == open(os.path.join(ORIG, name), 'rb').read(), f'{name} byte-identical to original')
print('RESULT: ' + ('ALL CHECKS PASSED' if ok else 'SOME CHECKS FAILED'))
sys.exit(0 if ok else 1)
