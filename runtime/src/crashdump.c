/*
 * crashdump.c -- last-resort crash reporter (Windows). On an unhandled SEH
 * exception (access violation, stack overflow, ...) write the exception code,
 * faulting address + RVA, a raw return-address backtrace, and (best-effort) a
 * symbolized stack to crash.txt + stderr.
 *
 * Hardened so it survives the two cases the simple version missed:
 *  - heap corruption (an OOB write): we write via raw Win32 CreateFile/WriteFile
 *    and format with wsprintfA, so nothing here touches the CRT heap; and the
 *    faulting RVA + raw backtrace are written BEFORE the heap-using dbghelp
 *    symbolization (which is wrapped in __try, so a fault there can't lose them).
 *  - stack overflow: SetThreadStackGuarantee reserves room for this handler.
 *
 * Resolve RVAs with the .map (build/<exe>.map) if symbol names are missing.
 * Build with /Zi + link /DEBUG /MAP. No-op on non-Windows.
 */
#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>

static HANDLE g_h = INVALID_HANDLE_VALUE;
static char   g_line[1024];

static void wr(const char *s) {
    DWORD n; int len = lstrlenA(s);
    if (g_h != INVALID_HANDLE_VALUE) WriteFile(g_h, s, (DWORD)len, &n, NULL);
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), s, (DWORD)len, &n, NULL);
}
/* wsprintfA does not use the CRT heap (unlike fprintf), so it's safe here. */
#define WR(...) do { wsprintfA(g_line, __VA_ARGS__); wr(g_line); } while (0)

