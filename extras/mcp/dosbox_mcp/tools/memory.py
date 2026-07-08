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
