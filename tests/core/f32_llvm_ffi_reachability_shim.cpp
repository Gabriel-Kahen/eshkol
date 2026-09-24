#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if !defined(_WIN32)
#include <csignal>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <eshkol/core/workspace.h>

namespace {

constexpr uint32_t kPatterns[] = {
    UINT32_C(0x00000000),  // +0
    UINT32_C(0x80000000),  // -0
    UINT32_C(0x00000001),  // minimum subnormal
    UINT32_C(0x3fc00000),  // 1.5
    UINT32_C(0x7f800000),  // +infinity
    UINT32_C(0x7fc12345),  // quiet NaN with payload
    UINT32_C(0x7f812345),  // signaling NaN with payload
    UINT32_C(0x007fffff),  // maximum subnormal
    UINT32_C(0x00800000),  // minimum normal
    UINT32_C(0x7f7fffff),  // maximum finite
    UINT32_C(0x3f800000),  // 1.0
    UINT32_C(0xff800000),  // -infinity
    UINT32_C(0xffc12345),  // negative quiet NaN with payload
    UINT32_C(0xff812345),  // negative signaling NaN with payload
    UINT32_C(0x426f0000),  // 59.75
    UINT32_C(0x42700000),  // 60.0
    UINT32_C(0x4560fc00),  // 3599.75
    UINT32_C(0x45610000),  // 3600.0
    UINT32_C(0x47a8bf80),  // 86399.0
    UINT32_C(0x47a8c000),  // 86400.0
    UINT32_C(0xbfc00000),  // -1.5
    UINT32_C(0x5f000000),  // +2^63 (outside int64 range)
    UINT32_C(0x5effffff),  // previous binary32 below +2^63
    UINT32_C(0xdf000000),  // -2^63
    UINT32_C(0xdf000001),  // next binary32 below -2^63
};

int g_value_calls;
int g_check_calls;

#if !defined(_WIN32)
struct SignalProbe {
    pid_t pid = -1;
    int event_fd = -1;
};

SignalProbe g_signal_probes[8];
int g_socket_pair[2] = {-1, -1};
volatile sig_atomic_t g_probe_event_write_fd = -1;

void signal_probe_term_handler(int) {
    const int saved_errno = errno;
    const char marker = 1;
    ssize_t written = -1;
    do {
        written = write(g_probe_event_write_fd, &marker, sizeof(marker));
    } while (written < 0 && errno == EINTR);
    errno = saved_errno;
}

SignalProbe* find_signal_probe(pid_t pid) {
    for (auto& probe : g_signal_probes) {
        if (probe.pid == pid) return &probe;
    }
    return nullptr;
}

SignalProbe* find_free_signal_probe() {
    for (auto& probe : g_signal_probes) {
        if (probe.pid <= 0 && probe.event_fd < 0) return &probe;
    }
    return nullptr;
}

void reap_signal_probe_child(pid_t child) {
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}

int64_t spawn_signal_probe(bool group_leader) {
    SignalProbe* const slot = find_free_signal_probe();
    if (!slot) return -1;
    int ready_pipe[2] = {-1, -1};
    int event_pipe[2] = {-1, -1};
    if (pipe(ready_pipe) != 0) return -1;
    if (pipe(event_pipe) != 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        return -1;
    }

    const pid_t child = fork();
    if (child < 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        close(event_pipe[0]);
        close(event_pipe[1]);
        return -1;
    }
    if (child == 0) {
        close(ready_pipe[0]);
        close(event_pipe[0]);
        if (group_leader && setpgid(0, 0) != 0) _exit(126);
        g_probe_event_write_fd = event_pipe[1];
        struct sigaction action {};
        action.sa_handler = signal_probe_term_handler;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        if (sigaction(SIGTERM, &action, nullptr) != 0) _exit(126);
        const char ready = 1;
        ssize_t written = -1;
        do {
            written = write(ready_pipe[1], &ready, sizeof(ready));
        } while (written < 0 && errno == EINTR);
        close(ready_pipe[1]);
        if (written != 1) _exit(126);
        for (;;) pause();
    }

    close(ready_pipe[1]);
    close(event_pipe[1]);
    char ready = 0;
    ssize_t bytes = -1;
    do {
        bytes = read(ready_pipe[0], &ready, sizeof(ready));
    } while (bytes < 0 && errno == EINTR);
    close(ready_pipe[0]);
    if (bytes == 1 && ready == 1) {
        slot->pid = child;
        slot->event_fd = event_pipe[0];
        return static_cast<int64_t>(child);
    }

    (void)kill(child, SIGKILL);
    reap_signal_probe_child(child);
    close(event_pipe[0]);
    return -1;
}
#endif

bool valid_code(int64_t code) {
    return code >= 0 &&
           static_cast<uint64_t>(code) < sizeof(kPatterns) / sizeof(kPatterns[0]);
}

bool persistence_path(char* out, size_t size, int64_t pid) {
    return std::snprintf(out, size,
                         "/tmp/eshkol-f32-persistence-%lld.kb",
                         static_cast<long long>(pid)) > 0;
}

bool write_exact(FILE* file, const void* data, size_t size) {
    return std::fwrite(data, 1, size, file) == size;
}

}  // namespace

