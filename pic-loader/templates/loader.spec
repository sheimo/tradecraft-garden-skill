# loader.spec — minimal PIC loader
# Tradecraft Garden / Crystal Palace  (BSD-3-Clause)
#
# Usage:
#   cpl link loader.spec /path/to/capability.dll out.bin
#
# Stack after `make pic`:  OBJECT (the loader COFF)
# Stack after `link`:      OBJECT (loader + appended DLL)
# Stack after `export`:    BYTES  (finished PIC blob)

name        "Simple Loader"
description "Minimal DLL loader — extend from here"
author      "You"
license     "BSD-3-Clause"

x64:
    load "bin/loader.x64.o"
    make pic +gofirst
    mergelib "../libtcg/libtcg.x64.zip"
    options +optimize
    dfr "resolve" "ror13"
    fixptrs "_caller"
    push $DLL
        link "dll_data"
    export

x86:
    load "bin/loader.x86.o"
    make pic +gofirst
    mergelib "../libtcg/libtcg.x86.zip"
    options +optimize
    dfr "resolve" "ror13"
    fixptrs "_caller"
    push $DLL
        link "dll_data"
    export
