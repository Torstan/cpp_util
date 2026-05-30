# conn_util

`conn_util` contains small POSIX connection helpers used by networking code:

- `conn_util::Endpoint` parses and formats IPv4 `host:port` endpoints and
  converts them to `sockaddr_in`.
- `conn_util::SetNonBlocking`, `conn_util::SetTcpNoDelay`,
  `conn_util::CreateTcpListenSocket`, and `conn_util::CreateTcpClientSocket`
  provide TCP socket setup helpers.
- `conn_util::FdNotifier` exposes a non-blocking read fd that becomes readable
  after `notify()` and is cleared by `drain()`.
- `conn_util::Status` is a lightweight status value used by notification APIs.

## Build And Test

```bash
make -C conn_util test
```

## Lint

```bash
make -C conn_util lint
```
