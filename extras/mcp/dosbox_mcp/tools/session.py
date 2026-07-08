# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="dosbox_status",
        description=(
            "Machine state: what program is running, mount status, version. "
            "One call answers 'what is the machine doing right now'."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _status(client),
    )

    add_tool(
        name="dosbox_shutdown",
        description="Shut down the emulator. Irreversible.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _shutdown(client),
    )


def _status(client):
    import mcp.types as types
    combined = {
        "status": client.get("/api/v1/status"),
        "program": client.get("/api/v1/program/state"),
        "info": client.get("/api/v1/dosbox/info"),
    }
    return [types.TextContent(type="text", text=json.dumps(combined, indent=2))]


def _shutdown(client):
    import mcp.types as types
    result = client.post("/api/v1/dosbox/shutdown")
    return [types.TextContent(type="text", text=json.dumps(result))]
