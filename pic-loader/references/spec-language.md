# Crystal Palace .spec Language Reference

A `.spec` file is Crystal Palace's linker script. There is no default spec — every project
writes one. Commands operate on a **stack** holding two kinds of things:

- **BYTES** — raw content (a loaded file, `$DLL`, an exported blob)
- **OBJECT** — an *export configuration object*: a malleable COFF intermediate plus the
  settings that will drive the final export

`load` pushes BYTES. `make pic|object|coff` pops BYTES and pushes an OBJECT. Most commands
transform the OBJECT in place. `export` pops the OBJECT and pushes finished BYTES.

Comments start with `#`. Whitespace/indentation is cosmetic. All relative file paths resolve
**relative to the .spec file's own location** (that's why `-r` / `resolve` exist).

## Targets and labels

```
x64:            # generic x64
x86:
x64.dll:        # x64 + DLL capability — more specific, wins if defined
x64.o:          # x64 + COFF capability
foo.x64:        # callable label, invoked as .foo or via call "file.spec" "foo"
```

Crystal Palace parses the capability passed to `cpl link` and picks the most specific label.
`$DLL` is set for a `.dll` argument; `$OBJECT` for a `.o`.

Callable labels take positional args, readable inside as `%1`, `%2`, …:

```
.foo "arg 1" %arg2
call "file.spec" "foo" "arg 1"       # path resolved at run time
callnear "file.spec" "foo"           # path canonicalized at parse time (needed by before/after)
run "other.spec"                     # run a whole spec: same target, $VARs, and stack
```

## Command reference

Pop/Push columns show stack effect.

### Loading and exporting

| Command | Pop → Push | Description |
|---|---|---|
| `load "file"` | → BYTES | Push file contents onto the stack |
| `load $VAR "file"` | — | Load file contents into `$VAR` |
| `push $VAR` | → BYTES | Push a byte[] variable |
| `pop $VAR` | BYTES → | Pop stack content into `$VAR` |
| `make pic [+opts]` | BYTES → OBJECT | Configure for position-independent code export |
| `make object [+opts]` | BYTES → OBJECT | Configure for PICO export |
| `make coff [+opts]` | BYTES → OBJECT | Configure for COFF export (linkable `.o`) |
| `export` | OBJECT → BYTES | Generate the program |
| `entry "symb"` | OBJECT → OBJECT | Redefine the entry point symbol (e.g. `go` → `X`) |

### Linking data and code

| Command | Pop → Push | Description |
|---|---|---|
| `link "section"` | BYTES, OBJECT → OBJECT | Append data and resolve `__attribute__((section("section")))` relocations to it |
| `linkfunc "symbol"` | BYTES, OBJECT → OBJECT | Append **code** bytes and link them to a function symbol |
| `linkpost "section" "unwind"` | OBJECT → OBJECT | (PIC only) Link generated `.pdata`/`.xdata` to a section. Implies `+unwind` |
| `merge` | BYTES, OBJECT → OBJECT | Merge COFF content into the object |
| `mergelib "lib.x64.zip"` | OBJECT → OBJECT | Merge every COFF in a shared-library zip |
| `patch "symbol" $VAR` | OBJECT → OBJECT | Patch `$VAR` bytes into a symbol (COFF anywhere; PIC only for `.text`-pinned globals) |
| `remap "old" "new"` | OBJECT → OBJECT | Rename a COFF symbol |
| `strip "a, b, c"` | OBJECT → OBJECT | (COFF only) Remove symbols. Stripped COFFs can't be reprocessed |
| `import "A, B, C"` | OBJECT → OBJECT | Map `IMPORTFUNCS` struct members to COFF symbols. First two are always `LoadLibraryA`, `GetProcAddress` |
| `exportfunc "fn" "__tag_fn"` | OBJECT → OBJECT | (PICO only) Export a function and generate the `__tag_fn()` intrinsic for `PicoGetExport` |

### PIC ergonomics

| Command | Description |
|---|---|
| `dfr "resolver" "method"` | Dynamic Function Resolution. Rewrites `MODULE$Function` refs into calls to `resolver`. Methods: `ror13`, `djb2`, `fnv1a`, `sdbm` (hash pairs), or `strings` (pushes module/function strings, passes pointers) |
| `dfr "res" "method" "M1, M2"` | Per-module resolver; takes priority over the default |
| `dfr ... +clear` | Wipe prior DFR configuration and start over |
| `fixptrs "_caller"` | (x86 PIC only) Rewrite partial pointers into full pointers using your `_caller` helper. Restores strings, linked data, local function pointers |
| `fixbss "getBSS"` | (PIC only) Rewrite `.bss` references to call `getBSS(size)` and index off the result. Restores uninitialized globals |
| `intrinsic "__foo" $CODE` | Define an intrinsic replaced by `$CODE` at link time. Symbol must start with `__` (x64) or `___` (x86) |
| `catch "function" "handler"` | Attach an x64 language-specific handler via unwind data. Requires `+unwind` |

