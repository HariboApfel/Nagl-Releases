// int3bench.c: what one breakpoint exception costs a Windows program on an engine (thread "Smooth out AION 2 frame
// stutter", 2026-11-25). AION 2 raises about 15,000 EXCEPTION_BREAKPOINT a second from int3 instructions in its own
// code and continues from a vectored handler (queue run 1200); this measures that round trip, alone and on several
// threads at once, beside a software exception (RaiseException: the same dispatch without a CPU trap) and a system
// call. x86_64, MinGW: x86_64-w64-mingw32-gcc -O1 -static -o int3bench.exe int3bench.c
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static LONG CALLBACK veh(EXCEPTION_POINTERS *e)
{
    DWORD c = e->ExceptionRecord->ExceptionCode;
    if (c == EXCEPTION_BREAKPOINT) { e->ContextRecord->Rip = (DWORD64)e->ExceptionRecord->ExceptionAddress + 1; return EXCEPTION_CONTINUE_EXECUTION; }
    if (c == 0xE0001234) return EXCEPTION_CONTINUE_EXECUTION;
    return EXCEPTION_CONTINUE_SEARCH;
}

static double now_us(void)
{
    static LARGE_INTEGER f; LARGE_INTEGER t;
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t);
    return (double)t.QuadPart * 1e6 / (double)f.QuadPart;
}

static HANDLE go;
static int kind, count;
static volatile LONG sink;

static void work(int k, int n)
{
    int i;
    if (k == 0) for (i = 0; i < n; i++) __asm__ volatile("int3");
    else if (k == 1) for (i = 0; i < n; i++) RaiseException(0xE0001234, 0, 0, NULL);
    else if (k == 2) for (i = 0; i < n; i++) { MEMORY_BASIC_INFORMATION m; VirtualQuery(&m, &m, sizeof m); }
    else for (i = 0; i < n; i++) InterlockedIncrement(&sink);
}

static DWORD WINAPI thread(void *arg) { (void)arg; WaitForSingleObject(go, INFINITE); work(kind, count); return 0; }

static void run(const char *name, int k, int threads, int n)
{
    HANDLE h[64]; int i; double t0, t1;
    kind = k; count = n; go = CreateEventA(NULL, TRUE, FALSE, NULL);
    for (i = 0; i < threads; i++) h[i] = CreateThread(NULL, 0, thread, NULL, 0, NULL);
    Sleep(100);
    t0 = now_us(); SetEvent(go);
    WaitForMultipleObjects(threads, h, TRUE, INFINITE);
    t1 = now_us();
    for (i = 0; i < threads; i++) CloseHandle(h[i]);
    CloseHandle(go);
    printf("%-10s threads=%2d n=%d wall_ms=%.1f us_per_event_on_a_thread=%.2f events_per_s_all=%.0f\n", name, threads, n,
           (t1 - t0) / 1e3, (t1 - t0) / n, (double)threads * n / ((t1 - t0) / 1e6));
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 20000;
    static const int T[] = { 1, 2, 4, 8, 16 };
    unsigned i;
    AddVectoredExceptionHandler(1, veh);
    if (argc > 3) {   // int3bench KIND THREADS N: one case alone, for a profile (KIND int3, raise or syscall)
        int k = !strcmp(argv[1], "raise") ? 1 : !strcmp(argv[1], "syscall") ? 2 : 0;
        run(argv[1], k, atoi(argv[2]), atoi(argv[3]));
        return 0;
    }
    run("warmup", 0, 1, 200);
    for (i = 0; i < sizeof T / sizeof *T; i++) run("int3", 0, T[i], n);
    for (i = 0; i < sizeof T / sizeof *T; i += 2) run("raise", 1, T[i], n);
    for (i = 0; i < sizeof T / sizeof *T; i += 2) run("syscall", 2, T[i], n * 5);
    run("nothing", 3, 1, n * 50);
    return 0;
}
