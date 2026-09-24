/*
 * Copyright (C) tsotchke
 *
 * SPDX-License-Identifier: MIT
 *
 * Symbol interning — process-global canonical symbol table with its
 * own process-lifetime backing store (NOT the global arena).
 *
 * Why a dedicated backing store:
 *   Symbols are process-lifetime by design — R7RS §6.5 requires eq?
 *   on symbol literals to hold across every module in the same image,
 *   so the canonical pointer must survive as long as any code that
 *   could reference it. The obvious backing store is the main
 *   arena, but the main arena gets reset during runtime lifecycle
 *   events (REPL restart, test-batch boundary, Python binding
 *   destroy) and between embedded instances — every reset dangles
 *   every interned symbol's char buffer. Storing them here, in a
 *   per-page malloc'd pool breaks that coupling: symbol pointers stay
 *   valid regardless of arena lifecycle, and dual-instance embedding
 *   becomes safe. The table is cleared only at process exit so leak
 *   checkers do not report intentional process-lifetime storage.
 *
 *   Each symbol needs an 8-byte ESHKOL_OBJECT_HEADER immediately
 *   preceding its char data so ESHKOL_GET_HEADER(ptr) at runtime
 *   reads HEAP_SUBTYPE_SYMBOL (not whatever happens to live there).
 *   We allocate (header + chars + NUL) as one block and return
 *   a pointer past the header, matching the invariant the runtime
 *   relies on.
 *
 * This TU also lives in eshkol-static so codegen-emitted calls to
 * eshkol_intern_symbol_lookup (symbol literals in compiled stdlib.o)
 * don't drag introspection.cpp.o into user binaries. All runtime
 * helpers that need interning — string->symbol, procedure-name,
 * type-of — route through the single shared table here.
 */

#include <eshkol/eshkol.h>
#include <eshkol/core/introspection.h>
#include <eshkol/exhaustive_dispatch.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

struct InternedEntry {
    char* symbol_ptr;  /* points past the header to the NUL-terminated data */
};

std::mutex g_symbol_mutex;
std::unordered_map<std::string, InternedEntry> g_interned_symbols;

/*
 * Allocate `len+1` bytes for a symbol's character data, preceded by
 * an 8-byte ESHKOL_OBJECT_HEADER with subtype=HEAP_SUBTYPE_SYMBOL.
 * Returns a pointer to the char data (header sits at ptr-8).
 *
 * Backed by plain malloc because symbols are process-lifetime. An
 * arena's bulk-reset is not helpful here and is actively harmful.
 * Small per-symbol overhead (~40 bytes including malloc bookkeeping)
 * times ~10k symbols in a large process is ~400 KB.
 */
char* alloc_symbol_block(const char* src, size_t len) {
    size_t header_sz = sizeof(eshkol_object_header_t);
    size_t total = header_sz + len + 1;
    uint8_t* block = static_cast<uint8_t*>(std::malloc(total));
    if (!block) return nullptr;
    eshkol_object_header_t* hdr = reinterpret_cast<eshkol_object_header_t*>(block);
    hdr->subtype = HEAP_SUBTYPE_SYMBOL;
    hdr->flags = 0;
    hdr->ref_count = 0;
    hdr->size = static_cast<uint32_t>(len + 1);
    char* data = reinterpret_cast<char*>(block + header_sz);
    std::memcpy(data, src, len);
    data[len] = '\0';
    return data;
}

/** Free a symbol's char block previously returned by alloc_symbol_block,
 *  accounting for the header offset. No-op if `symbol_ptr` is NULL. */
void free_symbol_block(char* symbol_ptr) {
    if (!symbol_ptr) return;
    std::free(reinterpret_cast<uint8_t*>(symbol_ptr) -
              sizeof(eshkol_object_header_t));
}

/** Free every interned symbol's backing block and clear the table.
 *  Registered via std::atexit so it runs once at process exit. */
void cleanup_symbol_table() {
    std::lock_guard<std::mutex> lock(g_symbol_mutex);
    for (auto& item : g_interned_symbols) {
        free_symbol_block(item.second.symbol_ptr);
    }
    g_interned_symbols.clear();
}

const bool g_cleanup_registered = [] {
    std::atexit(cleanup_symbol_table);
    return true;
}();

} /* anonymous namespace */

