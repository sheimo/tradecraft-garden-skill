# tradecraft.garden

A [Claude Code](https://claude.ai/code) skill for building evasive Windows shellcode loaders using [Crystal Palace](https://tradecraftgarden.org) — the linker and tradecraft language from the [Tradecraft Garden](https://tradecraftgarden.org/tradecraft.html).

Drop in your raw PIC shellcode. Describe the evasion you want. Get a Windows executable.

> **Authorized use only.** This tooling is for red team engagements, detection engineering, EDR evaluation, CTF competitions, and security research conducted against systems you own or have written permission to test.

---

## What this is

The `pic-loader` Claude skill teaches Claude Code the Tradecraft Garden conventions — position-independent code, Crystal Palace spec files, LibTCG, DFR, and the full evasion option set — so you can describe a loader in plain English and get working C and spec files back.

The templates in `pic-loader/templates/` are the starting point every build diverges from: a minimal `loader.c`, `loader.spec`, and `Makefile` that compile cleanly and give Claude a concrete base to extend rather than generate from scratch.

---

## Prerequisites

| Tool | Purpose | Install |
|---|---|---|
| `x86_64-w64-mingw32-gcc` | Cross-compile Windows binaries from Linux | `sudo apt install mingw-w64` |
| Java 11+ | Crystal Palace runtime | `sudo apt install default-jdk` (Kali ships Java 21) |
| `cpl` | Crystal Palace linker | See below |

---

## Installing Crystal Palace (`cpl`)

`cpl` is not a package — it ships as a tarball from the Tradecraft Garden.

1. Download `cpdist*.tgz` from **tradecraftgarden.org**
2. Extract and install (no root required — installs to `~/.local/bin`):

```bash
tar zxvf cpdist*.tgz
cd crystalpalace
./install
```

`~/.local/bin` is already on `$PATH` in Kali, so `cpl` is immediately available. Verify:

```bash
cpl help
```

**Optional:** enable tab completion by adding this to `~/.zshrc` or `~/.bashrc`:

```bash
source "/path/to/crystalpalace/cpl-completion.bash"
```

---

## Installing the Claude skill

Copy the `pic-loader/` directory into your Claude skills folder:

```bash
# Personal — available across all projects
cp -r pic-loader ~/.claude/skills/

# Project-scoped — checked into the repo, available to anyone who clones it
cp -r pic-loader .claude/skills/
```

Verify it registered:

```
/skills
```

The skill auto-loads when the topic matches. You can also invoke it explicitly:

```
/pic-loader
```

---

## Quick start

### What to use as `pic.payload.bin`

Any **stageless** x64 shellcode from your C2 works — you don't need to generate anything special. Modern C2 frameworks output position-independent shellcode by default because it has to run at arbitrary memory addresses. Just export the raw shellcode and name it `pic.payload.bin`.

| C2 | How to get stageless shellcode |
|---|---|
| Cobalt Strike | Attacks → Packages → Windows Stageless Payload → output: Raw |
| Havoc | Payload → Shellcode (stageless) |
| Sliver | `generate --format shellcode` |
| Metasploit | `msfvenom -p windows/x64/... -f raw -o pic.payload.bin` |

> **Stager vs. stageless:** A stager is the small ~300-byte payload that downloads the real beacon over the network — it works but requires your listener to be up at run time. A stageless payload is self-contained and is what you want for offline testing or when you want the full capability in one file.

1. Copy your shellcode to the project root as `pic.payload.bin`
2. Open Claude Code in this directory and invoke the skill:

```
/pic-loader build me an evasive loader for pic.payload.bin — I want XOR encryption on the payload, W^X memory, and fiber execution instead of CreateThread
```

3. Claude generates `src/loader.c`, `loader.spec`, and `Makefile`. Build:

```bash
make
```

4. Transfer `runner.exe` to your Windows VM and run it directly:

```bash
.\runner.exe
```

> **Note:** `runner.exe` is a standard PE executable — run it directly. The Crystal Palace demo runner (`run.x64.exe`) is for raw `.bin` shellcode blobs only; pointing it at a PE will crash.

Each `make clean && make` regenerates a fresh random XOR key, so every build produces a structurally unique binary.

---

## Evasion techniques

The default loader applies these layers. Ask Claude to add, remove, or swap any of them.

| Technique | Crystal Palace mechanism | What it defeats |
|---|---|---|
| XOR-encrypted payload | `generate $KEY 32` + `xor $KEY` | Static scanning of shellcode bytes in the binary |
| Randomized key per build | `generate` (non-deterministic) | Hash/signature matching across builds |
| W^X memory (RW → RX, never RWX) | `PAGE_READWRITE` + `VirtualProtect(RX)` | RWX allocation as a high-confidence AV signal |
| Stack-built API strings | Hand-coded char arrays in `loader.c` | Static string scanning for `VirtualAlloc`, `CreateThread`, etc. |
| Dynamic API resolution | `GetProcAddress` at runtime | IAT inspection — suspicious APIs absent from import table |
| Fiber execution | `CreateFiber` / `SwitchToFiber` | `CreateThread` as a shellcode execution IOC |
| Code mutation | `+mutate` | Constant and stack-string signature matching in the loader itself |
| Basic block randomization | `+blockparty` | Block-level code signatures |
| Function order shuffle | `+disco` | Function-layout signatures |
| CPU count check | `GetSystemInfo` | Single-CPU sandbox environments |
| Sleep delay | `Sleep(2000)` | Time-limited dynamic analysis sandboxes |

Additional techniques available in Crystal Palace (ask Claude):

- `rc4 $KEY` — RC4 encryption instead of XOR
- `+shatter` — aggressive block randomization across the whole binary
- `+regdance` — non-volatile register shuffling
- Alternative API hash algorithms (`djb2`, `fnv1a`, `sdbm`) via `dfr`
- Module stomping, page streaming, execution guardrails (environmental keying)
- Yara rule generation for what you just built: `cpl link ... -g out.yar`

---

## Example prompts

**Basic evasive loader:**
```
/pic-loader create a pic payload using pic.payload.bin with XOR encryption, W^X memory, and fiber execution. I want an exe.
```

**Swap to RC4 and add alternative API hashing:**
```
/pic-loader the loader got flagged. switch from XOR to RC4 and use djb2 API hashing instead of ror13
```

**Add environmental keying (guardrails):**
```
/pic-loader add an execution guardrail that keys the payload to the volume serial number of C:\ — payload should be inert on any other machine
```

**Module stomping instead of VirtualAlloc:**
```
/pic-loader replace VirtualAlloc with module stomping — load xpsservices.dll and overwrite its .text section with the shellcode
```

**Understand a detection:**
```
/pic-loader Defender is flagging on allocation behavior. what memory allocation alternatives does Crystal Palace support that avoid VirtualAlloc entirely?
```

**Debug a build error:**
```
/pic-loader cpl is throwing "unresolved relocation" on my x64 loader. here's the coffparse output: [paste]
```

**Generate Yara for what was built:**
```
/pic-loader add a disassemble step to the spec and generate a Yara rule from the invariant code in this loader
```

**Go from raw PIC output to a self-freeing loader:**
```
/pic-loader extend this loader with a Crystal Palace free PICO so the loader erases itself from memory before handing off to the payload
```

---

## Project layout

```
tradecraft.garden/
├── pic-loader/              # Claude Code skill
│   ├── SKILL.md             # Skill entrypoint — loaded by Claude automatically
│   ├── references/
│   │   ├── spec-language.md # Full .spec command reference
│   │   ├── pic-rules.md     # C rules for PIC — what the compiler must not emit
│   │   ├── libtcg-api.md    # LibTCG API (DLL loading, PICO, EAT walking)
│   │   └── projects.md      # Tradecraft Garden project catalog with cpl link commands
│   └── templates/
│       ├── loader.c         # Minimal working loader (Simple Loader baseline)
│       ├── loader.spec      # Minimal spec — extend from here
│       └── Makefile         # MinGW build + cpl targets
└── README.md
```

---

## Building from templates directly

If you want to build the template loader against a test DLL without Claude:

```bash
cp -r pic-loader/templates/* .
make                                          # produces bin/loader.x64.o, bin/loader.x86.o
cpl link loader.spec /path/to/test.dll out.bin
```

The Crystal Palace distribution ships `demo/test.x64.dll` and `demo/run.x64.exe` for exactly this. Run `out.bin` on a Windows VM:

```
run.x64.exe out.bin
```

---

## Recommended Claude Code permissions

Add to `.claude/settings.json` to reduce permission prompts during builds:

```json
{
  "permissions": {
    "allow": [
      "Bash(make:*)",
      "Bash(cpl link:*)",
      "Bash(cpl build:*)",
      "Bash(cpl coffparse:*)",
      "Bash(cpl disassemble:*)",
      "Bash(x86_64-w64-mingw32-gcc:*)",
      "Bash(i686-w64-mingw32-gcc:*)"
    ]
  }
}
```

---

## References

- [Tradecraft Garden](https://tradecraftgarden.org/tradecraft.html) — project catalog and technique write-ups
- [Crystal Palace](https://tradecraftgarden.org) — linker distribution and documentation
- © 2025–2026 Raphael Mudge / Adversary Fan Fiction Writers Guild, BSD-3-Clause
- Derived from Stephen Fewer's [ReflectiveDLLInjection](https://github.com/stephenfewer/ReflectiveDLLInjection) (BSD)
