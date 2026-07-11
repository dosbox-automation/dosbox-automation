# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool, feature=None):
    add_tool(
        name="freeze_set",
        description=(
            "Lock a memory address to a value. The value is rewritten every "
            "frame, trainer-style. Width is 1/2/4 bytes, little-endian."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "address": {
                    "type": "integer",
                    "description": "Physical memory address to freeze.",
                },
                "value": {
                    "type": "integer",
                    "description": "Value to hold at the address.",
                },
                "width": {
                    "type": "integer",
                    "description": "Width in bytes: 1, 2, or 4 (default 1).",
                    "default": 1,
                },
            },
            "required": ["address", "value"],
        },
        handler=lambda args: _freeze_set(client, args),
        feature=feature,
    )

    add_tool(
        name="freeze_list",
        description="List all active memory freezes.",
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _freeze_list(client),
        feature=feature,
    )

    add_tool(
        name="freeze_clear",
        description=(
            "Remove a freeze. Pass an address to remove one, or omit to "
            "clear all freezes."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "address": {
                    "type": "integer",
                    "description": "Address to unfreeze. Omit to clear all.",
                },
            },
        },
        handler=lambda args: _freeze_clear(client, args),
        feature=feature,
    )


def _freeze_set(client, args):
    import mcp.types as types
    body = {
        "address": args["address"],
        "value": args["value"],
        "width": args.get("width", 1),
    }
    result = client.post("/api/v1/memory/freeze", json=body)
    return [types.TextContent(type="text", text=json.dumps(result))]


def _freeze_list(client):
    import mcp.types as types
    result = client.get("/api/v1/memory/freeze")
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


def _freeze_clear(client, args):
    import mcp.types as types
    if "address" in args:
        result = client.delete("/api/v1/memory/freeze",
                               json={"address": args["address"]})
    else:
        result = client.delete("/api/v1/memory/freeze")
    return [types.TextContent(type="text", text=json.dumps(result))]
