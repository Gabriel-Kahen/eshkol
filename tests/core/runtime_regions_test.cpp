#include "../../lib/core/arena_memory.h"

#include <cstdint>
#include <csetjmp>
#include <cstring>
#include <iostream>
#include <limits>

namespace {

extern "C" void eshkol_clear_current_exception(void);

int fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    return 1;
}

void set_int(eshkol_tagged_value_t& value, int64_t n) {
    value.type = ESHKOL_VALUE_INT64;
    value.flags = 0;
    value.reserved = 0;
    value.data.int_val = n;
}

bool open_has_size(const eshkol_tagged_value_t* a,
                   const eshkol_tagged_value_t* b,
                   uint64_t expected) {
    eshkol_tagged_value_t out{};
    const uint64_t depth = region_get_depth();
    eshkol_region_open_builtin(&out, a, b, 1);
    const bool ok = out.type == ESHKOL_VALUE_INT64 &&
                    region_get_depth() == depth + 1 &&
                    region_current() != nullptr &&
                    region_current()->size_hint == expected;
    if (out.type == ESHKOL_VALUE_INT64) {
        (void)eshkol_region_handle_close(out.data.int_val, nullptr, 0);
    }
    return ok && region_get_depth() == depth;
}

bool rejects_region_size(const eshkol_tagged_value_t* a,
                         const eshkol_tagged_value_t* b,
                         const char* diagnostic) {
    eshkol_tagged_value_t out{};
    out.type = ESHKOL_VALUE_INT64;
    out.data.int_val = INT64_C(0x123456789abcdef);
    const uint64_t depth = region_get_depth();
    eshkol_clear_current_exception();
    jmp_buf handler;
    volatile int transferred = 0;
    eshkol_push_exception_handler(&handler);
    if (setjmp(handler) == 0) {
        eshkol_region_open_builtin(&out, a, b, 1);
    } else {
        transferred = 1;
    }
    eshkol_pop_exception_handler();
    const bool ok = transferred == 1 && region_get_depth() == depth &&
                    out.type == ESHKOL_VALUE_INT64 &&
                    out.data.int_val == INT64_C(0x123456789abcdef) &&
                    g_current_exception != nullptr &&
                    g_current_exception->type == ESHKOL_EXCEPTION_TYPE_ERROR &&
                    g_current_exception->message != nullptr &&
                    std::strcmp(g_current_exception->message, diagnostic) == 0;
    eshkol_clear_current_exception();
    return ok;
}

}  // namespace

