#define _POSIX_C_SOURCE 200809L

#include "eshkol/core/event_loop.h"
#include "eshkol/core/unix_socket_stream.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

/* The standalone example grants itself the network capability. An integrated
 * runtime supplies its own capability checker and builds without this flag. */
#ifdef ESHKOL_STANDALONE_EXAMPLE
int eshkol_capability_runtime_allows(const char *capability)
{
    return strcmp(capability, "network") == 0;
}
#endif

static int transfer(int64_t sender, int64_t receiver, const char *message, size_t size)
{
    char received[4];
    if (size != sizeof(received)) return -1;
    int64_t deadline = eshkol_resource_deadline(1000);
    size_t written = 0;
    size_t read_total = 0;
    while (read_total < size) {
        int32_t remaining = eshkol_resource_remaining(deadline);
        if (remaining <= 0) return -1;
        if (written < size) {
            int64_t count = eshkol_unix_stream_socket_write(sender, message + written, size - written);
            if (count > 0) written += (size_t)count;
            else if (count != ESHKOL_IO_TIMEOUT) return -1;
        }
        int64_t count = eshkol_unix_stream_socket_read(receiver, received + read_total, size - read_total);
        if (count > 0) {
            read_total += (size_t)count;
            continue;
        }
        if (count != ESHKOL_IO_TIMEOUT) return -1;
        int64_t socket = written < size ? sender : receiver;
        int32_t events = written < size ? ESHKOL_RESOURCE_WRITE : ESHKOL_RESOURCE_READ;
        remaining = eshkol_resource_remaining(deadline);
        if (remaining <= 0 || eshkol_unix_stream_socket_poll(socket, events, remaining) <= 0) {
            return -1;
        }
    }
    if (memcmp(message, received, size) != 0) return -1;
    printf("Received %.*s\n", (int)size, received);
    return 0;
}

static int close_socket(int64_t id)
{
    if (id <= 0) return 0;
    int64_t ticket = eshkol_unix_stream_socket_close_request(id);
    eshkol_resource_cleanup_result_t result;
    if (ticket <= 0 || eshkol_resource_cleanup_result(ticket, &result) != 0) {
        fprintf(stderr, "Could not observe socket cleanup\n");
        return -1;
    }
    int released = eshkol_resource_cleanup_release(ticket);
    if (result.state != ESHKOL_CLEANUP_COMPLETE || released != 0) {
        fprintf(stderr, "Socket cleanup incomplete (stage %d, OS error %d)\n",
                result.stage, result.os_error);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /absolute/private-directory/loop.sock\n", argv[0]);
        return 2;
    }
    int64_t loop = eshkol_event_loop_open_handle(8);
    if (loop <= 0) return 1;
    int64_t listener = -1, client = -1, peer = -1;
    int status = 1;

    listener = eshkol_unix_stream_socket_listen(loop, argv[1]);
    if (listener <= 0) goto done;
    client = eshkol_unix_stream_socket_connect_begin(loop, argv[1], 1000);
    if (client <= 0) goto done;
    peer = eshkol_unix_stream_socket_accept(listener, 1000);
    if (peer <= 0 || eshkol_unix_stream_socket_connect_finish(client) != 0) goto done;
    if (transfer(client, peer, "ping", 4) != 0 || transfer(peer, client, "pong", 4) != 0) goto done;
    status = 0;

done:
    if (status) fprintf(stderr, "Loopback setup or transfer failed\n");
    if (close_socket(peer) != 0) status = 1;
    if (close_socket(client) != 0) status = 1;
    if (close_socket(listener) != 0) status = 1;
    if (eshkol_event_loop_close_handle(loop) != ESHKOL_EVENT_LOOP_OK) status = 1;
    struct stat socket_entry;
    if (listener > 0 && (lstat(argv[1], &socket_entry) == 0 || errno != ENOENT)) status = 1;
    if (!status) puts("Loopback passed and socket path retired");
    return status;
}