static void crash_write(EXCEPTION_POINTERS *ep) {
    HMODULE base = GetModuleHandleA(NULL);
    g_h = CreateFileA("crash.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    DWORD code = ep->ExceptionRecord->ExceptionCode;
    void *addr = ep->ExceptionRecord->ExceptionAddress;
    WR("\r\n*** CRASH: exception 0x%08X at %p  (exe base %p, RVA 0x%p) ***\r\n",
       code, addr, base, (void *)((char *)addr - (char *)base));
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        WR("    access violation %s address %p\r\n",
           ep->ExceptionRecord->ExceptionInformation[0] ? "writing" : "reading",
           (void *)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    /* Raw return-address backtrace -- no heap, no symbols. RVAs map via the .map. */
    void *bt[48];
    USHORT got = RtlCaptureStackBackTrace(0, 48, bt, NULL);
    WR("--- raw backtrace (%u frames; RVA = addr - exe base) ---\r\n", got);
    for (USHORT i = 0; i < got; i++)
        WR("  raw #%2u %p  rva 0x%p\r\n", i, bt[i], (void *)((char *)bt[i] - (char *)base));

    /* Best-effort symbolized walk. Uses the heap (dbghelp), so it can fault if
     * the heap is corrupt -- guarded so the data above is never lost. */
    __try {
        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(proc, NULL, TRUE);
        CONTEXT *ctx = ep->ContextRecord;
        STACKFRAME64 sf; memset(&sf, 0, sizeof sf);
        sf.AddrPC.Offset = ctx->Rip;    sf.AddrPC.Mode = AddrModeFlat;
        sf.AddrFrame.Offset = ctx->Rbp; sf.AddrFrame.Mode = AddrModeFlat;
        sf.AddrStack.Offset = ctx->Rsp; sf.AddrStack.Mode = AddrModeFlat;
        char sbuf[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)sbuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 500;
        WR("--- symbolized backtrace ---\r\n");
        for (int i = 0; i < 48; i++) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf,
                             ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL))
                break;
            if (!sf.AddrPC.Offset) break;
            DWORD64 disp = 0;
            if (SymFromAddr(proc, sf.AddrPC.Offset, &disp, sym)) {
                IMAGEHLP_LINE64 line; DWORD ld = 0; line.SizeOfStruct = sizeof line;
                if (SymGetLineFromAddr64(proc, sf.AddrPC.Offset, &ld, &line))
                    WR("  #%2d %s +0x%p  (%s:%lu)\r\n", i, sym->Name,
                       (void *)disp, line.FileName, line.LineNumber);
                else
                    WR("  #%2d %s +0x%p\r\n", i, sym->Name, (void *)disp);
            } else {
                WR("  #%2d 0x%p\r\n", i, (void *)sf.AddrPC.Offset);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wr("  (symbolization faulted -- use the raw RVAs above + the .map)\r\n");
    }

    if (g_h != INVALID_HANDLE_VALUE) { FlushFileBuffers(g_h); CloseHandle(g_h); }
}

static LONG g_handled;   /* one-shot guard (handler can re-fault) */

/* Log a fatal *non-exception* failure (e.g. an uncaught Java exception that ends
 * in abort(), which never reaches the SEH handlers) to crash.txt + stderr, with a
 * symbolized backtrace of the current call stack. */
void crash_log_message(const char *msg) {
    if (InterlockedExchange(&g_handled, 1) != 0) return;
    HMODULE base = GetModuleHandleA(NULL);
    g_h = CreateFileA("crash.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    WR("\r\n*** FATAL: %s ***\r\n", msg ? msg : "");
    void *bt[64];
    USHORT got = RtlCaptureStackBackTrace(0, 64, bt, NULL);
    WR("--- raw backtrace (%u; RVA = addr - exe base %p) ---\r\n", got, base);
    for (USHORT i = 0; i < got; i++)
        WR("  raw #%2u %p  rva 0x%p\r\n", i, bt[i], (void *)((char *)bt[i] - (char *)base));
    __try {
        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
        SymInitialize(proc, NULL, TRUE);
        char sbuf[sizeof(SYMBOL_INFO) + 512];
        SYMBOL_INFO *sym = (SYMBOL_INFO *)sbuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO); sym->MaxNameLen = 500;
        WR("--- symbolized backtrace ---\r\n");
        for (USHORT i = 0; i < got; i++) {
            DWORD64 disp = 0;
            if (SymFromAddr(proc, (DWORD64)(ULONG_PTR)bt[i], &disp, sym)) {
                IMAGEHLP_LINE64 line; DWORD ld = 0; line.SizeOfStruct = sizeof line;
                if (SymGetLineFromAddr64(proc, (DWORD64)(ULONG_PTR)bt[i], &ld, &line))
                    WR("  #%2u %s +0x%p  (%s:%lu)\r\n", i, sym->Name, (void *)disp,
                       line.FileName, line.LineNumber);
                else
                    WR("  #%2u %s +0x%p\r\n", i, sym->Name, (void *)disp);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        wr("  (symbolization faulted)\r\n");
    }
    if (g_h != INVALID_HANDLE_VALUE) { FlushFileBuffers(g_h); CloseHandle(g_h); }
}

static LONG WINAPI crash_filter(EXCEPTION_POINTERS *ep) {
    if (InterlockedExchange(&g_handled, 1) == 0) crash_write(ep);
    return EXCEPTION_EXECUTE_HANDLER;   /* terminate */
}

/* True if `a` lies inside our exe image (vs a system DLL handling its own SEH). */
static int addr_in_exe(void *a) {
    HMODULE m = GetModuleHandleA(NULL);
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)m;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)((char *)m + dos->e_lfanew);
    char *b = (char *)m;
    return (char *)a >= b && (char *)a < b + nt->OptionalHeader.SizeOfImage;
}

/* First-chance vectored handler: some fatal crashes (heap/stack failures) never
 * reach the unhandled filter, but a first-chance AV/stack-overflow does. Only act
 * on a hard fault originating in OUR code (system DLLs handle their own SEH). */
static LONG WINAPI crash_veh(EXCEPTION_POINTERS *ep) {
    DWORD c = ep->ExceptionRecord->ExceptionCode;
    if ((c == EXCEPTION_ACCESS_VIOLATION || c == EXCEPTION_STACK_OVERFLOW ||
         c == EXCEPTION_ILLEGAL_INSTRUCTION || c == EXCEPTION_PRIV_INSTRUCTION ||
         c == EXCEPTION_ARRAY_BOUNDS_EXCEEDED || c == EXCEPTION_INT_DIVIDE_BY_ZERO) &&
        addr_in_exe(ep->ExceptionRecord->ExceptionAddress) &&
        InterlockedExchange(&g_handled, 1) == 0) {
        crash_write(ep);
        TerminateProcess(GetCurrentProcess(), c);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void install_crash_handler(void) {
    ULONG guarantee = 0x10000;
    SetThreadStackGuarantee(&guarantee);          /* room for the handler on overflow */
    AddVectoredExceptionHandler(1, crash_veh);    /* first-chance, before any bypass */
    SetUnhandledExceptionFilter(crash_filter);    /* fallback */
}

#else
void install_crash_handler(void) {}
void crash_log_message(const char *msg) { (void)msg; }
#endif
