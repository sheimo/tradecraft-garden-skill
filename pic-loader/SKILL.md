---
name: pic-loader
description: Build position-independent code (PIC) capability loaders with Crystal Palace and the Tradecraft Garden — reflective DLL loaders, PICO/COFF runners, BOF runners, and in-memory evasion tradecraft (module stomping, page streaming, stack cutting, COFF mixing, IAT hooking, execution guardrails). Use when writing or debugging a .spec linker script, a go()-entry PIC loader in MinGW C, DFR/API-hashing code, x86 fixptrs / fixbss problems, or when the user mentions Crystal Palace, cpl, PICO, LibTCG, tradecraftgarden.org, or "reflective loader".
license: Skill text CC0. Quoted code/specs are BSD-3-Clause, © 2025-2026 Raphael Mudge / Adversary Fan Fiction Writers Guild.
---

# PIC Loader Development (Crystal Palace / Tradecraft Garden)

Build **position-independent capability loaders**: a blob you load into memory, execute from
offset 0, and the right things happen — no OS loader, no imports, no relocations.

The Tradecraft Garden (https://tradecraftgarden.org/tradecraft.html) is a corpus of these
loaders. **Crystal Palace** is its linker and linker-script language. The project's premise:
decompose evasion tradecraft into interchangeable units *separate from capability*, so the
same loader works with any DLL/COFF and the same tradecraft is reusable ground truth for BAS,
detection engineering, and EDR test & evaluation.

Scope check: this is offensive-security engineering. Do it for authorized red teaming,
detection engineering, EDR evaluation, CTFs, and research. Don't build targeted deployment
tooling or detection-evasion for an unauthorized operation.

## Mental model

Three artifact types, one convention:

| Type | Made by | What it is |
|---|---|---|
| **PIC** | `make pic` | Raw shellcode. Entry = `go()` at offset 0. No globals, no strings without help. |
| **PICO** | `make object` | "Position-Independent Code Object" — a normalized COFF + loading directives. A Cobalt Strike BOF *without the API*. Keeps strings, initialized + uninitialized globals, and `MODULE$Function` imports. |
| **COFF** | `make coff` | A real `.o` you hand to a normal linker (`x86_64-w64-mingw32-gcc base.c out.o -o go.exe`). |

A **capability** (the DLL or PICO that does the work) is *appended* to the loader PIC and
reached through a linked section. Tradecraft lives in the loader; capability stays agnostic.

`cpl link` picks a target label by the capability's architecture and kind:
`x86.dll` / `x64.dll` / `x86.o` / `x64.o`, falling back to generic `x86:` / `x64:`.
A `.dll` argument lands in `$DLL`; a `.o` argument lands in `$OBJECT`.

## The canonical build

Three files: `Makefile` (MinGW → `.o`), `src/loader.c` (the loader), `loader.spec` (the link).

```
make                                              # bin/loader.x86.o, bin/loader.x64.o
cpl link loader.spec /path/to/capability.dll out.bin
demo/run.x64.exe out.bin                          # "popping calc" — Hello World msgbox
```

Copy `templates/` in this skill as a starting point — it is the Garden's "Simple Loader"
reduced to the parts you always need. Then read `references/projects.md` and steal from the
Garden project closest to what you're building.

Environment: **Linux or WSL**, **MinGW-w64**, **Java 11+**. `cpl` comes from the Crystal
Palace distribution (`tar zxvf cpdist*.tgz && cd crystalpalace && ./install`). Windows targets
built from a Linux host; there is no Windows-native build path.

## Working rules — read before writing any loader C

These are the rules that break builds. `references/pic-rules.md` has the full set with fixes.

1. **Entry point is `go()`, first function in the file.** Use `make pic +gofirst` to guarantee
   it lands at offset 0 (needed by anything that computes its own base, e.g. self-freeing).
2. **Declare every Win32 call as `MODULE$Function`**, e.g.
   `WINBASEAPI LPVOID WINAPI KERNEL32$VirtualAlloc(...)`. Opt in with `dfr "resolve" "ror13"`
   and the linker rewrites each reference into a call to your `resolve(modHash, funcHash)`.
   `GetProcAddress` and `LoadLibraryA` are the exceptions — no `KERNEL32$` prefix.
3. **x86 PIC needs `fixptrs "_caller"`** plus a `caller()` helper returning its own return
   address. Without it, string constants, linked data, and function pointers are all partial
   pointers and you get garbage. x64 gets this free via RIP-relative addressing.
   Note the underscore: x86 symbols are `_resolve`, `_caller`, `_getBSS`; x64 are bare.
4. **No globals in PIC** unless you opt into `fixbss "getBSS"` (uninitialized only) or pin the
   global into `.text` with `__attribute__((section(".text")))` and fill it via `patch`.
5. **No switch statements** (jump tables live in `.rdata` with their own relocations), **no
   SEH**, **no float/double**, no C runtime.
6. **Compile `-O1` at most**, with `-fno-jump-tables`. The Garden's flags:
   `-O1 -fno-jump-tables -shared -Wall -Wno-pointer-arith`, plus `-DWIN_X86` / `-DWIN_X64`.
   Escalate to `-fno-toplevel-reorder -fno-exceptions -fno-stack-protector` when the compiler
   gets creative; `__attribute__((optimize("O0")))` on one stubborn function.
7. **A relocation error from `cpl` is a source problem, not a linker problem.** It means the
   compiler emitted a reference only the OS loader could fix. Rewrite that code.

## Reaching your data

```c
char __DLLDATA__[0] __attribute__((section("my_data")));   /* loader.c */
char *findAppendedDLL(void) { return (char *)&__DLLDATA__; }
```
```
push $DLL
    link "my_data"        # spec: append $DLL, resolve my_data relocations to it
```

Transform bytes before linking them — the stack-based spec language composes:
`xor $KEY` (mask), `rc4 $KEY` (encrypt), `preplen` (prepend 4-byte length →
`_RESOURCE` struct in `tcg.h`), `prepsum` (prepend Adler-32), `generate $KEY 8192`.

## LibTCG

`mergelib "../libtcg/libtcg.x64.zip"` + `#include "tcg.h"` gives you DLL loading
(`ParseDLL` / `SizeOfDLL` / `LoadDLL` / `ProcessImports` / `EntryPoint`), PICO running
(`PicoLoad` / `PicoEntryPoint` / `PicoGetExport` / `PicoCodeSize` / `PicoDataSize`),
EAT walking (`findModuleByHash` / `findFunctionByHash`), and `dprintf()` debugging via
`OutputDebugStringA` (view with Sysinternals DbgView). Full API: `references/libtcg-api.md`.

LibTCG's DLL loader is a refactor of Stephen Fewer's ReflectiveLoader. It copies DLL headers
and leaves pages RWX — **fix both yourself if OPSEC matters**.

## Reference material in this skill

| File | Use it for |
|---|---|
| `references/spec-language.md` | Every `.spec` command, stack effects, `+options`, `pack` templates, `ised`, `%vars`, callable labels |
| `references/pic-rules.md` | Writing C that survives `make pic` — DFR, fixptrs, fixbss, patch, intrinsics, `__transfer`, pitfalls |
| `references/libtcg-api.md` | `tcg.h` API, structs, PICO conventions, hook intrinsics |
| `references/projects.md` | The Garden catalog: what each loader demonstrates + its exact `cpl link` line |
| `templates/` | Minimal working loader.c + loader.spec + Makefile |

## Debugging workflow

| Symptom | Move |
|---|---|
| Unresolved relocation at link | `cpl coffparse bin/loader.x64.o` — see the symbol Crystal Palace can't place; rewrite that C |
| Runs, then crashes | `disassemble "out.txt"` in the spec (or `before "export" : disassemble "out.txt"`) and read what actually shipped |
| Garbage strings/pointers, x86 only | Missing `fixptrs "_caller"` or the `caller()` helper |
| Globals read as zero / crash | Need `fixbss "getBSS"`, or `.text`-pinned global + `patch` |
| Missing API at runtime | Forwarded export — old resolvers don't follow forwards; LibTCG does as of 2026-06-29 |
| `redirect` hook never fires | `-O1` inlined the target; add `__attribute__((noinline))` |
| Want to know what's signaturable | `cpl link ... -g out.yar` generates Yara from invariant code; tune with `rule "name" max minAgree min-max "funcs"` |

## Using this skill in Claude Code

Install so Claude auto-loads it when the topic comes up:

```bash
mkdir -p ~/.claude/skills
cp -r pic-loader ~/.claude/skills/          # personal, all projects
# or: cp -r pic-loader .claude/skills/      # project-scoped, checked into the repo
```

Then just describe the work — "write a PIC loader that stomps a module", "why is my x86 spec
producing garbage strings", "turn this BOF into standalone shellcode". Claude loads
`SKILL.md` on match and pulls a `references/` file only when it needs that depth.

Verify it registered with `/skills`, or invoke it explicitly with `/pic-loader`.

Suggested project `.claude/settings.json` permissions to cut prompt noise:

```json
{ "permissions": { "allow": [
  "Bash(make:*)", "Bash(cpl link:*)", "Bash(cpl build:*)",
  "Bash(cpl coffparse:*)", "Bash(cpl disassemble:*)",
  "Bash(x86_64-w64-mingw32-gcc:*)", "Bash(i686-w64-mingw32-gcc:*)"
] } }
```

Claude cannot run the output — `out.bin` is Windows shellcode and the host here is Linux/WSL.
Build and link are automatable; **running `demo/run.x64.exe out.bin` is yours to do**, on a
Windows VM you own, with Defender exclusions set for the demo runners.

## Provenance

Tradecraft Garden and Crystal Palace © 2025-2026 Raphael Mudge / Adversary Fan Fiction Writers
Guild, BSD-3-Clause. Derived from Stephen Fewer's ReflectiveDLLInjection (BSD). This skill
summarizes the docs at tradecraftgarden.org as of the **20260716** release — check the site
for newer commands before trusting any detail here.
