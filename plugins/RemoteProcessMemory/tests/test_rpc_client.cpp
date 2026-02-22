/*
 * test_rpc_client  --  connects to a running test_rpc_host (or spawns one),
 *                      exercises every RPC command, and benchmarks throughput.
 *
 * Usage:
 *   test_rpc_client                          (auto-spawn host)
 *   test_rpc_client <pid> <nonce> [testbuf_hex testlen]
 */

#include "../rcx_rpc_protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <chrono>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#  include <semaphore.h>
#  include <libgen.h>
#  include <limits.h>
#endif

/* ══════════════════════════════════════════════════════════════════════
 *  Minimal standalone IPC client (no Qt, mirrors plugin's IpcClient)
 * ══════════════════════════════════════════════════════════════════════ */

struct TestIpcClient {
#ifdef _WIN32
    HANDLE hShm      = nullptr;
    HANDLE hReqEvent  = nullptr;
    HANDLE hRspEvent  = nullptr;
#else
    int    shmFd      = -1;
    sem_t* reqSem     = SEM_FAILED;
    sem_t* rspSem     = SEM_FAILED;
#endif
    void*  view       = nullptr;
    bool   ok         = false;

    bool connect(uint32_t pid, const char* nonce, int timeoutMs = 5000)
    {
        char shmName[128], reqName[128], rspName[128];
        rcx_rpc_shm_name(shmName, sizeof(shmName), pid, nonce);
        rcx_rpc_req_name(reqName, sizeof(reqName), pid, nonce);
        rcx_rpc_rsp_name(rspName, sizeof(rspName), pid, nonce);

#ifdef _WIN32
        ULONGLONG deadline = GetTickCount64() + (ULONGLONG)timeoutMs;
        while (!(hShm = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmName))) {
            if (GetTickCount64() >= deadline) return false;
            Sleep(10);
        }
        view = MapViewOfFile(hShm, FILE_MAP_ALL_ACCESS, 0, 0, RCX_RPC_SHM_SIZE);
        if (!view) { CloseHandle(hShm); hShm = nullptr; return false; }

        hReqEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, reqName);
        hRspEvent = OpenEventA(EVENT_ALL_ACCESS, FALSE, rspName);
        if (!hReqEvent || !hRspEvent) return false;
#else
        auto start = std::chrono::steady_clock::now();
        while (true) {
            shmFd = shm_open(shmName, O_RDWR, 0);
            if (shmFd >= 0) break;
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs) return false;
            usleep(10000);
        }
        view = mmap(nullptr, RCX_RPC_SHM_SIZE, PROT_READ | PROT_WRITE,
                     MAP_SHARED, shmFd, 0);
        if (view == MAP_FAILED) { view = nullptr; close(shmFd); shmFd = -1; return false; }

        reqSem = sem_open(reqName, 0);
        rspSem = sem_open(rspName, 0);
        if (reqSem == SEM_FAILED || rspSem == SEM_FAILED) return false;
#endif
        /* wait for payloadReady */
        auto* hdr = (RcxRpcHeader*)view;
#ifdef _WIN32
        while (!hdr->payloadReady) {
            if (GetTickCount64() >= deadline) return false;
            Sleep(5);
        }
#else
        while (!__atomic_load_n(&hdr->payloadReady, __ATOMIC_ACQUIRE)) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            if (elapsed >= timeoutMs) return false;
            usleep(5000);
        }
