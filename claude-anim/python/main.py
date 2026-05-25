"""Claude Code status indicator — MPU side.

Listens on 0.0.0.0:8765 for state pushes from the developer's Mac and
forwards them to the MCU via Bridge.call("set_state", <name>).

POST /state           {"state": "thinking"} → 200 {"ok": true}
GET  /state           → 200 {"state": "<current>"}
GET  /health          → 200 {"ok": true, "uptime": <seconds>}
"""

from arduino.app_utils import App, Bridge
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import logging
import threading
import time

PORT = 8765
VALID_STATES = {"idle", "thinking", "tool", "done", "notify", "error"}

logger = logging.getLogger("claude_anim")
logger.setLevel(logging.INFO)

current_state = "idle"
state_lock = threading.Lock()
started_at = time.time()


def push_state_to_mcu(state: str) -> bool:
    """Forward the state to the MCU. Returns True on success."""
    try:
        result = Bridge.call("set_state", state)
        ok = bool(result.result()[0]) if callable(getattr(result, "result", None)) else bool(result)
        return ok
    except Exception as exc:
        logger.warning("Bridge.call set_state(%s) failed: %s", state, exc)
        return False


class Handler(BaseHTTPRequestHandler):
    def _send_json(self, code: int, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        logger.info("%s - %s", self.address_string(), fmt % args)

    def do_GET(self):
        if self.path == "/health":
            self._send_json(200, {"ok": True, "uptime": int(time.time() - started_at)})
            return
        if self.path == "/state":
            with state_lock:
                self._send_json(200, {"state": current_state})
            return
        self._send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/state":
            self._send_json(404, {"error": "not found"})
            return
        length = int(self.headers.get("Content-Length") or 0)
        raw = self.rfile.read(length) if length else b""
        try:
            data = json.loads(raw or b"{}")
        except json.JSONDecodeError:
            self._send_json(400, {"error": "invalid json"})
            return
        state = (data.get("state") or "").strip().lower()
        if state not in VALID_STATES:
            self._send_json(400, {"error": "unknown state", "valid": sorted(VALID_STATES)})
            return
        ok = push_state_to_mcu(state)
        global current_state
        with state_lock:
            current_state = state
        self._send_json(200, {"ok": ok, "state": state})


def run_server():
    server = ThreadingHTTPServer(("0.0.0.0", PORT), Handler)
    logger.info("HTTP server listening on 0.0.0.0:%d", PORT)
    server.serve_forever()


def loop():
    time.sleep(60)


# Boot the HTTP server in a background thread; App.run() keeps the
# container alive while the sketch animates on the MCU.
threading.Thread(target=run_server, daemon=True).start()
# Seed the MCU into idle state on startup.
time.sleep(1)
push_state_to_mcu("idle")

App.run(user_loop=loop)