/*
 * Return the canonical char* pointer for a symbol spelled `name`.
 * The pointer refers to a NUL-terminated string with the SYMBOL
 * header at ptr-8 (so ESHKOL_GET_HEADER works). Every distinct
 * spelling maps to exactly one pointer so eq? on symbol literals
 * across modules is pointer equality (R7RS §6.5).
 *
 * Codegen emits `call` to this function for every symbol literal
 * and every user-facing interning path (string->symbol,
 * procedure-name, type-of) routes through it too, so runtime and
 * compile-time interning stay unified.
 *
 * Thread-safe. Returns nullptr on allocation failure or if `name`
 * is NULL.
 */
extern "C" void* eshkol_intern_symbol_lookup(const char* name) {
    if (!name) return nullptr;

    std::string key(name);

    /* Fast path: already interned. */
    {
        std::lock_guard<std::mutex> lock(g_symbol_mutex);
        auto it = g_interned_symbols.find(key);
        if (it != g_interned_symbols.end()) {
            return it->second.symbol_ptr;
        }
    }

    /* Allocate outside the lock — malloc is fast but we still don't
     * want to hold the interning lock across it. */
    char* sym_str = alloc_symbol_block(name, key.size());
    if (!sym_str) return nullptr;

    /* Insert. Re-check in case another thread won the race — if so,
     * free our speculative allocation so the canonical pointer
     * stays singular. */
    {
        std::lock_guard<std::mutex> lock(g_symbol_mutex);
        auto it = g_interned_symbols.find(key);
        if (it != g_interned_symbols.end()) {
            free_symbol_block(sym_str);
            return it->second.symbol_ptr;
        }
        g_interned_symbols[key] = {sym_str};
    }
    return sym_str;
}

