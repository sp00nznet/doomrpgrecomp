/*
 * crashdump.c -- last-resort crash reporter (Windows). On an unhandled SEH
 * exception (e.g. access violation) print the exception code, faulting address,
 * and a symbolized stack so a silent native crash in generated game code points
 * at the offending recompiled method. Build with /Zi + link /DEBUG so the .pdb
 * lets dbghelp resolve names. No-op on non-Windows.
 */
#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <stdio.h>

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(proc, NULL, TRUE);

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    fprintf(stderr, "\n*** CRASH: exception 0x%08lX at %p ***\n", (unsigned long)code, addr);
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        fprintf(stderr, "    access violation %s address %p\n",
                ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
                (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    CONTEXT *ctx = ep->ContextRecord;
    STACKFRAME64 sf; memset(&sf, 0, sizeof sf);
    sf.AddrPC.Offset = ctx->Rip;    sf.AddrPC.Mode = AddrModeFlat;
    sf.AddrFrame.Offset = ctx->Rbp; sf.AddrFrame.Mode = AddrModeFlat;
    sf.AddrStack.Offset = ctx->Rsp; sf.AddrStack.Mode = AddrModeFlat;

    char buf[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO *sym = (SYMBOL_INFO *)buf;
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 500;

    for (int i = 0; i < 40; i++) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf,
                         ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
            break;
        if (!sf.AddrPC.Offset) break;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym)) {
            IMAGEHLP_LINE64 line; DWORD ld = 0; line.SizeOfStruct = sizeof line;
            if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ld, &line))
                fprintf(stderr, "  #%2d %s +0x%llx  (%s:%lu)\n", i, sym->Name,
                        (unsigned long long)disp, line.FileName, line.LineNumber);
            else
                fprintf(stderr, "  #%2d %s +0x%llx\n", i, sym->Name, (unsigned long long)disp);
        } else {
            fprintf(stderr, "  #%2d 0x%llx\n", i, (unsigned long long)sf.AddrPC.Offset);
        }
    }
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;   /* terminate */
}

void install_crash_handler(void) { SetUnhandledExceptionFilter(crash_filter); }

#else
void install_crash_handler(void) {}
#endif
