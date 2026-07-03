# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Unit tests for the Lua script generator and replay helpers."""

from e2e_helpers import (
    DEFAULT_CPU_CYCLES,
    GameManifest,
    PromptStep,
    generate_install_script,
    resolve_cpu_cycles,
    resolve_cycle_settings,
    resolve_keyboard_layout,
)


def make_manifest(**overrides):
    """Build a minimal GameManifest for generator testing."""
    defaults = dict(
        name="Test Game", slug="test-game", media="floppy",
        license="shareware", source_url=None, source_sha256=None,
        disc_images=["disk1.ima"], installer_path="INSTALL.EXE",
        source_drive="A", target_drive="C", prompts=[],
        verify_files=[], screenshot_at=[],
    )
    defaults.update(overrides)
    return GameManifest(**defaults)


def test_key_step_emits_press_release():
    m = make_manifest(prompts=[PromptStep(key="down")])
    script = generate_install_script(m)
    assert 'dosbox.key("KBD_down", true)' in script
    assert 'dosbox.key("KBD_down", false)' in script
    assert "dosbox.wait_frames" in script


def test_pause_step_emits_wait_frames():
    m = make_manifest(prompts=[PromptStep(pause=45)])
    script = generate_install_script(m)
    assert "dosbox.wait_frames(45)" in script


def test_repeat_until_gfx_emits_loop():
    m = make_manifest(prompts=[
        PromptStep(repeat="enter", until_mode="gfx"),
    ])
    script = generate_install_script(m)
    assert "while dosbox.is_text_mode()" in script
    assert 'dosbox.key("KBD_enter", true)' in script


def test_repeat_until_text_emits_inverted_loop():
    m = make_manifest(prompts=[
        PromptStep(repeat="wait", until_mode="text"),
    ])
    script = generate_install_script(m)
    assert "while not dosbox.is_text_mode()" in script


def test_wait_plus_key_emits_wait_then_key():
    m = make_manifest(prompts=[
        PromptStep(wait="Controller Type", key="down"),
    ])
    script = generate_install_script(m)
    assert 'dosbox.wait_for_text("Controller Type"' in script
    assert 'dosbox.key("KBD_down", true)' in script


def test_swap_with_key_emits_swap_then_key():
    m = make_manifest(prompts=[
        PromptStep(wait="Insert Disk", action="swap:2", key="enter"),
    ])
    script = generate_install_script(m)
    assert 'dosbox.output["swap_' in script
    assert 'dosbox.key("KBD_enter", true)' in script


def test_classic_wait_type_still_works():
    m = make_manifest(prompts=[
        PromptStep(wait="Destination", type_text="C"),
    ])
    script = generate_install_script(m)
    assert 'dosbox.wait_for_text("Destination"' in script
    assert 'dosbox.type("C\\n")' in script


def test_change_drive_emits_type_with_colon():
    m = make_manifest(prompts=[
        PromptStep(change_drive="D"),
    ])
    script = generate_install_script(m)
    assert 'dosbox.type("D:\\n")' in script
    assert "dosbox.wait_frames(30)" in script


def test_change_drive_strips_trailing_colon():
    m = make_manifest(prompts=[
        PromptStep(change_drive="A:"),
    ])
    script = generate_install_script(m)
    assert 'dosbox.type("A:\\n")' in script


def test_resolve_cpu_cycles_prefers_recording():
    # The recording was made at this rate; replay must match it even if the
    # manifest now says something else.
    assert resolve_cpu_cycles("25000", {"cpu_cycles": "12000"}) == "25000"


def test_resolve_cpu_cycles_falls_back_to_manifest():
    assert resolve_cpu_cycles(None, {"cpu_cycles": "30000"}) == "30000"


def test_resolve_cpu_cycles_falls_back_to_default():
    assert resolve_cpu_cycles(None, {}) == DEFAULT_CPU_CYCLES
    assert resolve_cpu_cycles(None, None) == DEFAULT_CPU_CYCLES


def test_resolve_cpu_cycles_never_returns_auto():
    # The whole point: a replay must never run at DOSBox's auto default.
    for recorded, settings in [
        (None, None), (None, {}), ("18000", {}), (None, {"cpu_cycles": "9000"}),
    ]:
        result = resolve_cpu_cycles(recorded, settings)
        assert result != "auto"
        assert result


def test_resolve_cpu_cycles_coerces_to_string():
    # recording.json may hold a JSON number; the conf writer needs a string.
    assert resolve_cpu_cycles(20000, None) == "20000"


def test_cycle_settings_protected_mirrors_realmode_by_default():
    # One consistent rate: protected mode follows the realmode value when
    # nothing pins it. This is what keeps the game from jumping to 60000.
    out = resolve_cycle_settings({"cpu_cycles": "12000"}, {})
    assert out == {"cpu_cycles": "12000", "cpu_cycles_protected": "12000"}


def test_cycle_settings_record_time_uses_manifest():
    # At record time there is no recording yet (None).
    out = resolve_cycle_settings(None, {"cpu_cycles": "25000"})
    assert out == {"cpu_cycles": "25000", "cpu_cycles_protected": "25000"}


def test_cycle_settings_default_when_nothing_set():
    out = resolve_cycle_settings(None, None)
    assert out == {
        "cpu_cycles": DEFAULT_CPU_CYCLES,
        "cpu_cycles_protected": DEFAULT_CPU_CYCLES,
    }


def test_cycle_settings_explicit_protected_override():
    # A manifest may pin a different protected rate on purpose.
    out = resolve_cycle_settings(
        {"cpu_cycles": "12000"}, {"cpu_cycles_protected": "30000"}
    )
    assert out == {"cpu_cycles": "12000", "cpu_cycles_protected": "30000"}


def test_cycle_settings_recorded_protected_wins_over_manifest():
    out = resolve_cycle_settings(
        {"cpu_cycles": "12000", "cpu_cycles_protected": "18000"},
        {"cpu_cycles_protected": "30000"},
    )
    assert out["cpu_cycles_protected"] == "18000"


def test_cycle_settings_never_auto_or_60000_default():
    # The whole point: protected mode must never fall back to the ramping
    # 60000 default.
    out = resolve_cycle_settings(None, None)
    assert out["cpu_cycles_protected"] not in ("auto", "max", "60000")


def test_keyboard_layout_default_is_us():
    # 'auto' follows the host locale, so it can never be the resolved
    # value; without any pin the layout is us.
    assert resolve_keyboard_layout(None, None) == {"keyboard_layout": "us"}


def test_keyboard_layout_recorded_value_wins():
    # A recording made under another layout must replay under it.
    out = resolve_keyboard_layout(
        {"keyboard_layout": "de"}, {"keyboard_layout": "uk"}
    )
    assert out == {"keyboard_layout": "de"}


def test_keyboard_layout_manifest_used_at_record_time():
    # At record time there is no recording yet (None).
    out = resolve_keyboard_layout(None, {"keyboard_layout": "uk"})
    assert out == {"keyboard_layout": "uk"}


def test_keyboard_layout_never_auto():
    # The QWERTZ lesson: a recording types ':' and a German-locale host
    # receives 'OE' unless the guest layout is pinned.
    for recording, manifest in ((None, None), ({}, {}), ({"events": []}, {})):
        out = resolve_keyboard_layout(recording, manifest)
        assert out["keyboard_layout"] != "auto"
