#include <eshkol/eshkol.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <sys/mman.h>
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

namespace {

int failures;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

#if !defined(_WIN32)
struct SharedFixture {
    eshkol_tagged_value_t output;
    eshkol_tagged_value_t input;
    eshkol_tagged_value_t other;
};

enum class BuiltinKind {
    FormatRelative,
    RegexFree,
    AllowSleep,
    ProcessWait,
    PollFdDescriptor,
    PollFdTimeout
};

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
    check(eshkol_value_f32_from_bits_v1(&fixture->input,
                                        UINT32_C(0x00000001)) ==
              ESHKOL_VALUE_F32_OK,
          "could not construct malformed system input base");
    if (malformed) fixture->input.reserved = 1;
    fixture->other.type = ESHKOL_VALUE_INT64;
    fixture->other.data.int_val = 0;

    int stderr_pipe[2];
    if (pipe(stderr_pipe) != 0) {
        check(false, "could not create system diagnostic pipe");
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork malformed system test");
        close(stderr_pipe[0]);
        close(stderr_pipe[1]);
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
        } else {
            eshkol_builtin_poll_fd(&fixture->output, &fixture->other,
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
    munmap(mapping, sizeof(*fixture));
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
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d f32 system integer checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: f32 system integer malformed rejection");
    return 0;
}
