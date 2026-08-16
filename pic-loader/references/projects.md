# Tradecraft Garden — Project Catalog

All projects live at https://tradecraftgarden.org/tradecraft.html. Source files are
downloadable from each project page. This catalog gives you the "what it demonstrates" and
the `cpl link` command so you can pick the right starting point.

Environment: Linux/WSL, MinGW-w64, Java 11+, Crystal Palace (`cpl`).
Run output: `demo/run.x64.exe out.bin` (Windows VM only).

---

## Learning Path

### Reflective DLL Injection (`rdlli.html`)
Stephen Fewer's original technique stripped to core components and updated for MinGW/Crystal
Palace conventions. A baseline for understanding what others have built from. DLL headers are
copied; pages left RWX — acknowledged as a starting point, not a finished OPSEC artifact.

```
cpl link loader.spec /path/to/file.dll out.bin
```

---

### Simple Loader (`simple.html`)
The canonical starting point. A clean DLL loader using LibTCG (`ParseDLL`, `LoadDLL`,
`ProcessImports`, `EntryPoint`), DFR for API resolution, and `fixptrs "_caller"` for x86.
This is the template all other loaders diverge from.

```
cpl link loader.spec /path/to/file.dll out.bin
```

---

### Simple Loader — Free (`simplefree.html`)
Extends the Simple Loader with a Crystal Palace PICO that calls `free()` on the loader's
memory before handing execution to the DLL. Uses `make pic +gofirst` so the loader knows its
own base address (`&go` == start of PIC in memory). Demonstrates `PicoLoad` + `PicoEntryPoint`
and the self-freeing pattern.

```
cpl link loader.spec /path/to/file.dll out.bin
```

---

### Simple Loader — Resource Masking (`simplemask.html`)
Adds XOR encryption of the appended DLL and free-PICO using `generate` (random key) and
`xor`. Uses `preplen` so the loader knows how many bytes to unmask. Demonstrates dprintf
debugging (requires DbgView.exe).

```
cpl link loader.spec /path/to/file.dll out.bin
```

---

### Simple Loader — Pointer Patching (`simplepatch.html`)
Bootstraps API resolution by pre-computing `GetModuleHandle` and `GetProcAddress` pointer
values (constant within a reboot cycle) and injecting them at link time via `patch`. Avoids
the complexity of EAT walking — a simpler alternative to DFR for controlled environments.

```
cpl link loader.spec /path/to/file.dll out.bin GMHPTR=<hex> GPAPTR=<hex>
```
Helper demo executables reveal the current pointer values for your environment.

---

### Simple Loader — Alt. API Hashing (`simpleapi.html`)
Swaps ror13 for alternative hash algorithms (djb2, fnv1a, sdbm) to evade detections that
specifically hunt ror13-based API resolution. The core loader.c is unchanged from Simple
Loader; only the `.spec` and a hash-module file differ. Uses `redirect` to swap the resolver.

```
cpl link loader.spec /path/to/file.dll out.bin %apihash="djb2"
```
Valid `%apihash` values: `djb2`, `fnv1a`, `sdbm`.

---

### Simple Loader — Execution Guardrails (`simpleguard.html`)
A loader-agnostic stage that wraps the stage-2 loader output in RC4 encryption + Adler-32
checksum, keyed from environmental factors. The payload is inert if it executes outside the
intended environment. Merges `guardrail.c` and `free.c` PICOs together.

```
cpl link guardrail.spec demo/test.x64.dll out.bin \$ENVKEY=0302010003020100 -r %STAGE2="path/to/loader.spec"
```
`$ENVKEY` is the environment-derived key (8 bytes shown). `-r %STAGE2` resolves the stage-2
spec path relative to the current working directory.

---

### Simple Loader — Hooking (`simplehook.html`)
A modular architecture that separates tradecraft from the loader: the base loader has no
hooks; tradecraft modules (XOR hooks, stack cutting, etc.) are composed in at link time via
`%HOOKS`. Demonstrates the `__resolve_hook` IAT hooking intrinsic (`addhook`, `filterhooks`)
and hook chaining.

```
cpl link loader.spec demo/test.x64.dll out.bin %HOOKS="modules/xorhooks/xorhooks.spec"
```
Swap `%HOOKS` for any module spec. Multiple modules can be composed.

---

