#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>
#include <eshkol/llvm_backend.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

extern "C" void f32_reachability_reset(void);

#ifndef F32_REACHABILITY_SOURCE_PATH
#error F32_REACHABILITY_SOURCE_PATH must name the Eshkol fixture
#endif

namespace {

int fail(const char* message) {
    std::fprintf(stderr, "FAIL: f32 LLVM FFI JIT: %s\n", message);
    return 1;
}

}  // namespace

int main(int argc, char** argv) {
    const int optimization_level = argc > 1 ? std::atoi(argv[1]) : 0;
    if (optimization_level != 0 && optimization_level != 2) {
        return fail("expected optimization level 0 or 2");
    }

    std::ifstream input(F32_REACHABILITY_SOURCE_PATH);
    if (!input) return fail("could not open source fixture");
    std::ostringstream source;
    source << input.rdbuf();

    f32_reachability_reset();
    eshkol_set_optimization_level(optimization_level);
    const eshkol_tagged_value_t result =
        eshkol_eval_string(source.str().c_str(), nullptr);
    if (result.type != ESHKOL_VALUE_INT64 || result.data.int_val != 1) {
        return fail("fixture did not return its successful completion marker");
    }

    return 0;
}
