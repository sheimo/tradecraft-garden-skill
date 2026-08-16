# LibTCG API Reference

LibTCG is the Tradecraft Garden's shared library: DLL loading, PICO running, EAT walking, and
`dprintf` debugging in one mergeable zip.

```
mergelib "../libtcg/libtcg.x64.zip"   # x64
mergelib "../libtcg/libtcg.x86.zip"   # x86
```

```c
#include "tcg.h"
```

LibTCG itself uses `MODULE$Function` and no globals, so it is PIC/PICO-safe.

## API Resolution

```c
HANDLE findModuleByHash(DWORD moduleHash);
```
Walks the PEB InMemoryOrderModuleList and returns a handle to the first loaded module whose
name hashes to `moduleHash` (ROR13). May crash if no matching module is loaded — check first.

```c
FARPROC findFunctionByHash(HANDLE module, DWORD wantedFunction);
```
Walks the module's Export Address Table and returns the first function whose name hashes to
`wantedFunction` (ROR13). Follows export forwards (as of the 2026-06-29 release).

```c
DWORD ror13hash(const char * c);
```
Computes the ROR13 hash of a string. Use this in a throwaway program to precompute hashes;
store the constants in your loader rather than calling `ror13hash` at runtime.

## DLL Loading

LibTCG's DLL loader is a refactor of Stephen Fewer's ReflectiveLoader. It copies headers and
leaves pages **RWX** — fix permissions and suppress the reflective-loader export yourself if
OPSEC matters.

```c
void ParseDLL(char * src, DLLDATA * data);
```
Reads the PE headers at `src` and populates `data`. Call this before any other DLL function.

```c
DWORD SizeOfDLL(DLLDATA * data);
```
Returns the amount of memory needed to load the DLL (post-load size, not on-disk size).

```c
void LoadDLL(DLLDATA * dll, char * src, char * dst);
```
Copies sections and applies relocations. `dst` must be at least `SizeOfDLL` bytes.

```c
void ProcessImports(IMPORTFUNCS * funcs, DLLDATA * dll, char * dst);
```
Resolves the DLL's import table using `funcs->LoadLibraryA` and `funcs->GetProcAddress`.
The `IMPORTFUNCS` struct is extensible via the `.spec` `import` command (see below).

```c
DLLMAIN_FUNC EntryPoint(DLLDATA * dll, void * dst);
```
Returns a function pointer to the DLL's `DllMain`. Call it after `LoadDLL` and
`ProcessImports`. Typically followed by `__transfer` to hand off with a clean stack.

### DLLDATA struct

```c
typedef struct {
    // opaque — populated by ParseDLL, consumed by LoadDLL / ProcessImports / EntryPoint
    // size and layout are internal; always allocate on the stack or as a .text global
} DLLDATA;
```

### IMPORTFUNCS struct

```c
typedef struct {
    void * LoadLibraryA;      // first two members are always these two
    void * GetProcAddress;
    // additional members injected by the `import` spec command
} IMPORTFUNCS;
```

Extend it via the spec's `import` command to pass extra function pointers into PICO:

```
import "LoadLibraryA, GetProcAddress, BeaconOutput"
```

The loader's `IMPORTFUNCS` struct then has a third member `BeaconOutput`, which `PicoLoad`
fills in and the PICO can call through a cast.

## PICO Execution

A PICO is a "Position-Independent Code Object": a normalized COFF with loading directives
prepended. Code and data live in separate sections; `PicoLoad` places them independently.

```c
void PicoLoad(IMPORTFUNCS * funcs, char * src, char * dstCode, char * dstData);
```
Copies PICO code to `dstCode`, data to `dstData`, applies internal relocations, and resolves
Win32 imports via `funcs`. Both destinations must be pre-allocated to the right size.

```c
int PicoCodeSize(char * src);
int PicoDataSize(char * src);
```
Return the bytes needed for each region. Allocate these before calling `PicoLoad`.

