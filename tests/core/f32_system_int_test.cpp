#include <eshkol/eshkol.h>

#include "../../lib/core/arena_memory.h"

#include <cerrno>
#include <csetjmp>
#include <csignal>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if !defined(MAP_ANONYMOUS) && defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif
#endif

extern "C" void eshkol_builtin_format_relative(eshkol_tagged_value_t* out,
                                                 const eshkol_tagged_value_t* in);
extern "C" void eshkol_builtin_regex_free(eshkol_tagged_value_t* out,
                                            const eshkol_tagged_value_t* in);
extern "C" void eshkol_builtin_prevent_sleep(eshkol_tagged_value_t* out,
                                                const eshkol_tagged_value_t* in);
extern "C" void eshkol_builtin_allow_sleep(eshkol_tagged_value_t* out,
                                              const eshkol_tagged_value_t* in);
extern "C" void eshkol_builtin_process_wait(eshkol_tagged_value_t* out,
                                              const eshkol_tagged_value_t* in);
extern "C" void eshkol_builtin_poll_fd(eshkol_tagged_value_t* out,
                                         const eshkol_tagged_value_t* fd,
                                         const eshkol_tagged_value_t* timeout);
extern "C" void eshkol_builtin_file_chmod(eshkol_tagged_value_t* out,
                                            const eshkol_tagged_value_t* path,
                                            const eshkol_tagged_value_t* mode);
extern "C" void eshkol_builtin_process_kill(eshkol_tagged_value_t* out,
                                              const eshkol_tagged_value_t* pid,
                                              const eshkol_tagged_value_t* signal);
extern "C" void eshkol_builtin_process_kill_tree(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* pid,
    const eshkol_tagged_value_t* signal);
extern "C" void eshkol_builtin_process_setpgid(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* pid,
    const eshkol_tagged_value_t* pgid);
extern "C" void eshkol_builtin_process_read_nonblocking(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* fd,
    const eshkol_tagged_value_t* max_bytes);
extern "C" void eshkol_builtin_socket_send(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* fd,
    const eshkol_tagged_value_t* data);
extern "C" void eshkol_builtin_socket_recv(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* fd,
    const eshkol_tagged_value_t* max_bytes);
extern "C" void eshkol_builtin_socket_close(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* fd);
extern "C" void eshkol_builtin_term_set_scroll_region(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* top,
    const eshkol_tagged_value_t* bottom);
extern "C" void eshkol_builtin_fs_watch_native(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* path,
    const eshkol_tagged_value_t* callback);
extern "C" void eshkol_builtin_fs_watch_poll(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* handle);
extern "C" void eshkol_builtin_fs_unwatch(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* handle);
extern "C" void eshkol_builtin_string_truncate_display(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* input,
    const eshkol_tagged_value_t* maximum,
    const eshkol_tagged_value_t* suffix);
extern "C" void eshkol_builtin_string_index_of(
    eshkol_tagged_value_t* out, const eshkol_tagged_value_t* haystack,
    const eshkol_tagged_value_t* needle,
    const eshkol_tagged_value_t* start);
extern "C" void eshkol_clear_current_exception(void);

namespace {

int failures;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

#if !defined(_WIN32)
struct SignalProbe {
    pid_t pid = -1;
    int event_fd = -1;
};

volatile sig_atomic_t probe_event_write_fd = -1;

void signal_probe_term_handler(int) {
    const int saved_errno = errno;
    const char marker = 1;
    ssize_t written = -1;
    do {
        written = write(probe_event_write_fd, &marker, sizeof(marker));
    } while (written < 0 && errno == EINTR);
    errno = saved_errno;
}

void reap_signal_probe_child(pid_t child) {
    while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {}
}

SignalProbe spawn_signal_probe() {
    int ready_pipe[2] = {-1, -1};
    int event_pipe[2] = {-1, -1};
    if (pipe(ready_pipe) != 0) return {};
    if (pipe(event_pipe) != 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        return {};
    }

    const pid_t child = fork();
    if (child < 0) {
        close(ready_pipe[0]);
        close(ready_pipe[1]);
        close(event_pipe[0]);
        close(event_pipe[1]);
        return {};
    }
    if (child == 0) {
        close(ready_pipe[0]);
        close(event_pipe[0]);
        if (setpgid(0, 0) != 0) _exit(126);
        probe_event_write_fd = event_pipe[1];
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
    if (bytes == 1 && ready == 1) return {child, event_pipe[0]};

    (void)kill(child, SIGKILL);
    reap_signal_probe_child(child);
    close(event_pipe[0]);
    return {};
}

int observe_signal_probe(const SignalProbe& probe, int timeout_ms) {
    if (probe.pid <= 0 || probe.event_fd < 0) return -1;
    pollfd event{probe.event_fd, POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&event, 1, timeout_ms);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result == 0) return 0;
    if (poll_result < 0 || (event.revents & POLLIN) == 0) return -1;
    char marker = 0;
    ssize_t bytes = -1;
    do {
        bytes = read(probe.event_fd, &marker, sizeof(marker));
    } while (bytes < 0 && errno == EINTR);
    return bytes == 1 && marker == 1 ? 1 : -1;
}

void cleanup_signal_probe(const SignalProbe& probe) {
    if (probe.pid > 0) {
        eshkol_tagged_value_t pid{};
        pid.type = ESHKOL_VALUE_INT64;
        pid.data.int_val = probe.pid;
        eshkol_tagged_value_t signal{};
        signal.type = ESHKOL_VALUE_INT64;
        signal.data.int_val = SIGKILL;
        eshkol_tagged_value_t result{};
        eshkol_builtin_process_kill(&result, &pid, &signal);
        check(result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 1,
              "INT64 SIGKILL probe cleanup failed");
    }
    int status = 0;
    if (probe.pid > 0) {
        while (waitpid(probe.pid, &status, 0) < 0 && errno == EINTR) {}
    }
    if (probe.event_fd >= 0) close(probe.event_fd);
}

struct SharedFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
    eshkol_tagged_value_t other;
    char path[128];
};

enum class BuiltinKind {
    FormatRelative,
    RegexFree,
    AllowSleep,
    ProcessWait,
    PollFdDescriptor,
    PollFdTimeout,
    ProcessKillPid,
    ProcessKillSignal,
    ProcessKillTreePid,
    ProcessKillTreeSignal,
    ProcessSetpgidPid,
    ProcessSetpgidGroup,
    ProcessReadDescriptor,
    ProcessReadMaximum,
    FileChmod
};

bool is_process_signal_rejection(BuiltinKind builtin) {
    return builtin == BuiltinKind::ProcessKillPid ||
           builtin == BuiltinKind::ProcessKillSignal ||
           builtin == BuiltinKind::ProcessKillTreePid ||
           builtin == BuiltinKind::ProcessKillTreeSignal;
}

bool is_process_pid_position(BuiltinKind builtin) {
    return builtin == BuiltinKind::ProcessKillPid ||
           builtin == BuiltinKind::ProcessKillTreePid;
}

bool is_process_setpgid_rejection(BuiltinKind builtin) {
    return builtin == BuiltinKind::ProcessSetpgidPid ||
           builtin == BuiltinKind::ProcessSetpgidGroup;
}

bool is_process_read_rejection(BuiltinKind builtin) {
    return builtin == BuiltinKind::ProcessReadDescriptor ||
           builtin == BuiltinKind::ProcessReadMaximum;
}