### Instrumentation (Aspect-Oriented weaving)

| Command | Description |
|---|---|
| `attach "MOD$Func" "hook"` | Route all calls/references to a Win32 API through `hook`. Context-aware: `hook` can still call the original. Stackable, first-declared runs first |
| `redirect "function" "hook"` | Same, for a local function |
| `protect "f1, f2"` | Isolate listed functions from *all* attach/redirect hooks (`dprintf` is auto-protected) |
| `preserve "target" "f1, f2"` | Isolate one target (local fn or `MOD$Func`) from hooks inside the listed functions |
| `optout "target" "h1, h2"` | Block specific hooks inside one function — used to keep a tradecraft's setup free of its own hooks |
| `addhook "MOD$Func" ["hook"]` | Register a hook for the `__resolve_hook()` intrinsic (IAT hooking). No hook arg = use the `attach` chain |
| `filterhooks $DLL` \| `$OBJECT` | Walk the capability's import table and drop registered hooks it doesn't need |

### Data transforms

| Command | Pop → Push | Description |
|---|---|---|
| `xor $VAR` | BYTES → BYTES | Mask with key |
| `rc4 $VAR` | BYTES → BYTES | RC4 encrypt with key |
| `preplen` | BYTES → BYTES | Prepend 4-byte native-order length |
| `prepsum` | BYTES → BYTES | Prepend 4-byte Adler-32 checksum |
| `generate $VAR ##` | — | Generate `##` random bytes into `$VAR` |
| `pack $VAR "template" ...` | — | Marshal arguments into a byte array (see Pack below) |

### Control flow, variables, meta

| Command | Description |
|---|---|
| `set "%var" "value"` / `setg "%var" "value"` | Local-scope / global-scope string variable. Quote the variable name — it's a literal |
| `resolve "%var"` | Resolve comma-separated partial paths in `%var` relative to this .spec |
| `foreach %var: cmd %_` | Loop a comma-separated list, `%_` = current item |
| `next "%var": cmd %_` | Pop the first item off `%var` into `%_` and run cmd; no-op if empty |
| `echo ...` | Print to STDOUT / SpecLogger |
| `before "cmd1" ["type"] ["file"] ["target"] : cmd2` | Run cmd2 before cmd1. Empty `"cmd1"` = start of label. `%_` = full command, `%1..%n` = args, `%type` / `%file` / `%target` set |
| `after ...` | Same, after. Empty `"cmd1"` = end of label |
| `name` / `describe` / `author` / `reference` / `license` | Meta-info, placed above the first label |
| `meta "verb" "value"` | Update meta-info from a `@config.spec` |
| `rule "name" [max] [minAgree] [minLen-maxLen] ["f1, f2"]` | Yara generator hints. `max 0` disables signatures for that piece. Defaults: max 10, length 10-16. Scoped to `.text` only |
| `coffparse "file.txt"` / `disassemble "file.txt"` | Dump the object on the stack for inspection at export time. `disassemble +forms` shows iced generic forms |
| `magic "0x11111111, 0x22222222"` | Restrict `+mutate`'s magic-constant pool (makes results targetable by `ised`) |
| `ised verb pattern... $CODE +opts` | Program rewriting — see below |

### String concatenation and quoting

