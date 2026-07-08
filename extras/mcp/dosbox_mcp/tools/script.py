# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="script_run",
        description=(
            "Load and start a Lua script. The script runs sandboxed on the "
            "emulation thread. Any DOS automation capability without a "
            "dedicated tool is reachable through a Lua script."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "script": {
                    "type": "string",
                    "description": "Lua source code to execute.",
                },
            },
            "required": ["script"],
        },
        handler=lambda args: _script_run(client, args),
    )

    add_tool(
        name="script_status",
        description=(
            "Check the running script's state and read its output table. "
            "Scripts communicate results through dosbox.output['key'] = value."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _script_status(client),
    )

    add_tool(
        name="script_stop",
        description="Stop a running Lua script.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _script_stop(client),
    )


def _script_run(client, args):
    import mcp.types as types
    client.post("/api/v1/script/load", json={"script": args["script"]})
    result = client.post("/api/v1/script/start")
    return [types.TextContent(type="text", text=json.dumps(result))]


def _script_status(client):
    import mcp.types as types
    result = client.get("/api/v1/script/status")
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


def _script_stop(client):
    import mcp.types as types
    result = client.post("/api/v1/script/stop")
    return [types.TextContent(type="text", text=json.dumps(result))]