```c
PICOMAIN_FUNC PicoEntryPoint(char * src, char * base);
```
Returns a function pointer to the PICO's `go()` entry point, adjusted for `base` (the code
destination). Call this after `PicoLoad`.

```c
PICOMAIN_FUNC PicoGetExport(char * src, char * base, int tag);
```
Returns a function pointer for an `exportfunc`-tagged export. Tags start at 1 and are
assigned in declaration order in the PICO's `.spec`. Use this for PICOs with multiple
callable entry points.

```c
_RESOURCE * PicoGetUnwindData(char * src, char * base);
```
Returns a pointer to the PICO's stack unwind data (`.pdata`), generated when the PICO was
built with `make object +unwind`. Pass to `RtlAddFunctionTable` to register it with Windows.

### _RESOURCE struct

```c
typedef struct {
    DWORD length;           // byte length of the resource
    char  data[1];          // variable-length payload follows inline
} _RESOURCE;
```

`preplen` in a spec prepends this header; `prepsum` prepends an Adler-32 checksum before
the length. `_RESOURCE` is also used for masked/encrypted blobs:

```c
_RESOURCE * res = (_RESOURCE *)&__MYDATA__;
// res->length is the unmasked size; res->data is the payload
```

## Utility

```c
DWORD adler32sum(unsigned char * buffer, DWORD length);
```
Computes an Adler-32 checksum. Used by `prepsum` at link time; verify at runtime with this.

```c
void dprintf(char * format, ...);
```
Printf-style debug output via `OutputDebugStringA`. View with Sysinternals DbgView.

**Do not call `dprintf` from inside a `fixptrs`, `fixbss`, or `dfr` resolver** — those helpers
run before the stack is fully set up, and `dprintf` has an internal SEH dependency that will
crash in that context.

## IAT Hooking — `__resolve_hook`

When you use `addhook` in a spec, Crystal Palace generates an intrinsic named
`__resolve_hook(char *module, char *function)` (x64) / `___resolve_hook(...)` (x86).

The IAT hooking PICO (see `simplehook`) intercepts `GetProcAddress` and calls
`__resolve_hook(module, function)` for every import. If a hook is registered for that
`module!function` pair, the intrinsic returns the hook function pointer; otherwise it returns
`NULL` and the PICO falls through to the real `GetProcAddress`.

```c
// Pattern inside a hooking PICO:
void * result = __resolve_hook(moduleName, functionName);
if (result) return result;
return KERNEL32$GetProcAddress(handle, functionName);
```

The `filterhooks $DLL` spec command removes registered hooks that the capability's import
table doesn't need, keeping the final binary lean.

## `__transfer` intrinsic

```c
extern void __transfer(void * dst, void * arg);
```

Jumps to `dst` with `arg` in the first argument register and a clean (aligned) stack. Use for
the final hand-off so the loaded DLL/PICO doesn't see the loader's frame on its stack.

Crystal Palace generates `__transfer` from an intrinsic definition in the spec. If not
present, add it explicitly:

```
# x64: set rdi/rcx (arg), jmp rax (dst)
intrinsic "__transfer" $XFER_CODE
```

## Typical usage pattern

```c
void go(void) {
    // 1. Get bootstrap pointers (via DFR or patch)
    void *gmh = findModuleByHash(0x...);
    void *gpa = findFunctionByHash(gmh, 0x...);

    // 2. Find appended DLL
    char *dll = (char *)&__DLLDATA__;

    // 3. Parse and load
    DLLDATA dllInfo;
    ParseDLL(dll, &dllInfo);

    DWORD sz = SizeOfDLL(&dllInfo);
    char *base = (char *)KERNEL32$VirtualAlloc(NULL, sz, MEM_COMMIT|MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    IMPORTFUNCS funcs = { LoadLibraryA, GetProcAddress };
    LoadDLL(&dllInfo, dll, base);
    ProcessImports(&funcs, &dllInfo, base);

    // 4. Hand off
    __transfer((void *)EntryPoint(&dllInfo, base), base);
}
```
