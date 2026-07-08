# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="video_capture_start",
        description="Start ZMBV video recording of the emulator screen.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _capture_start(client),
    )

    add_tool(
        name="video_capture_stop",
        description="Stop ZMBV video recording.",
        read_only=False,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _capture_stop(client),
    )

    add_tool(
        name="video_capture_status",
        description="Check whether a video capture is in progress.",
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _capture_status(client),
    )


def _capture_start(client):
    import mcp.types as types
    result = client.post("/api/v1/capture/video/start")
    return [types.TextContent(type="text", text=json.dumps(result))]


def _capture_stop(client):
    import mcp.types as types
    result = client.post("/api/v1/capture/video/stop")
    return [types.TextContent(type="text", text=json.dumps(result))]


def _capture_status(client):
    import mcp.types as types
    result = client.get("/api/v1/capture/video/status")
    return [types.TextContent(type="text", text=json.dumps(result))]