#endif
        ok = true;
        return true;
    }

    void disconnect()
    {
#ifdef _WIN32
        if (view)      { UnmapViewOfFile(view); view = nullptr; }
        if (hShm)      { CloseHandle(hShm);      hShm = nullptr; }
        if (hReqEvent) { CloseHandle(hReqEvent);  hReqEvent = nullptr; }
        if (hRspEvent) { CloseHandle(hRspEvent);  hRspEvent = nullptr; }
#else
        if (view) { munmap(view, RCX_RPC_SHM_SIZE); view = nullptr; }
        if (shmFd >= 0) { close(shmFd); shmFd = -1; }
        if (reqSem != SEM_FAILED) { sem_close(reqSem); reqSem = SEM_FAILED; }
        if (rspSem != SEM_FAILED) { sem_close(rspSem); rspSem = SEM_FAILED; }
#endif
        ok = false;
    }

    bool signalAndWait(int timeoutMs = 2000)
    {
#ifdef _WIN32
        SetEvent(hReqEvent);
        return WaitForSingleObject(hRspEvent, (DWORD)timeoutMs) == WAIT_OBJECT_0;
#else
        sem_post(reqSem);
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec  += timeoutMs / 1000;
        ts.tv_nsec += (timeoutMs % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
        return sem_timedwait(rspSem, &ts) == 0;
#endif
    }

    /* ── RPC helpers ──────────────────────────────────────────────── */

    bool rpc_ping()
    {
        auto* hdr = (RcxRpcHeader*)view;
        hdr->command = RPC_CMD_PING;
        hdr->status  = RCX_RPC_STATUS_OK;
        return signalAndWait();
    }

    bool rpc_read(uint64_t addr, void* buf, uint32_t len)
    {
        auto* hdr  = (RcxRpcHeader*)view;
        auto* data = (uint8_t*)view + RCX_RPC_DATA_OFFSET;

        hdr->command      = RPC_CMD_READ_BATCH;
        hdr->requestCount = 1;
        hdr->status       = RCX_RPC_STATUS_OK;

        auto* entry       = (RcxRpcReadEntry*)data;
        entry->address    = addr;
        entry->length     = len;
        entry->dataOffset = sizeof(RcxRpcReadEntry);

        if (!signalAndWait()) return false;
        memcpy(buf, data + entry->dataOffset, len);
        return true;
    }

    bool rpc_read_batch(const uint64_t* addrs, const uint32_t* lens,
                        uint32_t count, uint8_t* outBuf)
    {
        auto* hdr  = (RcxRpcHeader*)view;
        auto* data = (uint8_t*)view + RCX_RPC_DATA_OFFSET;

        hdr->command      = RPC_CMD_READ_BATCH;
        hdr->requestCount = count;
        hdr->status       = RCX_RPC_STATUS_OK;

        /* lay out entries, then data offsets after all entries */
        uint32_t entriesSize = count * (uint32_t)sizeof(RcxRpcReadEntry);
        uint32_t dataOff = entriesSize;

        for (uint32_t i = 0; i < count; ++i) {
            auto* e = (RcxRpcReadEntry*)(data + i * sizeof(RcxRpcReadEntry));
            e->address    = addrs[i];
            e->length     = lens[i];
            e->dataOffset = dataOff;
            dataOff += lens[i];
        }

        if (!signalAndWait()) return false;

        /* copy out response data */
        uint32_t off = 0;
        for (uint32_t i = 0; i < count; ++i) {
            auto* e = (RcxRpcReadEntry*)(data + i * sizeof(RcxRpcReadEntry));
            memcpy(outBuf + off, data + e->dataOffset, e->length);
            off += e->length;
        }
        return true;
    }

    bool rpc_write(uint64_t addr, const void* buf, uint32_t len)
    {
        auto* hdr  = (RcxRpcHeader*)view;
        auto* data = (uint8_t*)view + RCX_RPC_DATA_OFFSET;

        hdr->command      = RPC_CMD_WRITE;
        hdr->writeAddress = addr;
        hdr->writeLength  = len;
        hdr->status       = RCX_RPC_STATUS_OK;
        memcpy(data, buf, len);

        if (!signalAndWait()) return false;
        return hdr->status == RCX_RPC_STATUS_OK;
    }

    struct ModInfo { uint64_t base; uint64_t size; char name[256]; };

    int rpc_enum_modules(ModInfo* out, int maxOut)
    {
        auto* hdr  = (RcxRpcHeader*)view;
        auto* data = (uint8_t*)view + RCX_RPC_DATA_OFFSET;

        hdr->command = RPC_CMD_ENUM_MODULES;
        hdr->status  = RCX_RPC_STATUS_OK;

        if (!signalAndWait()) return -1;
        if (hdr->status != RCX_RPC_STATUS_OK) return -1;

        int count = (int)hdr->responseCount;
        if (count > maxOut) count = maxOut;

        for (int i = 0; i < count; ++i) {
            auto* entry = (RcxRpcModuleEntry*)(data + i * sizeof(RcxRpcModuleEntry));
            out[i].base = entry->base;
            out[i].size = entry->size;
#ifdef _WIN32
            /* names are UTF-16 on Windows */
            int wchars = (int)(entry->nameLength / sizeof(wchar_t));
            WideCharToMultiByte(CP_UTF8, 0,
                (const wchar_t*)(data + entry->nameOffset), wchars,
                out[i].name, 255, nullptr, nullptr);
            out[i].name[255] = '\0';
#else
            int nLen = (int)entry->nameLength;
            if (nLen > 255) nLen = 255;
            memcpy(out[i].name, data + entry->nameOffset, nLen);
            out[i].name[nLen] = '\0';
#endif
        }
        return count;
    }

    void rpc_shutdown()
    {
        auto* hdr = (RcxRpcHeader*)view;
        hdr->command = RPC_CMD_SHUTDOWN;
        hdr->status  = RCX_RPC_STATUS_OK;
        signalAndWait(500);
    }
};

