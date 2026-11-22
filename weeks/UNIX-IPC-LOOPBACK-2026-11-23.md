# Unix socket loopback example

The example sends `ping` and `pong` over a Unix-domain stream using the native v1.4 IPC API. It opens one event loop, connects a client, accepts the peer, transfers four bytes in each direction, and observes a final cleanup result for each endpoint and the listener before closing the loop. Listener cleanup retires the socket pathname in the private directory. [Example source](support/unix_ipc_loopback.c) · [Native API](https://github.com/Gabriel-Kahen/eshkol/blob/8d99676f6467e362da34122bbe7f36d7e6e63907/inc/eshkol/core/unix_socket_stream.h)

## Run on Linux

From the notes checkout, run with Git, Bash, Clang, and Clang++. The source pin is the combined commit used for the earlier hosted native Unix socket qualification.

```bash
set -euo pipefail
pin=8d99676f6467e362da34122bbe7f36d7e6e63907
example=$(realpath weeks/support/unix_ipc_loopback.c)
scratch=$(mktemp -d)
git fetch --no-tags https://github.com/Gabriel-Kahen/eshkol.git "$pin"
git worktree add --detach "$scratch/source" "$pin"
source_tree="$scratch/source"
objects=()
for file in lib/agent/c/agent_unix_socket_stream.c lib/core/resource_lease.c \
    lib/core/event_loop.c lib/core/event_loop_epoll.c; do
    object="$scratch/$(basename "$file").o"
    clang -std=c11 -O2 -D_GNU_SOURCE -pthread -I"$source_tree/inc" \
        -c "$source_tree/$file" -o "$object"
    objects+=("$object")
done
clang++ -std=c++17 -O2 -pthread -I"$source_tree/inc" \
    -c "$source_tree/lib/core/runtime_handle.cpp" -o "$scratch/handles.o"
clang -std=c11 -Wall -Wextra -O2 -DESHKOL_STANDALONE_EXAMPLE -I"$source_tree/inc" \
    -c "$example" -o "$scratch/example.o"
clang++ -pthread "${objects[@]}" "$scratch/handles.o" "$scratch/example.o" \
    -o "$scratch/unix-ipc-loopback"
timeout 10s "$scratch/unix-ipc-loopback" "$scratch/loop.sock"
test ! -e "$scratch/loop.sock"
git worktree remove "$source_tree"
```

Expected output

```text
Received ping
Received pong
Loopback passed and socket path retired
```

The standalone executable supplies its own capability checker for the demo. An integrated build uses the runtime's capability checker. The example exercises real Unix sockets and Linux epoll at the pinned source. Server-side SSE, installed packaging, and public JIT/AOT remain separate checks. [Prior native qualification](https://github.com/Gabriel-Kahen/eshkol/actions/runs/43493801710)
