# dosbox-mcp API Reference

Complete reference for all 43 MCP tools exposed by the dosbox-mcp server.

## Tool Categories

| Category | Tools |
|----------|-------|
| **Status** | `get_status`, `get_program_state`, `get_dosbox_info` |
| **Control** | `shutdown`, `pause`, `resume`, `get_pause_state` |
| **CPU** | `get_registers`, `get_cycles`, `set_cycles`, `set_register` |
| **Memory** | `read_memory`, `write_memory`, `search_memory`, `allocate_memory`, `free_memory`, `freeze_memory`, `list_freezes`, `clear_freezes` |
| **Input** | `type_text`, `send_input_sequence` |
| **Video** | `capture_frame`, `get_frame_info`, `read_screen_text` |
| **Recording** | `start_recording`, `pause_recording`, `stop_recording`, `get_recording_status` |
| **Drive** | `swap_disk`, `lock_mounts`, `get_mount_lock` |
| **Script** | `load_script`, `start_script`, `stop_script`, `get_script_status` |
| **Capture** | `start_video_capture`, `stop_video_capture`, `get_capture_status`, `get_compression`, `set_compression` |
| **I/O** | `read_port`, `write_port` |
| **DOS** | `get_dos_internals` |

---

## Status Tools

### get_status
Get current DOSBox status including program state and CPU info.

**Parameters:** None

**Returns:** `Status` object with:
- `program` — Current program info
- `cpu` — CPU cycles and register state
- `video` — Frame dimensions and mode
- `input` — Recording state

**Example:**
```json
{ "tool": "get_status", "arguments": {} }
```

---

### get_program_state
Get the current program execution state.

**Parameters:** None

**Returns:** `ProgramState` with:
- `name` — Running program name
- `path` — Program path
- `state` — Execution state (running, paused, stopped)

**Example:**
```json
{ "tool": "get_program_state", "arguments": {} }
```

---

### get_dosbox_info
Get DOSBox version and build information.

**Parameters:** None

**Returns:** `DosBoxInfo` with:
- `version` — dosbox-automation version string
- `features` — Feature flags (memory, input, cpu_registers, video, etc.)
- `binary` — Path to DOSBox binary
- `appimage` — AppImage path if applicable

**Example:**
```json
{ "tool": "get_dosbox_info", "arguments": {} }
```

---

## Control Tools

### shutdown
Request graceful shutdown of DOSBox.

**Parameters:** None

**Returns:** `ControlResponse` with `status: "ok"`

**Example:**
```json
{ "tool": "shutdown", "arguments": {} }
```

---

### pause
Pause DOSBox emulation.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "pause", "arguments": {} }
```

---

### resume
Resume DOSBox emulation.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "resume", "arguments": {} }
```

---

### get_pause_state
Get current pause state.

**Parameters:** None

**Returns:** Boolean or object indicating paused state

**Example:**
```json
{ "tool": "get_pause_state", "arguments": {} }
```

---

## CPU Tools

### get_registers
Get all CPU register values.

**Parameters:** None

**Returns:** `Registers` object with:
- General purpose: `eax`, `ebx`, `ecx`, `edx`, `esi`, `edi`, `ebp`, `esp`
- Segment: `cs`, `ds`, `es`, `fs`, `gs`, `ss`
- Instruction pointer: `eip`
- Flags: `eflags`
- FPU registers

**Example:**
```json
{ "tool": "get_registers", "arguments": {} }
```

---

### get_cycles
Get current CPU cycles setting.

**Parameters:** None

**Returns:** `CpuCycles` with current cycles value

**Example:**
```json
{ "tool": "get_cycles", "arguments": {} }
```

---

### set_cycles
Set CPU cycles.

**Parameters:**
- `cycles` (string): Cycles value — numeric (e.g., `"5000"`), `"auto"`, or `"max"`

**Returns:** `SetCyclesResponse`

**Example:**
```json
{ "tool": "set_cycles", "arguments": { "cycles": "auto" } }
```

---

### set_register
Set a CPU register value.

