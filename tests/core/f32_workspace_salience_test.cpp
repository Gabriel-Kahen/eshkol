#include <eshkol/core/workspace.h>

#include <cmath>
#include <cstddef>
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

namespace {

struct TensorLayout {
    uint64_t* dimensions;
    uint64_t num_dimensions;
    int64_t* elements;
    uint64_t total_elements;
};

struct TaggedPair {
    eshkol_tagged_value_t car;
    eshkol_tagged_value_t cdr;
};

struct WorkspaceStorage {
    eshkol_workspace_t workspace;
    eshkol_workspace_module_t modules[2];
};

static_assert(offsetof(WorkspaceStorage, modules) == sizeof(eshkol_workspace_t));

int failures;

void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

eshkol_tagged_value_t f32_value(uint32_t bits) {
    eshkol_tagged_value_t value{};
    check(eshkol_value_f32_from_bits_v1(&value, bits) == ESHKOL_VALUE_F32_OK,
          "could not construct canonical f32 salience");
    return value;
}

eshkol_tagged_value_t f64_value(double value) {
    eshkol_tagged_value_t tagged{};
    tagged.type = ESHKOL_VALUE_DOUBLE;
    tagged.flags = ESHKOL_VALUE_INEXACT_FLAG;
    tagged.data.double_val = value;
    return tagged;
}

void set_proposal(TaggedPair& pair, TensorLayout& tensor, int64_t& element,
                  const eshkol_tagged_value_t& salience, double proposal) {
    std::memcpy(&element, &proposal, sizeof(element));
    tensor.dimensions = nullptr;
    tensor.num_dimensions = 1;
    tensor.elements = &element;
    tensor.total_elements = 1;
    // Build the fixture carrier with deterministic zero padding. C++ copies
    // only members and may leave the destination's struct padding unchanged.
    std::memset(&pair.car, 0, sizeof(pair.car));
    pair.car.type = salience.type;
    pair.car.flags = salience.flags;
    pair.car.reserved = salience.reserved;
    pair.car.data.raw_val = salience.data.raw_val;
    pair.cdr = {};
    pair.cdr.type = ESHKOL_VALUE_HEAP_PTR;
    pair.cdr.data.ptr_val = reinterpret_cast<uint64_t>(&tensor);
}

void run_case(const eshkol_tagged_value_t* scores, const double* proposals,
              uint32_t count, double expected, const char* message) {
    WorkspaceStorage storage{};
    double content = -999.0;
    storage.workspace.num_modules = count;
    storage.workspace.max_modules = count;
    storage.workspace.dim = 1;
    storage.workspace.content = &content;

    TaggedPair pairs[2]{};
    TensorLayout tensors[2]{};
    int64_t elements[2]{};
    eshkol_tagged_value_t results[2]{};
    for (uint32_t i = 0; i < count; ++i) {
        set_proposal(pairs[i], tensors[i], elements[i], scores[i], proposals[i]);
        results[i].type = ESHKOL_VALUE_HEAP_PTR;
        results[i].data.ptr_val = reinterpret_cast<uint64_t>(&pairs[i]);
    }

    eshkol_ws_step_finalize(&storage.workspace, results, count);
    check(content == expected, message);
    check(storage.workspace.step_count == 1, "workspace step was not finalized");
}

void test_canonical_salience() {
    {
        const eshkol_tagged_value_t scores[] = {
            f32_value(UINT32_C(0x3fc00000)), f64_value(1.0)};
        const double proposals[] = {11.0, 22.0};
        run_case(scores, proposals, 2, 11.0, "finite f32 salience lost");
    }
    {
        const eshkol_tagged_value_t scores[] = {
            f32_value(UINT32_C(0x80000000)), f64_value(-1.0)};
        const double proposals[] = {31.0, 32.0};
        run_case(scores, proposals, 2, 31.0, "negative-zero f32 salience lost");
    }
    {
        const eshkol_tagged_value_t scores[] = {
            f32_value(UINT32_C(0xff800000)), f64_value(-1.0)};
        const double proposals[] = {41.0, 42.0};
        run_case(scores, proposals, 2, 42.0, "negative-infinity f32 salience changed ordering");
    }

    const uint32_t nonfinite[] = {
        UINT32_C(0x7f800000), UINT32_C(0x7fc12345),
        UINT32_C(0x7f812345), UINT32_C(0xffc12345),
        UINT32_C(0xff812345)};
    for (uint32_t bits : nonfinite) {
        const eshkol_tagged_value_t score[] = {f32_value(bits)};
        const double proposal[] = {51.0};
        run_case(score, proposal, 1, 51.0,
                 "nonfinite canonical f32 salience was rejected");
    }
}

#if !defined(_WIN32)
struct SharedMalformedFixture {
    WorkspaceStorage storage;
    double content;
    TaggedPair pair;
    eshkol_tagged_value_t result;
};

void test_malformed_rejection_is_atomic() {
    void* mapping = mmap(nullptr, sizeof(SharedMalformedFixture),
                         PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS,
                         -1, 0);
    check(mapping != MAP_FAILED, "could not allocate shared malformed fixture");
    if (mapping == MAP_FAILED) return;
    auto* fixture = static_cast<SharedMalformedFixture*>(mapping);
    std::memset(fixture, 0, sizeof(*fixture));
    fixture->content = 456.0;
    fixture->storage.workspace.num_modules = 1;
    fixture->storage.workspace.max_modules = 1;
    fixture->storage.workspace.dim = 1;
    fixture->storage.workspace.step_count = 789;
    fixture->storage.workspace.content = &fixture->content;
    fixture->storage.modules[0].salience = 123.0;
    fixture->pair.car = f32_value(UINT32_C(0x3f800000));
    fixture->pair.car.reserved = 1;  // exact tag 11, noncanonical layout
    fixture->result.type = ESHKOL_VALUE_HEAP_PTR;
    fixture->result.data.ptr_val = reinterpret_cast<uint64_t>(&fixture->pair);

    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        check(false, "could not create malformed diagnostic pipe");
        munmap(mapping, sizeof(*fixture));
        return;
    }
    const pid_t child = fork();
    if (child < 0) {
        check(false, "could not fork malformed salience test");
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        munmap(mapping, sizeof(*fixture));
        return;
    }
    if (child == 0) {
        close(pipe_fds[0]);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[1]);
        eshkol_ws_step_finalize(&fixture->storage.workspace, &fixture->result, 1);
        _exit(99);
    }
    close(pipe_fds[1]);
    std::string diagnostic;
    char buffer[256];
    ssize_t count = 0;
    while ((count = read(pipe_fds[0], buffer, sizeof(buffer))) > 0) {
        diagnostic.append(buffer, static_cast<size_t>(count));
    }
    close(pipe_fds[0]);
    int status = 0;
    waitpid(child, &status, 0);

    check(WIFEXITED(status) && WEXITSTATUS(status) == 1,
          "malformed tag-11 salience did not fail explicitly");
    check(diagnostic.find("ws-step!: malformed FLOAT32 salience") !=
              std::string::npos,
          "malformed tag-11 salience diagnostic changed");
    check(fixture->content == 456.0,
          "malformed salience mutated workspace content");
    check(fixture->storage.workspace.step_count == 789,
          "malformed salience mutated workspace step count");
    check(fixture->storage.modules[0].salience == 123.0,
          "malformed salience mutated module state");
    munmap(mapping, sizeof(*fixture));
}
#endif

}  // namespace

int main() {
    test_canonical_salience();
#if !defined(_WIN32)
    test_malformed_rejection_is_atomic();
#endif
    if (failures != 0) {
        std::fprintf(stderr, "%d f32 workspace salience checks failed\n", failures);
        return 1;
    }
    std::puts("PASS: f32 workspace runtime salience");
    return 0;
}
