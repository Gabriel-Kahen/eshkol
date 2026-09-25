#include <eshkol/module_visibility.h>
#include "../../lib/core/arena_memory.h"

#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <vector>

static arena_t root_arena{};
static std::vector<void*> root_allocations;

static void check(bool condition) {
    if (!condition) {
        std::fputs("module visibility ownership test failed\n", stderr);
        std::abort();
    }
}

extern "C" arena_t* eshkol_root_arena_v1(void) { return &root_arena; }

extern "C" void* arena_allocate(arena_t* arena, size_t size) {
    check(arena == &root_arena);
    void* p = std::malloc(size);
    if (p) root_allocations.push_back(p);
    return p;
}

static char* parser_name(const char* name) {
    const size_t size = std::strlen(name) + 1;
    char* copy = new char[size];
    std::memcpy(copy, name, size);
    return copy;
}

int main() {
    eshkol_ast_t reference{};
    reference.type = ESHKOL_VAR;
    reference.variable.id = parser_name("helper");

    eshkol_ast_t private_definition{};
    private_definition.type = ESHKOL_OP;
    private_definition.operation.op = ESHKOL_DEFINE_OP;
    private_definition.operation.define_op.name = parser_name("helper");
    private_definition.operation.define_op.value = &reference;

    eshkol_ast_t assignment{};
    assignment.type = ESHKOL_OP;
    assignment.operation.op = ESHKOL_SET_OP;
    assignment.operation.set_op.name = parser_name("helper");

    // Macro expansion can produce strdup names. A lexical binder must keep
    // its original name and ownership while the private module name changes.
    eshkol_ast_t parameter{};
    parameter.type = ESHKOL_VAR;
    parameter.variable.id = ::strdup("helper");
    eshkol_ast_t bound_reference{};
    bound_reference.type = ESHKOL_VAR;
    bound_reference.variable.id = ::strdup("helper");
    eshkol_ast_t lambda{};
    lambda.type = ESHKOL_OP;
    lambda.operation.op = ESHKOL_LAMBDA_OP;
    lambda.operation.lambda_op.parameters = &parameter;
    lambda.operation.lambda_op.num_params = 1;
    lambda.operation.lambda_op.body = &bound_reference;

    std::vector<eshkol_ast_t> asts{private_definition, assignment, lambda};
    eshkol::rename_private_symbols(asts, "sample.module", {});

    const char* renamed = "__sample_module__helper";
    check(std::strcmp(asts[0].operation.define_op.name, renamed) == 0);
    check(std::strcmp(reference.variable.id, renamed) == 0);
    check(std::strcmp(asts[1].operation.set_op.name, renamed) == 0);
    check(std::strcmp(parameter.variable.id, "helper") == 0);
    check(std::strcmp(bound_reference.variable.id, "helper") == 0);
    check(root_allocations.size() == 3);

    std::free(parameter.variable.id);
    std::free(bound_reference.variable.id);
    for (void* p : root_allocations) std::free(p);
}