**Parameters:**
- `register` (string): Register name (e.g., `"eax"`, `"ebx"`, `"eip"`, `"cs"`, `"ds"`)
- `value` (u64): Value to set

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "set_register", "arguments": { "register": "eax", "value": 0 } }
```

---

## Memory Tools

### read_memory
Read memory at offset (max 128 MiB per request). Returns base64-encoded data.

**Parameters:**
- `offset` (u64): Physical memory offset
- `length` (u32): Number of bytes to read (max 134,217,728)

**Returns:** `MemoryReadResponse` with base64 data

**Example:**
```json
{ "tool": "read_memory", "arguments": { "offset": 1048576, "length": 4096 } }
```

---

### write_memory
Write memory at offset (max 128 MiB per request). Data is base64-encoded.

**Parameters:**
- `offset` (u64): Physical memory offset
- `data` (Vec<u8>): Base64-encoded data to write (max 134,217,728 bytes)

**Returns:** `MemoryWriteResponse`

**Example:**
```json
{ "tool": "write_memory", "arguments": { "offset": 1048576, "data": "AQIDBA==" } }
```

---

### search_memory
Search memory for a value (byte/word/dword). Max 16 MiB per request.

**Parameters:**
- `value` (u64): Value to search for
- `width` (u8, default=1): Width in bytes — 1 (byte), 2 (word), 4 (dword)
- `start` (u64, optional): Start address (default: 0)
- `length` (u32, optional): Bytes to scan (max 16,777,216, default: 16 MiB)

**Returns:** `MemorySearchResponse` with `matches: Vec<u64>` of matching addresses

**Example:**
```json
{ "tool": "search_memory", "arguments": { "value": 3, "width": 1 } }
```

---

### allocate_memory
Allocate memory in DOSBox (CONV, UMB, XMS, EMS).

**Parameters:**
- `size` (u32): Size in bytes
- `area` (string, default="CONV"): Memory area — `"CONV"`, `"UMB"`, `"XMS"`, `"EMS"`
- `strategy` (string, default="BEST_FIT"): Allocation strategy — `"BEST_FIT"`, `"FIRST_FIT"`, `"LAST_FIT"`

**Returns:** `MemoryAllocateResponse` with allocated `addr`

**Example:**
```json
{ "tool": "allocate_memory", "arguments": { "size": 65536, "area": "XMS" } }
```

---

### free_memory
Free previously allocated memory.

**Parameters:**
- `addr` (u64): Physical address of allocated block

**Returns:** `MemoryFreeResponse`

**Example:**
```json
{ "tool": "free_memory", "arguments": { "addr": 1048576 } }
```

---

### freeze_memory
Set a trainer-style value freeze (pins memory to fixed value each frame).

**Parameters:**
- `address` (u64): Physical address to freeze
- `value` (u64): Value to freeze
- `width` (u8, default=1): Width in bytes: 1, 2, or 4

**Returns:** `FreezeResponse`

**Example:**
```json
{ "tool": "freeze_memory", "arguments": { "address": 65536, "value": 99, "width": 1 } }
```

---

### list_freezes
List active value freezes.

**Parameters:** None

**Returns:** `ListFreezesResponse` with array of active freezes

**Example:**
```json
{ "tool": "list_freezes", "arguments": {} }
```

---

### clear_freezes
Remove freezes. With no address, clears all freezes.

**Parameters:**
- `address` (u64, optional): Specific address to clear (omit to clear all)

**Returns:** `DeleteFreezesResponse`

**Example:**
```json
{ "tool": "clear_freezes", "arguments": {} }
```

---

## Input Tools

### type_text
Type text into DOSBox at specified characters per second (default: 30 cps).

**Parameters:**
- `text` (string): Text to type (supports escape sequences like `\r`, `\n`, `\t`)
- `cps` (f32, default=30): Characters per second

**Returns:** `TypeTextResponse`

**Example:**
```json
{ "tool": "type_text", "arguments": { "text": "dir\r", "cps": 20 } }
```

---

### send_input_sequence
Send a sequence of input events (keys, mouse, wheel) with optional timing.

**Parameters:**
- `events` (InputEvent[]): Array of input events

**InputEvent types:**
```json
{ "type": "key", "key": "KBD_enter", "pressed": true, "t": 0 }
{ "type": "mouse_move", "x_rel": 5.0, "y_rel": -3.0 }
{ "type": "mouse_button", "button": "left", "pressed": true }
{ "type": "mouse_wheel", "delta": 1.0 }
```

**Returns:** `InputSequenceResponse`

**Example:**
```json
{ "tool": "send_input_sequence", "arguments": { "events": [{ "type": "key", "key": "KBD_enter", "pressed": true, "t": 0 }] } }
```

---

## Video Tools

### capture_frame
Capture a frame (raw or rendered) as PNG/JPEG/raw. Returns base64-encoded image.

**Parameters:**
- `mode` (string, default="raw"): `"raw"` (emulator framebuffer) or `"rendered"` (post-shader)
- `format` (string, default="png"): `"png"`, `"jpeg"`, `"raw"`
- `quality` (u8, default=85): JPEG quality 1-100

**Returns:** `CaptureFrameResult` with:
- `data` — base64-encoded image
- `size` — bytes
- `format` — output format
- `mode` — capture mode

**Example:**
```json
{ "tool": "capture_frame", "arguments": { "mode": "rendered", "format": "png" } }
```

---

### get_frame_info
Get frame dimensions and format info.

**Parameters:** None

**Returns:** `FrameInfo` with `width`, `height`, `bpp`, `pitch`

**Example:**
```json
{ "tool": "get_frame_info", "arguments": {} }
```

---

### read_screen_text
Read text-mode screen content (useful for driving installers).

**Parameters:** None

**Returns:** `ScreenText` with `rows`, `cols`, `text` content

**Example:**
```json
{ "tool": "read_screen_text", "arguments": {} }
```

---

## Recording Tools

### start_recording
Start recording input sequence.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "start_recording", "arguments": {} }
```

