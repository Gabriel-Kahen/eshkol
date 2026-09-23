#include <eshkol/eshkol.h>
#include <eshkol/llvm_backend.h>

#include <cstdio>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

namespace {

static_assert(sizeof(void*) != 8 || sizeof(eshkol_operations_t) == 128,
              "private define metadata must stay inside existing operation ABI size");
static_assert(sizeof(void*) != 8 || sizeof(eshkol_ast_t) == 160,
              "private define metadata must not grow the public AST ABI");

template <typename T>
bool expect_equal(const T& actual, const T& expected, const std::string& label) {
    if (actual == expected) {
        return true;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

bool expect_string(const char* actual, const char* expected, const std::string& label) {
    if (actual && std::strcmp(actual, expected) == 0) {
        return true;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

bool expect_contains(const std::string& haystack, const std::string& needle,
                     const std::string& label) {
    if (haystack.find(needle) != std::string::npos) {
        return true;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

bool expect_not_contains(const std::string& haystack, const std::string& needle,
                         const std::string& label) {
    if (haystack.find(needle) == std::string::npos) {
        return true;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

bool expect_line_contains(const std::string& text, const std::string& anchor,
                          const std::string& needle, const std::string& label) {
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find(anchor) == std::string::npos) {
            continue;
        }
        if (line.find(needle) != std::string::npos) {
            return true;
        }
        break;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

bool expect_line_not_contains(const std::string& text, const std::string& anchor,
                              const std::string& needle, const std::string& label) {
    std::stringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find(anchor) == std::string::npos) {
            continue;
        }
        if (line.find(needle) == std::string::npos) {
            return true;
        }
        break;
    }
    std::cerr << "FAIL: " << label << std::endl;
    return false;
}

std::string extract_function_ir(const std::string& ir, const std::string& name) {
    const std::string anchor = "@" + name + "(";
    const size_t name_pos = ir.find(anchor);
    if (name_pos == std::string::npos) return {};
    const size_t define_pos = ir.rfind("define ", name_pos);
    if (define_pos == std::string::npos) return {};
    const size_t end_pos = ir.find("\n}", name_pos);
    if (end_pos == std::string::npos) return {};
    return ir.substr(define_pos, end_pos + 2 - define_pos);
}

size_t count_occurrences(const std::string& text, const std::string& needle) {
    size_t count = 0;
    size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

eshkol_ast_t parse_single(const std::string& source) {
    std::stringstream stream(source);
    return eshkol_parse_next_ast_from_stream(stream);
}

bool dump_module_ir(LLVMModuleRef module, std::string* ir_out, const std::string& label) {
    if (!module || !ir_out) {
        std::cerr << "FAIL: " << label << " module setup" << std::endl;
        return false;
    }

    char ir_path[] = "/tmp/eshkol-decl-attrs-ir-XXXXXX";
    const int fd = mkstemp(ir_path);
    if (fd == -1) {
        std::cerr << "FAIL: " << label << " temp IR path" << std::endl;
        return false;
    }
    close(fd);

    bool ok = false;
    if (eshkol_dump_llvm_ir_to_file(module, ir_path) != 0) {
        std::cerr << "FAIL: " << label << " IR dump" << std::endl;
    } else {
        std::ifstream ir_stream(ir_path);
        std::stringstream ir_buffer;
        ir_buffer << ir_stream.rdbuf();
        *ir_out = ir_buffer.str();
        ok = true;
    }

    std::remove(ir_path);
    return ok;
}

bool test_define_attribute_parse_surface() {
    eshkol_ast_t ast = parse_single(
        "(define boot-flag 1 :link-section \".boot.data\" :align 32 :used :weak :export-symbol boot_flag_symbol)");

    if (ast.type != ESHKOL_OP || ast.operation.op != ESHKOL_DEFINE_OP) {
        std::cerr << "FAIL: define attribute parse shape" << std::endl;
        return false;
    }

    return expect_string(ast.operation.define_op.link_section, ".boot.data",
                         "define link-section parses") &&
           expect_equal(ast.operation.define_op.alignment, uint64_t{32},
                        "define align parses") &&
           expect_equal(ast.operation.define_op.has_alignment, uint8_t{1},
                        "define align flag parses") &&
           expect_equal(ast.operation.define_op.is_used, uint8_t{1},
                        "define used flag parses") &&
           expect_equal(ast.operation.define_op.is_weak, uint8_t{1},
                        "define weak flag parses") &&
           expect_equal(ast.operation.define_op.export_symbol, uint8_t{1},
                        "define export-symbol flag parses") &&
           expect_string(ast.operation.define_op.export_name, "boot_flag_symbol",
                         "define export-symbol emitted name parses");
}

bool test_define_same_name_export_surface() {
    eshkol_ast_t ast = parse_single("(define keep-name 1 :export-symbol)");

    if (ast.type != ESHKOL_OP || ast.operation.op != ESHKOL_DEFINE_OP) {
        std::cerr << "FAIL: define same-name export parse shape" << std::endl;
        return false;
    }

    return expect_equal(ast.operation.define_op.export_symbol, uint8_t{1},
                        "define same-name export flag parses") &&
           expect_equal(ast.operation.define_op.export_name == nullptr, true,
                        "define same-name export leaves emitted name empty");
}

bool test_extern_attribute_parse_surface() {
    eshkol_ast_t ast = parse_single(
        "(extern void halt :extern-symbol abort :weak :no-return)");

    if (ast.type != ESHKOL_OP || ast.operation.op != ESHKOL_EXTERN_OP) {
        std::cerr << "FAIL: extern attribute parse shape" << std::endl;
        return false;
    }

    return expect_string(ast.operation.extern_op.real_name, "abort",
                         "extern-symbol parses into real symbol name") &&
           expect_equal(ast.operation.extern_op.is_weak, uint8_t{1},
                        "extern weak flag parses") &&
           expect_equal(ast.operation.extern_op.is_no_return, uint8_t{1},
                        "extern no-return flag parses");
}

bool test_extern_var_attribute_parse_surface() {
    eshkol_ast_t ast = parse_single(
        "(extern-var int errno-slot :extern-symbol errno)");

    if (ast.type != ESHKOL_OP || ast.operation.op != ESHKOL_EXTERN_VAR_OP) {
        std::cerr << "FAIL: extern-var attribute parse shape" << std::endl;
        return false;
    }

    return expect_string(ast.operation.extern_var_op.real_name, "errno",
                         "extern-var real symbol name parses");
}

bool test_define_attribute_rejects_invalid_tails() {
    eshkol_ast_t unknown_modifier =
        parse_single("(define (f) : null 1 :bogus)");
    eshkol_ast_t bad_alignment =
        parse_single("(define (f) : null 1 :align 3)");
    eshkol_ast_t overlarge_alignment =
        parse_single("(define boot-flag 1 :align 9223372036854775808)");

    return expect_equal(unknown_modifier.type, ESHKOL_INVALID,
                        "function define rejects unknown declaration modifier") &&
           expect_equal(bad_alignment.type, ESHKOL_INVALID,
                        "function define rejects non-power-of-two alignment") &&
           expect_equal(overlarge_alignment.type, ESHKOL_INVALID,
                        "define rejects alignments above LLVM maximum");
}

bool test_runtime_emergency_rethrow_modifier_parse_surface() {
    eshkol_ast_t ast = parse_single(
        "(define (bridge operation caught) operation "
        ":runtime-emergency-rethrow-param caught)");
    if (ast.type != ESHKOL_OP || ast.operation.op != ESHKOL_DEFINE_OP) {
        std::cerr << "FAIL: runtime emergency rethrow modifier parse shape" << std::endl;
        return false;
    }
    return expect_equal(ast.operation.define_op.has_runtime_emergency_rethrow_param,
                        uint8_t{1}, "runtime emergency rethrow modifier flag") &&
           expect_equal(ast.operation.define_op.runtime_emergency_rethrow_param_index,
                        uint32_t{1}, "runtime emergency rethrow formal index");
}

bool test_runtime_emergency_rethrow_modifier_rejects_unsupported_forms() {
    eshkol_ast_t duplicate = parse_single(
        "(define (f caught) caught "
        ":runtime-emergency-rethrow-param caught "
        ":runtime-emergency-rethrow-param caught)");
    eshkol_ast_t variable = parse_single(
        "(define caught 1 :runtime-emergency-rethrow-param caught)");
    eshkol_ast_t variadic = parse_single(
        "(define (f caught . rest) caught "
        ":runtime-emergency-rethrow-param caught)");
    eshkol_ast_t keyword_rest = parse_single(
        "(define (f #:caught caught) caught "
        ":runtime-emergency-rethrow-param caught)");
    eshkol_ast_t unknown = parse_single(
        "(define (f caught) caught "
        ":runtime-emergency-rethrow-param missing)");
    eshkol_ast_t missing = parse_single(
        "(define (f caught) caught :runtime-emergency-rethrow-param)");
    eshkol_ast_t non_symbol = parse_single(
        "(define (f caught) caught "
        ":runtime-emergency-rethrow-param 1)");
    eshkol_ast_t duplicate_formal = parse_single(
        "(define (f caught caught) caught "
        ":runtime-emergency-rethrow-param caught)");
    eshkol_ast_t typed = parse_single(
        "(define (f (caught : real)) caught "
        ":runtime-emergency-rethrow-param caught)");
    eshkol_ast_t internal = parse_single(
        "(define (outer) "
        "  (define (inner caught) caught "
        "    :runtime-emergency-rethrow-param caught) "
        "  (inner 1))");
    eshkol_ast_t nested_begin = parse_single(
        "(begin "
        "  (define (inner caught) caught "
        "    :runtime-emergency-rethrow-param caught) "
        "  (inner 1))");

    char include_path[] = "/tmp/eshkol-rethrow-modifier-include-XXXXXX";
    const int include_fd = mkstemp(include_path);
    if (include_fd == -1) {
        std::cerr << "FAIL: runtime emergency modifier include temp file" << std::endl;
        return false;
    }
    close(include_fd);
    {
        std::ofstream included(include_path);
        included << "(define (included caught) caught "
                    ":runtime-emergency-rethrow-param caught)\n";
    }
    eshkol_set_parse_source_context("decl-attribute-parent.esk");
    eshkol_ast_t included = parse_single(
        std::string("(include \"") + include_path + "\")");
    const bool include_context_restored =
        std::strcmp(eshkol_get_parse_source_context(),
                    "decl-attribute-parent.esk") == 0;
    eshkol_set_parse_source_context("<unknown>");
    std::remove(include_path);

    return expect_equal(duplicate.type, ESHKOL_INVALID,
                        "duplicate runtime emergency modifier rejected") &&
           expect_equal(variable.type, ESHKOL_INVALID,
                        "variable define runtime emergency modifier rejected") &&
           expect_equal(variadic.type, ESHKOL_INVALID,
                        "variadic runtime emergency modifier rejected") &&
           expect_equal(keyword_rest.type, ESHKOL_INVALID,
                        "keyword-rest runtime emergency modifier rejected") &&
           expect_equal(unknown.type, ESHKOL_INVALID,
                        "unknown runtime emergency formal rejected") &&
           expect_equal(missing.type, ESHKOL_INVALID,
                        "missing runtime emergency formal rejected") &&
           expect_equal(non_symbol.type, ESHKOL_INVALID,
                        "non-symbol runtime emergency formal rejected") &&
           expect_equal(duplicate_formal.type, ESHKOL_INVALID,
                        "duplicate runtime emergency formal rejected") &&
           expect_equal(typed.type, ESHKOL_INVALID,
                        "typed runtime emergency formal rejected") &&
           expect_equal(internal.type, ESHKOL_INVALID,
                        "internal runtime emergency definition rejected") &&
           expect_equal(nested_begin.type, ESHKOL_INVALID,
                        "nested begin runtime emergency definition rejected") &&
           expect_equal(included.type, ESHKOL_INVALID,
                        "included runtime emergency definition rejected") &&
           expect_equal(include_context_restored, true,
                        "rejected include restores parser source context");
}

bool test_runtime_emergency_rethrow_modifier_ir_lowering() {
    eshkol_set_uses_stdlib(0);
    eshkol_ast_t asts[4] = {
        parse_single("(define (body_probe value) value)"),
        parse_single(
            "(define (bridge caught operation) "
            "  (body_probe operation) "
            ":runtime-emergency-rethrow-param caught)"),
        parse_single("(define (plain caught operation) operation)"),
        parse_single(
            "(define (recursive_bridge caught n) "
            "  (if (= n 0) n (recursive_bridge caught (- n 1))) "
            "  :runtime-emergency-rethrow-param caught)"),
    };

    LLVMModuleRef module = eshkol_generate_llvm_ir_library(
        asts, 4, "runtime_emergency_rethrow_modifier_test");
    if (!module) {
        std::cerr << "FAIL: runtime emergency rethrow LLVM module generation" << std::endl;
        return false;
    }

    std::string ir;
    bool ok = dump_module_ir(module, &ir, "runtime emergency rethrow modifier");
    if (ok) {
        const std::string bridge = extract_function_ir(ir, "bridge");
        const std::string plain = extract_function_ir(ir, "plain");
        const std::string recursive = extract_function_ir(ir, "recursive_bridge");
        const size_t alloca_pos = bridge.find("runtime_emergency_rethrow_param = alloca");
        const size_t store_pos = bridge.find("store ", alloca_pos);
        const size_t call_pos = bridge.find(
            "call void @eshkol_runtime_emergency_rethrow_if_v1", store_pos);
        const size_t first_call_pos = bridge.find("call ");
        const size_t body_call_pos = bridge.find("@body_probe(");

        ok = expect_equal(!bridge.empty(), true,
                          "annotated function appears in IR") &&
             expect_equal(alloca_pos != std::string::npos, true,
                          "annotated function spills selected formal") &&
             expect_equal(store_pos != std::string::npos && store_pos > alloca_pos, true,
                          "annotated function stores after entry alloca") &&
             expect_equal(call_pos != std::string::npos && call_pos > store_pos, true,
                          "annotated function calls exact runtime bridge after spill") &&
             expect_equal(first_call_pos, call_pos,
                          "runtime bridge is annotated function's first call") &&
             expect_contains(bridge, "%caught, ptr %runtime_emergency_rethrow_param",
                             "selected caught formal is stored into bridge slot") &&
             expect_equal(count_occurrences(
                              bridge,
                              "ptr %runtime_emergency_rethrow_param"),
                          size_t{2},
                          "bridge slot has exactly one store and one runtime use") &&
             expect_equal(body_call_pos != std::string::npos &&
                              body_call_pos > call_pos,
                          true,
                          "body call is emitted after runtime bridge") &&
             expect_not_contains(plain,
                                 "eshkol_runtime_emergency_rethrow_if_v1",
                                 "unannotated function has no runtime bridge") &&
             expect_equal(!recursive.empty(), true,
                          "annotated recursive function appears in IR") &&
             expect_not_contains(recursive, "tco_loop",
                                 "annotated recursive function keeps physical entries") &&
             expect_equal(count_occurrences(
                              recursive,
                              "call void @eshkol_runtime_emergency_rethrow_if_v1"),
                          size_t{1},
                          "recursive function has one physical-entry bridge") &&
             expect_equal(count_occurrences(recursive, "@recursive_bridge("),
                          size_t{2},
                          "annotated recursion remains one physical self-call");
    }

    eshkol_dispose_llvm_module(module);
    return ok;
}

bool test_runtime_emergency_rethrow_modifier_malformed_metadata_fails_closed() {
    eshkol_set_uses_stdlib(0);
    eshkol_ast_t ast = parse_single(
        "(define (bridge caught operation) operation "
        ":runtime-emergency-rethrow-param caught)");
    ast.operation.define_op.runtime_emergency_rethrow_param_index = 99;
    LLVMModuleRef module = eshkol_generate_llvm_ir_library(
        &ast, 1, "runtime_emergency_rethrow_malformed_metadata_test");
    if (module) {
        eshkol_dispose_llvm_module(module);
        std::cerr << "FAIL: malformed runtime emergency metadata generated a module"
                  << std::endl;
        return false;
    }

    eshkol_ast_t external = parse_single(
        "(define (external-bridge caught) caught "
        ":runtime-emergency-rethrow-param caught)");
    external.operation.define_op.is_external = 1;
    module = eshkol_generate_llvm_ir_library(
        &external, 1, "runtime_emergency_rethrow_external_metadata_test");
    if (module) {
        eshkol_dispose_llvm_module(module);
        std::cerr << "FAIL: external runtime emergency metadata generated a module"
                  << std::endl;
        return false;
    }
    return true;
}

bool test_declaration_attribute_ir_lowering() {
    eshkol_set_uses_stdlib(0);
    eshkol_set_target("x86_64-unknown-linux-gnu");

    eshkol_ast_t asts[4] = {
        parse_single("(extern void halt :extern-symbol abort :weak)"),
        parse_single("(extern-var int errno-slot :extern-symbol errno)"),
        parse_single("(define boot-flag 1 :link-section \".boot.data\" :align 32 :used :weak :export-symbol boot_flag_symbol)"),
        parse_single("(define (entry) : null (compiler-fence seq-cst) :link-section \".boot.text\" :align 16 :used :export-symbol entry_symbol :no-return)"),
    };

    LLVMModuleRef module =
        eshkol_generate_llvm_ir_library(asts, 4, "decl_attribute_test");
    if (!module) {
        std::cerr << "FAIL: declaration attribute LLVM module generation" << std::endl;
        eshkol_set_target(nullptr);
        return false;
    }

    bool ok = false;
    std::string ir;
    if (dump_module_ir(module, &ir, "declaration attribute")) {
        ok = expect_contains(ir, "target triple = \"x86_64-unknown-linux-gnu\"",
                             "library-mode IR honors explicit target triple") &&
             expect_line_contains(ir, "@boot_flag_symbol", "weak",
                                  "weak global lowering survives in IR") &&
             expect_line_contains(ir, "@boot_flag_symbol", "section \".boot.data\"",
                                  "global link-section lowering survives in IR") &&
             expect_line_contains(ir, "@boot_flag_symbol", "align 32",
                                  "global align lowering survives in IR") &&
             expect_contains(ir, "declare extern_weak void @abort()",
                             "extern weak symbol lowering survives in IR") &&
             expect_contains(ir, "@errno = external global i32",
                             "extern-var real symbol lowering survives in IR") &&
             expect_not_contains(ir, "@errno-slot",
                                 "extern-var source name is not emitted as a global") &&
             expect_line_contains(ir, "@entry_symbol(", "section \".boot.text\"",
                                  "function link-section lowering survives in IR") &&
             expect_line_contains(ir, "@entry_symbol(", "align 16",
                                  "function align lowering survives in IR") &&
             expect_line_not_contains(ir, "@entry_symbol(", "linkonce_odr",
                                      "export-symbol keeps entry out of linkonce_odr linkage") &&
             expect_contains(ir, "@llvm.used",
                             "used declarations lower through llvm.used") &&
             expect_contains(ir, "noreturn",
                             "function no-return lowers to LLVM noreturn attributes");
    }

    eshkol_dispose_llvm_module(module);
    eshkol_set_target(nullptr);
    return ok;
}

bool test_extern_no_return_ir_lowering() {
    eshkol_set_uses_stdlib(0);

    eshkol_ast_t asts[1] = {
        parse_single("(extern void fatal :extern-symbol abort :no-return)"),
    };

    LLVMModuleRef module =
        eshkol_generate_llvm_ir_library(asts, 1, "extern_no_return_test");
    if (!module) {
        std::cerr << "FAIL: extern no-return LLVM module generation" << std::endl;
        return false;
    }

    std::string ir;
    const bool ok =
        dump_module_ir(module, &ir, "extern no-return") &&
        expect_contains(ir, "declare void @abort()",
                        "extern no-return symbol lowering survives in IR") &&
        expect_contains(ir, "noreturn",
                        "extern no-return lowers to LLVM noreturn attributes");

    eshkol_dispose_llvm_module(module);
    return ok;
}

}  // namespace

int main() {
    if (!test_define_attribute_parse_surface()) {
        return 1;
    }
    if (!test_define_same_name_export_surface()) {
        return 1;
    }
    if (!test_extern_attribute_parse_surface()) {
        return 1;
    }
    if (!test_extern_var_attribute_parse_surface()) {
        return 1;
    }
    if (!test_define_attribute_rejects_invalid_tails()) {
        return 1;
    }
    if (!test_runtime_emergency_rethrow_modifier_parse_surface()) {
        return 1;
    }
    if (!test_runtime_emergency_rethrow_modifier_rejects_unsupported_forms()) {
        return 1;
    }
    if (!test_runtime_emergency_rethrow_modifier_ir_lowering()) {
        return 1;
    }
    if (!test_runtime_emergency_rethrow_modifier_malformed_metadata_fails_closed()) {
        return 1;
    }
    if (!test_declaration_attribute_ir_lowering()) {
        return 1;
    }
    if (!test_extern_no_return_ir_lowering()) {
        return 1;
    }

    std::cout << "PASS" << std::endl;
    return 0;
}
