# dosbox-mcp Configuration Guide

Complete configuration reference for the dosbox-mcp server.

## Configuration Sources (Priority Order)

1. **Config file** — `~/.config/dosbox-mcp/config.toml` (lowest priority)
2. **Environment variables** — `DOSBOX_MCP_*` prefix
3. **CLI arguments** — Highest priority

---

## Config File (`~/.config/dosbox-mcp/config.toml`)

```toml
[default]
base_url = "http://localhost:8386"
timeout_secs = 30
verify_tls = true

# Multi-instance support
[[instances]]
name = "main"
base_url = "http://localhost:8386"

[[instances]]
name = "ci-runner"
base_url = "http://192.168.1.50:8386"
```

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_url` | string | required | dosbox-automation REST API URL |
| `timeout_secs` | integer | 30 | Request timeout in seconds |
| `verify_tls` | boolean | true | Verify TLS certificates |
| `instances` | array | [] | Named instances for multi-server setups |

---

## CLI Options

```bash
dosbox-mcp [OPTIONS] [-- <DOSBOX_ARGS>...]

Options:
  --base-url <URL>           dosbox-automation API URL [env: DOSBOX_API_URL]
  --timeout-secs <N>         Request timeout in seconds (default: 30)
  --no-verify-tls            Disable TLS verification
  --config <PATH>            Config file path
  --log-level <LEVEL>        trace/debug/info/warn/error (default: info)

  --dosbox-bin <PATH>        dosbox-automation binary path [env: DOSBOX_AUTOMATION_BIN]
  --auto-start               Auto-start dosbox-automation (default: true)
  --no-auto-start            Don't auto-start, connect to existing
  --working-dir <DIR>        Working directory for dosbox-automation

  <DOSBOX_ARGS>...           Extra args passed to dosbox-automation
```

### Examples

```bash
# Auto-start with defaults
dosbox-mcp

# Connect to existing instance
dosbox-mcp --no-auto-start --base-url http://localhost:8386

# Custom binary and longer timeout
dosbox-mcp --dosbox-bin /usr/local/bin/dosbox-automation --timeout-secs 60

# Debug logging
dosbox-mcp --log-level debug

# Pass extra args to dosbox-automation
dosbox-mcp -- -conf /custom/dosbox.conf
```

---

## Environment Variables

| Variable | Description |
|----------|-------------|
| `DOSBOX_API_URL` | Base URL for dosbox-automation API |
| `DOSBOX_AUTOMATION_BIN` | Path to dosbox-automation binary |
| `DOSBOX_MCP_BASE_URL` | Config override for base_url |
| `DOSBOX_MCP_TIMEOUT_SECS` | Config override for timeout |
| `DOSBOX_MCP_VERIFY_TLS` | Config override for TLS verification |
| `DOSBOX_MCP_LOG_LEVEL` | Config override for log level |

### Example

```bash
export DOSBOX_API_URL=http://localhost:8386
export DOSBOX_MCP_TIMEOUT_SECS=60
dosbox-mcp
```

---

## Auto-Start Behavior

When `--auto-start` (default), the server:

1. **Detects binary** — Searches `build/release-linux/`, `/usr/local/bin/`, `PATH`
2. **Finds libraries** — Auto-locates `libiir.so`, sets `LD_LIBRARY_PATH`
3. **Configures webserver** — Applies secure defaults:
   ```ini
   webserver_enabled=on
   webserver_bind_address=127.0.0.1
   webserver_port=8386
   webserver_allow_remote=off
   webserver_token_file=off
   ```
4. **Health checks** — Waits up to 30s for API readiness
5. **Cleanup** — Graceful shutdown on MCP server exit

### Customizing Auto-Start

```bash
# Specify binary explicitly
dosbox-mcp --dosbox-bin /path/to/dosbox-automation

# Custom working directory
dosbox-mcp --working-dir /path/to/working/dir

# Disable auto-start (connect to existing)
dosbox-mcp --no-auto-start --base-url http://remote-host:8386
```

---

## Multi-Instance Setup

Run multiple DOSBox instances by defining named instances in config:

```toml
[default]
base_url = "http://localhost:8386"
timeout_secs = 30

[[instances]]
name = "game-server"
base_url = "http://localhost:8386"

[[instances]]
name = "test-runner-1"
base_url = "http://localhost:8387"

[[instances]]
name = "test-runner-2"
base_url = "http://192.168.1.100:8386"
```

**Note:** Each instance needs its own dosbox-automation process on a different port.

---

## TLS Configuration

For production/remote setups with TLS:

```toml
[default]
base_url = "https://dosbox.example.com:8386"
timeout_secs = 30
verify_tls = true  # Set false for self-signed certs (--no-verify-tls)
```

---

## Troubleshooting Configuration

### "dosbox-automation binary not found"
```bash
# Specify path explicitly
dosbox-mcp --dosbox-bin /path/to/dosbox-automation

# Or set env var
export DOSBOX_AUTOMATION_BIN=/path/to/dosbox-automation
```

### "Connection refused" / "Timeout waiting for webserver"
```bash
# Check if port 8386 is free
lsof -i :8386

# Increase timeout
dosbox-mcp --timeout-secs 60

# Check dosbox-automation logs
dosbox-mcp --log-level debug
```

### "ldd: libiir.so not found"
The launcher auto-detects `libiir.so` from the build directory. If using a system install:
```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
dosbox-mcp
```

### Permission denied on disk swap
The API requires `webserver_allow_remote=true` in dosbox config for non-localhost mounts, and mounts must be within allowed paths.

---

## Resource Access

Configuration docs available as MCP Resource:
```json
{ "method": "resources/read", "params": { "uri": "docs://dosbox-mcp/configuration" } }
```