---

### pause_recording
Pause/resume input recording.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "pause_recording", "arguments": {} }
```

---

### stop_recording
Stop input recording and return recorded data.

**Parameters:** None

**Returns:** `RecordingResult` with `events` array

**Example:**
```json
{ "tool": "stop_recording", "arguments": {} }
```

---

### get_recording_status
Get current recording status.

**Parameters:** None

**Returns:** `RecordingStatus` with recording state

**Example:**
```json
{ "tool": "get_recording_status", "arguments": {} }
```

---

## Drive Tools

### swap_disk
Swap floppy/CD image in a drive.

**Parameters:**
- `drive` (string): Drive letter (e.g., `"A"`, `"C"`, `"D"`)
- `image` (string): Path to disk image

**Returns:** `DriveSwapResponse`

**Example:**
```json
{ "tool": "swap_disk", "arguments": { "drive": "D", "image": "/path/to/game.iso" } }
```

---

### lock_mounts
Lock or unlock drive mounts.

**Parameters:**
- `lock` (boolean): Lock state

**Returns:** `MountLockResponse`

**Example:**
```json
{ "tool": "lock_mounts", "arguments": { "lock": true } }
```

---

### get_mount_lock
Get current mount lock state.

**Parameters:** None

**Returns:** `MountLockStatus`

**Example:**
```json
{ "tool": "get_mount_lock", "arguments": {} }
```

---

## Script Tools

### load_script
Load a Lua script.

**Parameters:**
- `script` (string): Lua script source code
- `name` (string, optional): Optional name for the script

**Returns:** `ScriptLoadResponse`

**Example:**
```json
{ "tool": "load_script", "arguments": { "script": "print('hello')", "name": "test" } }
```

---

### start_script
Start executing loaded Lua script.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "start_script", "arguments": {} }
```

---

