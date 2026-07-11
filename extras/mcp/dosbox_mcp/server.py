# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import asyncio

import mcp.server.stdio
from mcp.server.lowlevel import Server
import mcp.types as types

from .capabilities import GROUP_CAPABILITY
from .connection import Connection, guard
from .config import Config
from .tools import session, screen, input as input_tools, memory, freeze, io, cpu, debug, media, script


def build_server(conn):
    server = Server("dosbox-automation")
    registry = {}

    def add_tool(name, description, schema, handler, read_only=False, feature=None):
        wrapped = guard(conn, handler, feature=feature)
        annotations = types.ToolAnnotations(readOnlyHint=read_only)
        registry[name] = (
            types.Tool(
                name=name,
                description=description,
                inputSchema=schema,
                annotations=annotations,
            ),
            wrapped,
        )

    for mod in (session, screen, media, script):
        mod.register(server, conn, add_tool)

    input_tools.register(server, conn, add_tool, feature="input")
    memory.register(server, conn, add_tool, feature="memory")
    memory.register_search(server, conn, add_tool, feature="memory")
    freeze.register(server, conn, add_tool, feature="freeze")
    io.register(server, conn, add_tool, feature="port_io")
    cpu.register(server, conn, add_tool, feature="cpu_control")
    debug.register(server, conn, add_tool, feature="debugger")

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
    conn = Connection(config)
    server = build_server(conn)
    async with mcp.server.stdio.stdio_server() as (read, write):
        await server.run(read, write, server.create_initialization_options())


def main():
    asyncio.run(_run())
