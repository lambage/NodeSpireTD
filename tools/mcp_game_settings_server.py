#!/usr/bin/env python3
"""MCP server for live NodeSpireTD settings inspection and tweaking.

This server edits config/settings.json and is intended to be paired with the
in-game PlayLevel settings hot-reload loop.
"""

from __future__ import annotations

import json
import math
import os
import sys
from pathlib import Path
from typing import Any

JSONRPC_VERSION = "2.0"
SERVER_NAME = "nodespire-game-settings"
SERVER_VERSION = "0.1.0"
PROTOCOL_VERSION = "2025-06-18"

DEFAULT_SETTINGS: dict[str, Any] = {
    "fullscreen": False,
    "exclusiveFullscreen": False,
    "vSyncEnabled": True,
    "displayWidth": 1280,
    "displayHeight": 720,
    "refreshRate": 60,
    "graphicsQuality": 2,
    "masterVolume": 0.8,
    "musicVolume": 0.7,
    "sfxVolume": 0.8,
    "audioDevice": "",
    "muteWhenUnfocused": True,
}

DEFAULT_DEVTOOLS: dict[str, Any] = {
    "enableGameplayMcpTuning": False,
}

DEFAULT_GAMEPLAY_TUNING: dict[str, Any] = {
    "enemyHealthMultiplier": 1.0,
    "enemySpeedMultiplier": 1.0,
    "enemyRewardMultiplier": 1.0,
    "enemyBaseDamageMultiplier": 1.0,
    "hostMoneyOverride": None,
    "baseHealthOverride": None,
    "waveCountdownSecondsOverride": None,
}

BOOL_FIELDS = {
    "fullscreen",
    "exclusiveFullscreen",
    "vSyncEnabled",
    "muteWhenUnfocused",
}

INT_FIELDS: dict[str, tuple[int, int]] = {
    "displayWidth": (640, 7680),
    "displayHeight": (360, 4320),
    "refreshRate": (30, 360),
    "graphicsQuality": (0, 4),
}

FLOAT_FIELDS: dict[str, tuple[float, float]] = {
    "masterVolume": (0.0, 1.0),
    "musicVolume": (0.0, 1.0),
    "sfxVolume": (0.0, 1.0),
}


class JsonRpcError(Exception):
    def __init__(self, code: int, message: str, data: Any = None):
        super().__init__(message)
        self.code = code
        self.message = message
        self.data = data


def _read_message() -> dict[str, Any] | None:
    content_length = None
    while True:
        header_line = sys.stdin.buffer.readline()
        if not header_line:
            return None
        if header_line in (b"\r\n", b"\n"):
            break
        header = header_line.decode("utf-8").strip()
        if not header:
            continue
        name, sep, value = header.partition(":")
        if sep and name.lower() == "content-length":
            content_length = int(value.strip())

    if content_length is None:
        raise JsonRpcError(-32700, "Missing Content-Length header")

    payload = sys.stdin.buffer.read(content_length)
    if not payload:
        return None
    return json.loads(payload.decode("utf-8"))


def _write_message(payload: dict[str, Any]) -> None:
    body = json.dumps(payload, ensure_ascii=True, separators=(",", ":")).encode("utf-8")
    sys.stdout.buffer.write(f"Content-Length: {len(body)}\r\n\r\n".encode("ascii"))
    sys.stdout.buffer.write(body)
    sys.stdout.buffer.flush()


def _result(msg_id: Any, result: dict[str, Any]) -> dict[str, Any]:
    return {"jsonrpc": JSONRPC_VERSION, "id": msg_id, "result": result}


def _error(msg_id: Any, code: int, message: str, data: Any = None) -> dict[str, Any]:
    err: dict[str, Any] = {"code": code, "message": message}
    if data is not None:
        err["data"] = data
    return {"jsonrpc": JSONRPC_VERSION, "id": msg_id, "error": err}


