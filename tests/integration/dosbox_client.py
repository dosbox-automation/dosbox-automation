import time
from pathlib import Path

import requests


class DosboxClient:
    """Thin wrapper for the dosbox-automation REST API."""

    def __init__(self, base_url: str, token: str = "", timeout: float = 10.0):
        self.base_url = base_url.rstrip("/")
        self.timeout = timeout
        self.session = requests.Session()
        self.session.headers["Host"] = "127.0.0.1"
        if token:
            self.session.headers["Authorization"] = f"Bearer {token}"

    def _url(self, path: str) -> str:
        return f"{self.base_url}{path}"

    def _get(self, path: str, **kwargs) -> requests.Response:
        return self.session.get(self._url(path), timeout=self.timeout, **kwargs)

    def _post(self, path: str, **kwargs) -> requests.Response:
        return self.session.post(self._url(path), timeout=self.timeout, **kwargs)

    def _put(self, path: str, **kwargs) -> requests.Response:
        return self.session.put(self._url(path), timeout=self.timeout, **kwargs)

    # --- Status & Info ---

    def status(self) -> requests.Response:
        return self._get("/api/v1/status")

    def program_state(self) -> requests.Response:
        return self._get("/api/v1/program/state")

    def dosbox_info(self) -> requests.Response:
        return self._get("/api/v1/dosbox/info")

    # --- CPU ---

    def cpu_state(self) -> requests.Response:
        return self._get("/api/v1/cpu/state")

    # --- DOS ---

    def dos_internals(self) -> requests.Response:
        return self._get("/api/v1/dos/internals")

    # --- Input ---

    def input_sequence(self, events: list[dict]) -> requests.Response:
        return self._post("/api/v1/input/sequence", json={"events": events})

    def input_sequence_raw(self, body: str) -> requests.Response:
        return self._post(
            "/api/v1/input/sequence",
            data=body,
            headers={"Content-Type": "application/json"},
        )

    def press_key(self, key: str, pressed: bool = True) -> requests.Response:
        return self.input_sequence(
            [{"type": "key", "key": key, "pressed": pressed}]
        )

    def type_string(self, text: str, delay_ms: float = 80) -> requests.Response:
        key_map = {c: f"KBD_{c}" for c in "abcdefghijklmnopqrstuvwxyz0123456789"}
        key_map[" "] = "KBD_space"
        key_map["\n"] = "KBD_enter"
        key_map["\r"] = "KBD_enter"
        key_map["\\"] = "KBD_backslash"
        key_map["."] = "KBD_period"
        key_map[","] = "KBD_comma"
        key_map["-"] = "KBD_minus"
        key_map["="] = "KBD_equals"
        key_map["/"] = "KBD_slash"

        shifted = {
            ":": "KBD_semicolon",
            "!": "KBD_1",
            "@": "KBD_2",
            "#": "KBD_3",
            "$": "KBD_4",
            "%": "KBD_5",
            "^": "KBD_6",
            "&": "KBD_7",
            "*": "KBD_8",
            "(": "KBD_9",
            ")": "KBD_0",
            "_": "KBD_minus",
            "+": "KBD_equals",
            "?": "KBD_slash",
        }

        events = []
        t = 0.0
        for ch in text:
            lower = ch.lower()
            need_shift = ch in shifted or (ch.isalpha() and ch.isupper())
            k = shifted.get(ch) or key_map.get(lower)
            if k:
                if need_shift:
                    events.append({"t": t, "type": "key", "key": "KBD_leftshift", "pressed": True})
                events.append({"t": t, "type": "key", "key": k, "pressed": True})
                events.append(
                    {"t": t + delay_ms / 2, "type": "key", "key": k, "pressed": False}
                )
                if need_shift:
                    events.append({"t": t + delay_ms / 2, "type": "key", "key": "KBD_leftshift", "pressed": False})
                t += delay_ms
        return self.input_sequence(events)

    # --- Recording ---

    def recording_start(self) -> requests.Response:
        return self._post("/api/v1/input/record/start")

    def recording_pause(self) -> requests.Response:
        return self._post("/api/v1/input/record/pause")

    def recording_stop(self) -> requests.Response:
        return self._post("/api/v1/input/record/stop")

    def recording_status(self) -> requests.Response:
        return self._get("/api/v1/input/record/status")

    # --- Video ---

    def frame(self, fmt: str = "jpeg", quality: int = 98) -> requests.Response:
        params = {"format": fmt, "quality": str(quality)}
        return self._get("/api/v1/video/frame", params=params)

    def frame_info(self) -> requests.Response:
        return self._get("/api/v1/video/frame/info")

    def screen_text(self) -> requests.Response:
        return self._get("/api/v1/video/text")

    def capture_frame(self, path: Path, fmt: str = "jpeg") -> Path:
        r = self.frame(fmt=fmt)
        r.raise_for_status()
        path.write_bytes(r.content)
        return path

    # --- Drive ---

    def drive_swap(self, drive: str, image: str) -> requests.Response:
        return self._post("/api/v1/drive/swap", json={"drive": drive, "image": image})

    def drive_swap_raw(self, body: str) -> requests.Response:
        return self._post(
            "/api/v1/drive/swap",
            data=body,
            headers={"Content-Type": "application/json"},
        )

    # --- Mount Policy ---

    def mount_lock(self) -> requests.Response:
        return self._post("/api/v1/mount/lock")

    def mount_lock_status(self) -> requests.Response:
        return self._get("/api/v1/mount/lock")

    # --- Memory ---

    def memory_read(self, offset: int, length: int) -> requests.Response:
        return self._get(f"/api/v1/memory/{offset}/{length}")

    def memory_read_json(self, offset: int, length: int) -> requests.Response:
        return self._get(
            f"/api/v1/memory/{offset}/{length}",
            headers={"Accept": "application/json"},
        )

    def memory_write(self, offset: int, data: bytes) -> requests.Response:
        return self._put(
            f"/api/v1/memory/{offset}",
            data=data,
            headers={"Content-Type": "application/octet-stream"},
        )

    def memory_allocate(
        self, size: int, area: str = "CONV", strategy: str = "BEST_FIT"
    ) -> requests.Response:
        return self._post(
            "/api/v1/memory/allocate",
            json={"size": size, "area": area, "strategy": strategy},
        )

    def memory_free(self, addr: int) -> requests.Response:
        return self._post("/api/v1/memory/free", json={"addr": addr})

    # --- Control ---

    def shutdown(self) -> requests.Response:
        return self._post("/api/v1/control/shutdown")

    # --- Helpers ---

    def wait_ready(self, timeout: float = 10.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                r = self.status()
                if r.status_code == 200:
                    return
            except requests.ConnectionError:
                pass
            time.sleep(0.2)
        raise TimeoutError(f"DOSBox webserver not ready after {timeout}s")

    def wait_program(self, name: str, timeout: float = 15.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r = self.program_state()
            if r.status_code == 200:
                state = r.json()
                if name.lower() in state.get("segment_name", "").lower():
                    return state
            time.sleep(0.5)
        raise TimeoutError(f"Program '{name}' not detected after {timeout}s")

    def wait_shell(self, timeout: float = 15.0):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r = self.program_state()
            if r.status_code == 200:
                state = r.json()
                if state.get("is_shell"):
                    return state
            time.sleep(0.5)
        raise TimeoutError(f"Shell not detected after {timeout}s")

    def get_with_host(self, path: str, host: str) -> requests.Response:
        headers = {"Host": host}
        if "Authorization" in self.session.headers:
            headers["Authorization"] = self.session.headers["Authorization"]
        return self.session.get(
            self._url(path),
            timeout=self.timeout,
            headers=headers,
        )

    def get_without_token(self, path: str) -> requests.Response:
        headers = {"Host": "127.0.0.1"}
        return requests.get(
            self._url(path), timeout=self.timeout, headers=headers
        )

    # --- Lua Scripting ---

    def script_load(self, source: str, name: str = "test",
                    seed: int | None = None,
                    debug: bool = False) -> requests.Response:
        params = {"name": name}
        if seed is not None:
            params["seed"] = str(seed)
        if debug:
            params["debug"] = "true"
        return self._post(
            "/api/v1/script/load",
            params=params,
            data=source,
            headers={"Content-Type": "text/plain"},
        )

    def script_start(self) -> requests.Response:
        return self._post("/api/v1/script/start")

    def script_stop(self) -> requests.Response:
        return self._post("/api/v1/script/stop")

    def script_status(self) -> requests.Response:
        return self._get("/api/v1/script/status")

    def wait_script_done(self, timeout: float = 30.0) -> dict:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            r = self.script_status()
            if r.status_code == 200:
                data = r.json()
                if data["state"] in ("completed", "error"):
                    return data
            time.sleep(0.2)
        # Stop the still-running script before giving up. A module-scoped
        # fixture is shared across tests, so a script left running would make
        # every later script_load return 400 "a script is already running" and
        # cascade failures through the rest of the file.
        try:
            self.script_stop()
        except requests.RequestException:
            pass
        raise TimeoutError(f"Script did not finish within {timeout}s")

    # --- Video Capture ---

    def capture_start(self) -> requests.Response:
        return self._post("/api/v1/capture/video/start")

    def capture_stop(self) -> requests.Response:
        return self._post("/api/v1/capture/video/stop")

    def capture_status(self) -> requests.Response:
        return self._get("/api/v1/capture/video/status")
