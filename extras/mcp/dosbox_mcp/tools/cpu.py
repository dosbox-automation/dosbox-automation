# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import json


def register(server, client, add_tool):
    add_tool(
        name="cpu_write_register",
        description=(
            "Write a CPU register. General registers (eax..ebp) accept "
            "32-bit values. Segment registers (cs,ds,es,ss,fs,gs) accept "
            "16-bit values and update the cached physical base."
        ),
        read_only=False,
        schema={
            "type": "object",
            "properties": {
                "register": {
                    "type": "string",
                    "description": "Register name: eax, ebx, ecx, edx, esi, edi, esp, ebp, cs, ds, es, ss, fs, gs.",
                },
                "value": {
                    "type": "integer",
                    "description": "Value to write (0..0xFFFFFFFF for general, 0..0xFFFF for segment).",
                },
            },
            "required": ["register", "value"],
        },
        handler=lambda args: _cpu_write(client, args),
    )


def _cpu_write(client, args):
    import mcp.types as types
    body = {"register": args["register"], "value": args["value"]}
    result = client.put("/api/v1/cpu/register", json=body)
    return [types.TextContent(type="text", text=json.dumps(result))]
