# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import base64
import json


def register(server, client, add_tool, feature=None):
    add_tool(
        name="screen_text",
        description=(
            "Read the DOS text-mode screen buffer as a string. "
            "Works only in text modes (CGA/EGA/VGA/Hercules text)."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _screen_text(client),
    )

    add_tool(
        name="screen_capture",
        description=(
            "Capture the current screen as a PNG image. Works in all video modes."
        ),
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _screen_capture(client),
    )

    add_tool(
        name="screen_info",
        description="Frame metadata: resolution, pixel format, palette status.",
        read_only=True,
        schema={"type": "object", "properties": {}},
        handler=lambda args: _screen_info(client),
    )


def _screen_text(client):
    import mcp.types as types
    result = client.get("/api/v1/video/text")
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]


def _screen_capture(client):
    import mcp.types as types
    data = client.get("/api/v1/video/frame", params={"format": "png"})
    encoded = base64.b64encode(data).decode("ascii")
    return [types.ImageContent(type="image", data=encoded, mimeType="image/png")]


def _screen_info(client):
    import mcp.types as types
    result = client.get("/api/v1/video/frame/info")
    return [types.TextContent(type="text", text=json.dumps(result, indent=2))]
