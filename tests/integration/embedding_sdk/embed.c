#include <eshkol/eshkol_ffi.h>

#include <stdio.h>

static int ffi_error(const char *operation) {
    const char *detail = eshkol_ffi_last_error();
    fprintf(stderr, "%s failed: %s\n", operation, detail ? detail : "no diagnostic");
    return 1;
}

int main(void) {
    eshkol_ffi_context_t *context = eshkol_ffi_init();
    if (!context) {
        return ffi_error("eshkol_ffi_init");
    }

    eshkol_ffi_value_t value;
    if (eshkol_ffi_eval(context, "(+ 40 2)", &value) != 0) {
        int status = ffi_error("eshkol_ffi_eval");
        eshkol_ffi_shutdown(context);
        return status;
    }

    int passed = eshkol_ffi_type(value) == ESHKOL_FFI_TYPE_INT64 &&
                 eshkol_ffi_to_int64(value) == 42;
    eshkol_ffi_shutdown(context);
    if (!passed) {
        fputs("Expected integer 42 from eshkol_ffi_eval\n", stderr);
        return 1;
    }

    puts("eshkol-embedding-sdk-ok:42");
    return 0;
}
