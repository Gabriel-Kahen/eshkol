#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct eshkol_subprocess eshkol_subprocess_t;

extern eshkol_subprocess_t* qllm_process_spawn_shell(const char*, const char*, int64_t);
extern eshkol_subprocess_t* qllm_process_spawn_argv(const char*, const char*);
extern int32_t qllm_process_wait(eshkol_subprocess_t*, int32_t);
extern int64_t qllm_process_pid(eshkol_subprocess_t*);
extern int32_t qllm_process_stdout_fd(eshkol_subprocess_t*);
extern int32_t qllm_process_stderr_fd(eshkol_subprocess_t*);
extern int64_t qllm_process_last_stdout_read_length(eshkol_subprocess_t*);
extern void qllm_process_destroy(eshkol_subprocess_t*);

static void check(int condition, const char* message) {
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        exit(1);
    }
}

int main(void) {
    eshkol_subprocess_t* first = qllm_process_spawn_shell("exit 0", ".", 0);
    check(first != NULL, "shell spawn");
    check(qllm_process_wait(first, 5000) == 0, "shell wait");
    qllm_process_destroy(first);
    qllm_process_destroy(first);
    check(qllm_process_pid(first) == 0, "closed pid sentinel");
    check(qllm_process_wait(first, 0) == -1, "closed wait sentinel");
    check(qllm_process_stdout_fd(first) == -1, "closed stdout fd sentinel");
    check(qllm_process_stderr_fd(first) == -1, "closed stderr fd sentinel");
    check(qllm_process_last_stdout_read_length(first) == -1,
          "closed read length sentinel");

    /* A malloc implementation will commonly reuse the first struct's
     * address. Its old opaque token must stay closed across later spawns. */
    for (int i = 0; i < 32; ++i) {
        eshkol_subprocess_t* next = qllm_process_spawn_argv("true", ".");
        check(next != NULL, "argv spawn");
        check(next != first, "new handle has distinct identity");
        check(qllm_process_wait(next, 5000) == 0, "argv wait");
        check(qllm_process_pid(first) == 0, "old handle remains closed");
        qllm_process_destroy(next);
    }

    check(qllm_process_spawn_argv("eshkol-test-no-such-command", ".") == NULL,
          "failed spawn returns null");
    puts("PASS: subprocess handle lifetime");
    return 0;
}