extern "C" void f32_reachability_reset(void) {
    g_value_calls = 0;
    g_check_calls = 0;
}

extern "C" float f32_reachability_value(int64_t code) {
    if (!valid_code(code)) return 0.0f;
    float value = 0.0f;
    const uint32_t bits = kPatterns[code];
    std::memcpy(&value, &bits, sizeof(value));
    ++g_value_calls;
    return value;
}

extern "C" float f32_reachability_from_bits(int64_t raw_bits) {
    const uint32_t bits = static_cast<uint32_t>(raw_bits);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    ++g_value_calls;
    return value;
}

extern "C" int64_t f32_reachability_file_mode(const char* path) {
    if (!path) return -1;
    struct stat observed {};
    return stat(path, &observed) == 0
               ? static_cast<int64_t>(observed.st_mode & 0777)
               : -1;
}

extern "C" int64_t f32_reachability_socket_pair_open(void) {
#if !defined(_WIN32)
    for (int& fd : g_socket_pair) {
        if (fd >= 0) close(fd);
        fd = -1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_socket_pair) != 0) return -1;
    return g_socket_pair[0];
#else
    return -1;
#endif
}

extern "C" int64_t f32_reachability_socket_recv_pair_open(int64_t code) {
#if !defined(_WIN32)
    for (int& fd : g_socket_pair) {
        if (fd >= 0) close(fd);
        fd = -1;
    }
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, g_socket_pair) != 0) return -1;
    const char* payload = code == 1 ? "ABCDE" : code == 2 ? "UVWXYZ" : nullptr;
    const size_t size = code == 1 ? 5 : code == 2 ? 6 : 0;
    if (!payload) {
        for (int& fd : g_socket_pair) {
            if (fd >= 0) close(fd);
            fd = -1;
        }
        return -1;
    }
    size_t sent_total = 0;
    while (sent_total < size) {
        ssize_t sent = -1;
        do {
            sent = send(g_socket_pair[1], payload + sent_total,
                        size - sent_total, 0);
        } while (sent < 0 && errno == EINTR);
        if (sent <= 0) {
            for (int& fd : g_socket_pair) {
                if (fd >= 0) close(fd);
                fd = -1;
            }
            return -1;
        }
        sent_total += static_cast<size_t>(sent);
    }
    return g_socket_pair[0];
#else
    (void)code;
    return -1;
#endif
}

extern "C" int64_t f32_reachability_socket_pair_receive(int64_t code,
                                                           int64_t timeout_ms) {
#if !defined(_WIN32)
    if (g_socket_pair[1] < 0 || timeout_ms < 0 || timeout_ms > INT32_MAX)
        return -1;
    pollfd ready{g_socket_pair[1], POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&ready, 1, static_cast<int>(timeout_ms));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) return 0;
    if (poll_result < 0 || (ready.revents & POLLIN) == 0) return -1;
    char bytes[3] = {};
    ssize_t received = -1;
    do {
        received = recv(g_socket_pair[1], bytes, sizeof(bytes), MSG_WAITALL);
    } while (received < 0 && errno == EINTR);
    if (code == 1 && received == 3 && std::memcmp(bytes, "INT", 3) == 0)
        return 1;
    return -1;
#else
    (void)code;
    (void)timeout_ms;
    return -1;
#endif
}

