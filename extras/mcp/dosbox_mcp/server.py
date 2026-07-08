# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import asyncio

import mcp.server.stdio
from mcp.server.lowlevel import Server
import mcp.types as types

from .capabilities import registered_groups
from .client import DosboxClient
from .config import Config
from .tools import session, screen, input as input_tools, memory, freeze, io, cpu, media, script


def build_server(client, features):
    server = Server("dosbox-automation")
    groups = registered_groups(features)
    registry = {}

    def add_tool(name, description, schema, handler, read_only=False):
        annotations = types.ToolAnnotations(readOnlyHint=read_only)
        registry[name] = (
            types.Tool(
                name=name,
                description=description,
                inputSchema=schema,
                annotations=annotations,
            ),
            handler,
        )

    for mod in (session, screen, media, script):
        mod.register(server, client, add_tool)
    if "input" in groups:
        input_tools.register(server, client, add_tool)
    if "memory" in groups:
        memory.register(server, client, add_tool)
        memory.register_search(server, client, add_tool)
    if "freeze" in groups:
        freeze.register(server, client, add_tool)
    if "port_io" in groups:
        io.register(server, client, add_tool)
    if "cpu_control" in groups:
        cpu.register(server, client, add_tool)

    @server.list_tools()
    async def list_tools():
        return [tool for tool, _ in registry.values()]

    @server.call_tool()
    async def call_tool(name, arguments):
        if name not in registry:
            raise ValueError(f"unknown tool: {name}")
        _, handler = registry[name]
        return handler(arguments or {})

    def _registered_tool_names():
        return set(registry.keys())

    server.registered_tool_names = _registered_tool_names
    return server


async def _run():
    config = Config.from_env()
    client = DosboxClient(config.base_url, config.token)
    features = client.get("/api/v1/dosbox/info").get("features", {})
    server = build_server(client, features)
    async with mcp.server.stdio.stdio_server() as (read, write):
        await server.run(read, write, server.create_initialization_options())


def main():
    asyncio.run(_run())
