# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import pytest


@pytest.fixture(autouse=True)
def isolate_token(monkeypatch, tmp_path):
    """Keep the token lookup off the developer's real config.

    read_token() reads DOSBOX_API_TOKEN, then falls back to a token
    file. Without isolation a token file left on the machine (or the
    env var from a shell session) leaks into tests that assert 'no
    token available', making them pass or fail by environment. House
    rule: unit tests never touch live user configuration.
    """
    monkeypatch.delenv("DOSBOX_API_TOKEN", raising=False)
    monkeypatch.setenv("DOSBOX_TOKEN_FILE", str(tmp_path / "no_such_token"))
