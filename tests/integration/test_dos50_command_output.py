# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

"""PATH, APPEND and JOIN texts in the real binary; the unit tests stub messages.
Expected texts: MS-DOS 4.0 USA-MS.MSG and COMMAND.COM print_path (TCMD2A.ASM)."""

import time

import pytest


def screen_after(dosbox_e2e, tmp_path, lines):
    instance = dosbox_e2e(autoexec_lines=["cls", *lines], conf_dir=tmp_path)
    client = instance.client
    client.wait_shell(timeout=10)
    time.sleep(1.0)
    r = client.screen_text()
    assert r.status_code == 200, r.text
    return [line.rstrip() for line in r.json()["text"].splitlines()]


def output_of(screen, command, occurrence=0):
    prompts = [i for i, line in enumerate(screen) if line.endswith(">" + command)]
    prompt = prompts[occurrence]
    out = []
    for line in screen[prompt + 1 :]:
        if ">" in line and line.split(">", 1)[0].endswith(":\\"):
            break
        out.append(line)
    return [line for line in out if line]


def test_path_display_prefixes_the_variable_name(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path C:\\ONE;C:\\TWO", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\ONE;C:\\TWO"]


def test_path_cleared_prints_no_path(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path;", "path"])
    assert output_of(screen, "path") == ["No Path"]


def test_path_value_is_upper_cased_like_command_com(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["path c:\\one;d:\\two", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\ONE;D:\\TWO"]


def test_path_refuses_text_after_the_value(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["path C:\\KEEP", "path c:\\a b", "path c:\\dos,c:\\x", "path"]
    )
    assert output_of(screen, "path c:\\a b") == ["Too many parameters."]
    assert output_of(screen, "path c:\\dos,c:\\x") == ["Too many parameters."]
    assert output_of(screen, "path") == ["PATH=C:\\KEEP"]


def test_append_texts(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [
            "append",
            "append /q",
            "append /e c:\\x",
            "append c:\\x",
            "append",
            "append;",
            "append",
        ],
    )
    assert output_of(screen, "append") == ["No Append"]
    assert output_of(screen, "append /q") == ["Invalid parameter"]
    assert output_of(screen, "append /e c:\\x") == ["Invalid combination of parameters"]
    assert output_of(screen, "append c:\\x") == []
    assert output_of(screen, "append", occurrence=1) == ["APPEND=c:\\x"]
    assert output_of(screen, "append;") == []
    assert output_of(screen, "append", occurrence=2) == ["No Append"]


def test_append_env_mode_sets_and_clears_the_variable(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        ["append /e", "append b:\\letters;a:\\reports", "set", "append ;", "set"],
    )
    assert "APPEND=b:\\letters;a:\\reports" in output_of(screen, "set")
    cleared = screen[screen.index("Z:\\>append ;") :]
    assert not [line for line in cleared if line.startswith("APPEND=")]


@pytest.mark.parametrize(
    "command",
    ["join d: c:\\sales", "join d: /d", "join d: c:\\", "join /q"],
)
def test_join_failures_set_errorlevel(dosbox_e2e, tmp_path, command):
    screen = screen_after(dosbox_e2e, tmp_path, [command, "if errorlevel 1 echo LEVEL1"])
    assert output_of(screen, "if errorlevel 1 echo LEVEL1") == ["LEVEL1"]


def test_join_list_succeeds(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["join", "if errorlevel 1 echo LEVEL1"])
    assert output_of(screen, "if errorlevel 1 echo LEVEL1") == []


def test_set_skips_a_leading_comma_like_command_com(dosbox_e2e, tmp_path):
    # MS-DOS 4.0 TENV.ASM GETARG runs SCANOFF over every DELIM character.
    screen = screen_after(dosbox_e2e, tmp_path, ["set,a=1", "set a"])
    assert output_of(screen, "set a") == ["A=1"]


def test_path_skips_a_leading_comma_but_keeps_a_semicolon(dosbox_e2e, tmp_path):
    # MS-DOS 4.0 TMISC2.ASM PSCANOFF: ';' is not a delimiter for PATH.
    screen = screen_after(dosbox_e2e, tmp_path, ["path,C:\\DOS", "path"])
    assert output_of(screen, "path") == ["PATH=C:\\DOS"]


def test_path_with_only_delimiters_shows_the_path(dosbox_e2e, tmp_path):
    # PGETARG reaches the end of the line and PATH goes to disppath.
    screen = screen_after(dosbox_e2e, tmp_path, ["path C:\\DOS", "path,", "path"])
    assert output_of(screen, "path,") == ["PATH=C:\\DOS"]
    assert output_of(screen, "path") == ["PATH=C:\\DOS"]


# PROMPT (ada-nc0d): MS-DOS 5.0 User's Guide p. 545; the autoexec echoes each
# line after the prompt in force, so the prompt shows in front of it.
def test_prompt_sets_the_prompt_and_prompt_alone_restores_it(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["prompt $n$g", "echo one", "prompt", "echo two"]
    )
    assert "Z>echo one" in screen, screen
    assert "Z:\\>echo two" in screen, screen


def test_prompt_codes_render(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["prompt [$p$q$v$x]$_$n$b$g", "echo marker"]
    )
    at = screen.index("Z|>echo marker")
    assert screen[at - 1] == "[Z:\\=DOS version 5.00]", screen


def test_prompt_takes_an_equal_sign_and_shows_in_set(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["prompt=$n$g", "set"])
    assert "Z>set" in screen, screen
    assert "PROMPT=$n$g" in output_of(screen, "set"), screen


# BREAK, VERIFY, CTTY (ada-9i0i): texts from MS-DOS 4.0 USA-MS.MSG.
@pytest.mark.parametrize("command", ["break", "verify"])
def test_on_off_commands_show_and_set_their_flag(dosbox_e2e, tmp_path, command):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        [command, f"{command} on", command, f"{command} OFF", command, f"{command} x"],
    )
    word = command.upper()
    assert output_of(screen, command) == [f"{word} is off"]
    assert output_of(screen, f"{command} on") == []
    assert output_of(screen, command, occurrence=1) == [f"{word} is on"]
    assert output_of(screen, command, occurrence=2) == [f"{word} is off"]
    assert output_of(screen, f"{command} x") == ["Must specify ON or OFF"]


def test_ctty_nul_hides_the_output_until_ctty_con(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e,
        tmp_path,
        ["ctty nul", "echo hidden-line", "ctty con", "echo shown-line"],
    )
    text = "\n".join(screen)
    assert "hidden-line" not in text, text
    assert "shown-line" in screen, screen


def test_ctty_refuses_what_is_not_a_device(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["ctty", "ctty nosuchdev", "ctty con nul"])
    assert output_of(screen, "ctty") == ["Invalid device"]
    assert output_of(screen, "ctty nosuchdev") == ["Invalid device"]
    assert output_of(screen, "ctty con nul") == ["Invalid device"]


# The prompt reads end of input from NUL at once; without an idle the shell
# spun inside one call and the web API stopped answering.
def test_the_api_answers_after_ctty_nul_at_the_prompt(dosbox_e2e, tmp_path):
    instance = dosbox_e2e(autoexec_lines=["ctty nul"], conf_dir=tmp_path)
    time.sleep(2.0)
    for _ in range(5):
        r = instance.client.screen_text()
        assert r.status_code == 200, r.text
        time.sleep(0.2)


# DOSKEY (ada-ld1z): the shell has its own history, so DOSKEY loads nothing;
# macros are refused.
def test_doskey_says_it_once_and_loads_high(dosbox_e2e, tmp_path):
    screen = screen_after(
        dosbox_e2e, tmp_path, ["doskey", "lh doskey /bufsize=512 /insert", "doskey /m"]
    )
    assert output_of(screen, "doskey") == [
        "DOSKey installed (the command history is built into the shell)."
    ]
    assert output_of(screen, "lh doskey /bufsize=512 /insert") == []
    assert output_of(screen, "doskey /m") == []


def test_doskey_refuses_macros_and_unknown_switches(dosbox_e2e, tmp_path):
    screen = screen_after(dosbox_e2e, tmp_path, ["doskey ls=dir $*", "doskey /x"])
    assert output_of(screen, "doskey ls=dir $*") == ["DOSKEY macros are not supported."]
    assert output_of(screen, "doskey /x") == ["Illegal switch: /x"]


def test_doskey_history_lists_the_typed_commands(dosbox_e2e, tmp_path):
    instance = dosbox_e2e(autoexec_lines=["cls"], conf_dir=tmp_path)
    client = instance.client
    client.wait_shell(timeout=10)
    for line in ["echo first-typed\n", "doskey /h\n"]:
        deadline = time.monotonic() + 10
        while client.type_string(line).status_code == 409:
            assert time.monotonic() < deadline
            time.sleep(0.1)
        time.sleep(1.5)
    screen = [row.rstrip() for row in client.screen_text().json()["text"].splitlines()]
    assert output_of(screen, "doskey /h")[-2:] == ["echo first-typed", "doskey /h"], screen
