# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import ipaddress
import os
from dataclasses import dataclass
from pathlib import Path
from urllib.parse import urlparse

_LOOPBACK_NAMES = {"localhost"}


def validate_base_url(url: str) -> str:
    """Accept only http(s) URLs pointing at a loopback address."""
    parsed = urlparse(url)
    if parsed.scheme not in ("http", "https"):
        raise ValueError(f"scheme must be http or https, got {parsed.scheme!r}")
    host = parsed.hostname
    if host is None:
        raise ValueError(f"no host in URL {url!r}")
    if host in _LOOPBACK_NAMES:
        return url
    try:
        if ipaddress.ip_address(host).is_loopback:
            return url
    except ValueError:
        pass
    raise ValueError(f"host {host!r} is not a loopback address")


def read_token() -> str | None:
    """Token from DOSBOX_API_TOKEN, else the token file the launcher writes.
    Returns None if no token is available (dosbox not running yet)."""
    env = os.environ.get("DOSBOX_API_TOKEN")
    if env:
        return env
    token_path = Path(
        os.environ.get(
            "DOSBOX_TOKEN_FILE",
            Path.home() / ".config" / "dosbox-automation" / "webserver" / "api_token",
        )
    )
    if token_path.is_file():
        return token_path.read_text().strip()
    return None


@dataclass
class Config:
    base_url: str
    token: str | None = None

    @classmethod
    def from_env(cls) -> "Config":
        base = os.environ.get("DOSBOX_API_URL", "http://127.0.0.1:8386")
        return cls(base_url=validate_base_url(base), token=read_token())