### stop_script
Stop running Lua script.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "stop_script", "arguments": {} }
```

---

### get_script_status
Get Lua script execution status.

**Parameters:** None

**Returns:** `ScriptStatus`

**Example:**
```json
{ "tool": "get_script_status", "arguments": {} }
```

---

## Capture Tools

### start_video_capture
Start video capture (ZMBV).

**Parameters:**
- `mode` (string, default="raw"): `"raw"` or `"rendered"`

**Returns:** `CaptureStartResponse`

**Example:**
```json
{ "tool": "start_video_capture", "arguments": { "mode": "rendered" } }
```

---

### stop_video_capture
Stop video capture.

**Parameters:** None

**Returns:** `ControlResponse`

**Example:**
```json
{ "tool": "stop_video_capture", "arguments": {} }
```

---

### get_capture_status
Get video capture status.

**Parameters:** None

**Returns:** `CaptureStatus`

**Example:**
```json
{ "tool": "get_capture_status", "arguments": {} }
```

---

### get_compression
Get video capture compression settings.

**Parameters:** None

**Returns:** `CaptureCompression` with `raw` and `rendered` levels (0-9)

**Example:**
```json
{ "tool": "get_compression", "arguments": {} }
```

---

### set_compression
Set video capture compression settings.

**Parameters:**
- `raw` (u8, optional): Compression level for raw mode (0-9)
- `rendered` (u8, optional): Compression level for rendered mode (0-9)

**Returns:** `CaptureCompression`

**Example:**
```json
{ "tool": "set_compression", "arguments": { "raw": 5, "rendered": 7 } }
```

---

## I/O Tools

### read_port
Read I/O port (byte or word).

**Parameters:**
- `port` (u16): Port number (0-65535)
- `width` (u8, default=1): 1 for byte, 2 for word

**Returns:** `PortReadResponse`

**Example:**
```json
{ "tool": "read_port", "arguments": { "port": 1016, "width": 1 } }
```

---

### write_port
Write I/O port (byte or word).

**Parameters:**
- `port` (u16): Port number (0-65535)
- `value` (u32): Value to write
- `width` (u8, default=1): 1 for byte, 2 for word

**Returns:** `PortWriteResponse`

**Example:**
```json
{ "tool": "write_port", "arguments": { "port": 1016, "value": 128, "width": 1 } }
```

---

## DOS Tools

### get_dos_internals
Get DOS internals (MCB map, etc.).

**Parameters:** None

**Returns:** `DosInternals` with MCB chain, DOS structures

**Example:**
```json
{ "tool": "get_dos_internals", "arguments": {} }
```

---

## Error Handling

All tools return structured MCP errors:

```json
{
  "error": {
    "code": -32603,
    "message": "/memory/0x1000/1024 - Connection error: error sending request...",
    "data": null
  }
}
```

Common error codes:
- `-32600` — Invalid parameters
- `-32601` — Tool not found
- `-32603` — Internal error (connection, timeout, API error)

---

## Resource Access

In addition to tools, the server exposes documentation as **MCP Resources**:

| URI | Description |
|-----|-------------|
| `docs://dosbox-mcp/api-reference` | This document |
| `docs://dosbox-mcp/configuration` | Configuration guide |
| `docs://dosbox-mcp/workflows` | Common workflow examples |
| `docs://dosbox-mcp/quickstart` | Quick start guide |
| `docs://dosbox-mcp/tool/{name}` | Individual tool reference |

Read with `resources/read`:
```json
{ "method": "resources/read", "params": { "uri": "docs://dosbox-mcp/api-reference" } }
```

---

## Prompt Templates

The server provides **MCP Prompts** for common workflows:

| Prompt | Description | Arguments |
|--------|-------------|-----------|
| `install-game` | Step-by-step game installation | `drive`, `image_path`, `install_cmd` |
| `memory-trainer` | Find and freeze memory values | `value`, `width`, `freeze_value` |
| `automated-test` | Record/replay/verify workflow | `test_name`, `record_duration_secs` |
| `lua-script` | Load and execute Lua script | `script`, `name` |

Invoke with `prompts/get`:
```json
{ "method": "prompts/get", "params": { "name": "install-game", "arguments": { "drive": "D", "image_path": "/path/to/game.iso" } } }
```