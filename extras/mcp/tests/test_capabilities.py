# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

from dosbox_mcp.capabilities import registered_groups


def test_always_on_groups_present():
    groups = registered_groups({})
    for g in ("session", "screen", "script", "media"):
        assert g in groups


def test_true_flag_enables_group():
    features = {"memory": True, "input": True}
    groups = registered_groups(features)
    assert "memory" in groups
    assert "input" in groups


def test_false_flag_hides_group():
    features = {
        "memory": True, "input": True, "cpu_registers": True,
        "cpu_control": False, "port_io": False, "freeze": False,
        "debugger": False,
    }
    groups = registered_groups(features)
    assert "memory" in groups
    assert "input" in groups
    assert "freeze" not in groups
    assert "cpu_control" not in groups
    assert "debugger" not in groups


def test_all_on_registers_all_groups():
    features = {k: True for k in (
        "memory", "input", "cpu_registers", "cpu_control",
        "port_io", "freeze", "debugger",
    )}
    groups = registered_groups(features)
    assert "debugger" in groups
    assert "freeze" in groups
    assert "port_io" in groups
