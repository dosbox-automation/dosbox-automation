# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool, feature=None):
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
        feature=feature,
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
        feature=feature,
    )

    add_tool(
        name="input_sequence",
        description=(
            "Inject a timed sequence of key, mouse, and wheel events. "
            "For complex input patterns or recorded replays. "
            "Mouse movement is RELATIVE (x_rel/y_rel deltas from the "
            "current cursor position); there is no absolute positioning. "
            "To reach a known position, sweep past a screen corner first "
            "(e.g. x_rel:-4000, y_rel:-4000 pins the cursor top-left), "
            "then move by the target offset. A click is a mouse_button "
            "press event followed by a release event. Unknown fields are "
            "rejected with an error naming the allowed ones."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "events": {
                    "type": "array",
                    "description": (
                        "Input events, dispatched in order on one timeline."
                    ),
                    "items": {
                        "type": "object",
                        "properties": {
                            "type": {
                                "type": "string",
                                "enum": [
                                    "key",
                                    "mouse_move",
                                    "mouse_button",
                                    "mouse_wheel",
                                ],
                                "description": "Event kind (default: key).",
                            },
                            "delay_ms": {
                                "type": "number",
                                "description": (
                                    "Wait this many ms after the previous "
                                    "event before firing (relative timing; "
                                    "the natural choice for hand-written "
                                    "sequences). Mutually exclusive with 't'."
                                ),
                            },
                            "t": {
                                "type": "number",
                                "description": (
                                    "Absolute position on the sequence "
                                    "timeline in ms (recording format). "
                                    "Mutually exclusive with 'delay_ms'."
                                ),
                            },
                            "key": {
                                "type": "string",
                                "description": (
                                    "KBD_* key name (key events), "
                                    "e.g. KBD_enter, KBD_up, KBD_kp8."
                                ),
                            },
                            "pressed": {
                                "type": "boolean",
                                "description": (
                                    "Press (true) or release (false), for "
                                    "key and mouse_button events."
                                ),
                            },
                            "x_rel": {
                                "type": "number",
                                "description": (
                                    "Horizontal mouse delta in pixels, "
                                    "positive is right (mouse_move events)."
                                ),
                            },
                            "y_rel": {
                                "type": "number",
                                "description": (
                                    "Vertical mouse delta in pixels, "
                                    "positive is down (mouse_move events)."
                                ),
                            },
                            "button": {
                                "type": "string",
                                "enum": ["left", "right", "middle"],
                                "description": (
                                    "Mouse button (mouse_button events)."
                                ),
                            },
                            "delta": {
                                "type": "number",
                                "description": (
                                    "Wheel movement (mouse_wheel events)."
                                ),
                            },
                        },
                    },
                },
            },
            "required": ["events"],
        },
        handler=lambda args: _input_sequence(client, args),
        feature=feature,
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
