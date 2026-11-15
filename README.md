# UDP File Transfer

A C client/server exercise that requests files from a UDP server using small typed protocol data units (PDUs).

## Requirements

- A POSIX-compatible operating system
- Clang or GCC

## Build

```bash
cc -std=c11 -Wall -Wextra -Wpedantic UDPserver/server.c -o udp-server
cc -std=c11 -Wall -Wextra -Wpedantic UDPclient/client.c -o udp-client
```

Run the server from `UDPserver/` so its sample files are available, then start `udp-client` from a separate terminal. The client is configured to connect to `127.0.0.1` on UDP port `12345`.

The protocol uses `C` for a file request, `D` for a data chunk, `F` for the final chunk, and `E` for an error. Requests are restricted to simple filenames in the server's working directory; directory separators and symlinks are rejected. UDP does not guarantee delivery, ordering, or retransmission; this is a networking demonstration, not a reliable production transfer service.

## Contents

```text
UDPclient/client.c   Interactive file-request client
UDPserver/server.c   UDP file-serving loop
UDPclient/*.txt      Example client-side files
UDPserver/*.txt      Example files served to clients
```