int main() {
    arena_t* shared = get_global_arena_shared();
    if (!shared) return fail("shared global arena is null");
    if (get_global_arena_shared() != shared) return fail("shared global arena changed");

    eshkol_thread_init_worker(2048);
    arena_t* local = arena_get_thread_local();
    if (!local) return fail("thread-local arena is null after worker init");
    if (local == shared) return fail("thread-local arena did not override shared global arena");
    if (get_global_arena() != local) return fail("get_global_arena did not return worker arena");

    eshkol_tagged_value_t f32_size{};
    if (eshkol_value_f32_from_bits_v1(&f32_size, UINT32_C(0x45800400)) !=
        ESHKOL_VALUE_F32_OK) {
        return fail("canonical f32 size construction failed");
    }
    eshkol_tagged_value_t f64_size{};
    f64_size.type = ESHKOL_VALUE_DOUBLE;
    f64_size.flags = ESHKOL_VALUE_INEXACT_FLAG;
    f64_size.data.double_val = 4096.5;
    if (!open_has_size(&f32_size, nullptr, 4096) ||
        !open_has_size(&f64_size, nullptr, 4096)) {
        return fail("lone f32 region size did not match double conversion");
    }
    eshkol_tagged_value_t name{};
    name.type = ESHKOL_VALUE_SYMBOL;
    name.data.ptr_val = reinterpret_cast<uint64_t>("f32-sized");
    if (!open_has_size(&name, &f32_size, 4096) ||
        !open_has_size(&name, &f64_size, 4096)) {
        return fail("second-argument f32 region size did not match double conversion");
    }

    static constexpr char kMalformedDiagnostic[] =
        "region-open: malformed float32 size hint";
    eshkol_tagged_value_t malformed = f32_size;
    malformed.flags = 0;
    if (!rejects_region_size(&malformed, nullptr, kMalformedDiagnostic) ||
        !rejects_region_size(&name, &malformed, kMalformedDiagnostic)) {
        return fail("region-open accepted f32 with missing inexact flag");
    }
    malformed = f32_size;
    malformed.reserved = 1;
    if (!rejects_region_size(&malformed, nullptr, kMalformedDiagnostic)) {
        return fail("region-open accepted f32 with nonzero reserved field");
    }
    malformed = f32_size;
    reinterpret_cast<unsigned char*>(&malformed)[4] = 1;
    if (!rejects_region_size(&name, &malformed, kMalformedDiagnostic)) {
        return fail("region-open accepted f32 with nonzero implicit padding");
    }
    malformed = f32_size;
    malformed.data.raw_val |= UINT64_C(1) << 32;
    if (!rejects_region_size(&malformed, nullptr, kMalformedDiagnostic)) {
        return fail("region-open accepted f32 with nonzero upper payload");
    }
    malformed = f32_size;
    malformed.type = ESHKOL_VALUE_FLOAT32 | ESHKOL_VALUE_INEXACT_FLAG;
    if (!rejects_region_size(&malformed, nullptr, kMalformedDiagnostic) ||
        !rejects_region_size(&name, &malformed, kMalformedDiagnostic)) {
        return fail("region-open accepted folded f32 tag");
    }

    static constexpr char kRangeDiagnostic[] =
        "region-open: size hint is non-finite or out of range";
    eshkol_tagged_value_t f32_max{};
    eshkol_tagged_value_t f32_inf{};
    if (eshkol_value_f32_from_bits_v1(&f32_max, UINT32_C(0x7f7fffff)) !=
            ESHKOL_VALUE_F32_OK ||
        eshkol_value_f32_from_bits_v1(&f32_inf, UINT32_C(0x7f800000)) !=
            ESHKOL_VALUE_F32_OK ||
        !rejects_region_size(&f32_max, nullptr, kRangeDiagnostic) ||
        !rejects_region_size(&name, &f32_inf, kRangeDiagnostic)) {
        return fail("region-open accepted out-of-range or infinite f32 size");
    }
    eshkol_tagged_value_t f64_invalid = f64_size;
    f64_invalid.data.double_val = 0x1p64;
    if (!rejects_region_size(&f64_invalid, nullptr, kRangeDiagnostic)) {
        return fail("region-open accepted out-of-range double size");
    }
    f64_invalid.data.double_val = std::numeric_limits<double>::quiet_NaN();
    if (!rejects_region_size(&name, &f64_invalid, kRangeDiagnostic)) {
        return fail("region-open accepted non-finite double size");
    }

    void* fallback_alloc = region_allocate(24);
    if (!fallback_alloc) return fail("region_allocate fallback returned null");

    eshkol_region_t* outer = region_create("outer", 4096);
    if (!outer) return fail("outer region create failed");
    region_push(outer);
    if (region_get_depth() != 1) return fail("outer region depth mismatch");
    if (region_current() != outer) return fail("outer is not current region");
    if (!region_get_name(outer) || std::strcmp(region_get_name(outer), "outer") != 0) {
        return fail("outer region name mismatch");
    }

    auto* bytes = static_cast<unsigned char*>(region_allocate_zeroed(16));
    if (!bytes) return fail("region zeroed allocation failed");
    for (int i = 0; i < 16; ++i) {
        if (bytes[i] != 0) return fail("region zeroed allocation was not zero-filled");
    }
    std::memcpy(bytes, "region", 7);

    auto* escaped_outer = static_cast<unsigned char*>(region_escape(bytes, 7));
    if (!escaped_outer) return fail("region_escape to global failed");
    if (escaped_outer == bytes) return fail("region_escape did not copy while inside region");
    if (std::memcmp(escaped_outer, "region", 7) != 0) return fail("region_escape copied wrong bytes");
    if (outer->escape_count != 1) return fail("outer escape count after raw escape mismatch");

    const char* escaped_string = static_cast<const char*>(region_escape_string("hello"));
    if (!escaped_string) return fail("region_escape_string returned null");
    if (std::strcmp(escaped_string, "hello") != 0) return fail("escaped string content mismatch");
    if (ESHKOL_GET_HEADER(escaped_string)->subtype != HEAP_SUBTYPE_STRING) {
        return fail("escaped string is not header-backed");
    }
    if (outer->escape_count != 2) return fail("outer escape count after string escape mismatch");

    arena_tagged_cons_cell_t* cell = region_allocate_tagged_cons_cell();
    if (!cell) return fail("region cons allocation failed");
    set_int(cell->car, 11);
    set_int(cell->cdr, 22);
    eshkol_tagged_value_t malformed_f32;
    std::memset(&malformed_f32, 0x5a, sizeof(malformed_f32));
    malformed_f32.type = ESHKOL_VALUE_FLOAT32;
    malformed_f32.flags = ESHKOL_VALUE_INEXACT_FLAG;
    malformed_f32.reserved = 0;
    malformed_f32.data.raw_val = 0x000000003f800000ULL;
    std::memcpy(&cell->car, &malformed_f32, sizeof(malformed_f32));
    arena_tagged_cons_cell_t* escaped_cell = region_escape_tagged_cons_cell(cell);
    if (!escaped_cell) return fail("region_escape_tagged_cons_cell returned null");
    if (escaped_cell == cell) return fail("region_escape_tagged_cons_cell did not copy");
    if (std::memcmp(&escaped_cell->car, &malformed_f32,
                    sizeof(malformed_f32)) != 0 ||
        escaped_cell->cdr.data.int_val != 22) {
        return fail("escaped cons cell contents mismatch");
    }
    if (outer->escape_count != 3) return fail("outer escape count after cons escape mismatch");

    char* local_string = static_cast<char*>(arena_allocate_string_with_header(outer->arena, 3));
    if (!local_string) return fail("local string allocation failed");
    std::memcpy(local_string, "abc", 4);
    eshkol_tagged_value_t tagged{};
    tagged.type = ESHKOL_VALUE_HEAP_PTR;
    tagged.data.ptr_val = reinterpret_cast<uint64_t>(local_string);
    eshkol_tagged_value_t escaped_tagged = region_escape_tagged_value(tagged);
    if (escaped_tagged.data.ptr_val == tagged.data.ptr_val) {
        return fail("region_escape_tagged_value did not copy heap value");
    }
    const char* escaped_tagged_string =
        reinterpret_cast<const char*>(escaped_tagged.data.ptr_val);
    if (std::strcmp(escaped_tagged_string, "abc") != 0) {
        return fail("escaped tagged string content mismatch");
    }
    if (outer->escape_count != 4) return fail("outer escape count after tagged escape mismatch");

    eshkol_region_t* inner = region_create("inner", 2048);
    if (!inner) return fail("inner region create failed");
    region_push(inner);
    if (region_get_depth() != 2) return fail("inner region depth mismatch");
    if (inner->parent != outer) return fail("inner parent mismatch");

    char* inner_data = static_cast<char*>(region_allocate(6));
    if (!inner_data) return fail("inner allocation failed");
    std::memcpy(inner_data, "inner", 6);
    char* escaped_inner = static_cast<char*>(region_escape(inner_data, 6));
    if (!escaped_inner) return fail("inner escape failed");
    if (std::strcmp(escaped_inner, "inner") != 0) return fail("inner escape content mismatch");
    if (inner->escape_count != 1) return fail("inner escape count mismatch");
    region_pop();
    if (region_get_depth() != 1) return fail("depth after inner pop mismatch");
    if (std::strcmp(escaped_inner, "inner") != 0) {
        return fail("inner escaped data did not survive inner pop");
    }

    eshkol_region_t* active = region_create("active", 1024);
    if (!active) return fail("active region create failed");
    region_push(active);
    if (region_get_depth() != 2) return fail("active region depth mismatch");
    region_destroy(active);
    if (region_get_depth() != 1) return fail("active region destroy did not pop once");

    region_pop();
    if (region_get_depth() != 0) return fail("depth after outer pop mismatch");

    arena_t* dest = arena_create(1024);
    arena_t* src = arena_create(2048);
    if (!dest || !src) return fail("merge test arena create failed");
    (void)arena_allocate(dest, 32);
    void* src_value = arena_allocate(src, 64);
    if (!src_value) return fail("merge source allocation failed");
    const size_t src_total = src->total_allocated;
    arena_merge_to_parent(dest, src);
    if (src->current_block != nullptr) return fail("merge did not transfer source blocks");
    if (src->total_allocated != 0) return fail("merge did not clear source total");
    if (dest->total_allocated < src_total) return fail("merge did not account destination total");
    arena_destroy(src);
    arena_destroy(dest);

    eshkol_thread_shutdown_worker();
    if (region_get_depth() != 0) return fail("worker shutdown did not clear region stack");
    if (arena_get_thread_local() != shared) {
        return fail("thread-local arena still overrides global after shutdown");
    }

    std::cout << "PASS\n";
    return 0;
}
