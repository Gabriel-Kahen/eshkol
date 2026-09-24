#include "../../lib/core/arena_memory.h"
#include "../../inc/eshkol/core/bignum.h"

#include <cstring>
#include <iostream>

namespace {

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

eshkol_tagged_value_t make_null() {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_NULL;
    return value;
}

eshkol_tagged_value_t make_int(int64_t n) {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_INT64;
    value.data.int_val = n;
    return value;
}

eshkol_tagged_value_t make_bool(bool b) {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_BOOL;
    value.data.int_val = b ? 1 : 0;
    return value;
}

eshkol_tagged_value_t make_double(double d) {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_DOUBLE;
    value.data.double_val = d;
    return value;
}

void make_f32(eshkol_tagged_value_t* value, uint32_t bits) {
    std::memset(value, 0, sizeof(*value));
    (void)eshkol_value_f32_from_bits_v1(value, bits);
}

eshkol_tagged_value_t make_heap(void* ptr) {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_HEAP_PTR;
    value.data.ptr_val = reinterpret_cast<uint64_t>(ptr);
    return value;
}

eshkol_tagged_value_t make_bignum(arena_t* arena, const char* digits) {
    eshkol_bignum_t* bn = eshkol_bignum_from_string(arena, digits, std::strlen(digits));
    if (!bn) return make_null();
    return make_heap(bn);
}

eshkol_tagged_value_t make_legacy_string(const char* str) {
    eshkol_tagged_value_t value{};
    value.type = ESHKOL_VALUE_STRING_PTR;
    value.data.ptr_val = reinterpret_cast<uint64_t>(str);
    return value;
}

eshkol_tagged_value_t make_heap_string(arena_t* arena, const char* str) {
    const size_t len = std::strlen(str);
    char* data = arena_allocate_string_with_header(arena, len);
    if (!data) return make_null();
    std::memcpy(data, str, len + 1);
    return make_heap(data);
}

eshkol_tagged_value_t make_heap_symbol(arena_t* arena, const char* str) {
    const size_t len = std::strlen(str);
    char* data = static_cast<char*>(
        arena_allocate_with_header(arena, len + 1, HEAP_SUBTYPE_SYMBOL, 0));
    if (!data) return make_null();
    std::memcpy(data, str, len + 1);
    return make_heap(data);
}

eshkol_tagged_value_t make_cons(arena_t* arena,
                                const eshkol_tagged_value_t& car,
                                const eshkol_tagged_value_t& cdr) {
    arena_tagged_cons_cell_t* cell = arena_allocate_cons_with_header(arena);
    if (!cell) return make_null();
    arena_tagged_cons_set_tagged_value(cell, false, &car);
    arena_tagged_cons_set_tagged_value(cell, true, &cdr);
    return make_heap(cell);
}

eshkol_tagged_value_t make_vector2(arena_t* arena,
                                   const eshkol_tagged_value_t& a,
                                   const eshkol_tagged_value_t& b) {
    auto* data = static_cast<uint8_t*>(arena_allocate_vector_with_header(arena, 2));
    if (!data) return make_null();
    *reinterpret_cast<int64_t*>(data) = 2;
    auto* elems = reinterpret_cast<eshkol_tagged_value_t*>(data + 8);
    std::memcpy(&elems[0], &a, sizeof(a));
    std::memcpy(&elems[1], &b, sizeof(b));
    return make_heap(data);
}

int64_t double_bits(double d) {
    union {
        double d;
        int64_t i;
    } u{};
    u.d = d;
    return u.i;
}

eshkol_tagged_value_t make_tensor2x2(arena_t* arena, double a, double b, double c, double d) {
    eshkol_tensor_t* tensor = arena_allocate_tensor_full(arena, 2, 4);
    if (!tensor) return make_null();
    tensor->dimensions[0] = 2;
    tensor->dimensions[1] = 2;
    tensor->elements[0] = double_bits(a);
    tensor->elements[1] = double_bits(b);
    tensor->elements[2] = double_bits(c);
    tensor->elements[3] = double_bits(d);
    return make_heap(tensor);
}

bool deep_equal(const eshkol_tagged_value_t& a, const eshkol_tagged_value_t& b) {
    return eshkol_deep_equal(&a, &b);
}

}  // namespace

