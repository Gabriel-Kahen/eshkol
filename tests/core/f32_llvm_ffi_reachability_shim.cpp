#include <cerrno>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

#if !defined(_WIN32)
#include <csignal>
#include <fcntl.h>
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
int g_pty_master = -1;
int g_saved_stdout = -1;
bool g_pty_cleanup_registered = false;
bool g_watch_cleanup_registered = false;
int g_file_lock_fd = -1;
char g_file_lock_path[128] = {};
bool g_file_lock_cleanup_registered = false;
volatile sig_atomic_t g_probe_event_write_fd = -1;

bool cleanup_file_lock_fixture() {
    bool ok = true;
    if (g_file_lock_fd >= 0 && close(g_file_lock_fd) != 0) ok = false;
    g_file_lock_fd = -1;
    if (g_file_lock_path[0] != '\0' && unlink(g_file_lock_path) != 0 &&
        errno != ENOENT) {
        ok = false;
    }
    g_file_lock_path[0] = '\0';
    return ok;
}

void cleanup_file_lock_fixture_at_exit() {
    (void)cleanup_file_lock_fixture();
}

void watcher_path(const char* suffix, char* out, size_t size) {
    std::snprintf(out, size, "/tmp/eshkol-f32-watch-%lld-%s.txt",
                  static_cast<long long>(getpid()), suffix);
}

void cleanup_watcher_files() {
    char path[256] = {};
    watcher_path("alias", path, sizeof(path));
    (void)unlink(path);
    watcher_path("control", path, sizeof(path));
    (void)unlink(path);
    watcher_path("unwatch-alias", path, sizeof(path));
    (void)unlink(path);
    watcher_path("unwatch-int", path, sizeof(path));
    (void)unlink(path);
    watcher_path("unwatch-double", path, sizeof(path));
    (void)unlink(path);
}

bool restore_stdout(int saved_stdout) {
    int result = -1;
    do {
        result = dup2(saved_stdout, STDOUT_FILENO);
    } while (result < 0 && errno == EINTR);
    close(saved_stdout);
    if (result >= 0) return true;
    close(STDOUT_FILENO);
    return false;
}

void cleanup_pty_capture() {
    std::fflush(stdout);
    if (g_saved_stdout >= 0) {
        (void)restore_stdout(g_saved_stdout);
        g_saved_stdout = -1;
    }
    if (g_pty_master >= 0) {
        close(g_pty_master);
        g_pty_master = -1;
    }
}

bool open_pty(int& master, int& slave) {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) return false;
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        close(master);
        master = -1;
        return false;
    }
    const char* const slave_name = ptsname(master);
    if (!slave_name) {
        close(master);
        master = -1;
        return false;
    }
    slave = open(slave_name, O_RDWR | O_NOCTTY);
    if (slave >= 0) return true;
    close(master);
    master = -1;
    return false;
}

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

