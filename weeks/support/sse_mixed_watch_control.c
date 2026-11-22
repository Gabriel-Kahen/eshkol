#include "eshkol/core/event_loop.h"
#include "eshkol/core/resource_lease.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

void eshkol_event_loop_test_fail_remove_once(void);

typedef struct {
    eshkol_event_loop_t *loop;
    int fd;
    int close_attempts;
    int cleanup_calls;
} FixtureAdapter;

static int cleanup(uintptr_t native, void *data, eshkol_resource_cleanup_result_t *result)
{
    (void)data;
    FixtureAdapter *adapter = (FixtureAdapter *)native;
    ++adapter->cleanup_calls;
    int status = eshkol_event_loop_remove(adapter->loop, adapter->fd);
    if (status != ESHKOL_EVENT_LOOP_OK) {
        *result = (eshkol_resource_cleanup_result_t){
            .state = ESHKOL_CLEANUP_FAILED,
            .stage = ESHKOL_CLEANUP_STAGE_WATCH,
            .os_error = eshkol_event_loop_last_os_error(adapter->loop),
            .retryable = 1
        };
        return ESHKOL_IO_SYSTEM;
    }
    ++adapter->close_attempts;
    assert(close(adapter->fd) == 0);
    return 0;
}

static void check_other_watch(eshkol_event_loop_t *loop, const int other[2], char expected)
{
    assert(write(other[1], &expected, 1) == 1);
    const eshkol_event_t *events;
    int count;
    assert(eshkol_event_loop_poll(loop, 0, &events, &count) == ESHKOL_EVENT_LOOP_OK);
    bool delivered = false;
    for (int i = 0; i < count; ++i) {
        if (events[i].fd == other[0] && events[i].user_data == 777 &&
            (events[i].events & ESHKOL_EVENT_READ)) {
            delivered = true;
        }
    }
    assert(delivered);
    char observed;
    assert(read(other[0], &observed, 1) == 1 && observed == expected);
}

static void cancel_during_borrow(bool fail_removal)
{
    const uint32_t tag = 12, mask = UINT32_C(1) << tag;
    int stream[2], other[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, stream) == 0);
    assert(pipe(other) == 0);
    int64_t loop_id = eshkol_event_loop_open_handle(8);
    assert(loop_id > 0);
    eshkol_event_loop_t *loop = eshkol_event_loop_from_handle(loop_id);
    assert(loop);
    FixtureAdapter adapter = { .loop = loop, .fd = stream[0] };
    int64_t stream_id = eshkol_resource_adopt_on_loop(
        ESHKOL_HANDLE_KIND_SOCKET, tag, (uintptr_t)&adapter, cleanup, NULL, loop_id, 1);
    assert(stream_id > 0);
    assert(eshkol_event_loop_add(loop, stream[0], ESHKOL_EVENT_WRITE, (uint64_t)stream_id) == 0);
    assert(eshkol_event_loop_add(loop, other[0], ESHKOL_EVENT_READ, 777) == 0);

    eshkol_resource_lease_t borrow, rejected;
    assert(eshkol_resource_borrow(stream_id, ESHKOL_HANDLE_KIND_SOCKET, mask, &borrow) == 0);
    static const char prefix[] = "1f\r\ndata: caf\xc3";
    _Static_assert(sizeof(prefix) - 1 == 14, "expected prefix length");
    assert(write(stream[0], prefix, sizeof(prefix) - 1) == sizeof(prefix) - 1);
    assert(eshkol_resource_cancel(stream_id, ESHKOL_HANDLE_KIND_SOCKET, mask) == 0);
    assert(eshkol_resource_check(&borrow) == ESHKOL_IO_CANCELLED);

    int64_t ticket = eshkol_resource_close_request(stream_id, ESHKOL_HANDLE_KIND_SOCKET, mask);
    eshkol_resource_cleanup_result_t result;
    assert(ticket > 0 && eshkol_resource_cleanup_result(ticket, &result) == 0);
    assert(result.state == ESHKOL_CLEANUP_PENDING && adapter.close_attempts == 0);
    assert(fcntl(stream[0], F_GETFD) >= 0);
    assert(eshkol_resource_borrow(stream_id, ESHKOL_HANDLE_KIND_SOCKET, mask, &rejected) == ESHKOL_IO_INVALID);
    check_other_watch(loop, other, 'a');

    if (fail_removal) eshkol_event_loop_test_fail_remove_once();
    eshkol_resource_release(&borrow);
    if (fail_removal) {
        assert(eshkol_resource_cleanup_result(ticket, &result) == 0);
        assert(result.state == ESHKOL_CLEANUP_FAILED && result.retryable);
        assert(result.stage == ESHKOL_CLEANUP_STAGE_WATCH && result.os_error == EIO);
        assert(adapter.cleanup_calls == 1 && adapter.close_attempts == 0);
        assert(eshkol_event_loop_count(loop) == 2);
        assert(fcntl(stream[0], F_GETFD) >= 0);
        assert(eshkol_resource_cleanup_release(ticket) == ESHKOL_IO_BUSY);
        assert(eshkol_event_loop_close_handle(loop_id) == ESHKOL_EVENT_LOOP_EBUSY);
        check_other_watch(loop, other, 'b');
        assert(eshkol_resource_cleanup_retry(ticket) == 0);
    }
    assert(eshkol_resource_cleanup_result(ticket, &result) == 0);
    assert(result.state == ESHKOL_CLEANUP_COMPLETE);
    assert(adapter.close_attempts == 1 && adapter.cleanup_calls == (fail_removal ? 2 : 1));
    assert(eshkol_event_loop_count(loop) == 1);
    assert(eshkol_resource_cleanup_retry(ticket) == ESHKOL_IO_INVALID);
    assert(eshkol_resource_cleanup_release(ticket) == 0);

    char buf[32];
    assert(recv(stream[1], buf, sizeof(buf), MSG_WAITALL) == 14);
    assert(memcmp(buf, prefix, sizeof(prefix) - 1) == 0);
    assert(read(stream[1], buf, sizeof(buf)) == 0);
    check_other_watch(loop, other, 'c');
    assert(eshkol_event_loop_remove(loop, other[0]) == 0);
    assert(close(stream[1]) == 0 && close(other[0]) == 0 && close(other[1]) == 0);
    assert(eshkol_event_loop_close_handle(loop_id) == 0);
    printf("PASS: canceled active borrow, one close, live foreign watch%s\n",
           fail_removal ? ", recoverable watch-removal failure" : "");
}

int main(void)
{
    cancel_during_borrow(false);
    cancel_during_borrow(true);
    return 0;
}
