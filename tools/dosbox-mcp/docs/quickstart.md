# dosbox-mcp Quick Start Guide

Get running in 3 steps.

---

## Prerequisites

- **Rust 1.70+** — [Install Rust](https://rustup.rs/)
- **dosbox-automation** — Built from source or installed system-wide

---

## Step 1: Build the Server

```bash
git clone https://github.com/EternalNooblet/dosbox-automation-is-not-your-nanny
cd dosbox-automation-is-not-your-nanny/tools/dosbox-mcp
cargo build --release
```

Binary: `target/release/dosbox-mcp`

---

## Step 2: Configure Your MCP Client

### Claude Desktop (`claude_desktop_config.json`)

```json
{
  "mcpServers": {
    "dosbox": {
      "command": "/full/path/to/dosbox-mcp",
      "args": []
    }
  }
}
```

**Config file locations:**
- macOS: `~/Library/Application Support/Claude/claude_desktop_config.json`
- Linux: `~/.config/Claude/claude_desktop_config.json`
- Windows: `%APPDATA%\Claude\claude_desktop_config.json`

### Cursor (`.cursor/mcp.json`)

```json
{
  "mcpServers": {
    "dosbox": {
      "command": "/full/path/to/dosbox-mcp",
      "args": ["--config", "~/.config/dosbox-mcp/config.toml"]
    }
  }
}
```

### Continue.dev (`~/.continue/config.yaml`)

```yaml
mcpServers:
  dosbox:
    command: "/full/path/to/dosbox-mcp"
    args: []
```

---

## Step 3: Start the Server

```bash
# Auto-starts dosbox-automation (recommended)
dosbox-mcp

# Or connect to existing instance
dosbox-mcp --no-auto-start --base-url http://localhost:8386
```

The server runs via stdio — your MCP client will start it automatically when needed.

---

## Verify It Works

In your AI assistant, ask:

> "What's the DOSBox version and what features are available?"

The assistant should call `get_dosbox_info` and return something like:
```
dosbox-automation 0.85.1
Features: memory, input, cpu_registers, video, recording, drives, scripting, capture, io, dos
Binary: /path/to/dosbox
```

---

## First Commands to Try

```json
# Get status
{ "tool": "get_status", "arguments": {} }

# Read text-mode screen
{ "tool": "read_screen_text", "arguments": {} }

# Type a command
{ "tool": "type_text", "arguments": { "text": "dir\r", "cps": 20 } }

# Capture a frame
{ "tool": "capture_frame", "arguments": { "mode": "rendered", "format": "png" } }
```

---

## Common Issues

### "dosbox-automation binary not found"
```bash
dosbox-mcp --dosbox-bin /path/to/dosbox-automation
# or
export DOSBOX_AUTOMATION_BIN=/path/to/dosbox-automation
```

### "Connection refused" / "Timeout"
```bash
# Check port
lsof -i :8386

# Increase timeout
dosbox-mcp --timeout-secs 60

# Debug logs
dosbox-mcp --log-level debug
```

### "libiir.so not found"
```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
dosbox-mcp
```

---

## Next Steps

- **Read the full API reference**: `docs://dosbox-mcp/api-reference`
- **See workflow examples**: `docs://dosbox-mcp/workflows`
- **Configure multi-instance**: `docs://dosbox-mcp/configuration`
- **Use prompt templates**: `prompts/get` with `install-game`, `memory-trainer`, etc.

---

## Resource Access (for AI Assistants)

```json
# Quickstart
{ "method": "resources/read", "params": { "uri": "docs://dosbox-mcp/quickstart" } }

# Full API reference
{ "method": "resources/read", "params": { "uri": "docs://dosbox-mcp/api-reference" } }

# Individual tool
{ "method": "resources/read", "params": { "uri": "docs://dosbox-mcp/tool/type_text" } }

# Prompt for game install
{ "method": "prompts/get", "params": { "name": "install-game", "arguments": { "drive": "D", "image_path": "/path/to/game.iso" } } }
```