### Simple Loader — COFF Capability (`simpleobj.html`)
Applies the Simple Loader to a PICO capability instead of a DLL. When `cpl link` receives a
`.o` argument, the capability is in `$OBJECT`. Demonstrates `+optimize` to drop unused LibTCG
functions and `disassemble "out.txt"` for debugging the final binary.

```
cpl link loader.spec demo/test.x64.o out.bin
```

---

### Simple Loader — COFF or DLL (`simpleobjmix.html`)
One codebase, two capability types. Defines `go_object` and `go_dll` entry points; the spec
`remap`s the appropriate one to `go` depending on capability type. Provides `x86.o`, `x64.o`,
`x86.dll`, `x64.dll` targets in `link.spec`; `loader.spec` delegates via `run link.spec`.

```
cpl link loader.spec /path/to/file.dll out.bin   # DLL path
cpl link loader.spec /path/to/file.o out.bin     # COFF path
```

---

### Simple (Unwinding) Loader (`simpleunwind.html`)
Extends the COFF loader to register stack unwind data (`RtlAddFunctionTable`) so Windows can
properly walk the call stack for exceptions and debuggers. Uses `make object +unwind` +
`linkpost` to generate and append `.pdata`/`.xdata`, then registers it at runtime.

```
cpl link loader.spec demo/test.x64.o out.bin
# Optional: cpl link loader.spec demo/test.x64.o out.bin @verify.spec
```
`@verify.spec` injects debugging breakpoints and stack validation at link time.

---

### Simple PIC (`simplepic.html`)
Converts a PICO capability directly to PIC — no wrapping loader. Merges DFR, `fixptrs`, and
`fixbss` service modules into the PICO itself, making it self-contained shellcode. Demonstrates
`+optimize` to strip unused service functions.

```
cpl link loader.spec demo/test.x64.o out.bin
```

---

### Simple BOF (`simplebof.html`)
Runs a Cobalt Strike Beacon Object File with a PIC loader. Uses `bofprep.spec` to merge a
partial Beacon API implementation directly into the BOF (so the loader doesn't need to
provide it). The `import` command connects the BOF to `BOFIMPORTS` struct members
(`BeaconOutput`, etc.).

```
cpl link loader.spec /path/to/bof.x64.o out.bin
```

---

## Technique Hikes

Advanced tradecraft as composable modules, each with a dedicated page.

### COFF Mixing (`coffmixing.html`)
Embeds a capability into a valid PE by mixing its COFF with benign object files, making the
capability non-contiguous and non-shellcode. Two-phase: first `cpl link mixer.spec` to
transform the capability COFF, then link with a base PE via a normal toolchain.

```
cpl link mixer.spec /path/to/file.o out.x64.o
# Then: x86_64-w64-mingw32-gcc base.c out.x64.o -o go.exe
```
Uses `+disco` (function randomization), `+unwind`, `strip`, and DFR to obscure the capability.

---

### Module Stomping (`modulestomp.html`)
Loads a legitimate system DLL (`xpsservices.dll`), then overwrites its `.text` section with
PICO code and its `.pdata` with regenerated unwind data. The module's path and metadata stay
intact, so analysis tools see a legitimate module. Loaded with `DONT_RESOLVE_DLL_REFERENCES`.

```
cpl link loader.spec demo/test.x64.dll out.bin %HOOKS="modules/stomp/stomp.spec"
```
(Exact command varies; check the project page for the current `cpl link` invocation.)

---

### Page Streaming (`pagestream.html`)
Uses guard pages and a Vectored Exception Handler (VEH) to keep only a small window of the
loaded DLL's memory visible at any time. `MAXVISIBLE` (default 3 regions) controls the window
size. Assumes single-threaded execution. Uses `PAGE_GUARD` to trigger the VEH on access.

```
cpl link loader.spec demo/test.x64.dll out.bin %HOOKS="modules/pagestream/pagestream.spec"
```

---

### Stack Cutting (`stackcutting.html`)
Hides the loader and injected DLL from call stack walks by: placing a call proxy in a code
cave within an existing system module (e.g., kernel32.dll slack space), spoofing return
addresses to point to legitimate frames, and managing x64 shadow space. Uses the Hooking
Loader architecture (`simplehook`) for composition.

```
cpl link loader.spec demo/test.x64.dll out.bin %HOOKS="modules/stackcutting/stackcutting.spec"
```
