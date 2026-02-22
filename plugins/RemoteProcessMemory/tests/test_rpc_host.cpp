/*
 * test_rpc_host  --  loads rcx_payload in-process, acts as the "target".
 *
 * Usage:  test_rpc_host
 *
 * Prints a READY line (machine-parseable), then waits for the payload
 * to shut down (RPC_CMD_SHUTDOWN from the client).
 */

#include "../rcx_rpc_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <dlfcn.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <semaphore.h>
#  include <libgen.h>
#  include <limits.h>
#endif

/* ── Helpers ──────────────────────────────────────────────────────── */

static uint32_t current_pid()
{
#ifdef _WIN32
    return (uint32_t)GetCurrentProcessId();
#else
    return (uint32_t)getpid();
#endif
}

static void sleep_ms(int ms)
{
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    usleep((useconds_t)ms * 1000);
#endif
}

/* Resolve payload path relative to this executable */
static int payload_path(char* out, int outLen)
{
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (!slash) slash = strrchr(exePath, '/');
    if (slash) *(slash + 1) = '\0';
    snprintf(out, outLen, "%srcx_payload.dll", exePath);
#else
    char exePath[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n <= 0) return -1;
    exePath[n] = '\0';
    char* dir = dirname(exePath);
    snprintf(out, outLen, "%s/rcx_payload.so", dir);
#endif
    return 0;
}

/* Open the main shared memory (read-only, just to monitor payloadReady) */
static void* open_main_shm(uint32_t pid)
{
    char shmName[128];
    rcx_rpc_shm_name(shmName, sizeof(shmName), pid);

#ifdef _WIN32
    HANDLE h = nullptr;
    for (int i = 0; i < 500; ++i) {
        h = OpenFileMappingA(FILE_MAP_READ, FALSE, shmName);
        if (h) break;
        sleep_ms(10);
    }
    if (!h) return nullptr;
    void* v = MapViewOfFile(h, FILE_MAP_READ, 0, 0, sizeof(RcxRpcHeader));
    return v;
#else
    int fd = -1;
    for (int i = 0; i < 500; ++i) {
        fd = shm_open(shmName, O_RDONLY, 0);
        if (fd >= 0) break;
        sleep_ms(10);
    }
    if (fd < 0) return nullptr;
    void* v = mmap(nullptr, sizeof(RcxRpcHeader), PROT_READ, MAP_SHARED, fd, 0);
    close(fd);
    return (v == MAP_FAILED) ? nullptr : v;
#endif
}

/* ── Test buffer (known pattern for client to verify reads/writes) ── */
static uint8_t g_testBuf[65536];

/* ── main ─────────────────────────────────────────────────────────── */

int main(int, char**)
{
    uint32_t pid = current_pid();

    /* fill test buffer with known pattern */
    for (int i = 0; i < (int)sizeof(g_testBuf); ++i)
        g_testBuf[i] = (uint8_t)(i & 0xFF);

    /* load payload */
    char plPath[1024];
    if (payload_path(plPath, sizeof(plPath)) != 0) {
        fprintf(stderr, "ERROR: cannot determine payload path\n");
        return 1;
    }

#ifdef _WIN32
    HMODULE hPayload = LoadLibraryA(plPath);
    if (!hPayload) {
        fprintf(stderr, "ERROR: LoadLibrary(%s) failed (%lu)\n",
                plPath, GetLastError());
        return 1;
    }

    /* Call RcxPayloadInit() — DllMain is minimal, init must be explicit */
    typedef bool (*RcxPayloadInitFn)();
    auto pfnInit = (RcxPayloadInitFn)GetProcAddress(hPayload, "RcxPayloadInit");
    if (!pfnInit || !pfnInit()) {
        fprintf(stderr, "ERROR: RcxPayloadInit() failed or not found\n");
        FreeLibrary(hPayload);
        return 1;
    }
#else
    void* hPayload = dlopen(plPath, RTLD_NOW);
    if (!hPayload) {
        fprintf(stderr, "ERROR: dlopen(%s): %s\n", plPath, dlerror());
        return 1;
    }
#endif

    /* open main shm and wait for payloadReady */
    void* shmView = open_main_shm(pid);
    if (!shmView) {
        fprintf(stderr, "ERROR: failed to open main shared memory\n");
        return 1;
    }

    RcxRpcHeader* hdr = (RcxRpcHeader*)shmView;
    for (int i = 0; i < 500; ++i) {
        if (hdr->payloadReady) break;
        sleep_ms(10);
    }
    if (!hdr->payloadReady) {
        fprintf(stderr, "ERROR: payload did not become ready\n");
        return 1;
    }

    /* print READY line for the client to parse */
    printf("READY pid=%u testbuf=0x%llx testlen=%u\n",
           pid,
           (unsigned long long)(uintptr_t)g_testBuf,
           (unsigned)sizeof(g_testBuf));
    fflush(stdout);

    /* wait until payload shuts down */
    while (hdr->payloadReady)
        sleep_ms(100);

    printf("Payload shut down, exiting.\n");

#ifdef _WIN32
    /* give the timer queue a moment to drain */
    Sleep(200);
    FreeLibrary(hPayload);
    if (shmView) UnmapViewOfFile(shmView);
#else
    usleep(200000);
    dlclose(hPayload);
    if (shmView) munmap(shmView, sizeof(RcxRpcHeader));
#endif

    return 0;
}
