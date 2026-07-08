# dosbox-mcp

MCP bridge for dosbox-automation. Wraps the REST API as MCP tools so
an agent runtime can drive the emulator over stdio.

## Configuration

The bridge reads two environment variables:

- `DOSBOX_API_URL` - base URL of the running emulator (default
  `http://127.0.0.1:8386`). Must be a loopback address.
- `DOSBOX_API_TOKEN` - the 64-char hex token. If not set, falls back
  to reading `~/.config/dosbox-automation/webserver/api_token` (requires
  `webserver_token_file = true` in the emulator config).

## Running

Start the emulator with the webserver enabled, then launch the bridge:

```
dosbox-mcp
```

The bridge connects to the emulator, queries `/api/v1/dosbox/info` for
the capability report, and registers only the tool groups whose flags
are true. The `debugger` group stays dormant until its engine seam
exists.

Any capability without a dedicated tool is reachable through
`script_run`, which executes sandboxed Lua on the emulation thread.

## Tool groups

| Group | Flag | Tools |
|---|---|---|
| session | always | dosbox_status, dosbox_shutdown |
| screen | always | screen_text, screen_capture, screen_info |
| media | always | video_capture_start/stop/status |
| script | always | script_run, script_status, script_stop |
| input | input | input_type, input_key, input_sequence |
| memory | memory | mem_read, mem_write |

Additional groups (freeze, port_io, cpu_registers, cpu_control,
debugger) are registered by later tasks as their engine primitives land.

## Development

```
cd extras/mcp
python -m venv .venv && source .venv/bin/activate
pip install -e . && pip install pytest
python -m pytest -v
```

---
This project is developed with tooled assistance, but tested, reviewed and signed off by a human developer.
