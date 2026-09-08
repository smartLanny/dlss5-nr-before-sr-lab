#!/usr/bin/env python3
"""Prepare a ReShade pre-load carrier exclusion for an explicit graphics API.

This tool never infers the API from loaded modules, helper devices or game names.
Preview is the default. Applying appends one independent ADDON section while
preserving every existing byte, then emits a same-directory backup and receipt.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import stat
import sys
import tempfile


MAX_INI_BYTES = 4 * 1024 * 1024
ACTIVE_APIS = {"d3d12", "vulkan"}
CARRIER_PREFIX = "dlss5-native-carrier-"
CARRIER_SUFFIX = ".addon64"
BRIDGE_NAME = "dlss5-bridge.addon64"
HASH = re.compile(r"^[0-9a-f]{64}$")


class CarrierConfigError(ValueError):
    def __init__(self, code: str, message: str):
        super().__init__(message)
        self.code = code


def require(condition: bool, code: str, message: str) -> None:
    if not condition:
        raise CarrierConfigError(code, message)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def regular_path(path: Path, kind: str) -> None:
    require(not path.is_symlink(), "symlink-refused", f"{kind} must not be a symlink")
    require(path.is_file(), "not-regular-file", f"{kind} must be a regular file")


def read_ini(path: Path) -> tuple[bytes, str, str]:
    regular_path(path, "ini")
    size = path.stat().st_size
    require(size <= MAX_INI_BYTES, "ini-too-large", "INI exceeds the 4 MiB safety limit")
    data = path.read_bytes()
    require(b"\x00" not in data, "unsupported-encoding", "INI must be UTF-8, optionally with a BOM")
    bom = data.startswith(b"\xef\xbb\xbf")
    payload = data[3:] if bom else data
    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise CarrierConfigError("unsupported-encoding", "INI is not valid UTF-8") from exc
    require(not any((ord(character) < 32 and character not in "\t\r\n") or ord(character) == 127
                    for character in text), "unsupported-control-character",
            "INI contains a control character that ReShade text-mode parsing may not preserve")
    return data, text, "utf-8-bom" if bom else "utf-8"


def split_reshade_vector(value: str) -> list[str]:
    """Match ReShade ini_file.cpp: ',,' is one literal comma; ',' splits."""
    elements: list[str] = []
    offset = base = 0
    length = len(value)
    while offset <= length:
        found = value.find(",", offset)
        if found < 0:
            found = length
        if found + 1 < length and value[found + 1] == ",":
            offset = found + 2
            continue
        raw = value[base:found]
        elements.append(raw.replace(",,", ","))
        base = found + 1
        offset = base
    return elements


def addon_values(text: str) -> dict[str, list[str]]:
    values: dict[str, list[str]] = {}
    section = ""
    for physical in text.split("\n"):
        line = physical.strip(" \t\r\n")
        if not line or line[0] in ";/#":
            continue
        if line.startswith("["):
            end = line.find("]")
            section = line[:end if end >= 0 else len(line)].strip(" \t[]")
            continue
        if section != "ADDON" or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip(" \t\r\n")
        value = value.strip(" \t\r\n")
        values.setdefault(key, [])
        if value:
            values[key].extend(split_reshade_vector(value))
    return values


def is_carrier_name(name: str) -> bool:
    folded = name.casefold()
    return ((folded.startswith(CARRIER_PREFIX) and folded.endswith(CARRIER_SUFFIX)) or
            folded == BRIDGE_NAME)


def suspicious_direct_name(name: str) -> bool:
    folded = name.casefold()
    return is_carrier_name(name) or ("carrier" in folded and folded.endswith((".addon64", ".dll")))


def addon_directory(ini: Path, values: dict[str, list[str]]) -> Path:
    entries = values.get("AddonPath", [])
    if not entries or not entries[0]:
        return ini.parent
    configured = Path(entries[0])
    return configured if configured.is_absolute() else ini.parent / configured


def discover_carriers(directory: Path) -> list[str]:
    if not directory.exists():
        return []
    require(not directory.is_symlink() and directory.is_dir(), "addon-path-not-regular-directory",
            "AddonPath must name an ordinary directory")
    result = []
    for entry in directory.iterdir():
        if entry.is_symlink() or not entry.is_file():
            continue
        if is_carrier_name(entry.name):
            result.append(entry.name)
    return sorted(result, key=lambda value: (value.casefold(), value))


def direct_load_conflicts(directory: Path, values: dict[str, list[str]], carriers: list[str]) -> list[str]:
    actual = {name.casefold() for name in carriers}
    conflicts = []
    for configured in values.get("LoadFromDllMain", []):
        if not configured:
            continue
        leaf = Path(configured).name
        candidate = Path(configured)
        candidate = candidate if candidate.is_absolute() else directory / candidate
        if suspicious_direct_name(leaf) or leaf.casefold() in actual or (
                candidate.exists() and suspicious_direct_name(candidate.name)):
            conflicts.append(configured)
    return conflicts


def escape_reshade_element(value: str) -> str:
    return value.replace(",", ",,")


def append_exclusion(data: bytes, encoding: str, entries: list[str]) -> bytes:
    payload = data[3:] if encoding == "utf-8-bom" else data
    newline = "\r\n" if b"\r\n" in payload else "\n"
    separator = ""
    if payload:
        if not payload.endswith((b"\r", b"\n")):
            separator = newline + newline
        elif not payload.endswith((b"\r\n\r\n", b"\n\n")):
            separator = newline
    joined = ",".join(escape_reshade_element(value) for value in entries)
    block = f"{separator}[ADDON]{newline}DisabledAddons={joined}{newline}"
    return data + block.encode("utf-8")


@dataclass(frozen=True)
class Plan:
    ini: Path
    api: str
    original: bytes
    encoding: str
    before_sha256: str
    addon_path: Path
    carriers: tuple[str, ...]
    existing_disabled: tuple[str, ...]
    entries: tuple[str, ...]
    direct_loads: tuple[str, ...]
    after: bytes
    status: str

    def public(self, mode: str = "preview") -> dict[str, object]:
        return {
            "schema": 1,
            "mode": mode,
            "status": self.status,
            "api": self.api,
            "ini": str(self.ini),
            "iniEncoding": self.encoding,
            "beforeSha256": self.before_sha256,
            "afterSha256": digest(self.after),
            "addonPath": str(self.addon_path),
            "carriers": list(self.carriers),
            "entriesToAdd": list(self.entries),
            "directLoadConflicts": list(self.direct_loads),
        }


def make_plan(ini: Path, api: str) -> Plan:
    ini = Path(os.path.abspath(ini))
    data, text, encoding = read_ini(ini)
    values = addon_values(text)
    directory = addon_directory(ini, values)
    carriers = discover_carriers(directory)
    direct = direct_load_conflicts(directory, values, carriers)
    existing = values.get("DisabledAddons", [])
    covered_files = {value.split("@", 1)[1] for value in existing if "@" in value}
    entries = ["@" + name for name in carriers if name not in covered_files]
    if api not in ACTIVE_APIS:
        status = "no-change-api"
        entries = []
    elif direct:
        status = "unsupported-direct-load"
        entries = []
    elif not carriers:
        status = "no-carriers"
    elif not entries:
        status = "already-excluded"
    else:
        status = "would-apply"
    after = append_exclusion(data, encoding, entries) if entries else data
    return Plan(ini, api, data, encoding, digest(data), directory, tuple(carriers),
                tuple(existing), tuple(entries), tuple(direct), after, status)


def exclusive_write(path: Path, data: bytes) -> None:
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    descriptor = os.open(path, flags, 0o600)
    try:
        with os.fdopen(descriptor, "wb", closefd=False) as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
    finally:
        os.close(descriptor)


def current_bytes(path: Path) -> bytes:
    regular_path(path, "ini")
    data = path.read_bytes()
    require(len(data) <= MAX_INI_BYTES, "ini-too-large", "INI exceeds the 4 MiB safety limit")
    return data


def atomic_replace(path: Path, data: bytes, expected_sha256: str) -> None:
    require(digest(current_bytes(path)) == expected_sha256, "concurrent-modification",
            "INI changed after planning; no overwrite performed")
    mode = stat.S_IMODE(path.stat().st_mode)
    descriptor, temporary_name = tempfile.mkstemp(
        prefix=f".{path.name}.carrier-exclusion.", suffix=".tmp", dir=path.parent)
    temporary = Path(temporary_name)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.chmod(temporary, mode)
        require(digest(current_bytes(path)) == expected_sha256, "concurrent-modification",
                "INI changed before atomic replace; no overwrite performed")
        os.replace(temporary, path)
    finally:
        if temporary.exists():
            temporary.unlink()


def apply_plan(plan: Plan) -> dict[str, object]:
    require(plan.status != "unsupported-direct-load", "unsupported-direct-load",
            "LoadFromDllMain can bypass DisabledAddons; explicit carrier loading was left unchanged")
    if plan.status != "would-apply":
        return plan.public("apply")
    require(digest(current_bytes(plan.ini)) == plan.before_sha256, "concurrent-modification",
            "INI changed after planning; no backup or overwrite performed")
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    token = secrets.token_hex(4)
    evidence_prefix = f"{plan.ini.name}.carrier-exclusion.{stamp}.{token}"
    backup = plan.ini.parent / (evidence_prefix + ".bak")
    receipt = plan.ini.parent / (evidence_prefix + ".receipt.json")
    exclusive_write(backup, plan.original)
    receipt_data = {
        "schema": 1,
        "operation": "carrier-exclusion",
        "ini": plan.ini.name,
        "api": plan.api,
        "backup": backup.name,
        "beforeSha256": plan.before_sha256,
        "afterSha256": digest(plan.after),
        "entries": list(plan.entries),
    }
    exclusive_write(receipt, (json.dumps(receipt_data, ensure_ascii=False, indent=2) + "\n").encode("utf-8"))
    atomic_replace(plan.ini, plan.after, plan.before_sha256)
    result = plan.public("apply")
    result.update({"status": "applied", "backup": backup.name, "receipt": receipt.name})
    return result


def unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        require(key not in result, "invalid-receipt", "receipt contains duplicate JSON keys")
        result[key] = value
    return result


def restore(ini: Path, receipt_name: str) -> dict[str, object]:
    ini = Path(os.path.abspath(ini))
    regular_path(ini, "ini")
    require(Path(receipt_name).name == receipt_name and receipt_name not in ("", ".", ".."),
            "invalid-receipt-name", "--restore must be a receipt basename in the INI directory")
    prefix = re.escape(ini.name) + r"\.carrier-exclusion\.\d{8}T\d{6}Z\.[0-9a-f]{8}\.receipt\.json"
    require(re.fullmatch(prefix, receipt_name) is not None, "invalid-receipt-name",
            "receipt basename does not belong to this INI")
    receipt_path = ini.parent / receipt_name
    regular_path(receipt_path, "receipt")
    try:
        info = json.loads(receipt_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise CarrierConfigError("invalid-receipt", "receipt is not valid UTF-8 JSON") from exc
    require(isinstance(info, dict), "invalid-receipt", "receipt root must be a JSON object")
    require(info.get("schema") == 1 and info.get("operation") == "carrier-exclusion" and
            info.get("ini") == ini.name, "invalid-receipt", "receipt identity mismatch")
    before = info.get("beforeSha256")
    after = info.get("afterSha256")
    backup_name = info.get("backup")
    require(isinstance(before, str) and HASH.fullmatch(before) is not None and
            isinstance(after, str) and HASH.fullmatch(after) is not None and
            isinstance(backup_name, str), "invalid-receipt", "receipt hash or backup field is invalid")
    require(Path(backup_name).name == backup_name and
            backup_name == receipt_name.removesuffix(".receipt.json") + ".bak",
            "invalid-backup-name", "backup must be the paired basename in the INI directory")
    backup = ini.parent / backup_name
    regular_path(backup, "backup")
    backup_data = backup.read_bytes()
    require(digest(backup_data) == before, "backup-hash-mismatch", "backup no longer matches receipt")
    require(digest(current_bytes(ini)) == after, "current-hash-mismatch",
            "current INI differs from the applied receipt; restore refused")
    atomic_replace(ini, backup_data, after)
    return {
        "schema": 1,
        "mode": "restore",
        "status": "restored",
        "ini": str(ini),
        "receipt": receipt_name,
        "backup": backup_name,
        "beforeSha256": before,
        "afterSha256": after,
        "restoredSha256": digest(backup_data),
    }


def emit(value: dict[str, object]) -> None:
    print(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True))


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ini", required=True, type=Path)
    parser.add_argument("--api", choices=("d3d12", "vulkan", "d3d11", "unknown"), default="unknown")
    parser.add_argument("--apply", action="store_true", help="write the planned exclusion")
    parser.add_argument("--restore", metavar="RECEIPT", help="restore from a same-directory receipt basename")
    arguments = parser.parse_args(argv)
    if arguments.apply and arguments.restore:
        parser.error("--apply and --restore are mutually exclusive")
    try:
        if arguments.restore:
            emit(restore(arguments.ini, arguments.restore))
            return 0
        plan = make_plan(arguments.ini, arguments.api)
        if arguments.apply:
            emit(apply_plan(plan))
            return 0
        emit(plan.public())
        return 0
    except (CarrierConfigError, OSError) as exc:
        code = exc.code if isinstance(exc, CarrierConfigError) else "filesystem-error"
        emit({"schema": 1, "mode": "restore" if arguments.restore else "apply" if arguments.apply else "preview",
              "status": "error", "error": code, "message": str(exc)})
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
