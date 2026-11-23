/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
extern int eshkol_capability_runtime_allows(const char *) __attribute__((weak_import));
#else
extern int eshkol_capability_runtime_allows(const char *) __attribute__((weak));
#endif

/* The standalone stream harness has no hosted exception machinery. Honor the
 * same per-test checker at the shared launcher, and fail closed on denial.
 * Keep it optional so the missing-checker executable can exercise refusal. */
void eshkol_capability_enforce(const char *capability, const char *operation) {
    if (eshkol_capability_runtime_allows && eshkol_capability_runtime_allows(capability))
        return;
    fprintf(stderr, "FAIL: standalone subprocess capability denied: %s (%s)\n",
            capability, operation);
    abort();
}
