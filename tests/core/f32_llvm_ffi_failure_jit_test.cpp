#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>
#include <eshkol/llvm_backend.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

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
    eshkol_set_optimization_level(optimization_level);
    (void)eshkol_eval_string(source.str().c_str(), nullptr);
    std::fprintf(stderr, "FAIL: rejected f32 formatting operation returned\n");
    return 86;
}