extern "C" double f32_reachability_raw_double_from_bits(int64_t raw_bits) {
    const uint64_t bits = static_cast<uint64_t>(raw_bits);
    double value = 0.0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

extern "C" int64_t f32_reachability_file_mode(const char* path) {
    if (!path) return -1;
    struct stat observed {};
    return stat(path, &observed) == 0
               ? static_cast<int64_t>(observed.st_mode & 0777)
               : -1;
}

extern "C" int64_t f32_reachability_file_lock_begin(void) {
#if !defined(_WIN32)
    (void)cleanup_file_lock_fixture();
    std::snprintf(g_file_lock_path, sizeof(g_file_lock_path),
                  "/tmp/eshkol-f32-file-lock-%lld-XXXXXX",
                  static_cast<long long>(getpid()));
    g_file_lock_fd = mkstemp(g_file_lock_path);
    if (g_file_lock_fd < 0) {
        g_file_lock_path[0] = '\0';
        return -1;
    }
    if (!g_file_lock_cleanup_registered) {
        if (std::atexit(cleanup_file_lock_fixture_at_exit) != 0) {
            (void)cleanup_file_lock_fixture();
            return -1;
        }
        g_file_lock_cleanup_registered = true;
    }
    return g_file_lock_fd;
#else
    return -1;
#endif
}

// Returns 1 when a child can acquire the lock, 0 when the parent holds a
// conflicting lock, and -1 on fixture/probe failure.
extern "C" int64_t f32_reachability_file_lock_probe(int64_t fd) {
#if !defined(_WIN32)
    if (fd < 0 || fd != g_file_lock_fd || g_file_lock_path[0] == '\0') return -1;
    const pid_t child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        close(g_file_lock_fd);
        const int probe_fd = open(g_file_lock_path, O_RDWR);
        if (probe_fd < 0) _exit(2);
        struct flock lock {};
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        if (fcntl(probe_fd, F_SETLK, &lock) == 0) {
            lock.l_type = F_UNLCK;
            const int unlocked = fcntl(probe_fd, F_SETLK, &lock);
            close(probe_fd);
            _exit(unlocked == 0 ? 0 : 2);
        }
        const int saved_errno = errno;
        close(probe_fd);
        _exit(saved_errno == EACCES || saved_errno == EAGAIN ? 1 : 2);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (!WIFEXITED(status)) return -1;
    if (WEXITSTATUS(status) == 0) return 1;
    if (WEXITSTATUS(status) == 1) return 0;
#else
    (void)fd;
#endif
    return -1;
}

extern "C" int64_t f32_reachability_file_lock_end(int64_t fd) {
#if !defined(_WIN32)
    if (fd != g_file_lock_fd) return 0;
    return cleanup_file_lock_fixture() ? 1 : 0;
#else
    (void)fd;
    return 0;
#endif
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

extern "C" int64_t f32_reachability_socket_fd_state(int64_t raw_fd) {
#if !defined(_WIN32)
    if (raw_fd < 0 || raw_fd > INT32_MAX) return -1;
    errno = 0;
    if (fcntl(static_cast<int>(raw_fd), F_GETFD) >= 0) return 1;
    return errno == EBADF ? 0 : -1;
#else
    (void)raw_fd;
    return -1;
#endif
}

extern "C" int64_t f32_reachability_socket_pair_forget(int64_t raw_fd) {
#if !defined(_WIN32)
    if (raw_fd < 0 || raw_fd > INT32_MAX) return 0;
    for (int& fd : g_socket_pair) {
        if (fd == static_cast<int>(raw_fd)) {
            fd = -1;
            return 1;
        }
    }
#else
    (void)raw_fd;
#endif
    return 0;
}

extern "C" int64_t f32_reachability_pty_begin(void) {
#if !defined(_WIN32)
    cleanup_pty_capture();
    if (!g_pty_cleanup_registered) {
        if (std::atexit(cleanup_pty_capture) != 0) return 0;
        g_pty_cleanup_registered = true;
    }
    int master = -1;
    int slave = -1;
    if (!open_pty(master, slave)) return 0;
    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        close(master);
        close(slave);
        return 0;
    }
    std::fflush(stdout);
    if (dup2(slave, STDOUT_FILENO) < 0) {
        close(saved_stdout);
        close(master);
        close(slave);
        return 0;
    }
    close(slave);
    g_pty_master = master;
    g_saved_stdout = saved_stdout;
    return 1;
#else
    return 0;
#endif
}

extern "C" int64_t f32_reachability_pty_finish_no_bytes(int64_t timeout_ms) {
#if !defined(_WIN32)
    if (g_pty_master < 0 || g_saved_stdout < 0 || timeout_ms < 0 ||
        timeout_ms > INT32_MAX) {
        cleanup_pty_capture();
        return 0;
    }
    pollfd ready{g_pty_master, POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&ready, 1, static_cast<int>(timeout_ms));
    } while (poll_result < 0 && errno == EINTR);
    std::fflush(stdout);
    const int master = g_pty_master;
    g_pty_master = -1;
    const int saved_stdout = g_saved_stdout;
    g_saved_stdout = -1;
    const bool restored = restore_stdout(saved_stdout);

    size_t total = 0;
    bool read_ok = restored;
    char observed[32];
    while (read_ok && total < sizeof(observed)) {
        const ssize_t count =
            read(master, observed + total, sizeof(observed) - total);
        if (count > 0) {
            total += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count == 0 || (count < 0 && errno == EIO)) break;
        read_ok = false;
    }
    close(master);
    return poll_result == 0 && read_ok && total == 0 ? 1 : 0;
#else
    (void)timeout_ms;
    return 0;
#endif
}

extern "C" int64_t f32_reachability_pty_end(void) {
#if !defined(_WIN32)
    static constexpr char kExpected[] = "\033[1;2r";
    std::fflush(stdout);
    if (g_saved_stdout < 0 || g_pty_master < 0) {
        cleanup_pty_capture();
        return 0;
    }
    const int master = g_pty_master;
    g_pty_master = -1;
    const int saved_stdout = g_saved_stdout;
    g_saved_stdout = -1;
    const bool restored = restore_stdout(saved_stdout);

    char observed[32] = {};
    size_t total = 0;
    bool read_ok = restored;
    while (read_ok && total < sizeof(observed)) {
        const ssize_t count =
            read(master, observed + total, sizeof(observed) - total);
        if (count > 0) {
            total += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count == 0 || (count < 0 && errno == EIO)) break;
        read_ok = false;
    }
    close(master);
    return read_ok && total == sizeof(kExpected) - 1 &&
                   std::memcmp(observed, kExpected, sizeof(kExpected) - 1) == 0
               ? 1
               : 0;
#else
    return 0;
#endif
}

extern "C" int64_t f32_reachability_watch_cleanup_register(void) {
#if !defined(_WIN32)
    cleanup_watcher_files();
    if (!g_watch_cleanup_registered) {
        if (std::atexit(cleanup_watcher_files) != 0) return 0;
        g_watch_cleanup_registered = true;
    }
    return 1;
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
    constexpr int64_t kExpectedMask = 33554431;
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
