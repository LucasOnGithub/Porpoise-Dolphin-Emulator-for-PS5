#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
mkdir -p build/tests
extra=()
if [[ -n ${SLIPPI_ENET_SOURCE:-} ]]; then
    [[ -f $SLIPPI_ENET_SOURCE/unix.c ]] || { echo "Missing ENet source" >&2; exit 2; }
    mkdir -p build/tests/slippi-ps5-enet
    for source in callbacks compress host list packet peer protocol unix; do
        "${CC:-cc}" -std=c11 -D_DEFAULT_SOURCE -O1 -g -I"$SLIPPI_ENET_SOURCE/include" \
            -DHAS_POLL -DHAS_SOCKLEN_T -DHAS_MSGHDR_FLAGS -DHAS_INET_PTON -DHAS_INET_NTOP \
            -DHAS_GETADDRINFO -DHAS_GETNAMEINFO -Dsocket=slippi_ps5_socket -Dioctl=slippi_ps5_ioctl \
            -c "$SLIPPI_ENET_SOURCE/$source.c" -o "build/tests/slippi-ps5-enet/$source.o"
    done
    extra=(-DSLIPPI_TEST_ENET -I"$SLIPPI_ENET_SOURCE/include" tests/slippi_ps5_enet_tests.c build/tests/slippi-ps5-enet/*.o)
fi
"${CC:-cc}" -std=c11 -D_GNU_SOURCE -DPORPOISE_SLIPPI_PLATFORM_HOST_TEST -O1 -g \
    -Wall -Wextra -Werror -Wno-error=cpp -Itooling/slippi-ps5 \
    tooling/slippi-ps5/runtime.c tests/slippi_ps5_platform_tests.c "${extra[@]}" \
    -pthread -ldl -Wl,--wrap=dlopen -Wl,--wrap=dlsym -Wl,--wrap=setsockopt -Wl,--wrap=close \
    -o build/tests/slippi_ps5_platform_tests
for mode in normal no-random no-net; do
    build/tests/slippi_ps5_platform_tests "$mode"
done