class GameSettingsServer:
    def __init__(self) -> None:
        repo_root = Path(__file__).resolve().parents[1]
        self.settings_path = repo_root / "config" / "settings.json"
        self.devtools_path = repo_root / "config" / "devtools.json"
        self.gameplay_tuning_path = repo_root / "config" / "gameplay_tuning.json"
        # Explicit opt-in gate for gameplay write operations.
        self.allow_gameplay_writes = os.getenv("NODESPIRE_ENABLE_DEV_MCP_TUNING", "0") == "1"

    @staticmethod
    def _read_json_or_default(path: Path, defaults: dict[str, Any]) -> dict[str, Any]:
        if not path.exists():
            return dict(defaults)
        try:
            loaded = json.loads(path.read_text(encoding="utf-8"))
            merged = dict(defaults)
            if isinstance(loaded, dict):
                merged.update(loaded)
            return merged
        except json.JSONDecodeError as exc:
            raise JsonRpcError(-32010, f"{path.name} is invalid JSON", {"error": str(exc)}) from exc

    @staticmethod
    def _write_json(path: Path, payload: dict[str, Any]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(payload, indent=4) + "\n", encoding="utf-8")

    def read_settings(self) -> dict[str, Any]:
        if not self.settings_path.exists():
            self.write_settings(DEFAULT_SETTINGS)
            return dict(DEFAULT_SETTINGS)

        try:
            content = json.loads(self.settings_path.read_text(encoding="utf-8"))
        except json.JSONDecodeError as exc:
            raise JsonRpcError(-32010, "settings.json is invalid JSON", {"error": str(exc)}) from exc

        merged = dict(DEFAULT_SETTINGS)
        if isinstance(content, dict):
            merged.update(content)
        return merged

    def write_settings(self, settings: dict[str, Any]) -> None:
        self._write_json(self.settings_path, settings)

    def read_devtools(self) -> dict[str, Any]:
        current = self._read_json_or_default(self.devtools_path, DEFAULT_DEVTOOLS)
        self._write_json(self.devtools_path, current)
        return current

    def set_gameplay_tuning_enabled(self, enabled: bool) -> dict[str, Any]:
        if not self.allow_gameplay_writes:
            raise JsonRpcError(
                -32020,
                "Gameplay tuning writes are locked. Set NODESPIRE_ENABLE_DEV_MCP_TUNING=1 to enable.",
            )
        devtools = self.read_devtools()
        devtools["enableGameplayMcpTuning"] = bool(enabled)
        self._write_json(self.devtools_path, devtools)
        return devtools

    def read_gameplay_tuning(self) -> dict[str, Any]:
        current = self._read_json_or_default(self.gameplay_tuning_path, DEFAULT_GAMEPLAY_TUNING)
        self._write_json(self.gameplay_tuning_path, current)
        return current

    def patch_gameplay_tuning(self, patch: dict[str, Any]) -> dict[str, Any]:
        if not self.allow_gameplay_writes:
            raise JsonRpcError(
                -32020,
                "Gameplay tuning writes are locked. Set NODESPIRE_ENABLE_DEV_MCP_TUNING=1 to enable.",
            )
        devtools = self.read_devtools()
        if not bool(devtools.get("enableGameplayMcpTuning", False)):
            raise JsonRpcError(
                -32021,
                "Gameplay tuning is disabled in config/devtools.json. Enable it first with gameplay_tuning_set_enabled.",
            )
        if not isinstance(patch, dict):
            raise JsonRpcError(-32602, "patch must be an object")

        current = self.read_gameplay_tuning()
        for key, value in patch.items():
            if key not in DEFAULT_GAMEPLAY_TUNING:
                raise JsonRpcError(-32602, f"Unknown gameplay tuning field '{key}'")
            current[key] = self._validate_gameplay_field(key, value)
        self._write_json(self.gameplay_tuning_path, current)
        return current

    @staticmethod
    def _validate_gameplay_field(key: str, value: Any) -> Any:
        multiplier_bounds = {
            "enemyHealthMultiplier": (0.05, 20.0),
            "enemySpeedMultiplier": (0.05, 20.0),
            "enemyRewardMultiplier": (0.0, 20.0),
            "enemyBaseDamageMultiplier": (0.0, 20.0),
        }
        optional_nonnegative = {
            "hostMoneyOverride",
            "baseHealthOverride",
            "waveCountdownSecondsOverride",
        }

        if key in multiplier_bounds:
            if not isinstance(value, (int, float)):
                raise JsonRpcError(-32602, f"{key} must be number")
            val = float(value)
            low, high = multiplier_bounds[key]
            if math.isnan(val) or math.isinf(val) or val < low or val > high:
                raise JsonRpcError(-32602, f"{key} must be between {low} and {high}")
            return val

        if key in optional_nonnegative:
            if value is None:
                return None
            if not isinstance(value, (int, float)):
                raise JsonRpcError(-32602, f"{key} must be number or null")
            val = float(value)
            if math.isnan(val) or math.isinf(val) or val < 0.0:
                raise JsonRpcError(-32602, f"{key} must be null or >= 0")
            return val

        raise JsonRpcError(-32602, f"Unsupported gameplay tuning field '{key}'")

    def patch_settings(self, patch: dict[str, Any]) -> dict[str, Any]:
        if not isinstance(patch, dict):
            raise JsonRpcError(-32602, "patch must be an object")

        current = self.read_settings()
        for key, value in patch.items():
            if key not in DEFAULT_SETTINGS:
                raise JsonRpcError(-32602, f"Unknown setting '{key}'")
            current[key] = self._validate_field(key, value)

        self.write_settings(current)
        return current

    @staticmethod
    def _validate_field(key: str, value: Any) -> Any:
        if key in BOOL_FIELDS:
            if isinstance(value, bool):
                return value
            raise JsonRpcError(-32602, f"{key} must be boolean")

        if key in INT_FIELDS:
            if not isinstance(value, int):
                raise JsonRpcError(-32602, f"{key} must be integer")
            low, high = INT_FIELDS[key]
            if value < low or value > high:
                raise JsonRpcError(-32602, f"{key} must be between {low} and {high}")
            return value

        if key in FLOAT_FIELDS:
            if not isinstance(value, (int, float)):
                raise JsonRpcError(-32602, f"{key} must be number")
            val = float(value)
            if math.isnan(val) or math.isinf(val):
                raise JsonRpcError(-32602, f"{key} must be finite")
            low, high = FLOAT_FIELDS[key]
            if val < low or val > high:
                raise JsonRpcError(-32602, f"{key} must be between {low} and {high}")
            return val

        if key == "audioDevice":
            if isinstance(value, str):
                return value
            raise JsonRpcError(-32602, "audioDevice must be string")

        raise JsonRpcError(-32602, f"Unsupported setting '{key}'")

    @staticmethod
    def analyze(settings: dict[str, Any]) -> dict[str, Any]:
        master = float(settings["masterVolume"])
        music = float(settings["musicVolume"])
        sfx = float(settings["sfxVolume"])

        effective_music = max(0.0, min(1.0, master * music))
        effective_sfx = max(0.0, min(1.0, master * sfx))

        warnings: list[str] = []
        if master <= 0.01:
            warnings.append("Master volume is effectively muted.")
        if settings["displayWidth"] < 1024 or settings["displayHeight"] < 720:
            warnings.append("Display resolution is below 1024x720.")
        if settings["refreshRate"] < 60:
            warnings.append("Refresh rate is below 60 Hz.")

        return {
            "effectiveMusicGain": round(effective_music, 4),
            "effectiveSfxGain": round(effective_sfx, 4),
            "warnings": warnings,
        }

    def list_tools(self) -> list[dict[str, Any]]:
        return [
            {
                "name": "settings_get",
                "description": "Get current NodeSpireTD settings.json values.",
                "inputSchema": {"type": "object", "properties": {}},
            },
            {
                "name": "settings_patch",
                "description": "Patch one or more settings fields and persist to config/settings.json.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "patch": {
                            "type": "object",
                            "description": "Partial settings object to merge.",
                        }
                    },
                    "required": ["patch"],
                    "additionalProperties": False,
                },
            },
            {
                "name": "settings_reset_defaults",
                "description": "Reset config/settings.json to built-in defaults.",
                "inputSchema": {"type": "object", "properties": {}},
            },
            {
                "name": "settings_analyze",
                "description": "Return derived settings analysis and warnings.",
                "inputSchema": {"type": "object", "properties": {}},
            },
            {
                "name": "gameplay_tuning_status",
                "description": "Get gameplay tuning status, lock state, and current tuning values.",
                "inputSchema": {"type": "object", "properties": {}},
            },
            {
                "name": "gameplay_tuning_set_enabled",
                "description": "Enable or disable host runtime gameplay tuning (config/devtools.json).",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "enabled": {"type": "boolean"},
                    },
                    "required": ["enabled"],
                    "additionalProperties": False,
                },
            },
            {
                "name": "gameplay_tuning_patch",
                "description": "Patch gameplay tuning fields in config/gameplay_tuning.json.",
                "inputSchema": {
                    "type": "object",
                    "properties": {
                        "patch": {
                            "type": "object",
                            "description": "Partial gameplay tuning object to merge.",
                        }
                    },
                    "required": ["patch"],
                    "additionalProperties": False,
                },
            },
            {
                "name": "gameplay_tuning_reset",
                "description": "Reset gameplay tuning values to defaults.",
                "inputSchema": {"type": "object", "properties": {}},
            },
        ]

    @staticmethod
    def _tool_response(payload: Any) -> dict[str, Any]:
        return {
            "content": [{"type": "text", "text": json.dumps(payload, indent=2)}],
            "structuredContent": payload,
        }

    def call_tool(self, name: str, args: dict[str, Any] | None) -> dict[str, Any]:
        args = args or {}
        if name == "settings_get":
            settings = self.read_settings()
            return self._tool_response(
                {
                    "settingsFile": str(self.settings_path),
                    "settings": settings,
                }
            )
        if name == "settings_patch":
            settings = self.patch_settings(args.get("patch"))
            return self._tool_response({"updated": True, "settings": settings})
        if name == "settings_reset_defaults":
            self.write_settings(dict(DEFAULT_SETTINGS))
            return self._tool_response({"updated": True, "settings": dict(DEFAULT_SETTINGS)})
        if name == "settings_analyze":
            settings = self.read_settings()
            return self._tool_response(
                {
                    "settings": settings,
                    "analysis": self.analyze(settings),
                }
            )
        if name == "gameplay_tuning_status":
            devtools = self.read_devtools()
            tuning = self.read_gameplay_tuning()
            return self._tool_response(
                {
                    "buildRuntimeNote": "Gameplay tuning only takes effect in game builds compiled with NODESPIRE_ENABLE_GAMEPLAY_MCP_TUNING=ON.",
                    "writeGateEnvVar": "NODESPIRE_ENABLE_DEV_MCP_TUNING",
                    "writeGateOpen": self.allow_gameplay_writes,
                    "enabledInDevtools": bool(devtools.get("enableGameplayMcpTuning", False)),
                    "devtoolsFile": str(self.devtools_path),
                    "tuningFile": str(self.gameplay_tuning_path),
                    "tuning": tuning,
                }
            )
        if name == "gameplay_tuning_set_enabled":
            enabled = args.get("enabled")
            if not isinstance(enabled, bool):
                raise JsonRpcError(-32602, "enabled must be boolean")
            devtools = self.set_gameplay_tuning_enabled(enabled)
            return self._tool_response(
                {
                    "updated": True,
                    "enabledInDevtools": bool(devtools.get("enableGameplayMcpTuning", False)),
                    "devtoolsFile": str(self.devtools_path),
                }
            )
        if name == "gameplay_tuning_patch":
            tuning = self.patch_gameplay_tuning(args.get("patch"))
            return self._tool_response(
                {
                    "updated": True,
                    "tuningFile": str(self.gameplay_tuning_path),
                    "tuning": tuning,
                }
            )
        if name == "gameplay_tuning_reset":
            if not self.allow_gameplay_writes:
                raise JsonRpcError(
                    -32020,
                    "Gameplay tuning writes are locked. Set NODESPIRE_ENABLE_DEV_MCP_TUNING=1 to enable.",
                )
            self._write_json(self.gameplay_tuning_path, dict(DEFAULT_GAMEPLAY_TUNING))
            return self._tool_response(
                {
                    "updated": True,
                    "tuningFile": str(self.gameplay_tuning_path),
                    "tuning": dict(DEFAULT_GAMEPLAY_TUNING),
                }
            )

        raise JsonRpcError(-32602, f"Unknown tool '{name}'")


