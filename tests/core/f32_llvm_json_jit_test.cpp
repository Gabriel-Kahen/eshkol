#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>
#include <eshkol/llvm_backend.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

extern "C" void f32_reachability_reset(void);

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "FAIL: expected fixture path and optimization level\n");
        return 1;
    }
    const int optimization_level = std::atoi(argv[2]);
    if (optimization_level != 0 && optimization_level != 2) {
        std::fprintf(stderr, "FAIL: expected optimization level 0 or 2\n");
        return 1;
    }
    std::ifstream input(argv[1]);
    if (!input) {
        std::fprintf(stderr, "FAIL: could not open source fixture\n");
        return 1;
    }
    std::ostringstream source;
    source << input.rdbuf();
    f32_reachability_reset();
    eshkol_set_optimization_level(optimization_level);
    const eshkol_tagged_value_t result =
        eshkol_eval_string(source.str().c_str(), nullptr);
    if (result.type != ESHKOL_VALUE_INT64 || result.data.int_val != 1) {
        std::fprintf(stderr,
                     "FAIL: f32 JSON fixture did not finish successfully "
                     "(type=%d value=%lld)\n",
                     static_cast<int>(result.type),
                     static_cast<long long>(result.data.int_val));
        return 1;
    }
    return 0;
}
