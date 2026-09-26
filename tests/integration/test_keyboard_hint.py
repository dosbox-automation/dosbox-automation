# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""The keyboard layout hint after the shell banner (ada-b9e0, D6).

The layout no longer follows the host. When the layout in use is the
default and the host keyboard differs, the shell names the host layout
and how to switch. The host side is faked with a KDE kxkbrc in the
engine's XDG_CONFIG_HOME, so the result does not depend on the machine
running the tests.
"""

import pytest

KDE = {"XDG_CURRENT_DESKTOP": "KDE"}


def screen_after_start(dosbox_e2e, tmp_path, layout_list, keyboard_layout=None):
    work_dir = tmp_path / "work"
    config_home = work_dir / ".config"
    config_home.mkdir(parents=True)
    (config_home / "kxkbrc").write_text(
        f"[Layout]\nLayoutList={layout_list}\nVariantList=\n", encoding="utf-8")

    settings = {"keyboard_layout": keyboard_layout} if keyboard_layout else None
    instance = dosbox_e2e(work_dir=work_dir, settings=settings, extra_env=KDE)
    instance.client.wait_shell(timeout=15)
    response = instance.client.screen_text()
    assert response.status_code == 200
    return response.json()["text"]


def test_default_layout_on_a_german_host_names_the_host_layout(dosbox_e2e, tmp_path):
    text = screen_after_start(dosbox_e2e, tmp_path, "de")

    assert "Host keyboard looks German" in text
    assert "DOS is using US" in text
    assert "Type KEYB GR453 now" in text


def test_auto_behaves_like_the_default(dosbox_e2e, tmp_path):
    text = screen_after_start(dosbox_e2e, tmp_path, "de", keyboard_layout="auto")

    assert "Host keyboard looks German" in text


def test_a_chosen_layout_gets_no_hint(dosbox_e2e, tmp_path):
    text = screen_after_start(dosbox_e2e, tmp_path, "de", keyboard_layout="gr")

    assert "Host keyboard" not in text


def test_a_written_us_still_gets_the_hint(dosbox_e2e, tmp_path):
    # A written 'us' gets the hint like the default, by decision (D6, ada-2lbz)
    text = screen_after_start(dosbox_e2e, tmp_path, "de", keyboard_layout="us")

    assert "Host keyboard looks German" in text


def test_a_us_host_gets_no_hint(dosbox_e2e, tmp_path):
    text = screen_after_start(dosbox_e2e, tmp_path, "us")

    assert "Host keyboard" not in text


def test_an_unmapped_host_layout_is_named(dosbox_e2e, tmp_path):
    text = screen_after_start(dosbox_e2e, tmp_path, "custom")

    assert "Host keyboard layout 'custom' has no DOS equivalent" in text


@pytest.mark.parametrize("line_start", ["Host keyboard looks", "DOS is using", "Type KEYB"])
def test_hint_lines_fit_the_screen(dosbox_e2e, tmp_path, line_start):
    text = screen_after_start(dosbox_e2e, tmp_path, "de")

    lines = [line for line in text.splitlines() if line.startswith(line_start)]
    assert lines, f"no line starting with {line_start!r}:\n{text}"
    # a wrapped line would leave its tail at the start of the next row
    assert lines[0].rstrip().endswith("."), lines[0]
