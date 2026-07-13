#ifdef __linux__
#include "linux_compat.h"
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <execinfo.h>
#include <stdarg.h>
#include <atomic>
#include <string.h>
#include <stdlib.h>

// Forward decls from cstrike/core
extern "C" bool Setup_Linux();
extern "C" void Destroy_Linux();

static FILE* g_DebugLogFile = nullptr;
static bool g_IsCs2Process = false;
static std::atomic<bool> g_SetupSucceeded{false};

static void ResetDebugLogs()
{
    static const char* paths[] = {
        "/tmp/cs2_inject_debug.log",
        "/tmp/cs2_init_debug.log",
        "/tmp/cs2_hook_debug.log",
        "/tmp/cs2_esp_debug.log",
        "/tmp/cs2_chams_debug.log",
        "/tmp/cs2_vulkan_debug.log",
        "/tmp/cs2_legit_debug.log",
        "/tmp/cs2_modules.txt",
    };
    if (g_DebugLogFile != nullptr)
    {
        fclose(g_DebugLogFile);
        g_DebugLogFile = nullptr;
    }
    for (const char* path : paths)
        unlink(path);
}

static bool IsCs2Process()
{
    char executable[4096] = {};
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0)
        return false;
    executable[length] = '\0';
    const char* name = strrchr(executable, '/');
    name = name ? name + 1 : executable;
    return strcmp(name, "cs2") == 0;
}

static void DebugLog(const char* fmt, ...)
{
    if (!g_DebugLogFile)
        g_DebugLogFile = fopen("/tmp/cs2_inject_debug.log", "a");
    if (!g_DebugLogFile)
        return;

    va_list args;
    va_start(args, fmt);
    vfprintf(g_DebugLogFile, fmt, args);
    va_end(args);
    fflush(g_DebugLogFile);
}

static void DebugBacktrace(int sig)
{
    void* buf[32];
    const int nptrs = backtrace(buf, sizeof(buf) / sizeof(buf[0]));
    char** strings = backtrace_symbols(buf, nptrs);

    DebugLog("[cs2_inject] signal %d backtrace:\n", sig);
    for (int i = 0; i < nptrs; ++i)
        DebugLog("  #%02d %s\n", i, strings ? strings[i] : "<unknown>");

    free(strings);
}

static void SignalHandler(int sig, siginfo_t* info, void* ucontext)
{
    (void)ucontext;
    DebugLog("[cs2_inject] caught signal %d at address %p\n", sig, info ? info->si_addr : nullptr);
    DebugBacktrace(sig);
    if (g_DebugLogFile)
        fclose(g_DebugLogFile);
    signal(sig, SIG_DFL);
    raise(sig);
}

static void InstallSignalHandlers()
{
    struct sigaction sa;
    sa.sa_sigaction = SignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    int result = 0;
    result = sigaction(SIGSEGV, &sa, nullptr);
    DebugLog("[cs2_inject] sigaction(SIGSEGV)=%d\n", result);
    result = sigaction(SIGABRT, &sa, nullptr);
    DebugLog("[cs2_inject] sigaction(SIGABRT)=%d\n", result);
    result = sigaction(SIGFPE, &sa, nullptr);
    DebugLog("[cs2_inject] sigaction(SIGFPE)=%d\n", result);
    result = sigaction(SIGILL, &sa, nullptr);
    DebugLog("[cs2_inject] sigaction(SIGILL)=%d\n", result);
    result = sigaction(SIGBUS, &sa, nullptr);
    DebugLog("[cs2_inject] sigaction(SIGBUS)=%d\n", result);
}

static void* cheat_thread(void*) {
    DebugLog("[cs2_inject] cheat_thread started, pid=%d\n", getpid());
    DebugLog("[cs2_inject] cheat_thread sleeping before Setup_Linux\n");
    // wait for CS2 to fully load its modules
    usleep(5000000);  // 5 seconds to ensure CS2 is fully loaded
    DebugLog("[cs2_inject] cheat_thread woke from sleep\n");

    // Log process maps for debugging
    DebugLog("[cs2_inject] Checking loaded modules:\n");
    char maps_line[4096];
    FILE* maps_fp = fopen("/proc/self/maps", "r");
    if (maps_fp) {
        while (fgets(maps_line, sizeof(maps_line), maps_fp)) {
            if (strstr(maps_line, ".so") && (
                strstr(maps_line, "libclient") ||
                strstr(maps_line, "libengine2") ||
                strstr(maps_line, "libscenesystem") ||
                strstr(maps_line, "libschemasystem") ||
                strstr(maps_line, "libtier0") ||
                strstr(maps_line, "libinputsystem") ||
                strstr(maps_line, "librendersystem") ||
                strstr(maps_line, "libparticles")))
                DebugLog("  %s", maps_line);
        }
        fclose(maps_fp);
    }

    // Log SDL3 and Vulkan availability
    void* sdl3 = dlopen("libSDL3.so.0", RTLD_LAZY | RTLD_NOLOAD);
    DebugLog("[cs2_inject] libSDL3.so.0 loaded: %p\n", sdl3);
    void* vulkan = dlopen("libvulkan.so.1", RTLD_LAZY | RTLD_NOLOAD);
    DebugLog("[cs2_inject] libvulkan.so.1 loaded: %p\n", vulkan);

    bool result = Setup_Linux();
    DebugLog("[cs2_inject] Setup_Linux returned %d\n", result);

    if (!result) {
        DebugLog("[cs2_inject] Setup_Linux failed\n");
        Destroy_Linux();
    } else {
        g_SetupSucceeded.store(true);
        DebugLog("[cs2_inject] Setup_Linux succeeded\n");
    }

    return nullptr;
}

__attribute__((constructor))
static void on_load() {
    g_IsCs2Process = IsCs2Process();
    if (!g_IsCs2Process)
        return;

    // A new CS2 process is a new diagnostic session. Keeping old process
    // addresses in these logs made failures needlessly difficult to read.
    ResetDebugLogs();
    DebugLog("[cs2_inject] on_load constructor called\n");
    // Never replace CS2/Steam's process-wide handlers during normal use.
    // Some networking code relies on its own fault handling.
    const char* crashHandler = getenv("AXION_CRASH_HANDLER");
    if (crashHandler != nullptr && strcmp(crashHandler, "1") == 0)
        InstallSignalHandlers();
    else
        DebugLog("[cs2_inject] preserving game signal handlers\n");

    pthread_t t;
    int threadResult = pthread_create(&t, nullptr, cheat_thread, nullptr);
    DebugLog("[cs2_inject] pthread_create result=%d\n", threadResult);
    if (threadResult == 0)
        pthread_detach(t);
}

__attribute__((destructor))
static void on_unload() {
    if (!g_IsCs2Process)
        return;
    DebugLog("[cs2_inject] on_unload destructor called\n");
    if (g_SetupSucceeded.exchange(false))
        Destroy_Linux();
    if (g_DebugLogFile)
        fclose(g_DebugLogFile);
}

#endif // __linux__
