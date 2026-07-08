# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import pytest

from dosbox_mcp.config import validate_base_url


@pytest.mark.parametrize("url", [
    "http://127.0.0.1:8386",
    "http://localhost:8386",
    "https://[::1]:8386",
])
def test_loopback_urls_accepted(url):
    assert validate_base_url(url) == url


@pytest.mark.parametrize("url", [
    "http://10.0.0.5:8386",
    "http://example.com:8386",
    "ftp://127.0.0.1:8386",
    "127.0.0.1:8386",
])
def test_non_loopback_or_bad_scheme_rejected(url):
    with pytest.raises(ValueError):
        validate_base_url(url)
