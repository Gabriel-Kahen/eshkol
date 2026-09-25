/*
 * Exhaustive C-ABI coverage for the runtime-eligible semantic type-symbol
 * helper.  This target links only eshkol-runtime, so it also proves the helper
 * is available to generated programs without the compiler/tool archive.
 */

#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;

typedef struct header_backed_value {
    eshkol_object_header_t header;
    uint64_t payload;
} header_backed_value_t;

_Static_assert(offsetof(header_backed_value_t, payload) ==
                   sizeof(eshkol_object_header_t),
               "test payload must immediately follow its object header");

static void check(int condition, const char* context, const char* detail) {
    if (condition) return;
    fprintf(stderr, "FAIL: %s: %s\n", context, detail);
    ++failures;
}

static void check_type_symbol_ref(const eshkol_tagged_value_t* value,
                                  const char* expected,
                                  const char* context) {
    eshkol_tagged_value_t actual = eshkol_type_of_ref_v1(value);
    check(actual.type == ESHKOL_VALUE_HEAP_PTR, context,
          "result is not HEAP_PTR");
    check(actual.flags == 0, context, "result flags are nonzero");
    check(actual.reserved == 0, context, "result reserved field is nonzero");
    check(actual.data.ptr_val != 0, context, "result symbol pointer is null");
    if (actual.type != ESHKOL_VALUE_HEAP_PTR || actual.data.ptr_val == 0) return;

    const char* spelling = (const char*)(uintptr_t)actual.data.ptr_val;
    const eshkol_object_header_t* header = ESHKOL_GET_HEADER(spelling);
    check(header->subtype == HEAP_SUBTYPE_SYMBOL, context,
          "result does not carry SYMBOL subtype");
    check(strcmp(spelling, expected) == 0, context, "symbol spelling mismatch");

    void* canonical = eshkol_intern_symbol_lookup(expected);
    check((void*)(uintptr_t)actual.data.ptr_val == canonical, context,
          "result does not use canonical interned pointer");
    eshkol_tagged_value_t repeated = eshkol_type_of_ref_v1(value);
    check(repeated.data.ptr_val == actual.data.ptr_val, context,
          "repeated result changed symbol identity");
}

static void check_type_symbol(eshkol_tagged_value_t value,
                              const char* expected,
                              const char* context) {
    check_type_symbol_ref(&value, expected, context);
#ifdef ESHKOL_TEST_LEGACY_TYPE_OF
    eshkol_tagged_value_t canonical = eshkol_type_of_ref_v1(&value);
    eshkol_tagged_value_t legacy = eshkol_type_of(value);
    check(legacy.type == canonical.type, context,
          "legacy result type differs from ref_v1");
    check(legacy.flags == canonical.flags, context,
          "legacy result flags differ from ref_v1");
    check(legacy.reserved == canonical.reserved, context,
          "legacy result reserved field differs from ref_v1");
    check(legacy.data.ptr_val == canonical.data.ptr_val, context,
          "legacy result symbol differs from ref_v1");
#endif
}

typedef struct direct_case {
    uint8_t tag;
    const char* expected;
} direct_case_t;

