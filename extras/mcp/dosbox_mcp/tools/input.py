# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="input_type",
        description=(
            "Type a string on the DOS keyboard. Characters are injected "
            "with pacing so the 8-slot i8042 buffer never overflows. "
            "Supports printable ASCII and common symbols (US layout)."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "text": {
                    "type": "string",
                    "description": "Text to type (max 4096 chars).",
                },
                "cps": {
                    "type": "number",
                    "description": "Characters per second (default 30).",
                },
            },
            "required": ["text"],
        },
        handler=lambda args: _input_type(client, args),
    )

    add_tool(
        name="input_key",
        description=(
            "Press or release a single key by its KBD_* name. "
            "Use for special keys (F1-F12, arrows, Escape, etc) that "
            "input_type cannot produce."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "key": {
                    "type": "string",
                    "description": "Key name, e.g. KBD_enter, KBD_f1, KBD_esc.",
                },
                "pressed": {
                    "type": "boolean",
                    "description": "True for press, false for release.",
                    "default": True,
                },
            },
            "required": ["key"],
        },
        handler=lambda args: _input_key(client, args),
    )

    add_tool(
        name="input_sequence",
        description=(
            "Inject a timed sequence of key, mouse, and wheel events. "
            "For complex input patterns or recorded replays."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "events": {
                    "type": "array",
                    "description": "Array of input events with timing.",
                    "items": {"type": "object"},
                },
            },
            "required": ["events"],
        },
        handler=lambda args: _input_sequence(client, args),
    )


def _input_type(client, args):
    import mcp.types as types
    body = {"text": args["text"]}
    if "cps" in args:
        body["cps"] = args["cps"]
    result = client.post("/api/v1/input/type", json=body)
    return [types.TextContent(type="text", text=json.dumps(result))]


def _input_key(client, args):
    import mcp.types as types
    pressed = args.get("pressed", True)
    events = [{"type": "key", "key": args["key"], "pressed": pressed}]
    if pressed:
        events.append({"type": "key", "key": args["key"], "pressed": False})
    result = client.post("/api/v1/input/sequence", json={"events": events})
    return [types.TextContent(type="text", text=json.dumps(result))]


def _input_sequence(client, args):
    import mcp.types as types
    result = client.post("/api/v1/input/sequence", json={"events": args["events"]})
    return [types.TextContent(type="text", text=json.dumps(result))]
