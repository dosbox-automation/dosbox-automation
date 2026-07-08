# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="mem_read",
        description=(
            "Read bytes from guest physical memory. Returns base64 data "
            "and current CPU register state."
        ),
        read_only=True,
        schema={
            "type": "object",
            "properties": {
                "offset": {
                    "type": "integer",
                    "description": "Physical memory offset to read from.",
                },
                "length": {
                    "type": "integer",
                    "description": "Number of bytes to read (max 65536).",
                },
            },
            "required": ["offset", "length"],
        },
        handler=lambda args: _mem_read(client, args),
    )

    add_tool(
        name="mem_write",
        description="Write bytes to guest physical memory.",
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "offset": {
                    "type": "integer",
                    "description": "Physical memory offset to write to.",
                },
                "data": {
                    "type": "string",
                    "description": "Base64-encoded data to write.",
                },
            },
            "required": ["offset", "data"],
        },
        handler=lambda args: _mem_write(client, args),
    )


def _mem_read(client, args):
    import mcp.types as types
    seg = args["offset"] >> 4
    off = args["offset"] & 0xF
    path = f"/api/v1/memory/{seg}/{off}"
    result = client.get(path, params={
        "length": args["length"],
        "format": "json",
    })
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


def _mem_write(client, args):
    import mcp.types as types
    seg = args["offset"] >> 4
    off = args["offset"] & 0xF
    path = f"/api/v1/memory/{seg}/{off}"
    result = client.put(path, json={"data": args["data"]})
    return [types.TextContent(type="text", text=json.dumps(result))]


def register_search(server, client, add_tool):
    add_tool(
        name="mem_search",
        description=(
            "Scan a range of guest memory for a value. Returns matching "
            "physical addresses. Width is 1 (byte), 2 (word), or 4 (dword), "
            "little-endian."
        ),
        read_only=True,
        schema={
            "type": "object",
            "properties": {
                "start": {
                    "type": "integer",
                    "description": "Start of search range (physical address).",
                },
                "end": {
                    "type": "integer",
                    "description": "End of search range (exclusive).",
                },
                "value": {
                    "type": "integer",
                    "description": "Value to search for.",
                },
                "width": {
                    "type": "integer",
                    "description": "Width in bytes: 1, 2, or 4 (default 1).",
                    "default": 1,
                },
            },
            "required": ["start", "end", "value"],
        },
        handler=lambda args: _mem_search(client, args),
    )

    add_tool(
        name="dos_memory_map",
        description=(
            "Walk the DOS MCB chain and report which PSP owns which memory "
            "block. Shows the full conventional memory layout."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _dos_memory_map(client),
    )


def _mem_search(client, args):
    import mcp.types as types
    body = {
        "start": args["start"],
        "end": args["end"],
        "value": args["value"],
        "width": args.get("width", 1),
    }
    result = client.post("/api/v1/memory/search", json=body)
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


def _dos_memory_map(client):
    import mcp.types as types
    result = client.get("/api/v1/dos/internals")
    mem_map = result.get("memoryMap", [])
    return [types.TextContent(type="text", text=json.dumps(mem_map, indent=2))]