extern "C" int64_t f32_reachability_socket_pair_close(void) {
#if !defined(_WIN32)
    int64_t closed = 0;
    for (int& fd : g_socket_pair) {
        if (fd >= 0 && close(fd) == 0) ++closed;
        fd = -1;
    }
    return closed;
#else
    return 0;
#endif
}

extern "C" int64_t f32_reachability_spawn_signal_probe(void) {
#if !defined(_WIN32)
    return spawn_signal_probe(true);
#endif
    return -1;
}

extern "C" int64_t f32_reachability_spawn_plain_signal_probe(void) {
#if !defined(_WIN32)
    return spawn_signal_probe(false);
#else
    return -1;
#endif
}

extern "C" int64_t f32_reachability_process_group(int64_t raw_pid) {
#if !defined(_WIN32)
    if (raw_pid <= 0) return -1;
    return static_cast<int64_t>(getpgid(static_cast<pid_t>(raw_pid)));
#else
    (void)raw_pid;
    return -1;
#endif
}

extern "C" int64_t f32_reachability_signal_probe_observed(int64_t raw_pid,
                                                            int64_t timeout_ms) {
#if !defined(_WIN32)
    if (raw_pid <= 0 || timeout_ms < 0 || timeout_ms > INT32_MAX) return -1;
    SignalProbe* const probe = find_signal_probe(static_cast<pid_t>(raw_pid));
    if (!probe) return -1;
    pollfd event{probe->event_fd, POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&event, 1, static_cast<int>(timeout_ms));
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) return 0;
    if (poll_result < 0 || (event.revents & POLLIN) == 0) return -1;
    char marker = 0;
    ssize_t bytes = -1;
    do {
        bytes = read(probe->event_fd, &marker, sizeof(marker));
    } while (bytes < 0 && errno == EINTR);
    return bytes == 1 && marker == 1 ? 1 : -1;
#else
    (void)raw_pid;
    (void)timeout_ms;
    return -1;
#endif
}

extern "C" int64_t f32_reachability_signal_probe_release(int64_t raw_pid) {
#if !defined(_WIN32)
    if (raw_pid <= 0) return 0;
    SignalProbe* const probe = find_signal_probe(static_cast<pid_t>(raw_pid));
    if (!probe) return 0;
    const int closed = close(probe->event_fd) == 0 ? 1 : 0;
    *probe = {};
    return closed;
#else
    (void)raw_pid;
    return 0;
#endif
}

extern "C" int64_t f32_reachability_check_bits(int64_t code, float value) {
    if (!valid_code(code)) return 0;
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    ++g_check_calls;
    return bits == kPatterns[code] ? 1 : 0;
}

extern "C" int64_t f32_reachability_check_promoted(int64_t code,
                                                       double value) {
    if (!valid_code(code)) return 0;
    const uint32_t source_bits = kPatterns[code];
    uint64_t expected_bits = 0;
    if ((source_bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) &&
        (source_bits & UINT32_C(0x007fffff)) != 0) {
        expected_bits = UINT64_C(0x7ff8000000000000);
    } else {
        float source = 0.0f;
        std::memcpy(&source, &source_bits, sizeof(source));
        const double expected = static_cast<double>(source);
        std::memcpy(&expected_bits, &expected, sizeof(expected_bits));
    }
    uint64_t actual_bits = 0;
    std::memcpy(&actual_bits, &value, sizeof(actual_bits));
    return actual_bits == expected_bits ? 1 : 0;
}

extern "C" int64_t f32_reachability_persistence_prepare(int64_t mode,
                                                          int64_t pid) {
    char path[128];
    if (!persistence_path(path, sizeof(path), pid)) return 0;
    FILE* file = std::fopen(path, "wb");
    if (!file) return 0;

    bool ok = false;
    if (mode == 0) {
        static const uint8_t sentinel[] = {0x43, 0x32, 0xfa, 0x11, 0xed};
        ok = write_exact(file, sentinel, sizeof(sentinel));
    } else if (mode == 1) {
        const uint32_t magic = UINT32_C(0x45534b42);
        const uint32_t version = 2;
        const uint32_t one = 1;
        const uint32_t zero = 0;
        const uint32_t name_length = 6;
        const char predicate[] = "metric";
        const uint8_t type = 11;
        const uint8_t flags = 0x20;
        const uint64_t signaling_nan = UINT64_C(0x7f812345);
        ok = write_exact(file, &magic, sizeof(magic)) &&
             write_exact(file, &version, sizeof(version)) &&
             write_exact(file, &one, sizeof(one)) &&
             write_exact(file, &zero, sizeof(zero)) &&
             write_exact(file, &name_length, sizeof(name_length)) &&
             write_exact(file, predicate, name_length) &&
             write_exact(file, &one, sizeof(one)) &&
             write_exact(file, &type, sizeof(type)) &&
             write_exact(file, &flags, sizeof(flags)) &&
             write_exact(file, &signaling_nan, sizeof(signaling_nan));
    }
    if (std::fclose(file) != 0) ok = false;
    if (!ok) std::remove(path);
    return ok ? 1 : 0;
}

