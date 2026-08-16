# Writing C That Survives `make pic`

Treat MinGW C here as a **high-level assembler**. You give up clever language features and
source-level debugging; you gain byte-level control and weavable output.

## What PIC actually is

The compiler emits a COFF: code, referenced data (`.rdata` strings), and **relocations** —
records saying "something outside lives here, the linker or OS loader will fill it in."
`make pic` extracts `.text` and `.rdata`, concatenates them, and resolves what it can on its
own. Anything it can't resolve is an error, because there is no OS loader at runtime.

- **x64** does well: RIP-relative addressing means references to appended data (`link`),
  `.rdata` strings, and in-`.text` function pointers all resolve statically.
- **x86** has no PC-relative data addressing. Those symbols become *partial pointers* that
  need runtime fixing — which is what `fixptrs` does.

## Entry point

The first function in your C file is the entry point. **Name it `go`** — Crystal Palace's
link-time optimization, function-disco, and error checks all expect that name.

```c
void go(void) { ... }
```

`make pic +gofirst` guarantees `go` is at offset 0. That matters whenever the program needs
its own base address (self-freeing loaders take `&go` as "start of my PIC in memory").

For a PICO, `go` is optional — omit it when merging several PICOs and reaching functionality
through `exportfunc` instead.

## Resolving Win32 APIs — Dynamic Function Resolution

Declare each API with the `MODULE$Function` convention:

```c
WINBASEAPI LPVOID WINAPI KERNEL32$VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
WINBASEAPI BOOL   WINAPI KERNEL32$VirtualFree(LPVOID, SIZE_T, DWORD);
WINBASEAPI HANDLE WINAPI KERNEL32$CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T,
    LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD);
```

Enable DFR in your `.spec` with:

```
dfr "resolve" "ror13"
```

Crystal Palace then rewrites every `MODULE$Function` reference into a call to your `resolve(modHash, funcHash)` function. Alternatives: `djb2`, `fnv1a`, `sdbm` (same call sig), or `strings` (passes `char *module, char *function` pointers instead of hashes).

`GetProcAddress` and `LoadLibraryA` are **exceptions** — declare and call them bare (no `KERNEL32$` prefix). They are the primitives DFR uses internally.

Prototype declarations must exactly match the Win32 headers — `WINAPI` / `NTAPI` calling convention matters. Mismatch means a misaligned stack and a crash that looks random.

## x86 pointer fixing — `fixptrs`

x86 has no RIP-relative data addressing. Every reference to a string constant, a `link`-ed section, or a local function pointer becomes a *partial pointer* — a 32-bit offset that needs a runtime base added to it.

Add to your `.spec`:

```
fixptrs "_caller"
```

And provide this helper in `loader.c`:

```c
void * __attribute__((noinline)) caller(void) {
    void *ret;
    __asm__ volatile ("mov (%%esp), %0" : "=r"(ret));
    return ret;
}
```

Crystal Palace patches every partial pointer to call `_caller`, compute the base, and add the offset. Without this, x86 PIC with strings or linked data produces garbage — the most common x86 bug.

x64 gets this for free (RIP-relative). Never add `fixptrs` to an x64 spec.

## Uninitialized globals — `fixbss`

Globals in `.bss` resolve to addresses that don't exist after the OS loader strips them. For PIC use `fixbss`:

```
fixbss "getBSS"
```

Provide a `getBSS(size_t sz)` function that allocates `sz` bytes in a writable cave (e.g., a `.text` buffer, or `VirtualAlloc`). Crystal Palace rewrites all `.bss` references to call `getBSS` and index off the result.

Alternatively, pin a global into `.text`:

```c
int myGlobal __attribute__((section(".text"))) = 0;
```

Then use `patch "myGlobal" $DATA` in the spec to fill it at link time. This works for any architecture and doesn't require a `getBSS` helper — but only for data whose size is known at link time.

Initialized globals (`.data`) — just don't use them. They carry relocations; Crystal Palace can't resolve them in PIC output.

## What the compiler must not emit

| Forbidden | Why | Workaround |
|---|---|---|
| Switch statements | Jump tables live in `.rdata` with their own relocations | Chains of `if`/`else if`, or hand-roll with function pointers in `.text` |
| SEH (`__try` / `__except`) | Relies on OS structures not present in a raw PIC | Use explicit error paths; LibTCG's `dprintf` avoids SEH contexts |
| Float / double | FPU requires CRT init | Use integer approximations or pass values in from outside |
| C runtime (`strlen`, `memcpy`, etc.) | CRT has its own global state and relocations | Provide your own inline versions or use Win32 (`RtlCopyMemory`) via DFR |
| `__builtin_expect` and similar | May emit instructions that reference `.rdata` | Just don't; the optimizer doesn't need hints in `-O1` code |

## Compiler flags

The Garden uses:
```
-O1 -fno-jump-tables -shared -Wall -Wno-pointer-arith -DWIN_X64   # (or WIN_X86)
```

Escalate when the compiler still misbehaves:
- `-fno-toplevel-reorder` — prevents reordering functions (breaks `+gofirst`)
- `-fno-exceptions` — suppresses C++ EH linkage symbols
- `-fno-stack-protector` — kills the `__stack_chk_fail` extern
- `__attribute__((optimize("O0")))` on a single stubborn function

`-shared` tells MinGW not to insert a `DllMain` stub. You want a bare COFF, not a DLL.

Check your output with `cpl coffparse bin/loader.x64.o` before linking — if you see unexpected relocations or unknown externals, the compiler got creative.

## `patch` — injecting data at link time

```c
typedef struct { void *gmh; void *gpa; } BOOTSTRAP;
BOOTSTRAP __bootstrapData __attribute__((section(".text")));
```

```
load $GMH_ADDR "data/gmh.bin"
load $GPA_ADDR "data/gpa.bin"
pack $BOOTSTRAP "vv" $GMH_ADDR $GPA_ADDR
patch "__bootstrapData" $BOOTSTRAP
```

The symbol must be in `.text` for PIC (`.data` symbols have relocations). For COFF or PICO, `.data` symbols work fine with `patch`.

## `__transfer` — handing off execution

The `__transfer(dst, arg)` intrinsic jumps to `dst` with `arg` in the first argument register, with a clean stack. Use it for the final hand-off to a loaded DLL entry point or PICO entry, so the loaded code doesn't see your loader's frame.

```c
extern void __transfer(void *dst, void *arg);
// ...
__transfer((void *)EntryPoint(&dllData, base), (void *)base);
```

No `__transfer` intrinsic in the `.spec`? Add `intrinsic "__transfer" $CODE` where `$CODE` is the raw bytes for `jmp rax` / `jmp eax` with argument setup — or use LibTCG's copy which Crystal Palace ships.

## Debugging

- `cpl coffparse bin/loader.x64.o` — shows Crystal Palace's view of your COFF: symbols, relocations, sections. First stop for any link error.
- Add `disassemble "out.txt"` in the spec (or `before "export" : disassemble "out.txt"`) to see the final byte sequence.
- `dprintf("value: %d\n", x)` sends output to `OutputDebugStringA`; view with Sysinternals DbgView. Do not call `dprintf` from inside a `fixptrs`, `fixbss`, or `dfr` helper — it depends on SEH internally.
- A crash with no obvious cause on x86 almost always means missing `fixptrs "_caller"` or a call to a forbidden feature (switch, float, CRT).
