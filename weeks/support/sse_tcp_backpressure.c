#include "eshkol/agent_http_sse_adapter.h"
#include "eshkol/core/event_loop.h"
#include "eshkol/core/resource_lease.h"

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

extern int64_t eshkol_http_server_create(int32_t port);
extern int32_t eshkol_http_server_port(int64_t handle);
extern int32_t eshkol_http_server_accept(int64_t handle, char* buffer, int32_t capacity,
                                        int32_t timeout_ms);
extern void eshkol_http_server_close(int64_t handle);

enum { CHUNK_BYTES = 16 * 1024, MAX_CHUNKS = 8, WATCH_COOKIE = 42 };

static int open_peer(int64_t server) {
    int peer = socket(AF_INET, SOCK_STREAM, 0);
    assert(peer >= 0);
    int buffer_size = 4096;
    struct timeval timeout = {2, 0};
    assert(setsockopt(peer, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size)) == 0);
    assert(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0);
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        .sin_port = htons((uint16_t)eshkol_http_server_port(server)),
    };
    assert(connect(peer, (struct sockaddr*)&address, sizeof(address)) == 0);
    char host[INET_ADDRSTRLEN], request[128], accepted[128];
    assert(inet_ntop(AF_INET, &address.sin_addr, host, sizeof(host)));
    int length = snprintf(request, sizeof(request), "GET /events HTTP/1.1\r\nHost: %s\r\n\r\n", host);
    assert(length > 0 && (size_t)length < sizeof(request));
    assert(send(peer, request, (size_t)length, 0) == length);
    assert(eshkol_http_server_accept(server, accepted, sizeof(accepted), 1000) == length);
    return peer;
}

static void make_chunk(uint8_t* chunk, int index) {
    memcpy(chunk, "3ff8\r\ndata: ", 12);
    memset(chunk + 12, 'a' + index, CHUNK_BYTES - 16);
    memcpy(chunk + CHUNK_BYTES - 4, "\n\n\r\n", 4);
}

int main(void) {
    static uint8_t expected[CHUNK_BYTES * MAX_CHUNKS];
    static uint8_t received[sizeof(expected) + 1];
    int64_t server = eshkol_http_server_create(0);
    int64_t loop_id = eshkol_event_loop_open_handle(8);
    assert(server > 0 && loop_id > 0);
    eshkol_event_loop_t* loop = eshkol_event_loop_from_handle(loop_id);
    assert(loop);
    int peer = open_peer(server);
    int64_t lease = 0;
    eshkol_http_sse_adapter_t* adapter = NULL;
    assert(eshkol_http_sse_adapter_adopt(server, loop_id, &lease, &adapter) == 0);
    int fd = eshkol_http_sse_adapter_fd(adapter);
    int buffer_size = 4096;
    assert(setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size)) == 0);

    int index = 0, stalled = 0;
    size_t offset = 0, short_write = 0;
    int64_t deadline = eshkol_resource_deadline(ESHKOL_SSE_WRITE_BUDGET_MS);
    make_chunk(expected, index);
    for (int step = 0; step < 64 && index < MAX_CHUNKS; ++step) {
        size_t written = 0, before = offset;
        int rc = eshkol_http_sse_adapter_write_step(lease, adapter, expected + index * CHUNK_BYTES,
            CHUNK_BYTES, &offset, deadline, WATCH_COOKIE, &written);
        assert(offset == before + written);
        if (rc == ESHKOL_SSE_WOULD_BLOCK) {
            assert(eshkol_http_sse_adapter_watch_armed(adapter) && offset < CHUNK_BYTES);
            if (!written) {
                stalled = 1;
                break;
            }
            if (!short_write) short_write = written;
            continue;
        }
        assert(rc == 0 && offset == CHUNK_BYTES);
        offset = 0;
        if (++index < MAX_CHUNKS) make_chunk(expected + index * CHUNK_BYTES, index);
        deadline = eshkol_resource_deadline(ESHKOL_SSE_WRITE_BUDGET_MS);
    }
    assert(stalled && short_write);
    size_t stalled_offset = offset;

    size_t size = 0;
    int woke = 0;
    while (offset < CHUNK_BYTES) {
        assert(eshkol_resource_remaining(deadline) > 0);
        ssize_t n = recv(peer, received + size, sizeof(received) - size, MSG_DONTWAIT);
        if (n > 0) size += (size_t)n;
        else assert(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        const eshkol_event_t* events;
        int count;
        assert(eshkol_event_loop_poll(loop, 10, &events, &count) == ESHKOL_EVENT_LOOP_OK);
        if (!count) continue;
        assert(count == 1 && events[0].fd == fd && events[0].user_data == WATCH_COOKIE);
        assert(events[0].events & ESHKOL_EVENT_WRITE);
        woke = 1;
        int rc = eshkol_http_sse_adapter_write_step(lease, adapter, expected + index * CHUNK_BYTES,
            CHUNK_BYTES, &offset, deadline, WATCH_COOKIE, NULL);
        assert(rc == (offset < CHUNK_BYTES ? ESHKOL_SSE_WOULD_BLOCK : 0));
    }
    assert(woke && !eshkol_http_sse_adapter_watch_armed(adapter));
    int64_t ticket = eshkol_http_sse_adapter_request_close(lease);
    eshkol_resource_cleanup_result_t result;
    assert(ticket > 0 && eshkol_resource_cleanup_result(ticket, &result) == 0);
    assert(result.state == ESHKOL_CLEANUP_COMPLETE);
    assert(eshkol_resource_cleanup_release(ticket) == 0);
    for (;;) {
        ssize_t n = recv(peer, received + size, sizeof(received) - size, 0);
        assert(n >= 0);
        if (n == 0) break;
        size += (size_t)n;
    }
    assert(size == (size_t)(index + 1) * CHUNK_BYTES && memcmp(expected, received, size) == 0);
    close(peer);
    eshkol_http_server_close(server);
    assert(eshkol_event_loop_close_handle(loop_id) == ESHKOL_EVENT_LOOP_OK);
    printf("PASS: short write %zu, stalled at offset %zu, wire %zu bytes\n",
           short_write, stalled_offset, size);
    return 0;
}
