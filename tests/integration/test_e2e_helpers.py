# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""Unit tests for the Lua script generator."""

from e2e_helpers import GameManifest, PromptStep, generate_install_script


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
