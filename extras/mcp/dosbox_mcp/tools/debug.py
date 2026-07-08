# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="debug_status",
        description="Debugger state: paused, breakpoints, current instruction.",
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _not_available("debug_status"),
    )

    add_tool(
        name="debug_pause",
        description="Pause emulation at the current instruction.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _not_available("debug_pause"),
    )

    add_tool(
        name="debug_continue",
        description="Resume emulation from the current pause point.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _not_available("debug_continue"),
    )

    add_tool(
        name="debug_step",
        description="Execute one instruction and pause again.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _not_available("debug_step"),
    )


def _not_available(tool_name):
    import mcp.types as types
    return [types.TextContent(
        type="text",
        text=json.dumps({
            "error": f"{tool_name}: debugger capability not built in this binary",
        }),
    )]