static void test_declared_direct_tags(void) {
    static const direct_case_t cases[] = {
        {ESHKOL_VALUE_NULL, "null"},
        {ESHKOL_VALUE_INT64, "integer"},
        {ESHKOL_VALUE_DOUBLE, "real"},
        {ESHKOL_VALUE_BOOL, "boolean"},
        {ESHKOL_VALUE_CHAR, "char"},
        {ESHKOL_VALUE_SYMBOL, "symbol"},
        {ESHKOL_VALUE_DUAL_NUMBER, "dual-number"},
        {ESHKOL_VALUE_COMPLEX, "complex"},
        {ESHKOL_VALUE_HEAP_PTR, "heap-object"},
        {ESHKOL_VALUE_CALLABLE, "procedure"},
        {ESHKOL_VALUE_LOGIC_VAR, "logic-variable"},
        {ESHKOL_VALUE_HANDLE, "handle"},
        {ESHKOL_VALUE_BUFFER, "buffer"},
        {ESHKOL_VALUE_STREAM, "stream"},
        {ESHKOL_VALUE_EVENT, "event"},
        {ESHKOL_VALUE_CONS_PTR, "pair"},
        {ESHKOL_VALUE_STRING_PTR, "string"},
        {ESHKOL_VALUE_VECTOR_PTR, "vector"},
        {ESHKOL_VALUE_TENSOR_PTR, "tensor"},
        {ESHKOL_VALUE_HASH_PTR, "hash-table"},
        {ESHKOL_VALUE_EXCEPTION, "exception"},
        {ESHKOL_VALUE_CLOSURE_PTR, "closure"},
        {ESHKOL_VALUE_LAMBDA_SEXPR, "lambda-sexpr"},
        {ESHKOL_VALUE_AD_NODE_PTR, "ad-node"},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        eshkol_tagged_value_t value = {0};
        value.type = cases[i].tag;
        value.flags = UINT8_C(0x50);
        check_type_symbol(value, cases[i].expected, cases[i].expected);
    }

    eshkol_tagged_value_t exact_integer = {0};
    exact_integer.type = ESHKOL_VALUE_INT64;
    exact_integer.flags = ESHKOL_VALUE_EXACT_FLAG;
    check_type_symbol(exact_integer, "integer", "exact integer flags");

    eshkol_tagged_value_t inexact_real = {0};
    inexact_real.type = ESHKOL_VALUE_DOUBLE;
    inexact_real.flags = ESHKOL_VALUE_INEXACT_FLAG;
    check_type_symbol(inexact_real, "real", "inexact real flags");
}

typedef struct subtype_case {
    uint8_t subtype;
    const char* expected;
} subtype_case_t;

static void test_heap_subtypes(void) {
    static const subtype_case_t cases[] = {
        {HEAP_SUBTYPE_CONS, "pair"},
        {HEAP_SUBTYPE_STRING, "string"},
        {HEAP_SUBTYPE_VECTOR, "vector"},
        {HEAP_SUBTYPE_TENSOR, "tensor"},
        {HEAP_SUBTYPE_MULTI_VALUE, "values"},
        {HEAP_SUBTYPE_HASH, "hash-table"},
        {HEAP_SUBTYPE_EXCEPTION, "exception"},
        {HEAP_SUBTYPE_RECORD, "record"},
        {HEAP_SUBTYPE_BYTEVECTOR, "bytevector"},
        {HEAP_SUBTYPE_PORT, "port"},
        {HEAP_SUBTYPE_SYMBOL, "symbol"},
        {HEAP_SUBTYPE_BIGNUM, "integer"},
        {HEAP_SUBTYPE_SUBSTITUTION, "substitution"},
        {HEAP_SUBTYPE_FACT, "fact"},
        {HEAP_SUBTYPE_KNOWLEDGE_BASE, "knowledge-base"},
        {HEAP_SUBTYPE_FACTOR_GRAPH, "factor-graph"},
        {HEAP_SUBTYPE_WORKSPACE, "workspace"},
        {HEAP_SUBTYPE_PROMISE, "promise"},
        {HEAP_SUBTYPE_RATIONAL, "rational"},
        {HEAP_SUBTYPE_PRNG, "prng"},
        {HEAP_SUBTYPE_DNC, "dnc"},
        {HEAP_SUBTYPE_SDNC, "sdnc"},
        {HEAP_SUBTYPE_TAYLOR, "taylor"},
        {HEAP_SUBTYPE_PARAMETER, "parameter"},
        {HEAP_SUBTYPE_I128, "i128"},
    };

    header_backed_value_t storage = {0};
    eshkol_tagged_value_t value = {0};
    value.type = ESHKOL_VALUE_HEAP_PTR;
    value.data.ptr_val = (uint64_t)(uintptr_t)&storage.payload;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        storage.header.subtype = cases[i].subtype;
        check_type_symbol(value, cases[i].expected, cases[i].expected);
    }

    storage.header.subtype = UINT8_C(14);
    check_type_symbol(value, "heap-object", "reserved heap subtype");
    storage.header.subtype = UINT8_C(255);
    check_type_symbol(value, "heap-object", "unknown heap subtype");

    storage.header.subtype = HEAP_SUBTYPE_PORT;
    value.flags = ESHKOL_PORT_INPUT_FLAG | ESHKOL_PORT_BINARY_FLAG;
    check_type_symbol(value, "port", "port direction flags");
}