def _handle_request(server: GameSettingsServer, request: dict[str, Any]) -> dict[str, Any] | None:
    msg_id = request.get("id")
    method = request.get("method")
    params = request.get("params", {})

    if not method:
        if msg_id is None:
            return None
        return _error(msg_id, -32600, "Invalid request: missing method")

    try:
        if method == "initialize":
            return _result(
                msg_id,
                {
                    "protocolVersion": PROTOCOL_VERSION,
                    "capabilities": {"tools": {}},
                    "serverInfo": {"name": SERVER_NAME, "version": SERVER_VERSION},
                    "instructions": (
                        "Use settings_get and settings_analyze to inspect current values, then "
                        "settings_patch to tweak config/settings.json while the game is running."
                    ),
                },
            )

        if method == "tools/list":
            return _result(msg_id, {"tools": server.list_tools()})

        if method == "tools/call":
            name = params.get("name")
            arguments = params.get("arguments")
            if not isinstance(name, str):
                raise JsonRpcError(-32602, "tools/call requires string param 'name'")
            result = server.call_tool(name, arguments if isinstance(arguments, dict) or arguments is None else {})
            return _result(msg_id, result)

        # Notifications we intentionally ignore.
        if method in {"notifications/initialized", "initialized"}:
            return None

        if msg_id is None:
            return None
        return _error(msg_id, -32601, f"Method not found: {method}")
    except JsonRpcError as exc:
        if msg_id is None:
            return None
        return _error(msg_id, exc.code, exc.message, exc.data)
    except Exception as exc:  # defensive guard for MCP host stability
        if msg_id is None:
            return None
        return _error(msg_id, -32000, "Internal server error", {"error": str(exc)})


def main() -> int:
    server = GameSettingsServer()
    while True:
        try:
            request = _read_message()
        except JsonRpcError as exc:
            _write_message(_error(None, exc.code, exc.message, exc.data))
            continue
        except Exception as exc:
            _write_message(_error(None, -32700, "Parse error", {"error": str(exc)}))
            continue

        if request is None:
            return 0

        response = _handle_request(server, request)
        if response is not None:
            _write_message(response)


if __name__ == "__main__":
    raise SystemExit(main())
