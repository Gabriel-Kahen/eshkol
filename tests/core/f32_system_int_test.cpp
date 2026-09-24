#include <eshkol/eshkol.h>

#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
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
