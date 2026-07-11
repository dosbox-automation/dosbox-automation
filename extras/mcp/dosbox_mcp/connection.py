# This file is part of the dosbox-automation Project.
# License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
#

import logging

import httpx
import mcp.types as types

from .client import DosboxClient
from .config import Config, read_token

log = logging.getLogger(__name__)


class Connection:
    """Lazy connector to a dosbox-automation instance.

    Starts disconnected. On the first tool call, tries to read the token
    and probe the running instance. Reconnects automatically if the
    instance restarts (new token) or comes back after going away."""

    def __init__(self, config: Config):
        self._config = config
        self._client: DosboxClient | None = None
        self._features: dict = {}

    @property
    def connected(self) -> bool:
        return self._client is not None

    @property
    def base_url(self) -> str:
        return self._config.base_url

    @property
    def features(self) -> dict:
        return self._features

    def _try_connect(self):
        token = self._config.token or read_token()
        if token is None:
            raise NotConnected("No API token available - is dosbox-automation running?")

        client = DosboxClient(self._config.base_url, token)
        try:
            info = client.get("/api/v1/dosbox/info")
        except httpx.ConnectError:
            raise NotConnected(
                f"Cannot reach dosbox-automation at {self._config.base_url}"
            )

        self._client = client
        self._features = info.get("features", {})
        log.info("attached to %s (%s)", self._config.base_url, info.get("version", "?"))

    def ensure_connected(self):
        if self._client is None:
            self._try_connect()

    def detach(self):
        self._client = None
        self._features = {}
        log.info("detached from dosbox-automation")

    def call(self, method, path, **kwargs):
        """Execute an HTTP call, reconnecting once on failure."""
        self.ensure_connected()
        fn = getattr(self._client, method)
        try:
            return fn(path, **kwargs)
        except (httpx.ConnectError, httpx.RemoteProtocolError):
            self.detach()
            raise NotConnected("dosbox-automation went away during the call")
        except RuntimeError as e:
            if "401" in str(e):
                self.detach()
                self._try_connect()
                fn = getattr(self._client, method)
                return fn(path, **kwargs)
            raise

    def get(self, path, **kwargs):
        return self.call("get", path, **kwargs)

    def post(self, path, **kwargs):
        return self.call("post", path, **kwargs)

    def put(self, path, **kwargs):
        return self.call("put", path, **kwargs)

    def delete(self, path, **kwargs):
        return self.call("delete", path, **kwargs)


class NotConnected(Exception):
    pass


def guard(connection: Connection, handler, feature=None):
    """Wrap a tool handler with connection and feature checks."""
    def guarded(args):
        try:
            connection.ensure_connected()
            if feature and not connection.features.get(feature):
                return [types.TextContent(
                    type="text",
                    text=f"Feature '{feature}' is not enabled in the running instance.",
                )]
            return handler(args)
        except NotConnected as e:
            return [types.TextContent(type="text", text=str(e))]
    return guarded
