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
};

enum class BuiltinKind { FormatRelative, RegexFree };

void expect_rejection(BuiltinKind builtin, const char* diagnostic,
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
    fixture->input.reserved = 1;

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
        } else {
            eshkol_builtin_regex_free(&fixture->output, &fixture->input);
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
        std::fprintf(stderr, "FAIL: malformed system diagnostic changed for %s: %s\n",
                     label, observed.c_str());
        ++failures;
    }
    check(fixture->output.type == ESHKOL_VALUE_INT64 &&
              fixture->output.flags == ESHKOL_VALUE_EXACT_FLAG &&
              fixture->output.data.int_val == INT64_C(0x123456789abcdef),
          "malformed system input mutated output before rejection");
    munmap(mapping, sizeof(*fixture));
}
#endif

}  // namespace

int main() {
#if !defined(_WIN32)
    expect_rejection(BuiltinKind::FormatRelative,
                     "Type error in format-relative: expected canonical float32",
                     "malformed format-relative input did not fail explicitly");
    expect_rejection(BuiltinKind::RegexFree,
                     "Type error in system integer/resource argument: expected non-float32 value",
                     "malformed resource handle did not fail explicitly");
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d f32 system integer checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: f32 system integer malformed rejection");
    return 0;
}