extern "C" int64_t f32_reachability_persistence_check(int64_t mode,
                                                        int64_t pid) {
    char path[128];
    if (!persistence_path(path, sizeof(path), pid)) return 0;
    FILE* file = std::fopen(path, "rb");
    if (!file) return 0;
    uint8_t bytes[64];
    const size_t size = std::fread(bytes, 1, sizeof(bytes), file);
    const bool eof = std::fgetc(file) == EOF;
    const bool closed = std::fclose(file) == 0;
    std::remove(path);
    if (!eof || !closed) return 0;

    if (mode == 0) {
        static const uint8_t sentinel[] = {0x43, 0x32, 0xfa, 0x11, 0xed};
        return size == sizeof(sentinel) &&
               std::memcmp(bytes, sentinel, sizeof(sentinel)) == 0;
    }
    if (mode == 1) {
        return size == 40 && bytes[30] == 11 && bytes[31] == 0x20;
    }
    return 0;
}

extern "C" int64_t f32_reachability_finish(int64_t semantic_ok) {
    constexpr int64_t kExpectedSemanticMask = 16383;
    const bool ok = semantic_ok == kExpectedSemanticMask &&
                    g_value_calls >= 14 && g_check_calls == 15;
    if (!ok) {
        std::fprintf(stderr,
                     "FAIL: f32 LLVM FFI reachability "
                     "(semantic-mask=%lld expected=%lld values=%d checks=%d)\n",
                     static_cast<long long>(semantic_ok),
                     static_cast<long long>(kExpectedSemanticMask),
                     g_value_calls, g_check_calls);
        return 0;
    }
    std::puts("PASS: f32 LLVM FFI reachability");
    return 1;
}

extern "C" int64_t f32_reachability_json_finish(int64_t ok) {
    if (ok == 1) std::puts("PASS: f32 JSON rejection and atomicity");
    return ok == 1 ? 1 : 0;
}

extern "C" int64_t f32_reachability_normalize_finish(int64_t ok) {
    if (ok == 1) std::puts("PASS: f32 checked promotion and normalization");
    return ok == 1 ? 1 : 0;
}

extern "C" int64_t f32_reachability_workspace_check(
    const eshkol_workspace_t* workspace, double expected_content) {
    if (!workspace || workspace->dim == 0 || !workspace->content ||
        workspace->step_count != 1) {
        return 0;
    }
    return workspace->content[0] == expected_content ? 1 : 0;
}

extern "C" int64_t f32_reachability_workspace_finish(int64_t ok) {
    if (ok == 1) std::puts("PASS: f32 workspace salience promotion");
    return ok == 1 ? 1 : 0;
}

extern "C" int64_t f32_reachability_system_finish(int64_t semantic_mask) {
    constexpr int64_t kExpectedMask = 131071;
    if (semantic_mask == kExpectedMask) {
        std::puts("PASS: f32 system quantity promotion and resource rejection");
        return 1;
    }
    std::fprintf(stderr,
                 "FAIL: f32 system integer semantic mask=%lld expected=%lld\n",
                 static_cast<long long>(semantic_mask),
                 static_cast<long long>(kExpectedMask));
    return 0;
}

extern "C" int64_t f32_reachability_time_finish(int64_t ok) {
    if (ok == 1) {
        std::puts("PASS: f32 format-iso8601 quantity promotion and rejection");
        return 1;
    }
    std::fputs("FAIL: f32 format-iso8601 semantic witness\n", stderr);
    return 0;
}