void expect_rejection(BuiltinKind builtin, bool malformed,
                      const char* diagnostic,
                      const char* label) {
    void* mapping = mmap(nullptr, sizeof(SharedFixture), PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate shared system fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<SharedFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);
    SignalProbe target_probe;
    int consumption_pipe[2] = {-1, -1};
    int consumption_flags = -1;
    uint32_t input_bits = UINT32_C(0x00000001);
    if (is_process_read_rejection(builtin)) {
        check(pipe(consumption_pipe) == 0,
              "could not create process-read rejection pipe");
        if (consumption_pipe[0] < 0 || consumption_pipe[1] < 0) {
            munmap(mapping, sizeof(*fixture));
            return;
        }
        static constexpr char kPayload[] = "ABCDE";
        ssize_t written = -1;
        do {
            written = write(consumption_pipe[1], kPayload,
                            sizeof(kPayload) - 1);
        } while (written < 0 && errno == EINTR);
        check(written == static_cast<ssize_t>(sizeof(kPayload) - 1),
              "could not populate process-read rejection pipe");
        if (written != static_cast<ssize_t>(sizeof(kPayload) - 1)) {
            close(consumption_pipe[0]);
            close(consumption_pipe[1]);
            munmap(mapping, sizeof(*fixture));
            return;
        }
        consumption_flags = fcntl(consumption_pipe[0], F_GETFL, 0);
        check(consumption_flags >= 0,
              "could not inspect process-read rejection descriptor flags");
        if (consumption_flags < 0) {
            close(consumption_pipe[0]);
            close(consumption_pipe[1]);
            munmap(mapping, sizeof(*fixture));
            return;
        }
        input_bits = builtin == BuiltinKind::ProcessReadDescriptor
                         ? static_cast<uint32_t>(consumption_pipe[0])
                         : UINT32_C(2);
    } else if (is_process_signal_rejection(builtin)) {
        target_probe = spawn_signal_probe();
        check(target_probe.pid > 0 && target_probe.event_fd >= 0 &&
                  static_cast<uint64_t>(target_probe.pid) <= UINT32_MAX,
              "could not spawn process-kill rejection target");
        if (target_probe.pid <= 0 || target_probe.event_fd < 0 ||
            static_cast<uint64_t>(target_probe.pid) > UINT32_MAX) {
            cleanup_signal_probe(target_probe);
            munmap(mapping, sizeof(*fixture));
            return;
        }
        input_bits = is_process_pid_position(builtin)
                         ? static_cast<uint32_t>(target_probe.pid)
                         : UINT32_C(15);
    } else if (is_process_setpgid_rejection(builtin)) {
        input_bits = static_cast<uint32_t>(getpid());
    }
    check(eshkol_value_f32_from_bits_v1(&fixture->input,
                                        input_bits) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct malformed system input base");
    if (malformed) fixture->input.reserved = 1;
    if (builtin == BuiltinKind::FileChmod) {
        std::snprintf(fixture->path, sizeof(fixture->path),
                      "/tmp/eshkol-f32-chmod-native-%ld-%d",
                      static_cast<long>(getpid()), malformed ? 1 : 0);
        unlink(fixture->path);
        const int file = open(fixture->path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        check(file >= 0, "could not create file-chmod rejection fixture");
        if (file >= 0) close(file);
        check(chmod(fixture->path, 0644) == 0,
              "could not initialize file-chmod rejection mode");
        fixture->other.type = ESHKOL_VALUE_HEAP_PTR;
        fixture->other.flags = 0x01;
        fixture->other.data.ptr_val =
            reinterpret_cast<uintptr_t>(fixture->path);
    } else if (is_process_pid_position(builtin)) {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = SIGTERM;
    } else if (is_process_signal_rejection(builtin)) {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = target_probe.pid;
    } else if (is_process_setpgid_rejection(builtin)) {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = getpid();
    } else if (builtin == BuiltinKind::ProcessReadDescriptor) {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = 2;
    } else if (builtin == BuiltinKind::ProcessReadMaximum) {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = consumption_pipe[0];
    } else {
        fixture->other.type = ESHKOL_VALUE_INT64;
        fixture->other.data.int_val = 0;
    }

    int stderr_pipe[2];
    if (pipe(stderr_pipe) != 0) {
        check(false, "could not create system diagnostic pipe");
        if (builtin == BuiltinKind::FileChmod) unlink(fixture->path);
        cleanup_signal_probe(target_probe);
        if (consumption_pipe[0] >= 0) close(consumption_pipe[0]);
        if (consumption_pipe[1] >= 0) close(consumption_pipe[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork malformed system test");
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        if (builtin == BuiltinKind::FileChmod) unlink(fixture->path);
        cleanup_signal_probe(target_probe);
        if (consumption_pipe[0] >= 0) close(consumption_pipe[0]);
        if (consumption_pipe[1] >= 0) close(consumption_pipe[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    if (child == 0) {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);
        if (builtin == BuiltinKind::FormatRelative) {
            eshkol_builtin_format_relative(&fixture->output, &fixture->input);
        } else if (builtin == BuiltinKind::RegexFree) {
            eshkol_builtin_regex_free(&fixture->output, &fixture->input);
        } else if (builtin == BuiltinKind::AllowSleep) {
            eshkol_builtin_allow_sleep(&fixture->output, &fixture->input);
        } else if (builtin == BuiltinKind::ProcessWait) {
            eshkol_builtin_process_wait(&fixture->output, &fixture->input);
        } else if (builtin == BuiltinKind::PollFdDescriptor) {
            eshkol_builtin_poll_fd(&fixture->output, &fixture->input,
                                   &fixture->other);
        } else if (builtin == BuiltinKind::PollFdTimeout) {
            eshkol_builtin_poll_fd(&fixture->output, &fixture->other,
                                   &fixture->input);
        } else if (builtin == BuiltinKind::ProcessKillPid) {
            eshkol_builtin_process_kill(&fixture->output, &fixture->input,
                                        &fixture->other);
        } else if (builtin == BuiltinKind::ProcessKillSignal) {
            eshkol_builtin_process_kill(&fixture->output, &fixture->other,
                                        &fixture->input);
        } else if (builtin == BuiltinKind::ProcessKillTreePid) {
            eshkol_builtin_process_kill_tree(&fixture->output, &fixture->input,
                                             &fixture->other);
        } else if (builtin == BuiltinKind::ProcessKillTreeSignal) {
            eshkol_builtin_process_kill_tree(&fixture->output, &fixture->other,
                                             &fixture->input);
        } else if (builtin == BuiltinKind::ProcessSetpgidPid) {
            eshkol_builtin_process_setpgid(&fixture->output, &fixture->input,
                                           &fixture->other);
        } else if (builtin == BuiltinKind::ProcessSetpgidGroup) {
            eshkol_builtin_process_setpgid(&fixture->output, &fixture->other,
                                           &fixture->input);
        } else if (builtin == BuiltinKind::ProcessReadDescriptor) {
            eshkol_builtin_process_read_nonblocking(
                &fixture->output, &fixture->input, &fixture->other);
        } else if (builtin == BuiltinKind::ProcessReadMaximum) {
            eshkol_builtin_process_read_nonblocking(
                &fixture->output, &fixture->other, &fixture->input);
        } else {
            eshkol_builtin_file_chmod(&fixture->output, &fixture->other,
                                      &fixture->input);
        }
        _exit(99);
    }
    close(stderr_pipe[1]);
    std::string observed;
    char buffer[256];
    ssize_t count = 0;
    while ((count = read(stderr_pipe[0], buffer, sizeof(buffer))) > 0) {
        observed.append(buffer, static_cast<size_t>(count));
    }
    close(stderr_pipe[0]);
    int status = 0;
    waitpid(child, &status, 0);

    check(WIFEXITED(status) && WEXITSTATUS(status) == 1, label);
    if (observed.find(diagnostic) == std::string::npos) {
        std::fprintf(stderr, "FAIL: system rejection diagnostic changed for %s: %s\n",
                     label, observed.c_str());
        ++failures;
    }
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "system input mutated output before rejection");
    if (is_process_signal_rejection(builtin)) {
        check(observe_signal_probe(target_probe, 250) == 0,
              builtin == BuiltinKind::ProcessKillPid ||
                      builtin == BuiltinKind::ProcessKillSignal
                  ? "f32 process-kill delivered SIGTERM before rejection"
                  : "f32 process-kill-tree delivered SIGTERM before rejection");
        cleanup_signal_probe(target_probe);
    }
    if (builtin == BuiltinKind::FileChmod) {
        struct stat observed {};
        check(stat(fixture->path, &observed) == 0 &&
                  (observed.st_mode & 0777) == 0644,
              "f32 file-chmod mutated mode before rejection");
        unlink(fixture->path);
    }
    if (is_process_read_rejection(builtin)) {
        close(consumption_pipe[1]);
        consumption_pipe[1] = -1;
        const int flags_after = fcntl(consumption_pipe[0], F_GETFL, 0);
        check(flags_after == consumption_flags &&
                  (flags_after & O_NONBLOCK) ==
                      (consumption_flags & O_NONBLOCK),
              "f32 process-read changed descriptor flags before rejection");
        char remaining[sizeof("ABCDE") - 1] = {};
        ssize_t remaining_count = -1;
        do {
            remaining_count = read(consumption_pipe[0], remaining,
                                   sizeof(remaining));
        } while (remaining_count < 0 && errno == EINTR);
        check(remaining_count == static_cast<ssize_t>(sizeof(remaining)) &&
                  std::memcmp(remaining, "ABCDE", sizeof(remaining)) == 0,
              builtin == BuiltinKind::ProcessReadDescriptor
                  ? "f32 process-read descriptor consumed bytes before rejection"
                  : "f32 process-read maximum consumed bytes before rejection");
        close(consumption_pipe[0]);
    }
    munmap(mapping, sizeof(*fixture));
}

void expect_process_kill_control(bool raw_double, bool tree) {
    const SignalProbe probe = spawn_signal_probe();
    check(probe.pid > 0 && probe.event_fd >= 0,
          tree ? "could not spawn process-kill-tree control"
               : "could not spawn process-kill control");
    if (probe.pid <= 0 || probe.event_fd < 0) {
        cleanup_signal_probe(probe);
        return;
    }

    eshkol_tagged_value_t pid{};
    eshkol_tagged_value_t signal{};
    pid.type = signal.type = raw_double ? ESHKOL_VALUE_DOUBLE
                                        : ESHKOL_VALUE_INT64;
    if (raw_double) {
        pid.flags = signal.flags = ESHKOL_VALUE_INEXACT_FLAG;
        pid.data.raw_val = static_cast<uint64_t>(probe.pid);
        signal.data.raw_val = SIGTERM;
    } else {
        pid.data.int_val = probe.pid;
        signal.data.int_val = SIGTERM;
    }

    eshkol_tagged_value_t result{};
    if (tree) {
        eshkol_builtin_process_kill_tree(&result, &pid, &signal);
    } else {
        eshkol_builtin_process_kill(&result, &pid, &signal);
    }
    check(result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 1,
          tree ? "process-kill-tree control behavior changed"
               : "process-kill control behavior changed");
    check(observe_signal_probe(probe, 250) == 1,
          tree ? "process-kill-tree control did not deliver SIGTERM"
               : "process-kill control did not deliver SIGTERM");
    cleanup_signal_probe(probe);
}

struct SetpgidControlFixture {
    eshkol_tagged_value_t output;
    pid_t child;
    pid_t before_group;
    pid_t after_group;
};

void expect_process_setpgid_control(bool raw_double) {
    void* mapping = mmap(nullptr, sizeof(SetpgidControlFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate setpgid control fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<SetpgidControlFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));

    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork setpgid control");
        munmap(mapping, sizeof(*fixture));
        return;
    }
    if (child == 0) {
        fixture->child = getpid();
        fixture->before_group = getpgrp();
        eshkol_tagged_value_t pid{};
        eshkol_tagged_value_t pgid{};
        pid.type = pgid.type = raw_double ? ESHKOL_VALUE_DOUBLE
                                         : ESHKOL_VALUE_INT64;
        if (raw_double) {
            pid.flags = pgid.flags = ESHKOL_VALUE_INEXACT_FLAG;
            pid.data.raw_val = static_cast<uint64_t>(fixture->child);
            pgid.data.raw_val = static_cast<uint64_t>(fixture->child);
        } else {
            pid.data.int_val = fixture->child;
            pgid.data.int_val = fixture->child;
        }
        eshkol_builtin_process_setpgid(&fixture->output, &pid, &pgid);
        fixture->after_group = getpgrp();
        _exit(0);
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    check(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          raw_double ? "raw DOUBLE setpgid control child failed"
                     : "INT64 setpgid control child failed");
    check(fixture->child == child && fixture->before_group != child &&
              fixture->after_group == child &&
              fixture->output.type == ESHKOL_VALUE_BOOL &&
              fixture->output.data.raw_val == 1,
          raw_double ? "historical raw DOUBLE setpgid behavior changed"
                     : "INT64 setpgid behavior changed");
    munmap(mapping, sizeof(*fixture));
}

enum class ProcessReadControlKind {
    Int64,
    RawDoubleDescriptor,
    RawDoubleMaximum
};

void expect_process_read_control(ProcessReadControlKind kind) {
    int data_pipe[2] = {-1, -1};
    check(pipe(data_pipe) == 0, "could not create process-read control pipe");
    if (data_pipe[0] < 0 || data_pipe[1] < 0) return;

    static constexpr char kPayload[] = "ABCDEF";
    ssize_t written = -1;
    do {
        written = write(data_pipe[1], kPayload, sizeof(kPayload) - 1);
    } while (written < 0 && errno == EINTR);
    check(written == static_cast<ssize_t>(sizeof(kPayload) - 1),
          "could not populate process-read control pipe");
    if (written != static_cast<ssize_t>(sizeof(kPayload) - 1)) {
        close(data_pipe[0]);
        close(data_pipe[1]);
        return;
    }

    eshkol_tagged_value_t fd{};
    eshkol_tagged_value_t maximum{};
    fd.type = kind == ProcessReadControlKind::RawDoubleDescriptor
                  ? ESHKOL_VALUE_DOUBLE
                  : ESHKOL_VALUE_INT64;
    maximum.type = kind == ProcessReadControlKind::RawDoubleMaximum
                       ? ESHKOL_VALUE_DOUBLE
                       : ESHKOL_VALUE_INT64;
    if (fd.type == ESHKOL_VALUE_DOUBLE) {
        fd.flags = ESHKOL_VALUE_INEXACT_FLAG;
        fd.data.raw_val = static_cast<uint64_t>(data_pipe[0]);
    } else {
        fd.data.int_val = data_pipe[0];
    }
    if (maximum.type == ESHKOL_VALUE_DOUBLE) {
        maximum.flags = ESHKOL_VALUE_INEXACT_FLAG;
        maximum.data.raw_val = 3;
    } else {
        maximum.data.int_val = 3;
    }

    const int flags_before = fcntl(data_pipe[0], F_GETFL, 0);
    eshkol_tagged_value_t result{};
    eshkol_builtin_process_read_nonblocking(&result, &fd, &maximum);
    const int flags_after = fcntl(data_pipe[0], F_GETFL, 0);
    close(data_pipe[1]);
    data_pipe[1] = -1;
    char remaining[4] = {};
    ssize_t remaining_count = -1;
    do {
        remaining_count = read(data_pipe[0], remaining, 3);
    } while (remaining_count < 0 && errno == EINTR);
    close(data_pipe[0]);

    const char* label = kind == ProcessReadControlKind::Int64
                            ? "INT64 process-read behavior changed"
                            : kind == ProcessReadControlKind::RawDoubleDescriptor
                                  ? "historical raw DOUBLE process-read descriptor behavior changed"
                                  : "historical raw DOUBLE process-read maximum behavior changed";
    check(result.type == ESHKOL_VALUE_HEAP_PTR && result.data.ptr_val != 0 &&
              std::memcmp(reinterpret_cast<const void*>(result.data.ptr_val),
                          "ABC", 3) == 0 &&
              remaining_count == 3 &&
              std::memcmp(remaining, "DEF", 3) == 0 &&
              flags_before >= 0 && flags_after == flags_before,
          label);
}

struct SocketSendFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t descriptor;
    eshkol_tagged_value_t data;
    char payload[4];
};

void expect_socket_send_rejection(bool malformed) {
    void* mapping = mmap(nullptr, sizeof(SocketSendFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate socket-send fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<SocketSendFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);
    std::memcpy(fixture->payload, "F32", 4);
    fixture->data.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->data.flags = 0x01;
    fixture->data.data.ptr_val =
        reinterpret_cast<uintptr_t>(fixture->payload);

    int sockets[2] = {-1, -1};
    check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "could not create socket-send rejection pair");
    if (sockets[0] < 0 || sockets[1] < 0) {
        munmap(mapping, sizeof(*fixture));
        return;
    }
    check(eshkol_value_f32_from_bits_v1(
              &fixture->descriptor, static_cast<uint32_t>(sockets[0])) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct socket-send f32 descriptor");
    if (malformed) fixture->descriptor.reserved = 1;

    int stderr_pipe[2] = {-1, -1};
    if (pipe(stderr_pipe) != 0) {
        check(false, "could not create socket-send diagnostic pipe");
        close(sockets[0]);
        close(sockets[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork socket-send rejection test");
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        close(sockets[0]);
        close(sockets[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    if (child == 0) {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);
        eshkol_builtin_socket_send(&fixture->output, &fixture->descriptor,
                                   &fixture->data);
        _exit(99);
    }

    close(stderr_pipe[1]);
    std::string observed;
    char diagnostic_buffer[256];
    ssize_t diagnostic_count = 0;
    while ((diagnostic_count = read(stderr_pipe[0], diagnostic_buffer,
                                    sizeof(diagnostic_buffer))) > 0) {
        observed.append(diagnostic_buffer,
                        static_cast<size_t>(diagnostic_count));
    }
    close(stderr_pipe[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}

    check(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          malformed ? "malformed f32 socket descriptor did not fail explicitly"
                    : "canonical f32 socket descriptor did not fail explicitly");
    check(observed.find(
              "Type error in system integer/resource argument: expected non-float32 value") !=
              std::string::npos,
          "socket-send rejection diagnostic changed");
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "socket-send mutated output before rejection");

    pollfd peer{sockets[1], POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&peer, 1, 250);
    } while (poll_result < 0 && errno == EINTR);
    check(poll_result == 0,
          "f32 socket-send delivered bytes before rejection");

    eshkol_tagged_value_t int_descriptor{};
    int_descriptor.type = ESHKOL_VALUE_INT64;
    int_descriptor.data.int_val = sockets[0];
    char int_payload[] = "INT";
    eshkol_tagged_value_t int_data{};
    int_data.type = ESHKOL_VALUE_HEAP_PTR;
    int_data.flags = 0x01;
    int_data.data.ptr_val = reinterpret_cast<uintptr_t>(int_payload);
    eshkol_tagged_value_t int_result{};
    eshkol_builtin_socket_send(&int_result, &int_descriptor, &int_data);
    pollfd control_peer{sockets[1], POLLIN, 0};
    int control_poll_result = -1;
    do {
        control_poll_result = poll(&control_peer, 1, 250);
    } while (control_poll_result < 0 && errno == EINTR);
    char control_received[3] = {};
    ssize_t control_received_count = -1;
    if (control_poll_result == 1 && (control_peer.revents & POLLIN) != 0) {
        do {
            control_received_count =
                recv(sockets[1], control_received, sizeof(control_received),
                     MSG_WAITALL);
        } while (control_received_count < 0 && errno == EINTR);
    }
    check(int_result.type == ESHKOL_VALUE_INT64 &&
              int_result.data.int_val == 3 &&
              control_received_count == 3 &&
              std::memcmp(control_received, "INT",
                          sizeof(control_received)) == 0,
          "INT64 socket-send did not preserve same-pair usability");
    close(sockets[0]);
    close(sockets[1]);
    munmap(mapping, sizeof(*fixture));
}

void expect_socket_send_control(bool raw_double) {
    int sockets[2] = {-1, -1};
    check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "could not create socket-send control pair");
    if (sockets[0] < 0 || sockets[1] < 0) return;

    eshkol_tagged_value_t descriptor{};
    descriptor.type = raw_double ? ESHKOL_VALUE_DOUBLE : ESHKOL_VALUE_INT64;
    if (raw_double) {
        descriptor.flags = ESHKOL_VALUE_INEXACT_FLAG;
        descriptor.data.raw_val = static_cast<uint64_t>(sockets[0]);
    } else {
        descriptor.data.int_val = sockets[0];
    }
    char payload[] = "CTL";
    eshkol_tagged_value_t data{};
    data.type = ESHKOL_VALUE_HEAP_PTR;
    data.flags = 0x01;
    data.data.ptr_val = reinterpret_cast<uintptr_t>(payload);
    eshkol_tagged_value_t result{};
    eshkol_builtin_socket_send(&result, &descriptor, &data);

    pollfd peer{sockets[1], POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&peer, 1, 250);
    } while (poll_result < 0 && errno == EINTR);
    char received[3] = {};
    ssize_t received_count = -1;
    if (poll_result == 1 && (peer.revents & POLLIN) != 0) {
        do {
            received_count = recv(sockets[1], received, sizeof(received),
                                  MSG_WAITALL);
        } while (received_count < 0 && errno == EINTR);
    }
    close(sockets[0]);
    close(sockets[1]);

    check(result.type == ESHKOL_VALUE_INT64 && result.data.int_val == 3 &&
              received_count == 3 &&
              std::memcmp(received, "CTL", sizeof(received)) == 0,
          raw_double ? "historical raw DOUBLE socket-send behavior changed"
                     : "INT64 socket-send behavior changed");
}

bool make_queued_socket_pair(int sockets[2], const char* payload, size_t size) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;
    size_t sent_total = 0;
    while (sent_total < size) {
        ssize_t sent = -1;
        do {
            sent = send(sockets[1], payload + sent_total, size - sent_total, 0);
        } while (sent < 0 && errno == EINTR);
        if (sent <= 0) break;
        sent_total += static_cast<size_t>(sent);
    }
    if (sent_total == size) return true;
    close(sockets[0]);
    close(sockets[1]);
    sockets[0] = sockets[1] = -1;
    return false;
}

struct SocketRecvFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
    eshkol_tagged_value_t other;
};

void expect_socket_recv_rejection(bool descriptor_position, bool malformed) {
    void* mapping = mmap(nullptr, sizeof(SocketRecvFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate socket-recv fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<SocketRecvFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);

    int sockets[2] = {-1, -1};
    check(make_queued_socket_pair(sockets, "ABCDE", 5),
          "could not create queued socket-recv rejection pair");
    if (sockets[0] < 0 || sockets[1] < 0) {
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const uint32_t input_bits = descriptor_position
                                    ? static_cast<uint32_t>(sockets[0])
                                    : UINT32_C(3);
    check(eshkol_value_f32_from_bits_v1(&fixture->input, input_bits) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct socket-recv f32 input");
    if (malformed) fixture->input.reserved = 1;
    fixture->other.type = ESHKOL_VALUE_INT64;
    fixture->other.data.int_val = descriptor_position ? 5 : sockets[0];
    const int flags_before = fcntl(sockets[0], F_GETFL, 0);

    int stderr_pipe[2] = {-1, -1};
    if (pipe(stderr_pipe) != 0) {
        check(false, "could not create socket-recv diagnostic pipe");
        close(sockets[0]);
        close(sockets[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork socket-recv rejection test");
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
        close(sockets[0]);
        close(sockets[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    if (child == 0) {
        close(stderr_pipe[0]);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stderr_pipe[1]);
        if (descriptor_position) {
            eshkol_builtin_socket_recv(&fixture->output, &fixture->input,
                                       &fixture->other);
        } else {
            eshkol_builtin_socket_recv(&fixture->output, &fixture->other,
                                       &fixture->input);
        }
        _exit(99);
    }

    close(stderr_pipe[1]);
    std::string observed;
    char diagnostic_buffer[256];
    ssize_t diagnostic_count = 0;
    while ((diagnostic_count = read(stderr_pipe[0], diagnostic_buffer,
                                    sizeof(diagnostic_buffer))) > 0) {
        observed.append(diagnostic_buffer,
                        static_cast<size_t>(diagnostic_count));
    }
    close(stderr_pipe[0]);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}

    check(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          descriptor_position
              ? malformed
                    ? "malformed f32 socket-recv descriptor did not fail explicitly"
                    : "canonical f32 socket-recv descriptor did not fail explicitly"
              : malformed
                    ? "malformed f32 socket-recv maximum did not fail explicitly"
                    : "canonical f32 socket-recv maximum did not fail explicitly");
    check(observed.find(
              "Type error in system integer/resource argument: expected non-float32 value") !=
              std::string::npos,
          "socket-recv rejection diagnostic changed");
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "socket-recv mutated output before rejection");
    const int flags_after = fcntl(sockets[0], F_GETFL, 0);
    check(flags_before >= 0 && flags_after == flags_before,
          "f32 socket-recv changed receiver flags before rejection");

    eshkol_tagged_value_t int_descriptor{};
    int_descriptor.type = ESHKOL_VALUE_INT64;
    int_descriptor.data.int_val = sockets[0];
    eshkol_tagged_value_t int_maximum{};
    int_maximum.type = ESHKOL_VALUE_INT64;
    int_maximum.data.int_val = 5;
    eshkol_tagged_value_t int_result{};
    eshkol_builtin_socket_recv(&int_result, &int_descriptor, &int_maximum);
    check(int_result.type == ESHKOL_VALUE_HEAP_PTR &&
              int_result.data.ptr_val != 0 &&
              std::memcmp(
                  reinterpret_cast<const void*>(int_result.data.ptr_val),
                  "ABCDE", 5) == 0 &&
              reinterpret_cast<const char*>(int_result.data.ptr_val)[5] == '\0',
          descriptor_position
              ? "f32 socket-recv descriptor consumed queued bytes before rejection"
              : "f32 socket-recv maximum consumed queued bytes before rejection");
    close(sockets[0]);
    close(sockets[1]);
    munmap(mapping, sizeof(*fixture));
}

enum class SocketRecvControlKind {
    Int64,
    RawDoubleDescriptor,
    RawDoubleMaximum
};

void expect_socket_recv_control(SocketRecvControlKind kind) {
    int sockets[2] = {-1, -1};
    check(make_queued_socket_pair(sockets, "ABCDEF", 6),
          "could not create queued socket-recv control pair");
    if (sockets[0] < 0 || sockets[1] < 0) return;

    eshkol_tagged_value_t descriptor{};
    descriptor.type = kind == SocketRecvControlKind::RawDoubleDescriptor
                          ? ESHKOL_VALUE_DOUBLE
                          : ESHKOL_VALUE_INT64;
    if (descriptor.type == ESHKOL_VALUE_DOUBLE) {
        descriptor.flags = ESHKOL_VALUE_INEXACT_FLAG;
        descriptor.data.raw_val = static_cast<uint64_t>(sockets[0]);
    } else {
        descriptor.data.int_val = sockets[0];
    }
    eshkol_tagged_value_t maximum{};
    maximum.type = kind == SocketRecvControlKind::RawDoubleMaximum
                       ? ESHKOL_VALUE_DOUBLE
                       : ESHKOL_VALUE_INT64;
    if (maximum.type == ESHKOL_VALUE_DOUBLE) {
        maximum.flags = ESHKOL_VALUE_INEXACT_FLAG;
        maximum.data.raw_val = 3;
    } else {
        maximum.data.int_val = 3;
    }

    const int flags_before = fcntl(sockets[0], F_GETFL, 0);
    eshkol_tagged_value_t result{};
    eshkol_builtin_socket_recv(&result, &descriptor, &maximum);
    eshkol_tagged_value_t int_descriptor{};
    int_descriptor.type = ESHKOL_VALUE_INT64;
    int_descriptor.data.int_val = sockets[0];
    eshkol_tagged_value_t int_maximum{};
    int_maximum.type = ESHKOL_VALUE_INT64;
    int_maximum.data.int_val = 3;
    eshkol_tagged_value_t remainder{};
    eshkol_builtin_socket_recv(&remainder, &int_descriptor, &int_maximum);
    const int flags_after = fcntl(sockets[0], F_GETFL, 0);
    close(sockets[0]);
    close(sockets[1]);

    const char* label = kind == SocketRecvControlKind::Int64
                            ? "INT64 socket-recv behavior changed"
                            : kind == SocketRecvControlKind::RawDoubleDescriptor
                                  ? "historical raw DOUBLE socket-recv descriptor behavior changed"
                                  : "historical raw DOUBLE socket-recv maximum behavior changed";
    check(result.type == ESHKOL_VALUE_HEAP_PTR &&
              result.data.ptr_val != 0 &&
              std::memcmp(reinterpret_cast<const void*>(result.data.ptr_val),
                          "ABC", 3) == 0 &&
              reinterpret_cast<const char*>(result.data.ptr_val)[3] == '\0' &&
              remainder.type == ESHKOL_VALUE_HEAP_PTR &&
              remainder.data.ptr_val != 0 &&
              std::memcmp(
                  reinterpret_cast<const void*>(remainder.data.ptr_val),
                  "DEF", 3) == 0 &&
              reinterpret_cast<const char*>(remainder.data.ptr_val)[3] == '\0' &&
              flags_before >= 0 && flags_after == flags_before,
          label);
}

int close_socket_pair(int sockets[2]) {
    int closed = 0;
    for (int index = 0; index < 2; ++index) {
        int& fd = sockets[index];
        if (fd >= 0 && close(fd) == 0) ++closed;
        fd = -1;
    }
    return closed;
}

bool socket_marker_round_trip(const int sockets[2]) {
    static constexpr char kMarker[] = "CHK";
    int send_flags = 0;
#ifdef MSG_NOSIGNAL
    send_flags |= MSG_NOSIGNAL;
#endif
    size_t offset = 0;
    while (offset < sizeof(kMarker) - 1) {
        const ssize_t sent = send(sockets[0], kMarker + offset,
                                  sizeof(kMarker) - 1 - offset, send_flags);
        if (sent > 0) {
            offset += static_cast<size_t>(sent);
            continue;
        }
        if (sent < 0 && errno == EINTR) continue;
        return false;
    }

    pollfd ready{sockets[1], POLLIN, 0};
    int poll_result = -1;
    do {
        poll_result = poll(&ready, 1, 250);
    } while (poll_result < 0 && errno == EINTR);
    if (poll_result != 1 || (ready.revents & POLLIN) == 0) return false;

    char observed[sizeof(kMarker) - 1] = {};
    size_t received_total = 0;
    while (received_total < sizeof(observed)) {
        const ssize_t received = recv(sockets[1], observed + received_total,
                                      sizeof(observed) - received_total, 0);
        if (received > 0) {
            received_total += static_cast<size_t>(received);
            continue;
        }
        if (received < 0 && errno == EINTR) continue;
        return false;
    }
    return std::memcmp(observed, kMarker, sizeof(observed)) == 0;
}

struct SocketCloseFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
};

void expect_socket_close_rejection(bool malformed) {
    int sockets[2] = {-1, -1};
    check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "could not create socket-close rejection pair");
    if (sockets[0] < 0 || sockets[1] < 0) return;

    void* mapping = mmap(nullptr, sizeof(SocketCloseFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate socket-close fixture");
    if (mapping == MAP_FAILED) {
        (void)close_socket_pair(sockets);
        return;
    }
    auto* fixture = static_cast<SocketCloseFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);
    check(eshkol_value_f32_from_bits_v1(
              &fixture->input, static_cast<uint32_t>(sockets[0])) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct socket-close f32 descriptor");
    if (malformed) fixture->input.reserved = 1;

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_builtin_socket_close(&fixture->output, &fixture->input);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(transferred == 1,
          malformed ? "malformed f32 socket-close did not raise"
                    : "canonical f32 socket-close did not raise");
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "socket-close rejection exception changed");
    eshkol_clear_current_exception();

    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "socket-close mutated output before rejection");
    check(fcntl(sockets[0], F_GETFD) >= 0,
          "f32 socket-close closed descriptor before rejection");
    check(socket_marker_round_trip(sockets),
          "f32 socket-close left descriptor unusable after rejection");

    eshkol_tagged_value_t descriptor{};
    descriptor.type = ESHKOL_VALUE_INT64;
    descriptor.data.int_val = sockets[0];
    eshkol_tagged_value_t result{};
    eshkol_builtin_socket_close(&result, &descriptor);
    errno = 0;
    const bool closed = fcntl(sockets[0], F_GETFD) == -1 && errno == EBADF;
    check(result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 1 && closed,
          "INT64 socket-close did not close descriptor after f32 rejection");
    if (closed) sockets[0] = -1;
    check(close_socket_pair(sockets) == 1,
          "socket-close rejection cleanup did not leave exactly the peer open");
    munmap(mapping, sizeof(*fixture));
}

void expect_socket_close_control(bool raw_double) {
    int sockets[2] = {-1, -1};
    check(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
          "could not create socket-close control pair");
    if (sockets[0] < 0 || sockets[1] < 0) return;

    eshkol_tagged_value_t descriptor{};
    descriptor.type = raw_double ? ESHKOL_VALUE_DOUBLE : ESHKOL_VALUE_INT64;
    if (raw_double) {
        descriptor.flags = ESHKOL_VALUE_INEXACT_FLAG;
        descriptor.data.raw_val = static_cast<uint64_t>(sockets[0]);
    } else {
        descriptor.data.int_val = sockets[0];
    }
    eshkol_tagged_value_t result{};
    eshkol_builtin_socket_close(&result, &descriptor);
    errno = 0;
    const bool closed = fcntl(sockets[0], F_GETFD) == -1 && errno == EBADF;
    if (closed) sockets[0] = -1;
    const int cleanup = close_socket_pair(sockets);
    check(result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 1 &&
              closed && cleanup == 1,
          raw_double ? "historical raw DOUBLE socket-close behavior changed"
                     : "INT64 socket-close behavior changed");
}

bool create_test_pty(int& master, int& slave) {
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

bool read_pty_output(int master, std::string& output) {
    char buffer[64];
    for (;;) {
        const ssize_t count = read(master, buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        return count == 0 || (count < 0 && errno == EIO);
    }
}

bool restore_test_stdout(int saved_stdout) {
    int result = -1;
    do {
        result = dup2(saved_stdout, STDOUT_FILENO);
    } while (result < 0 && errno == EINTR);
    close(saved_stdout);
    if (result >= 0) return true;
    close(STDOUT_FILENO);
    return false;
}

struct ScrollRegionFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
    eshkol_tagged_value_t other;
};

void expect_scroll_region_rejection(bool top_position, bool malformed) {
    void* mapping = mmap(nullptr, sizeof(ScrollRegionFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate scroll-region fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<ScrollRegionFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);
    check(eshkol_value_f32_from_bits_v1(&fixture->input,
                                        top_position ? 1 : 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct scroll-region f32 coordinate");
    if (malformed) fixture->input.reserved = 1;
    fixture->other.type = ESHKOL_VALUE_INT64;
    fixture->other.data.int_val = top_position ? 2 : 1;

    int master = -1;
    int slave = -1;
    check(create_test_pty(master, slave),
          "could not create scroll-region rejection PTY");
    if (master < 0 || slave < 0) {
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        check(false, "could not save stdout for scroll-region rejection");
        close(master);
        close(slave);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    std::fflush(stdout);
    if (dup2(slave, STDOUT_FILENO) < 0) {
        check(false, "could not redirect stdout for scroll-region rejection");
        close(saved_stdout);
        close(master);
        close(slave);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    close(slave);

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        if (top_position) {
            eshkol_builtin_term_set_scroll_region(
                &fixture->output, &fixture->input, &fixture->other);
        } else {
            eshkol_builtin_term_set_scroll_region(
                &fixture->output, &fixture->other, &fixture->input);
        }
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    std::fflush(stdout);
    const bool restored = restore_test_stdout(saved_stdout);
    std::string terminal_output;
    const bool terminal_read = read_pty_output(master, terminal_output);
    close(master);

    check(transferred == 1,
          top_position
              ? malformed
                    ? "malformed f32 scroll top did not raise"
                    : "canonical f32 scroll top did not raise"
              : malformed
                    ? "malformed f32 scroll bottom did not raise"
                    : "canonical f32 scroll bottom did not raise");
    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "scroll-region rejection exception changed");
    eshkol_clear_current_exception();
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "scroll-region mutated output before rejection");
    check(restored && terminal_read && terminal_output.empty(),
          "f32 scroll-region emitted terminal bytes before rejection");
    munmap(mapping, sizeof(*fixture));
}

enum class ScrollRegionControlKind { Int64, RawDoubleTop, RawDoubleBottom };

void expect_scroll_region_control(ScrollRegionControlKind kind) {
    int master = -1;
    int slave = -1;
    check(create_test_pty(master, slave),
          "could not create scroll-region control PTY");
    if (master < 0 || slave < 0) return;
    const int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0) {
        check(false, "could not save stdout for scroll-region control");
        close(master);
        close(slave);
        return;
    }
    std::fflush(stdout);
    if (dup2(slave, STDOUT_FILENO) < 0) {
        check(false, "could not redirect stdout for scroll-region control");
        close(saved_stdout);
        close(master);
        close(slave);
        return;
    }
    close(slave);

    eshkol_tagged_value_t top{};
    top.type = kind == ScrollRegionControlKind::RawDoubleTop
                   ? ESHKOL_VALUE_DOUBLE
                   : ESHKOL_VALUE_INT64;
    top.flags = top.type == ESHKOL_VALUE_DOUBLE ? ESHKOL_VALUE_INEXACT_FLAG
                                                : ESHKOL_VALUE_EXACT_FLAG;
    top.data.raw_val = 1;
    eshkol_tagged_value_t bottom{};
    bottom.type = kind == ScrollRegionControlKind::RawDoubleBottom
                      ? ESHKOL_VALUE_DOUBLE
                      : ESHKOL_VALUE_INT64;
    bottom.flags = bottom.type == ESHKOL_VALUE_DOUBLE
                       ? ESHKOL_VALUE_INEXACT_FLAG
                       : ESHKOL_VALUE_EXACT_FLAG;
    bottom.data.raw_val = 2;
    eshkol_tagged_value_t result{};
    eshkol_builtin_term_set_scroll_region(&result, &top, &bottom);
    std::fflush(stdout);
    const bool restored = restore_test_stdout(saved_stdout);

    std::string terminal_output;
    const bool terminal_read = read_pty_output(master, terminal_output);
    close(master);
    static constexpr char kExpected[] = "\033[1;2r";
    const char* label =
        kind == ScrollRegionControlKind::Int64
            ? "INT64 scroll-region behavior changed"
            : kind == ScrollRegionControlKind::RawDoubleTop
                  ? "historical raw DOUBLE scroll top behavior changed"
                  : "historical raw DOUBLE scroll bottom behavior changed";
    check(restored && result.type == ESHKOL_VALUE_BOOL &&
              result.data.raw_val == 1 && terminal_read &&
              terminal_output.size() == sizeof(kExpected) - 1 &&
              std::memcmp(terminal_output.data(), kExpected,
                          sizeof(kExpected) - 1) == 0,
          label);
}

bool write_watch_file(const char* path, const char* contents) {
    const int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    const size_t length = std::strlen(contents);
    size_t offset = 0;
    while (offset < length) {
        const ssize_t count = write(fd, contents + offset, length - offset);
        if (count > 0) {
            offset += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        close(fd);
        return false;
    }
    return close(fd) == 0;
}

int start_native_watcher(const char* path) {
    eshkol_tagged_value_t path_value{};
    path_value.type = ESHKOL_VALUE_HEAP_PTR;
    path_value.flags = 0x01;
    path_value.data.ptr_val = reinterpret_cast<uintptr_t>(path);
    eshkol_tagged_value_t callback{};
    callback.type = ESHKOL_VALUE_BOOL;
    eshkol_tagged_value_t result{};
    eshkol_builtin_fs_watch_native(&result, &path_value, &callback);
    return result.type == ESHKOL_VALUE_INT64 && result.data.int_val > 0
               ? static_cast<int>(result.data.int_val)
               : -1;
}

bool unwatch_native(int handle) {
    if (handle <= 0) return false;
    eshkol_tagged_value_t handle_value{};
    handle_value.type = ESHKOL_VALUE_INT64;
    handle_value.data.int_val = handle;
    eshkol_tagged_value_t result{};
    eshkol_builtin_fs_unwatch(&result, &handle_value);
    return result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 1;
}

bool watcher_has_no_event(int handle) {
    eshkol_tagged_value_t handle_value{};
    handle_value.type = ESHKOL_VALUE_INT64;
    handle_value.data.int_val = handle;
    eshkol_tagged_value_t result{};
    eshkol_builtin_fs_watch_poll(&result, &handle_value);
    return result.type == ESHKOL_VALUE_BOOL && result.data.raw_val == 0;
}

struct WatchPollFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
    char path[192];
};

void expect_watch_poll_rejection(bool malformed) {
    void* mapping = mmap(nullptr, sizeof(WatchPollFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate fs-watch fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<WatchPollFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    std::snprintf(fixture->path, sizeof(fixture->path),
                  "/tmp/eshkol-f32-watch-native-%ld-%d.txt",
                  static_cast<long>(getpid()), malformed ? 1 : 0);
    (void)unlink(fixture->path);
    check(write_watch_file(fixture->path, "a"),
          "could not create fs-watch rejection file");
    const int handle = start_native_watcher(fixture->path);
    check(handle > 0, "could not start fs-watch rejection watcher");
    if (handle <= 0) {
        (void)unlink(fixture->path);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    check(watcher_has_no_event(handle),
          "fs-watch rejection watcher lacked an initial snapshot");
    check(write_watch_file(fixture->path, "abcdef"),
          "could not mutate fs-watch rejection file");
    check(eshkol_value_f32_from_bits_v1(
              &fixture->input, static_cast<uint32_t>(handle)) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct fs-watch f32 handle");
    if (malformed) fixture->input.reserved = 1;
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_builtin_fs_watch_poll(&fixture->output, &fixture->input);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(transferred == 1,
          malformed ? "malformed f32 fs-watch handle did not raise"
                    : "canonical f32 fs-watch handle did not raise");
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "fs-watch rejection exception changed");
    eshkol_clear_current_exception();
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "fs-watch mutated output before rejection");

    eshkol_tagged_value_t int_handle{};
    int_handle.type = ESHKOL_VALUE_INT64;
    int_handle.data.int_val = handle;
    eshkol_tagged_value_t event{};
    eshkol_builtin_fs_watch_poll(&event, &int_handle);
    char expected[256] = {};
    const int expected_length = std::snprintf(
        expected, sizeof(expected), "change\t%s", fixture->path);
    check(expected_length > 0 &&
              static_cast<size_t>(expected_length) < sizeof(expected) &&
              event.type == ESHKOL_VALUE_HEAP_PTR &&
              event.data.ptr_val != 0 &&
              std::strcmp(reinterpret_cast<const char*>(event.data.ptr_val),
                          expected) == 0,
          "f32 fs-watch rejection consumed the pending change");
    check(watcher_has_no_event(handle),
          "supported fs-watch recovery did not advance the snapshot");
    check(unwatch_native(handle),
          "could not unwatch fs-watch rejection watcher");
    check(unlink(fixture->path) == 0,
          "could not remove fs-watch rejection file");
    munmap(mapping, sizeof(*fixture));
}

void expect_watch_poll_control(bool raw_double) {
    char path[192] = {};
    std::snprintf(path, sizeof(path),
                  "/tmp/eshkol-f32-watch-control-%ld-%d.txt",
                  static_cast<long>(getpid()), raw_double ? 1 : 0);
    (void)unlink(path);
    check(write_watch_file(path, "x"),
          "could not create fs-watch control file");
    const int handle = start_native_watcher(path);
    check(handle > 0, "could not start fs-watch control watcher");
    if (handle <= 0) {
        (void)unlink(path);
        return;
    }
    check(watcher_has_no_event(handle),
          "fs-watch control lacked an initial snapshot");
    check(write_watch_file(path, "xyz123"),
          "could not mutate fs-watch control file");

    eshkol_tagged_value_t handle_value{};
    handle_value.type = raw_double ? ESHKOL_VALUE_DOUBLE
                                   : ESHKOL_VALUE_INT64;
    handle_value.flags = raw_double ? ESHKOL_VALUE_INEXACT_FLAG
                                    : ESHKOL_VALUE_EXACT_FLAG;
    handle_value.data.raw_val = static_cast<uint64_t>(handle);
    eshkol_tagged_value_t event{};
    eshkol_builtin_fs_watch_poll(&event, &handle_value);
    char expected[256] = {};
    const int expected_length =
        std::snprintf(expected, sizeof(expected), "change\t%s", path);
    const char* label = raw_double
                            ? "historical raw DOUBLE fs-watch behavior changed"
                            : "INT64 fs-watch behavior changed";
    const bool after_empty = watcher_has_no_event(handle);
    check(expected_length > 0 &&
              static_cast<size_t>(expected_length) < sizeof(expected) &&
              event.type == ESHKOL_VALUE_HEAP_PTR &&
              event.data.ptr_val != 0 &&
              std::strcmp(reinterpret_cast<const char*>(event.data.ptr_val),
                          expected) == 0 &&
              after_empty,
          label);
    check(unwatch_native(handle), "could not unwatch fs-watch control watcher");
    check(unlink(path) == 0, "could not remove fs-watch control file");
}

void expect_unwatch_rejection(bool malformed) {
    void* mapping = mmap(nullptr, sizeof(WatchPollFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate fs-unwatch fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<WatchPollFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    std::snprintf(fixture->path, sizeof(fixture->path),
                  "/tmp/eshkol-f32-unwatch-native-%ld-%d.txt",
                  static_cast<long>(getpid()), malformed ? 1 : 0);
    (void)unlink(fixture->path);
    check(write_watch_file(fixture->path, "a"),
          "could not create fs-unwatch rejection file");
    const int handle = start_native_watcher(fixture->path);
    check(handle > 0, "could not start fs-unwatch rejection watcher");
    if (handle <= 0) {
        (void)unlink(fixture->path);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    check(watcher_has_no_event(handle),
          "fs-unwatch rejection watcher lacked an initial snapshot");
    check(write_watch_file(fixture->path, "abcdef"),
          "could not mutate fs-unwatch rejection file");
    check(eshkol_value_f32_from_bits_v1(
              &fixture->input, static_cast<uint32_t>(handle)) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct fs-unwatch f32 handle");
    if (malformed) fixture->input.reserved = 1;
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_builtin_fs_unwatch(&fixture->output, &fixture->input);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(transferred == 1,
          malformed ? "malformed f32 fs-unwatch handle did not raise"
                    : "canonical f32 fs-unwatch handle did not raise");
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "fs-unwatch rejection exception changed");
    eshkol_clear_current_exception();
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "fs-unwatch mutated output before rejection");

    eshkol_tagged_value_t int_handle{};
    int_handle.type = ESHKOL_VALUE_INT64;
    int_handle.data.int_val = handle;
    eshkol_tagged_value_t event{};
    eshkol_builtin_fs_watch_poll(&event, &int_handle);
    char expected[256] = {};
    const int expected_length = std::snprintf(
        expected, sizeof(expected), "change\t%s", fixture->path);
    const bool after_empty = watcher_has_no_event(handle);
    eshkol_tagged_value_t first_unwatch{};
    eshkol_builtin_fs_unwatch(&first_unwatch, &int_handle);
    eshkol_tagged_value_t second_unwatch{};
    eshkol_builtin_fs_unwatch(&second_unwatch, &int_handle);
    check(expected_length > 0 &&
              static_cast<size_t>(expected_length) < sizeof(expected) &&
              event.type == ESHKOL_VALUE_HEAP_PTR &&
              event.data.ptr_val != 0 &&
              std::strcmp(reinterpret_cast<const char*>(event.data.ptr_val),
                          expected) == 0 &&
              after_empty && first_unwatch.type == ESHKOL_VALUE_BOOL &&
              first_unwatch.data.raw_val == 1 &&
              second_unwatch.type == ESHKOL_VALUE_BOOL &&
              second_unwatch.data.raw_val == 0,
          "f32 fs-unwatch rejection changed watcher lifetime or snapshot");
    check(unlink(fixture->path) == 0,
          "could not remove fs-unwatch rejection file");
    munmap(mapping, sizeof(*fixture));
}

void expect_unwatch_control(bool raw_double) {
    char path[192] = {};
    std::snprintf(path, sizeof(path),
                  "/tmp/eshkol-f32-unwatch-control-%ld-%d.txt",
                  static_cast<long>(getpid()), raw_double ? 1 : 0);
    (void)unlink(path);
    check(write_watch_file(path, "x"),
          "could not create fs-unwatch control file");
    const int handle = start_native_watcher(path);
    check(handle > 0, "could not start fs-unwatch control watcher");
    if (handle <= 0) {
        (void)unlink(path);
        return;
    }
    check(watcher_has_no_event(handle),
          "fs-unwatch control lacked an initial snapshot");
    check(write_watch_file(path, "xyz123"),
          "could not mutate fs-unwatch control file");

    eshkol_tagged_value_t handle_value{};
    handle_value.type = raw_double ? ESHKOL_VALUE_DOUBLE
                                   : ESHKOL_VALUE_INT64;
    handle_value.flags = raw_double ? ESHKOL_VALUE_INEXACT_FLAG
                                    : ESHKOL_VALUE_EXACT_FLAG;
    handle_value.data.raw_val = static_cast<uint64_t>(handle);
    eshkol_tagged_value_t first_unwatch{};
    eshkol_builtin_fs_unwatch(&first_unwatch, &handle_value);

    eshkol_tagged_value_t int_handle{};
    int_handle.type = ESHKOL_VALUE_INT64;
    int_handle.data.int_val = handle;
    eshkol_tagged_value_t inactive_poll{};
    eshkol_builtin_fs_watch_poll(&inactive_poll, &int_handle);
    eshkol_tagged_value_t second_unwatch{};
    eshkol_builtin_fs_unwatch(&second_unwatch, &int_handle);
    const char* label = raw_double
                            ? "historical raw DOUBLE fs-unwatch behavior changed"
                            : "INT64 fs-unwatch behavior changed";
    check(first_unwatch.type == ESHKOL_VALUE_BOOL &&
              first_unwatch.data.raw_val == 1 &&
              inactive_poll.type == ESHKOL_VALUE_BOOL &&
              inactive_poll.data.raw_val == 0 &&
              second_unwatch.type == ESHKOL_VALUE_BOOL &&
              second_unwatch.data.raw_val == 0,
          label);
    check(unlink(path) == 0, "could not remove fs-unwatch control file");
}

struct TruncateDisplayFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t maximum;
    eshkol_tagged_value_t input;
    eshkol_tagged_value_t suffix;
    char input_text[7];
    char suffix_text[3];
};

void initialize_truncate_fixture(TruncateDisplayFixture* fixture) {
    std::memset(fixture, 0, sizeof(*fixture));
    std::memcpy(fixture->input_text, "abcdef", sizeof(fixture->input_text));
    std::memcpy(fixture->suffix_text, "..", sizeof(fixture->suffix_text));
    fixture->input.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->input.flags = 0x01;
    fixture->input.data.ptr_val =
        reinterpret_cast<uintptr_t>(fixture->input_text);
    fixture->suffix.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->suffix.flags = 0x01;
    fixture->suffix.data.ptr_val =
        reinterpret_cast<uintptr_t>(fixture->suffix_text);
}

void expect_truncate_display_rejection(bool malformed) {
    void* mapping = mmap(nullptr, sizeof(TruncateDisplayFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate truncate-display fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<TruncateDisplayFixture*>(mapping);
    initialize_truncate_fixture(fixture);
    check(eshkol_value_f32_from_bits_v1(&fixture->maximum, 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct truncate-display f32 maximum");
    if (malformed) fixture->maximum.reserved = 1;
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_builtin_string_truncate_display(
            &fixture->output, &fixture->input, &fixture->maximum,
            &fixture->suffix);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(transferred == 1,
          malformed ? "malformed f32 truncate maximum did not raise"
                    : "canonical f32 truncate maximum did not raise");
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "truncate-display rejection exception changed");
    eshkol_clear_current_exception();
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "truncate-display mutated output before rejection");
    munmap(mapping, sizeof(*fixture));
}

enum class TruncateControlKind { Int64, RawDouble, UnchangedInt64 };

void expect_truncate_display_control(TruncateControlKind kind) {
    TruncateDisplayFixture fixture{};
    initialize_truncate_fixture(&fixture);
    fixture.maximum.type = kind == TruncateControlKind::RawDouble
                               ? ESHKOL_VALUE_DOUBLE
                               : ESHKOL_VALUE_INT64;
    fixture.maximum.flags = kind == TruncateControlKind::RawDouble
                                ? ESHKOL_VALUE_INEXACT_FLAG
                                : ESHKOL_VALUE_EXACT_FLAG;
    fixture.maximum.data.raw_val =
        kind == TruncateControlKind::UnchangedInt64 ? 6 : 2;
    eshkol_builtin_string_truncate_display(
        &fixture.output, &fixture.input, &fixture.maximum, &fixture.suffix);
    const char* expected = kind == TruncateControlKind::UnchangedInt64
                               ? "abcdef"
                               : "..";
    const char* label =
        kind == TruncateControlKind::Int64
            ? "INT64 truncate-display behavior changed"
            : kind == TruncateControlKind::RawDouble
                  ? "historical raw DOUBLE truncate-display behavior changed"
                  : "unchanged-input truncate-display behavior changed";
    check(fixture.output.type == ESHKOL_VALUE_HEAP_PTR &&
              fixture.output.data.ptr_val != 0 &&
              std::strcmp(
                  reinterpret_cast<const char*>(fixture.output.data.ptr_val),
                  expected) == 0,
          label);
}

void expect_truncate_display_input_precedence() {
    TruncateDisplayFixture fixture{};
    initialize_truncate_fixture(&fixture);
    fixture.input = {};
    check(eshkol_value_f32_from_bits_v1(&fixture.maximum, 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct truncate-display precedence maximum");
    eshkol_builtin_string_truncate_display(
        &fixture.output, &fixture.input, &fixture.maximum, &fixture.suffix);
    check(fixture.output.type == ESHKOL_VALUE_HEAP_PTR &&
              fixture.output.data.ptr_val != 0 &&
              reinterpret_cast<const char*>(fixture.output.data.ptr_val)[0] ==
                  '\0',
          "truncate-display input validation precedence changed");
}

struct StringIndexFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t start;
    eshkol_tagged_value_t haystack;
    eshkol_tagged_value_t needle;
    char haystack_text[7];
    char needle_text[3];
};

void initialize_string_index_fixture(StringIndexFixture* fixture) {
    std::memset(fixture, 0, sizeof(*fixture));
    std::memcpy(fixture->haystack_text, "abcabc",
                sizeof(fixture->haystack_text));
    std::memcpy(fixture->needle_text, "bc", sizeof(fixture->needle_text));
    fixture->haystack.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->haystack.flags = 0x01;
    fixture->haystack.data.ptr_val =
        reinterpret_cast<uintptr_t>(fixture->haystack_text);
    fixture->needle.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->needle.flags = 0x01;
    fixture->needle.data.ptr_val =
        reinterpret_cast<uintptr_t>(fixture->needle_text);
}

void expect_string_index_rejection(bool malformed) {
    void* mapping = mmap(nullptr, sizeof(StringIndexFixture),
                         PROT_READ | PROT_WRITE,
                         MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    check(mapping != MAP_FAILED, "could not allocate string-index fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<StringIndexFixture*>(mapping);
    initialize_string_index_fixture(fixture);
    check(eshkol_value_f32_from_bits_v1(&fixture->start, 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct string-index f32 start");
    if (malformed) fixture->start.reserved = 1;
    fixture->output.type = ESHKOL_VALUE_INT64;
    fixture->output.flags = ESHKOL_VALUE_EXACT_FLAG;
    fixture->output.data.int_val = INT64_C(0x123456789abcdef);

    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_builtin_string_index_of(
            &fixture->output, &fixture->haystack, &fixture->needle,
            &fixture->start);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();

    static constexpr char kDiagnostic[] =
        "Type error in system integer/resource argument: expected non-float32 value";
    check(transferred == 1,
          malformed ? "malformed f32 string-index start did not raise"
                    : "canonical f32 string-index start did not raise");
    check(g_current_exception != nullptr &&
              g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
              g_current_exception->message != nullptr &&
              std::strcmp(g_current_exception->message, kDiagnostic) == 0,
          "string-index rejection exception changed");
    eshkol_clear_current_exception();
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "string-index mutated output before rejection");
    munmap(mapping, sizeof(*fixture));
}

enum class StringIndexControlKind {
    StartZero,
    StartTwo,
    RawDouble,
    CharNeedle,
    EmptyNeedle
};

void expect_string_index_control(StringIndexControlKind kind) {
    StringIndexFixture fixture{};
    initialize_string_index_fixture(&fixture);
    fixture.start.type = kind == StringIndexControlKind::RawDouble
                             ? ESHKOL_VALUE_DOUBLE
                             : ESHKOL_VALUE_INT64;
    fixture.start.flags = kind == StringIndexControlKind::RawDouble
                              ? ESHKOL_VALUE_INEXACT_FLAG
                              : ESHKOL_VALUE_EXACT_FLAG;
    fixture.start.data.raw_val = kind == StringIndexControlKind::StartZero ||
                                         kind == StringIndexControlKind::CharNeedle
                                     ? 0
                                     : 2;
    if (kind == StringIndexControlKind::CharNeedle) {
        fixture.needle.type = ESHKOL_VALUE_CHAR;
        fixture.needle.flags = ESHKOL_VALUE_EXACT_FLAG;
        fixture.needle.data.raw_val = static_cast<uint64_t>('b');
    } else if (kind == StringIndexControlKind::EmptyNeedle) {
        fixture.needle_text[0] = '\0';
    }
    eshkol_builtin_string_index_of(
        &fixture.output, &fixture.haystack, &fixture.needle, &fixture.start);
    const int64_t expected =
        kind == StringIndexControlKind::StartZero ||
                kind == StringIndexControlKind::CharNeedle
            ? 1
            : kind == StringIndexControlKind::EmptyNeedle ? 2 : 4;
    const char* label =
        kind == StringIndexControlKind::StartZero
            ? "INT64 zero-start string-index behavior changed"
            : kind == StringIndexControlKind::StartTwo
                  ? "INT64 nonzero-start string-index behavior changed"
                  : kind == StringIndexControlKind::RawDouble
                        ? "historical raw DOUBLE string-index behavior changed"
                        : kind == StringIndexControlKind::CharNeedle
                              ? "character-needle string-index behavior changed"
                              : "empty-needle string-index behavior changed";
    check(fixture.output.type == ESHKOL_VALUE_INT64 &&
              fixture.output.data.int_val == expected,
          label);
}

void expect_string_index_input_precedence() {
    StringIndexFixture fixture{};
    initialize_string_index_fixture(&fixture);
    check(eshkol_value_f32_from_bits_v1(&fixture.start, 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct string-index precedence start");
    fixture.haystack = {};
    eshkol_builtin_string_index_of(
        &fixture.output, &fixture.haystack, &fixture.needle, &fixture.start);
    const bool invalid_haystack = fixture.output.type == ESHKOL_VALUE_BOOL &&
                                  fixture.output.data.raw_val == 0;
    initialize_string_index_fixture(&fixture);
    check(eshkol_value_f32_from_bits_v1(&fixture.start, 2) ==
              ESHKOL_VALUE_F32_OK,
          "could not reconstruct string-index precedence start");
    fixture.needle = {};
    eshkol_builtin_string_index_of(
        &fixture.output, &fixture.haystack, &fixture.needle, &fixture.start);
    check(invalid_haystack && fixture.output.type == ESHKOL_VALUE_BOOL &&
              fixture.output.data.raw_val == 0,
          "string-index text validation precedence changed");
}
#endif

}  // namespace

int main() {
#if !defined(_WIN32)
    expect_rejection(BuiltinKind::FormatRelative, true,
                     "Type error in format-relative: expected canonical float32",
                     "malformed format-relative input did not fail explicitly");
    expect_rejection(BuiltinKind::RegexFree, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed resource handle did not fail explicitly");

    eshkol_tagged_value_t ignored{};
    eshkol_tagged_value_t inhibitor{};
    eshkol_builtin_prevent_sleep(&inhibitor, &ignored);
    check(inhibitor.type == ESHKOL_VALUE_INT64 && inhibitor.data.int_val == 1,
          "could not allocate sleep-inhibitor handle 1");
    expect_rejection(BuiltinKind::AllowSleep, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 sleep handle did not fail explicitly");
    expect_rejection(BuiltinKind::AllowSleep, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 sleep handle did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessWait, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process handle did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessWait, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process handle did not fail explicitly");
    expect_rejection(BuiltinKind::PollFdDescriptor, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 poll descriptor did not fail explicitly");
    expect_rejection(BuiltinKind::PollFdDescriptor, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 poll descriptor did not fail explicitly");
    expect_rejection(BuiltinKind::PollFdTimeout, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 poll timeout did not fail explicitly");
    expect_rejection(BuiltinKind::PollFdTimeout, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 poll timeout did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillPid, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-kill PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillPid, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-kill PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillSignal, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-kill signal did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillSignal, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-kill signal did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillTreePid, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-kill-tree PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillTreePid, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-kill-tree PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillTreeSignal, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-kill-tree signal did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessKillTreeSignal, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-kill-tree signal did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessSetpgidPid, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-setpgid PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessSetpgidPid, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-setpgid PID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessSetpgidGroup, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-setpgid PGID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessSetpgidGroup, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-setpgid PGID did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessReadDescriptor, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-read descriptor did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessReadDescriptor, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-read descriptor did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessReadMaximum, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 process-read maximum did not fail explicitly");
    expect_rejection(BuiltinKind::ProcessReadMaximum, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 process-read maximum did not fail explicitly");
    expect_rejection(BuiltinKind::FileChmod, false,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "canonical f32 file mode did not fail explicitly");
    expect_rejection(BuiltinKind::FileChmod, true,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed f32 file mode did not fail explicitly");
    expect_socket_send_rejection(false);
    expect_socket_send_rejection(true);
    expect_socket_recv_rejection(true, false);
    expect_socket_recv_rejection(true, true);
    expect_socket_recv_rejection(false, false);
    expect_socket_recv_rejection(false, true);
    arena_t* const shared_arena = get_global_arena_shared();
    check(shared_arena != nullptr,
          "system exception arena initialization failed");
    if (shared_arena) {
        __repl_shared_arena.store(shared_arena);
        expect_socket_close_rejection(false);
        expect_socket_close_rejection(true);
        expect_scroll_region_rejection(true, false);
        expect_scroll_region_rejection(true, true);
        expect_scroll_region_rejection(false, false);
        expect_scroll_region_rejection(false, true);
        expect_watch_poll_rejection(false);
        expect_watch_poll_rejection(true);
        expect_unwatch_rejection(false);
        expect_unwatch_rejection(true);
        expect_truncate_display_rejection(false);
        expect_truncate_display_rejection(true);
        expect_string_index_rejection(false);
        expect_string_index_rejection(true);
    }

    eshkol_tagged_value_t released{};
    eshkol_builtin_allow_sleep(&released, &inhibitor);
    check(released.type == ESHKOL_VALUE_BOOL && released.data.raw_val == 1,
          "INT64 sleep handle was not preserved after f32 rejection");

    eshkol_tagged_value_t inhibitor2{};
    eshkol_builtin_prevent_sleep(&inhibitor2, &ignored);
    check(inhibitor2.type == ESHKOL_VALUE_INT64 && inhibitor2.data.int_val == 2,
          "could not allocate sleep-inhibitor handle 2");
    eshkol_tagged_value_t historical_double{};
    historical_double.type = ESHKOL_VALUE_DOUBLE;
    historical_double.flags = ESHKOL_VALUE_INEXACT_FLAG;
    historical_double.data.raw_val = 2;
    eshkol_builtin_allow_sleep(&released, &historical_double);
    check(released.type == ESHKOL_VALUE_BOOL && released.data.raw_val == 1,
          "historical raw DOUBLE sleep-handle behavior changed");

    expect_process_kill_control(false, false);
    expect_process_kill_control(true, false);
    expect_process_kill_control(false, true);
    expect_process_kill_control(true, true);
    expect_process_setpgid_control(false);
    expect_process_setpgid_control(true);
    expect_process_read_control(ProcessReadControlKind::Int64);
    expect_process_read_control(ProcessReadControlKind::RawDoubleDescriptor);
    expect_process_read_control(ProcessReadControlKind::RawDoubleMaximum);
    expect_socket_send_control(false);
    expect_socket_send_control(true);
    expect_socket_recv_control(SocketRecvControlKind::Int64);
    expect_socket_recv_control(SocketRecvControlKind::RawDoubleDescriptor);
    expect_socket_recv_control(SocketRecvControlKind::RawDoubleMaximum);
    expect_socket_close_control(false);
    expect_socket_close_control(true);
    expect_scroll_region_control(ScrollRegionControlKind::Int64);
    expect_scroll_region_control(ScrollRegionControlKind::RawDoubleTop);
    expect_scroll_region_control(ScrollRegionControlKind::RawDoubleBottom);
    expect_watch_poll_control(false);
    expect_watch_poll_control(true);
    expect_unwatch_control(false);
    expect_unwatch_control(true);
    expect_truncate_display_control(TruncateControlKind::Int64);
    expect_truncate_display_control(TruncateControlKind::RawDouble);
    expect_truncate_display_control(TruncateControlKind::UnchangedInt64);
    expect_truncate_display_input_precedence();
    expect_string_index_control(StringIndexControlKind::StartZero);
    expect_string_index_control(StringIndexControlKind::StartTwo);
    expect_string_index_control(StringIndexControlKind::RawDouble);
    expect_string_index_control(StringIndexControlKind::CharNeedle);
    expect_string_index_control(StringIndexControlKind::EmptyNeedle);
    expect_string_index_input_precedence();

    const pid_t int_child = fork();
    if (int_child == 0) _exit(7);
    check(int_child > 0, "could not fork INT64 process-wait control");
    if (int_child > 0) {
        eshkol_tagged_value_t pid_value{};
        pid_value.type = ESHKOL_VALUE_INT64;
        pid_value.data.int_val = int_child;
        eshkol_tagged_value_t wait_result{};
        eshkol_builtin_process_wait(&wait_result, &pid_value);
        check(wait_result.type == ESHKOL_VALUE_INT64 &&
                  wait_result.data.int_val == 7,
              "INT64 process-wait behavior changed");
        int cleanup_status = 0;
        (void)waitpid(int_child, &cleanup_status, 0);
    }

    const pid_t double_child = fork();
    if (double_child == 0) _exit(8);
    check(double_child > 0, "could not fork raw DOUBLE process-wait control");
    if (double_child > 0) {
        eshkol_tagged_value_t raw_double{};
        raw_double.type = ESHKOL_VALUE_DOUBLE;
        raw_double.flags = ESHKOL_VALUE_INEXACT_FLAG;
        raw_double.data.raw_val = static_cast<uint64_t>(double_child);
        eshkol_tagged_value_t wait_result{};
        eshkol_builtin_process_wait(&wait_result, &raw_double);
        check(wait_result.type == ESHKOL_VALUE_INT64 &&
                  wait_result.data.int_val == 8,
              "historical raw DOUBLE process-wait behavior changed");
        int cleanup_status = 0;
        (void)waitpid(double_child, &cleanup_status, 0);
    }

    int poll_pipe[2] = {-1, -1};
    check(pipe(poll_pipe) == 0, "could not create poll-fd control pipe");
    if (poll_pipe[0] >= 0 && poll_pipe[1] >= 0) {
        check(write(poll_pipe[1], "x", 1) == 1,
              "could not make poll-fd control pipe ready");
        eshkol_tagged_value_t int_fd{};
        int_fd.type = ESHKOL_VALUE_INT64;
        int_fd.data.int_val = poll_pipe[0];
        eshkol_tagged_value_t int_timeout{};
        int_timeout.type = ESHKOL_VALUE_INT64;
        int_timeout.data.int_val = 0;
        eshkol_tagged_value_t poll_result{};
        eshkol_builtin_poll_fd(&poll_result, &int_fd, &int_timeout);
        check(poll_result.type == ESHKOL_VALUE_BOOL &&
                  poll_result.data.raw_val == 1,
              "INT64 poll-fd behavior changed");

        eshkol_tagged_value_t raw_double_fd{};
        raw_double_fd.type = ESHKOL_VALUE_DOUBLE;
        raw_double_fd.flags = ESHKOL_VALUE_INEXACT_FLAG;
        raw_double_fd.data.raw_val = static_cast<uint64_t>(poll_pipe[0]);
        eshkol_builtin_poll_fd(&poll_result, &raw_double_fd, &int_timeout);
        check(poll_result.type == ESHKOL_VALUE_BOOL &&
                  poll_result.data.raw_val == 1,
              "historical raw DOUBLE poll descriptor behavior changed");

        eshkol_tagged_value_t raw_double_timeout{};
        raw_double_timeout.type = ESHKOL_VALUE_DOUBLE;
        raw_double_timeout.flags = ESHKOL_VALUE_INEXACT_FLAG;
        raw_double_timeout.data.raw_val = 0;
        eshkol_builtin_poll_fd(&poll_result, &int_fd, &raw_double_timeout);
        check(poll_result.type == ESHKOL_VALUE_BOOL &&
                  poll_result.data.raw_val == 1,
              "historical raw DOUBLE poll timeout behavior changed");
        close(poll_pipe[0]);
        close(poll_pipe[1]);
    }

    char chmod_path[128];
    std::snprintf(chmod_path, sizeof(chmod_path),
                  "/tmp/eshkol-f32-chmod-controls-%ld",
                  static_cast<long>(getpid()));
    unlink(chmod_path);
    const int chmod_file = open(chmod_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    check(chmod_file >= 0, "could not create file-chmod control fixture");
    if (chmod_file >= 0) {
        close(chmod_file);
        eshkol_tagged_value_t path_value{};
        path_value.type = ESHKOL_VALUE_HEAP_PTR;
        path_value.flags = 0x01;
        path_value.data.ptr_val = reinterpret_cast<uintptr_t>(chmod_path);
        eshkol_tagged_value_t int_mode{};
        int_mode.type = ESHKOL_VALUE_INT64;
        int_mode.data.int_val = 0600;
        eshkol_tagged_value_t chmod_result{};
        eshkol_builtin_file_chmod(&chmod_result, &path_value, &int_mode);
        struct stat observed {};
        check(chmod_result.type == ESHKOL_VALUE_BOOL &&
                  chmod_result.data.raw_val == 1 &&
                  stat(chmod_path, &observed) == 0 &&
                  (observed.st_mode & 0777) == 0600,
              "INT64 file-chmod behavior changed");

        check(chmod(chmod_path, 0644) == 0,
              "could not reset raw DOUBLE file-chmod control mode");
        eshkol_tagged_value_t raw_double_mode{};
        raw_double_mode.type = ESHKOL_VALUE_DOUBLE;
        raw_double_mode.flags = ESHKOL_VALUE_INEXACT_FLAG;
        raw_double_mode.data.raw_val = 0600;
        eshkol_builtin_file_chmod(&chmod_result, &path_value,
                                  &raw_double_mode);
        check(chmod_result.type == ESHKOL_VALUE_BOOL &&
                  chmod_result.data.raw_val == 1 &&
                  stat(chmod_path, &observed) == 0 &&
                  (observed.st_mode & 0777) == 0600,
              "historical raw DOUBLE file-chmod behavior changed");
        unlink(chmod_path);
    }
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d f32 system integer checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: f32 system integer malformed rejection");
    return 0;
}
