# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="port_read",
        description=(
            "Read an x86 I/O port. Width is 1 (byte) or 2 (word). "
            "Use for VGA registers, sound cards, HGC control ports."
        ),
        read_only=True,
        schema={
            "type": "object",
            "properties": {
                "port": {
                    "type": "integer",
                    "description": "I/O port address (0x0000..0xFFFF).",
                },
                "width": {
                    "type": "integer",
                    "description": "Width: 1 (byte) or 2 (word). Default 1.",
                    "default": 1,
                },
            },
            "required": ["port"],
        },
        handler=lambda args: _port_read(client, args),
    )

    add_tool(
        name="port_write",
        description=(
            "Write to an x86 I/O port. Width is 1 (byte) or 2 (word). "
            "For Mode X unchaining, Hercules graphics, hardware config."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "port": {
                    "type": "integer",
                    "description": "I/O port address (0x0000..0xFFFF).",
                },
                "value": {
                    "type": "integer",
                    "description": "Value to write.",
                },
                "width": {
                    "type": "integer",
                    "description": "Width: 1 (byte) or 2 (word). Default 1.",
                    "default": 1,
                },
            },
            "required": ["port", "value"],
        },
        handler=lambda args: _port_write(client, args),
    )


def _port_read(client, args):
    import mcp.types as types
    params = {"port": args["port"], "width": args.get("width", 1)}
    result = client.get("/api/v1/io/port", params=params)
    return [types.TextContent(type="text", text=json.dumps(result))]


def _port_write(client, args):
    import mcp.types as types
    body = {
        "port": args["port"],
        "value": args["value"],
        "width": args.get("width", 1),
    }
    result = client.put("/api/v1/io/port", json=body)
    return [types.TextContent(type="text", text=json.dumps(result))]