static void test_callable_subtypes(void) {
    static const subtype_case_t cases[] = {
        {CALLABLE_SUBTYPE_CLOSURE, "closure"},
        {CALLABLE_SUBTYPE_LAMBDA_SEXPR, "lambda-sexpr"},
        {CALLABLE_SUBTYPE_AD_NODE, "ad-node"},
        {CALLABLE_SUBTYPE_PRIMITIVE, "primitive"},
        {CALLABLE_SUBTYPE_CONTINUATION, "continuation"},
    };

    header_backed_value_t storage = {0};
    eshkol_tagged_value_t value = {0};
    value.type = ESHKOL_VALUE_CALLABLE;
    value.data.ptr_val = (uint64_t)(uintptr_t)&storage.payload;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        storage.header.subtype = cases[i].subtype;
        check_type_symbol(value, cases[i].expected, cases[i].expected);
    }

    storage.header.subtype = UINT8_C(5);
    check_type_symbol(value, "procedure", "reserved callable subtype");
    storage.header.subtype = UINT8_C(255);
    check_type_symbol(value, "procedure", "unknown callable subtype");
}

static void test_float32_and_unknown_controls(void) {
    check_type_symbol((eshkol_tagged_value_t){0}, "null", "zero carrier");
    eshkol_tagged_value_t null_result = eshkol_type_of_ref_v1(NULL);
    check(null_result.type == ESHKOL_VALUE_HEAP_PTR &&
              null_result.data.ptr_val != 0 &&
              strcmp((const char*)(uintptr_t)null_result.data.ptr_val,
                     "unknown") == 0,
          "null helper argument", "did not report unknown");

    eshkol_tagged_value_t canonical = {0};
    check(eshkol_value_f32_from_bits_v1(&canonical, UINT32_C(0x7fc12345)) == 0,
          "canonical float32", "fixture construction failed");
    check_type_symbol(canonical, "float32", "canonical float32");

    eshkol_tagged_value_t malformed = canonical;
    malformed.flags = 0;
    check_type_symbol(malformed, "unknown", "float32 missing inexact flag");
    malformed = canonical;
    malformed.flags |= ESHKOL_VALUE_EXACT_FLAG;
    check_type_symbol(malformed, "unknown", "float32 extra flag");
    for (size_t byte = offsetof(eshkol_tagged_value_t, reserved);
         byte < offsetof(eshkol_tagged_value_t, reserved) + sizeof(uint16_t);
         ++byte) {
        malformed = canonical;
        ((uint8_t*)&malformed)[byte] = UINT8_C(1);
        check_type_symbol_ref(&malformed, "unknown",
                              "float32 nonzero reserved byte");
    }
    for (size_t byte = 4; byte < offsetof(eshkol_tagged_value_t, data); ++byte) {
        malformed = canonical;
        ((uint8_t*)&malformed)[byte] = UINT8_C(1);
        check_type_symbol_ref(&malformed, "unknown",
                              "float32 nonzero ABI padding");
    }
    for (size_t byte = offsetof(eshkol_tagged_value_t, data) + sizeof(uint32_t);
         byte < sizeof(eshkol_tagged_value_t); ++byte) {
        malformed = canonical;
        ((uint8_t*)&malformed)[byte] = UINT8_C(1);
        check_type_symbol(malformed, "unknown",
                          "float32 nonzero upper payload byte");
    }

    static const uint8_t unknown_tags[] = {12, 15, 20, 27, 31, 41, 43, 255};
    for (size_t i = 0; i < sizeof(unknown_tags) / sizeof(unknown_tags[0]); ++i) {
        eshkol_tagged_value_t value = {0};
        value.type = unknown_tags[i];
        check_type_symbol(value, "unknown", "undeclared direct tag");
    }
}

int main(void) {
    test_declared_direct_tags();
    test_heap_subtypes();
    test_callable_subtypes();
    test_float32_and_unknown_controls();
    if (failures != 0) {
        fprintf(stderr, "runtime_type_symbol_c_abi_test: %d failure(s)\n",
                failures);
        return 1;
    }
    puts("runtime_type_symbol_c_abi_test: PASS");
    return 0;
}
