# Environment check

Checked on 2026-10-14 on the user's Windows machine. Every row below was run or probed in this session; nothing is assumed.

## Workspace

| Item | Finding |
|---|---|
| Workspace root | `C:\REA` (not a git repository) |
| Original game files | `C:\REA\game_data\` — an extracted APK (1,411 files, 58.3 MB). No packed `.apk` file is present. |
| Extraction integrity | All 1,408 entries listed in `META-INF/MANIFEST.MF` exist and match their signed SHA-1 digests; no extra unsigned files (`scripts/verify_manifest_digests.py`). |
| Host OS | Windows 11 Home 10.0.26300, x64 |
| Shells | PowerShell 5.1 and Git Bash both work; full terminal access confirmed |

## REA

| Check | Result |
|---|---|
| REA MCP server | Connected: `rea-agents` 6.5.0 (`binary_session` answered) |
| REA skill | `reverse-engineer-anything` read from `C:\Users\ANI\.claude\skills` |
| `open_binary` on `libgame.so` | Works: ELF, ARM, SHA-256 `f7946f81…72a1` |
| Native analysis (`binary_overview`, decompile, xrefs) | **Unavailable.** REA reports two independent blockers: (1) Ghidra is not configured (`GHIDRA_INSTALL_DIR` unset, no Ghidra install found; only a cache dir at `%LOCALAPPDATA%\ghidra`), and (2) **REA's Windows Ghidra profile only accepts PE and raw 6502 targets** (`target_format_unsupported` for ARM ELF). Installing Ghidra on Windows would therefore *not* let REA decompile `libgame.so`. |
| Hopper | Not supported on Windows (REA: `unsupported_host`) |
| IDA | Not configured (`REA_IDA_MCP_CONFIG` unset) |
| Android tools in REA | `inspect_android_package` etc. need a JADX MCP JAR (missing); `decode_android_resources` needs apktool (missing); all `adb` tools need adb (missing) |
| User's Ghidra install (found later) | `C:\Users\ANI\OneDrive\Desktop\ghidra_12.1.4_PUBLIC` (the Desktop is OneDrive-redirected). Ghidra 12.1.4, requires Java 21+, includes `analyzeHeadless.bat` and the win_x86_64 + linux_x86_64 decompilers. `rea doctor` with `GHIDRA_INSTALL_DIR` set to it passes every Ghidra check. |
| REA + that Ghidra on `libgame.so` | **Still refused**: `rea inspect` returns `target_format_unsupported` — "Windows Ghidra P0 accepts native PE and explicitly selected raw 6502 targets only." This is a limit of REA 6.5.0 on Windows, not of the Ghidra install. |
| Workaround in use | Ghidra headless (`analyzeHeadless`) run directly on working copies in `analysis/work/`, project in `analysis/ghidra/`. Results from this route are Ghidra results, **not REA results**. |
| Possible route to REA native analysis | WSL 2 is installed (distros `Arch`, `archlinux`, `docker-desktop`, all stopped). REA + Ghidra 12.1 inside Linux should accept ARM ELF. Not attempted: it needs Ghidra and REA installed inside WSL. |

## Toolchain

| Tool | Status |
|---|---|
| Python | 3.12.5 ✅ (no androguard / capstone / lief / unicorn installed) |
| JDK | Temurin 25.0.4 ✅ (`java`, `javac`, `keytool`). Note: current Android Gradle Plugin releases may not run on JDK 25; a JDK 17/21 may be needed for Gradle. |
| Node.js | 22.20.0 ✅ |
| Git | ✅ |
| GNU binutils (MinGW) | `readelf` 2.45 ✅ (reads ARM ELF); `objdump` is x86-only, cannot disassemble ARM |
| Android SDK (platform, build-tools, `aapt2`, `d8`, `zipalign`, `apksigner`) | ❌ not installed (`ANDROID_HOME` unset, no `%LOCALAPPDATA%\Android\Sdk`) |
| Android NDK (clang for arm64, `llvm-readelf`, `llvm-objdump`) | ❌ not installed |
| Gradle | ❌ not installed |
| ADB | ❌ not installed (BlueStacks leftovers in `%LOCALAPPDATA%` but no BlueStacks install or `HD-Adb.exe` found) |
| APKTool | ❌ |
| JADX | ❌ |
| Ghidra | ❌ |
| `file` utility | ❌ (ELF identity checked with `readelf -h` instead) |
| Android emulator / test device | ❌ none detected |

## What works today without installs

Static inventory with `readelf`, my own read-only Python parsers for the binary manifest (`scripts/axml_decode.py`) and `classes.dex` (`scripts/dex_inventory.py`), signature-digest verification, and SHA-256 hashing. This is enough for the inventory and compatibility assessment, but **not** enough to build any APK.

## Minimum setup to build the port (needs the user's approval)

1. **Android SDK command-line tools** from Google, then `sdkmanager "platform-tools" "build-tools;35.0.0" "platforms;android-35" "ndk;27.2.12479018"` into `C:\Android\Sdk` (~3–4 GB). Gives `aapt2`, `d8`, `zipalign`, `apksigner`, `adb`, NDK clang + `llvm-objdump` (ARM disassembly).
2. **Gradle** via a project wrapper (downloads Gradle + AGP from Maven), plus **Temurin JDK 21** if AGP rejects JDK 25.
3. Optional: **JADX** (to decompile `classes.dex` and enable REA's Android tools) and **Ghidra 12.1 + REA inside WSL** (to enable REA native decompilation of ARM ELF).

## Tools actually used

- **REA 6.5.0:** setup and provider checks; artifact identity of the native libraries (`open_binary`); Java decompilation through the REA CLI with the JADX provider (`REA_JADX_MCP_JAR=C:\Users\ANI\tools\rea-deps\jadx-headless-mcp-0.7.1-all.jar`, installed by the paid-content thread), e.g. `Cocos2dxRenderer.onDrawFrame` and `Cocos2dxActivity.showPreloadCicle`.
- **Ghidra 12.1.4 headless, run directly** (REA refuses ARM ELF on Windows): project `analysis/ghidra/`, scripts in `scripts/ghidra/` (function export, selective decompilation, callers, import callers, external data references).
- **Android SDK/NDK** (installed with the user's license approval): build-tools 36.0.0, platform 36, NDK 28.2.13676358, platform-tools (adb).
- **dynarmic** (azahar-emu fork, commit `a46601580d55`) with bundled fmt/mcl/oaknut/robin-map/xbyak/Zydis, and Boost 1.86 headers (SHA-256 `2575e74f…01f`).
- MinGW-w64 GCC 15.1 + CMake + Ninja for the PC harness; Python 3.12 for the scripts; ffmpeg to analyse the device screen recording.
