# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool, feature=None):
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

    add_tool(
        name="session_info",
        description=(
            "Connection details for driving the REST API directly: base "
            "URL, current bearer token, and a ready-to-use curl example. "
            "Use this when you need an endpoint the MCP tools don't wrap, "
            "such as saving a video frame to a file. The token is "
            "re-read fresh on every call and rotates whenever DOSBox "
            "restarts. Be aware the returned token becomes part of the "
            "conversation transcript."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _session_info(client),
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


def _session_info(client):
    import mcp.types as types
    from dosbox_mcp.config import read_token

    base_url = client.base_url
    token = read_token()

    info = {"base_url": base_url}
    if token:
        info["token"] = token
        info["example"] = (
            f'curl -H "Authorization: Bearer {token}" {base_url}/api/v1/status'
        )
    else:
        info["token"] = None
        info["note"] = (
            "No token available yet: DOSBOX_API_TOKEN is unset and the "
            "token file does not exist. Start dosbox with "
            "webserver_token_file=true, or export the env var."
        )
    return [types.TextContent(type="text", text=json.dumps(info, indent=2))]