int main() {
    arena_t* arena = arena_create(8192);
    if (!arena) return fail("arena_create returned null");

    if (!eshkol_deep_equal(nullptr, nullptr)) return fail("null pointers should compare equal");
    eshkol_tagged_value_t zero = make_int(0);
    if (eshkol_deep_equal(&zero, nullptr)) return fail("value vs null pointer should not compare equal");

    if (!deep_equal(make_int(130), make_int(130))) return fail("matching int64 values not equal");
    if (deep_equal(make_int(130), make_int(131))) return fail("different int64 values equal");
    if (!deep_equal(make_int(130), make_double(130.0))) return fail("numeric int/double equality failed");
    if (deep_equal(make_bool(true), make_bool(false))) return fail("different booleans equal");

    eshkol_tagged_value_t f32_pos_zero, f32_neg_zero, f32_one;
    eshkol_tagged_value_t f32_next, f32_subnormal, f32_qnan;
    make_f32(&f32_pos_zero, UINT32_C(0x00000000));
    make_f32(&f32_neg_zero, UINT32_C(0x80000000));
    make_f32(&f32_one, UINT32_C(0x3f800000));
    make_f32(&f32_next, UINT32_C(0x3f800001));
    make_f32(&f32_subnormal, UINT32_C(0x00000001));
    make_f32(&f32_qnan, UINT32_C(0x7fc12345));
    if (!deep_equal(f32_pos_zero, f32_neg_zero)) return fail("f32 signed zeros not equal");
    if (hash_tagged_value(&f32_pos_zero) != hash_tagged_value(&f32_neg_zero)) {
        return fail("equal f32 signed zeros hashed differently");
    }
    if (!hash_keys_equal(&f32_pos_zero, &f32_neg_zero)) {
        return fail("f32 signed-zero hash-key equality failed");
    }
    if (!deep_equal(f32_one, f32_one)) return fail("matching canonical f32 values not equal");
    if (deep_equal(f32_one, f32_next)) return fail("different canonical f32 values equal");
    if (!deep_equal(f32_subnormal, f32_subnormal)) return fail("matching f32 subnormal not equal");
    if (deep_equal(f32_qnan, f32_qnan) || hash_keys_equal(&f32_qnan, &f32_qnan)) {
        return fail("f32 NaN compared equal");
    }
    if (deep_equal(f32_one, make_double(1.0)) || deep_equal(f32_one, make_int(1))) {
        return fail("f32 crossed the same-tag equality boundary");
    }

    eshkol_tagged_value_t malformed_f32 = f32_one;
    malformed_f32.flags = 0;
    if (deep_equal(malformed_f32, malformed_f32) ||
        hash_keys_equal(&malformed_f32, &malformed_f32)) {
        return fail("malformed f32 compared equal");
    }
    eshkol_tagged_value_t folded_f32 = f32_one;
    folded_f32.type = 27;
    if (deep_equal(folded_f32, f32_one) || hash_keys_equal(&folded_f32, &f32_one)) {
        return fail("folded tag 27 compared as f32");
    }

    eshkol_hash_table_t* f32_table = arena_hash_table_create(arena);
    if (!f32_table || !hash_table_set(arena, f32_table, &f32_pos_zero, &f32_one)) {
        return fail("f32 hash-table fixture insertion failed");
    }
    eshkol_tagged_value_t f32_lookup{};
    if (!hash_table_get(f32_table, &f32_neg_zero, &f32_lookup) ||
        !deep_equal(f32_lookup, f32_one)) {
        return fail("f32 signed-zero hash-table lookup failed");
    }

    if (!deep_equal(make_bignum(arena, "9223372036854775808"),
                    make_bignum(arena, "9223372036854775808"))) {
        return fail("matching bignums not equal");
    }
    if (deep_equal(make_bignum(arena, "9223372036854775808"),
                   make_bignum(arena, "9223372036854775809"))) {
        return fail("different bignums equal");
    }
    if (!deep_equal(make_bignum(arena, "130"), make_int(130))) {
        return fail("bignum/int64 equality failed");
    }
    if (deep_equal(make_bignum(arena, "131"), make_int(130))) {
        return fail("different bignum/int64 values equal");
    }

    if (!deep_equal(make_legacy_string("same"), make_heap_string(arena, "same"))) {
        return fail("legacy/header string equality failed");
    }
    if (deep_equal(make_heap_string(arena, "same"), make_heap_string(arena, "different"))) {
        return fail("different header strings equal");
    }

    if (!deep_equal(make_heap_symbol(arena, "alpha"), make_heap_symbol(arena, "alpha"))) {
        return fail("matching symbols not equal");
    }
    if (deep_equal(make_heap_symbol(arena, "alpha"), make_heap_symbol(arena, "beta"))) {
        return fail("different symbols equal");
    }

    const eshkol_tagged_value_t nil = make_null();
    eshkol_tagged_value_t list1 =
        make_cons(arena, make_int(1), make_cons(arena, make_heap_string(arena, "tail"), nil));
    eshkol_tagged_value_t list2 =
        make_cons(arena, make_int(1), make_cons(arena, make_heap_string(arena, "tail"), nil));
    eshkol_tagged_value_t list3 =
        make_cons(arena, make_int(1), make_cons(arena, make_heap_string(arena, "other"), nil));
    if (!deep_equal(list1, list2)) return fail("matching nested cons lists not equal");
    if (deep_equal(list1, list3)) return fail("different nested cons lists equal");

    eshkol_tagged_value_t vec1 = make_vector2(arena, list1, make_double(4.5));
    eshkol_tagged_value_t vec2 = make_vector2(arena, list2, make_double(4.5));
    eshkol_tagged_value_t vec3 = make_vector2(arena, list2, make_double(5.5));
    if (!deep_equal(vec1, vec2)) return fail("matching vectors not equal");
    if (deep_equal(vec1, vec3)) return fail("different vectors equal");

    eshkol_tagged_value_t f32_vec1 = make_vector2(arena, f32_one, f32_subnormal);
    eshkol_tagged_value_t f32_vec2 = make_vector2(arena, f32_one, f32_subnormal);
    eshkol_tagged_value_t f32_vec3 = make_vector2(arena, f32_next, f32_subnormal);
    if (!deep_equal(f32_vec1, f32_vec2)) return fail("matching nested f32 vectors not equal");
    if (deep_equal(f32_vec1, f32_vec3)) return fail("different nested f32 vectors equal");

    eshkol_tagged_value_t tensor1 = make_tensor2x2(arena, 1.0, 2.0, 3.0, -0.0);
    eshkol_tagged_value_t tensor2 = make_tensor2x2(arena, 1.0, 2.0, 3.0, 0.0);
    eshkol_tagged_value_t tensor3 = make_tensor2x2(arena, 1.0, 2.0, 3.0, 9.0);
    if (!deep_equal(tensor1, tensor2)) return fail("matching tensor values not equal");
    if (deep_equal(tensor1, tensor3)) return fail("different tensor values equal");

    arena_destroy(arena);
    std::cout << "PASS\n";
    return 0;
}
