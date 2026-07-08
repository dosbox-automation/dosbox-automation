# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

# Maps a bridge tool group to the feature flag that must be true for it
# to register. Groups always present (session, screen, script, media)
# are not gated.
GROUP_CAPABILITY = {
    "memory": "memory",
    "input": "input",
    "freeze": "freeze",
    "cpu_registers": "cpu_registers",
    "cpu_control": "cpu_control",
    "port_io": "port_io",
    "debugger": "debugger",
}

ALWAYS_ON = {"session", "screen", "script", "media"}


def registered_groups(features: dict) -> set:
    groups = set(ALWAYS_ON)
    for group, flag in GROUP_CAPABILITY.items():
        if features.get(flag) is True:
            groups.add(group)
    return groups