/* ══════════════════════════════════════════════════════════════════════
 *  Auto-spawn host
 * ══════════════════════════════════════════════════════════════════════ */

#ifdef _WIN32
static HANDLE g_hostProcess = nullptr;
#else
static pid_t  g_hostPid = 0;
#endif
static FILE*  g_hostPipe = nullptr;

static bool spawn_host(uint32_t* outPid, char* outNonce,
                        uint64_t* outTestBuf, uint32_t* outTestLen)
{
    /* resolve path to test_rpc_host next to ourselves */
    char cmd[2048];
#ifdef _WIN32
    char exePath[MAX_PATH];
    GetModuleFileNameA(nullptr, exePath, MAX_PATH);
    char* slash = strrchr(exePath, '\\');
    if (!slash) slash = strrchr(exePath, '/');
    if (slash) *(slash + 1) = '\0';
    snprintf(cmd, sizeof(cmd), "\"%stest_rpc_host.exe\" autotest", exePath);
    g_hostPipe = _popen(cmd, "r");
#else
    char exePath[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (n <= 0) return false;
    exePath[n] = '\0';
    char* dir = dirname(exePath);
    snprintf(cmd, sizeof(cmd), "%s/test_rpc_host autotest", dir);
    g_hostPipe = popen(cmd, "r");
#endif
    if (!g_hostPipe) {
        fprintf(stderr, "ERROR: cannot spawn host: %s\n", cmd);
        return false;
    }

    /* read READY line */
    char line[512];
    if (!fgets(line, sizeof(line), g_hostPipe)) {
        fprintf(stderr, "ERROR: no output from host\n");
        return false;
    }

    /* parse: READY pid=X nonce=Y testbuf=0xZ testlen=N */
    unsigned long long tbuf = 0;
    unsigned tlen = 0;
    if (sscanf(line, "READY pid=%u nonce=%63s testbuf=0x%llx testlen=%u",
               outPid, outNonce, &tbuf, &tlen) < 2) {
        fprintf(stderr, "ERROR: cannot parse host output: %s\n", line);
        return false;
    }
    *outTestBuf = (uint64_t)tbuf;
    *outTestLen = (uint32_t)tlen;
    return true;
}

static void cleanup_host()
{
    if (g_hostPipe) {
#ifdef _WIN32
        _pclose(g_hostPipe);
#else
        pclose(g_hostPipe);
#endif
        g_hostPipe = nullptr;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 *  Printing helpers
 * ══════════════════════════════════════════════════════════════════════ */

static void print_pass(const char* name) { printf("  [PASS] %s\n", name); }
static void print_fail(const char* name) { printf("  [FAIL] %s\n", name); exit(1); }

/* ══════════════════════════════════════════════════════════════════════
 *  main
 * ══════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv)
{
    uint32_t pid = 0;
    char nonce[64] = {};
    uint64_t testBuf = 0;
    uint32_t testLen = 0;
    bool autoMode = false;

    if (argc >= 3) {
        pid = (uint32_t)atoi(argv[1]);
        strncpy(nonce, argv[2], 63);
        if (argc >= 5) {
            testBuf = (uint64_t)strtoull(argv[3], nullptr, 0);
            testLen = (uint32_t)atoi(argv[4]);
        }
    } else {
        autoMode = true;
        printf("Auto-spawning test_rpc_host...\n");
        if (!spawn_host(&pid, nonce, &testBuf, &testLen)) return 1;
    }

    printf("Connecting to PID=%u  nonce=%s  testbuf=0x%llx  testlen=%u\n\n",
           pid, nonce, (unsigned long long)testBuf, testLen);

    /* ── connect ── */
    TestIpcClient ipc;
    if (!ipc.connect(pid, nonce)) {
        fprintf(stderr, "ERROR: IPC connect failed\n");
        if (autoMode) cleanup_host();
        return 1;
    }
    printf("=== Functional Tests ===\n");

    /* ── test: ping ── */
    if (ipc.rpc_ping()) print_pass("Ping");
    else                print_fail("Ping");

    /* ── test: enumerate modules ── */
    TestIpcClient::ModInfo mods[512];
    int modCount = ipc.rpc_enum_modules(mods, 512);
    if (modCount > 0) {
        printf("  [PASS] EnumModules (%d modules)\n", modCount);
        printf("         first: %s  base=0x%llx  size=0x%llx\n",
               mods[0].name,
               (unsigned long long)mods[0].base,
               (unsigned long long)mods[0].size);
    } else {
        print_fail("EnumModules");
    }

    /* ── test: read module header (MZ / ELF magic) ── */
    if (modCount > 0) {
        uint8_t header[4] = {};
        if (ipc.rpc_read(mods[0].base, header, 4)) {
#ifdef _WIN32
            if (header[0] == 'M' && header[1] == 'Z')
                print_pass("ReadModuleHeader (MZ)");
            else
                print_fail("ReadModuleHeader (expected MZ)");
#else
            if (header[0] == 0x7F && header[1] == 'E' &&
                header[2] == 'L'  && header[3] == 'F')
                print_pass("ReadModuleHeader (ELF)");
            else
                print_fail("ReadModuleHeader (expected ELF)");
#endif
        } else {
            print_fail("ReadModuleHeader (read failed)");
        }
    }

    /* ── test: read test buffer (known pattern) ── */
    if (testBuf && testLen >= 4096) {
        uint8_t buf[4096];
        if (ipc.rpc_read(testBuf, buf, 4096)) {
            bool good = true;
            for (int i = 0; i < 4096; ++i) {
                if (buf[i] != (uint8_t)(i & 0xFF)) { good = false; break; }
            }
            if (good) print_pass("ReadTestBuffer (4096 bytes, pattern verified)");
            else      print_fail("ReadTestBuffer (pattern mismatch)");
        } else {
            print_fail("ReadTestBuffer (read failed)");
        }
    }

    /* ── test: write ── */
    if (testBuf && testLen >= 16) {
        uint8_t patch[4] = {0xDE, 0xAD, 0xBE, 0xEF};
        if (ipc.rpc_write(testBuf, patch, 4)) {
            uint8_t verify[4] = {};
            ipc.rpc_read(testBuf, verify, 4);
            if (memcmp(verify, patch, 4) == 0)
                print_pass("Write + ReadBack (0xDEADBEEF)");
            else
                print_fail("Write + ReadBack (readback mismatch)");
        } else {
            print_fail("Write (write failed)");
        }
    }

    /* ── test: batch read ── */
    if (testBuf && testLen >= 8192) {
        const uint32_t N = 4;
        uint64_t addrs[N];
        uint32_t lens[N];
        for (uint32_t i = 0; i < N; ++i) {
            addrs[i] = testBuf + i * 1024;
            lens[i]  = 1024;
        }
        uint8_t out[4096];
        if (ipc.rpc_read_batch(addrs, lens, N, out)) {
            print_pass("BatchRead (4 x 1024 bytes)");
        } else {
            print_fail("BatchRead");
        }
    }

    printf("\n=== Benchmarks ===\n");

    /* choose a valid address for benchmarking */
    uint64_t benchAddr = testBuf ? testBuf : (modCount > 0 ? mods[0].base : 0);
    if (!benchAddr) {
        printf("  (no valid address for benchmarks, skipping)\n");
    } else {

        /* ── benchmark: single 4 KB reads ── */
        {
            const int ITERS = 10000;
            const int PAGE  = 4096;
            uint8_t tmp[4096];

            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERS; ++i)
                ipc.rpc_read(benchAddr, tmp, PAGE);
            auto t1 = std::chrono::high_resolution_clock::now();

            double us = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            double secs = us / 1e6;
            double totalMB = (double)ITERS * PAGE / (1024.0 * 1024.0);

            printf("  Single 4 KB reads:\n");
            printf("    Iterations : %d\n", ITERS);
            printf("    Total data : %.2f MB\n", totalMB);
            printf("    Wall time  : %.3f s\n", secs);
            printf("    Throughput : %.2f MB/s\n", totalMB / secs);
            printf("    Avg latency: %.2f us/read\n", us / ITERS);
        }

        /* ── benchmark: single 64 B reads (pointer-chase-size) ── */
        {
            const int ITERS = 50000;
            const int SZ    = 64;
            uint8_t tmp[64];

            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERS; ++i)
                ipc.rpc_read(benchAddr, tmp, SZ);
            auto t1 = std::chrono::high_resolution_clock::now();

            double us = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            double secs = us / 1e6;
            double totalKB = (double)ITERS * SZ / 1024.0;

            printf("  Single 64 B reads (pointer-chase):\n");
            printf("    Iterations : %d\n", ITERS);
            printf("    Total data : %.2f KB\n", totalKB);
            printf("    Wall time  : %.3f s\n", secs);
            printf("    Throughput : %.2f KB/s\n", totalKB / secs);
            printf("    Avg latency: %.2f us/read\n", us / ITERS);
        }

        /* ── benchmark: batch read (50 x 4 KB, simulating refresh) ── */
        {
            const int ITERS = 2000;
            const uint32_t BATCH = 50;
            const uint32_t PAGE  = 4096;

            uint64_t addrs[BATCH];
            uint32_t lens[BATCH];
            for (uint32_t i = 0; i < BATCH; ++i) {
                /* wrap within test buffer or module */
                addrs[i] = benchAddr + (i * PAGE) % 65536;
                lens[i]  = PAGE;
            }

            /* allocate response buffer */
            uint8_t* outBuf = (uint8_t*)malloc(BATCH * PAGE);
            if (!outBuf) {
                printf("  (batch malloc failed, skipping)\n");
            } else {
                auto t0 = std::chrono::high_resolution_clock::now();
                for (int i = 0; i < ITERS; ++i)
                    ipc.rpc_read_batch(addrs, lens, BATCH, outBuf);
                auto t1 = std::chrono::high_resolution_clock::now();

                double us = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
                double secs = us / 1e6;
                double totalMB = (double)ITERS * BATCH * PAGE / (1024.0 * 1024.0);

                printf("  Batch read (%u x %u B, simulating refresh):\n", BATCH, PAGE);
                printf("    Iterations : %d\n", ITERS);
                printf("    Total data : %.2f MB\n", totalMB);
                printf("    Wall time  : %.3f s\n", secs);
                printf("    Throughput : %.2f MB/s\n", totalMB / secs);
                printf("    Avg latency: %.2f us/batch\n", us / ITERS);
                printf("    Per-page   : %.2f us/page\n", us / (ITERS * BATCH));

                free(outBuf);
            }
        }

        /* ── benchmark: write 4 KB ── */
        if (testBuf && testLen >= 4096) {
            const int ITERS = 10000;
            const int PAGE  = 4096;
            uint8_t tmp[4096];
            memset(tmp, 0x42, sizeof(tmp));

            auto t0 = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < ITERS; ++i)
                ipc.rpc_write(testBuf, tmp, PAGE);
            auto t1 = std::chrono::high_resolution_clock::now();

            double us = (double)std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
            double secs = us / 1e6;
            double totalMB = (double)ITERS * PAGE / (1024.0 * 1024.0);

            printf("  Write 4 KB:\n");
            printf("    Iterations : %d\n", ITERS);
            printf("    Total data : %.2f MB\n", totalMB);
            printf("    Wall time  : %.3f s\n", secs);
            printf("    Throughput : %.2f MB/s\n", totalMB / secs);
            printf("    Avg latency: %.2f us/write\n", us / ITERS);
        }
    }

    /* ── shutdown ── */
    printf("\nSending shutdown...\n");
    ipc.rpc_shutdown();
    ipc.disconnect();

    if (autoMode) {
        /* wait for host to exit */
#ifdef _WIN32
        Sleep(500);
#else
        usleep(500000);
#endif
        cleanup_host();
    }

    printf("Done.\n");
    return 0;
}