namespace {

const char* heap_semantic_type_name(const eshkol_tagged_value_t& value) {
    if (value.data.ptr_val == 0) return "heap-object";

    const auto* header = ESHKOL_GET_HEADER(
        reinterpret_cast<const void*>(value.data.ptr_val));
    if (!header || !eshkol_heap_subtype_is_declared(header->subtype)) {
        return "heap-object";
    }

    ESHKOL_EXHAUSTIVE_SWITCH_BEGIN
    switch (static_cast<heap_subtype_t>(header->subtype)) {
        case HEAP_SUBTYPE_CONS:           return "pair";
        case HEAP_SUBTYPE_STRING:         return "string";
        case HEAP_SUBTYPE_VECTOR:         return "vector";
        case HEAP_SUBTYPE_TENSOR:         return "tensor";
        case HEAP_SUBTYPE_MULTI_VALUE:    return "values";
        case HEAP_SUBTYPE_HASH:           return "hash-table";
        case HEAP_SUBTYPE_EXCEPTION:      return "exception";
        case HEAP_SUBTYPE_RECORD:         return "record";
        case HEAP_SUBTYPE_BYTEVECTOR:     return "bytevector";
        case HEAP_SUBTYPE_PORT:           return "port";
        case HEAP_SUBTYPE_SYMBOL:         return "symbol";
        case HEAP_SUBTYPE_BIGNUM:         return "integer";
        case HEAP_SUBTYPE_SUBSTITUTION:   return "substitution";
        case HEAP_SUBTYPE_FACT:           return "fact";
        case HEAP_SUBTYPE_KNOWLEDGE_BASE: return "knowledge-base";
        case HEAP_SUBTYPE_FACTOR_GRAPH:   return "factor-graph";
        case HEAP_SUBTYPE_WORKSPACE:      return "workspace";
        case HEAP_SUBTYPE_PROMISE:        return "promise";
        case HEAP_SUBTYPE_RATIONAL:       return "rational";
        case HEAP_SUBTYPE_PRNG:           return "prng";
        case HEAP_SUBTYPE_DNC:            return "dnc";
        case HEAP_SUBTYPE_SDNC:           return "sdnc";
        case HEAP_SUBTYPE_TAYLOR:         return "taylor";
        case HEAP_SUBTYPE_PARAMETER:      return "parameter";
        case HEAP_SUBTYPE_I128:           return "i128";
    }
    ESHKOL_EXHAUSTIVE_SWITCH_END
    return "heap-object";
}

const char* callable_semantic_type_name(const eshkol_tagged_value_t& value) {
    if (value.data.ptr_val == 0) return "procedure";

    const auto* header = ESHKOL_GET_HEADER(
        reinterpret_cast<const void*>(value.data.ptr_val));
    if (!header || !eshkol_callable_subtype_is_declared(header->subtype)) {
        return "procedure";
    }

    ESHKOL_EXHAUSTIVE_SWITCH_BEGIN
    switch (static_cast<callable_subtype_t>(header->subtype)) {
        case CALLABLE_SUBTYPE_CLOSURE:      return "closure";
        case CALLABLE_SUBTYPE_LAMBDA_SEXPR: return "lambda-sexpr";
        case CALLABLE_SUBTYPE_AD_NODE:      return "ad-node";
        case CALLABLE_SUBTYPE_PRIMITIVE:    return "primitive";
        case CALLABLE_SUBTYPE_CONTINUATION: return "continuation";
    }
    ESHKOL_EXHAUSTIVE_SWITCH_END
    return "procedure";
}

const char* semantic_type_name(const eshkol_tagged_value_t& value) {
    ESHKOL_EXHAUSTIVE_SWITCH_BEGIN
    switch (static_cast<eshkol_value_type_t>(value.type)) {
        case ESHKOL_VALUE_NULL:         return "null";
        case ESHKOL_VALUE_INT64:        return "integer";
        case ESHKOL_VALUE_DOUBLE:       return "real";
        case ESHKOL_VALUE_BOOL:         return "boolean";
        case ESHKOL_VALUE_CHAR:         return "char";
        case ESHKOL_VALUE_SYMBOL:       return "symbol";
        case ESHKOL_VALUE_DUAL_NUMBER:  return "dual-number";
        case ESHKOL_VALUE_COMPLEX:      return "complex";
        case ESHKOL_VALUE_HEAP_PTR:     return heap_semantic_type_name(value);
        case ESHKOL_VALUE_CALLABLE:     return callable_semantic_type_name(value);
        case ESHKOL_VALUE_LOGIC_VAR:    return "logic-variable";
        case ESHKOL_VALUE_FLOAT32:
            return eshkol_value_is_f32_v1(&value) ? "float32" : "unknown";
        case ESHKOL_VALUE_HANDLE:       return "handle";
        case ESHKOL_VALUE_BUFFER:       return "buffer";
        case ESHKOL_VALUE_STREAM:       return "stream";
        case ESHKOL_VALUE_EVENT:        return "event";
        case ESHKOL_VALUE_CONS_PTR:     return "pair";
        case ESHKOL_VALUE_STRING_PTR:   return "string";
        case ESHKOL_VALUE_VECTOR_PTR:   return "vector";
        case ESHKOL_VALUE_TENSOR_PTR:   return "tensor";
        case ESHKOL_VALUE_HASH_PTR:     return "hash-table";
        case ESHKOL_VALUE_EXCEPTION:    return "exception";
        case ESHKOL_VALUE_CLOSURE_PTR:  return "closure";
        case ESHKOL_VALUE_LAMBDA_SEXPR: return "lambda-sexpr";
        case ESHKOL_VALUE_AD_NODE_PTR:  return "ad-node";
    }
    ESHKOL_EXHAUSTIVE_SWITCH_END
    return "unknown";
}

}  // namespace

extern "C" eshkol_tagged_value_t eshkol_type_of_ref_v1(
    const eshkol_tagged_value_t* value) {
    const char* type_name = value ? semantic_type_name(*value) : "unknown";
    void* symbol = eshkol_intern_symbol_lookup(type_name);

    eshkol_tagged_value_t result{};
    if (!symbol) {
        result.type = ESHKOL_VALUE_BOOL;
        result.data.int_val = 0;
        return result;
    }

    result.type = ESHKOL_VALUE_HEAP_PTR;
    result.data.ptr_val = reinterpret_cast<uint64_t>(symbol);
    return result;
}

extern "C" void eshkol_type_of_ref_v1_store(
    eshkol_tagged_value_t* result,
    const eshkol_tagged_value_t* value) {
    if (!result) return;
    *result = eshkol_type_of_ref_v1(value);
}