`<>` concatenates: `load %foo <> ".x64.o"`.
`:` gathers everything after it into one argument (that's how `foreach x: cmd` parses).
Per-command quote character: `echo,' 'a "string" with a new quote char'`.

## Binary transformation `+options`

Valid on `make pic` / `make object` / `make coff`, and addable later with `options +x +y`.

| Option | Effect |
|---|---|
| `+optimize` | Link-time optimization — drop functions nothing calls or references. Essential when using `mergelib` |
| `+gofirst` | (PIC) Force `go()` to offset 0 |
| `+disco` | Randomize function order (with `make pic` the first function is left alone) |
| `+blockparty` | Randomize basic blocks within each function |
| `+shatter` | Aggressive `+blockparty` — randomize blocks across the whole program |
| `+mutate` | Break up constants and stack strings (`mov reg, k - magic` / `add reg, magic`) |
| `+regdance` | Shuffle non-volatile registers where safe |
| `+relax` | Global variable reference relaxation; removes `.refptr.X` symbols from `.rdata` |
| `+unwind` | Generate x64 stack unwind data. COFF → real `.pdata`/`.xdata`; PICO → appended `_RESOURCE` (read with `PicoGetUnwindData`); PIC → bind via `linkpost` |

Order doesn't matter. Use `disassemble "out.txt"` to see what a transform actually did.

## `ised` — surgical program rewriting

```
ised replace|insert "pattern1" ["pattern2" ...] $CODE +options
```

Patterns match a sequence of instructions, each written as:
- a specific disassembly string — `sub rsp, 0x20`
- an iced generic form — `SUB r/m64, imm8` (see `cpl disassemble -f`)
- a bare mnemonic — `SUB`

| Option | Effect |
|---|---|
| `+first` / `+last` | Which instruction of the match to act on (default `+last`) |
| `+before` / `+after` | Where to place `$CODE` (default `+after`) |
| `+split` | Insert an artificial block break — pairs with `+blockparty` / `+shatter`. Use `$NULL` for `$CODE` if that's all you want |
| `+safe` | Attest `$CODE` is RFLAGS-aware, overriding the danger-zone guard |

Constraints: `insert` can target any instruction; `replace` cannot touch calls, branches, or
instructions carrying relocations. With `+unwind` on, prologues and epilogues are off-limits.
`ised` runs after `+mutate` and before `+regdance`. Edited instructions are excluded from
generated Yara signatures. Multiple `ised` commands matching the same instruction form a pool
Crystal Palace picks from — a cheap per-build randomizer.

`$CODE` is raw object code and is **not validated**. Preserving semantics is on you.

## `pack` templates

```
pack $VAR "template" arg1 arg2 ...
```
Little-endian. Numbers accept decimal, `0x` hex, `0` octal.

| Tmpl | Arg | Type | Size |
|---|---|---|---|
| `a` | "string" | UTF-8 | var |
| `z` | "string" | UTF-8, NULL-terminated | var |
| `w` | "string" | UTF-16LE | var |
| `Z` | "string" | UTF-16LE, NULL-terminated | var |
| `h` | "hex pairs" | byte string | var |
| `b` / `s` / `i` / `l` | number | byte / short / int / long | 1 / 2 / 4 / 8 |
| `p` | number | pointer | 4 or 8 (arch) |
| `v` | `$VAR` | byte string | var |
| `x` | — | NULL byte | 1 |
| `@4` / `@8` / `@n` | — | align to 4 / 8 / natural | var |
| `#t` | — | process template `t` and prepend its 4-byte length | var |

## CLI

```
cpl link  loader.spec capability.dll out.bin [KEY=hexbytes] [%var="value"] [-r %var="a, b"] [@config.spec] [-g out.yar]
cpl build loader.spec x64 out.bin        # no capability — assemble from what's in the .spec
cpl coffparse file.o                     # how Crystal Palace sees your object
cpl disassemble file.o [-f]              # -f shows iced generic forms
cpl server                               # JSON-over-HTTP sidecar, 127.0.0.1:60060, -p to change
cpl help [command]
```

- `KEY=04030201` sets `$KEY` to bytes `04 03 02 01` — i.e. DWORD `0x01020304`. No `$` prefix
  on the CLI; byte order is your responsibility.
- `%var="value"` sets a template string. `-r %var="a, b"` additionally resolves each path
  against the **current working directory** instead of the .spec's directory.
- `@config.spec` runs a config spec first — the idiomatic place for `setg`, `pack`, `resolve`,
  `meta`, and `before`/`after` hooks that instrument a project you don't want to edit.

Useful `@config.spec` recipes:
```
before "export" : disassemble "code.txt"
before "export" : options +regdance +mutate
```

## Java / sidecar APIs

Java: `LinkSpec.Parse(file)` → `LinkSpec`, `Capability.Parse(byte[])` → `Capability`, then
`spec.run(capability, Map<String,Object>)` mapping `"$KEY"` → `byte[]` and `"%var"` → `String`.
Returns the finished blob. Throws `SpecParseException` / `SpecProgramException`, both with
readable `toString()`.

Sidecar: POST JSON to `127.0.0.1:60060`; response carries logs, base64 output, and Yara rules.
Follows the same concepts as `cpl link` / `cpl build`.
