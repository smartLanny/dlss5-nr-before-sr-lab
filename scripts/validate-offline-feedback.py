"""Run the shipped offline collector in Windows PowerShell against bounded fixtures."""
import ctypes
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile


root = Path(__file__).resolve().parents[1]
collector = root / "scripts/collect-feedback.ps1"
checks = 0


def check(condition, label):
    global checks
    checks += 1
    if not condition:
        raise AssertionError(label)


def collect(source, output, unavailable_gpu=False):
    command = [
        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        str(collector), "-NoPause", "-SourceDirectory", str(source), "-OutputRoot", str(output)
    ]
    if unavailable_gpu:
        quote = lambda value: "'" + str(value).replace("'", "''") + "'"
        command = ["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                   "function Get-CimInstance { throw 'fixture GPU provider unavailable' }; & " + quote(collector) +
                   " -NoPause -SourceDirectory " + quote(source) + " -OutputRoot " + quote(output)]
    result = subprocess.run(command, capture_output=True, timeout=60)
    check(result.returncode == 0, "collector exits successfully: " + result.stdout.decode(errors="replace"))
    packages = list(output.glob("*/feedback-offline.zip"))
    check(len(packages) == 1, "one unique destination package")
    return zipfile.ZipFile(packages[0])


with tempfile.TemporaryDirectory(prefix="dlss5-offline-fixture-") as temporary:
    fixture = Path(temporary)
    source = fixture / "游戏 中文目录" / "bin" / "插件 空格"
    source.mkdir(parents=True)
    log = b"old-prefix" * 103 + bytes(range(256)) * 32768 + b"LAST-FRAME-MARKER\n"
    config = b"[config]\n" + b"x" * (1024 * 1024)
    inputs = {
        source / "nr-before-sr.log": log,
        source / "nr-before-sr.previous.log": b"previous run\n",
        source / "ReShade.log": b"ReShade current\n",
        source / "nr_before_sr.ini": config,
        source / "ReShade.ini": b"[GENERAL]\nValue=unchanged\n",
        source.parent / "ReShade.log": b"parent log\n",
        source / "_storage_" / "addons" / "插件 一个.addon64": b"stable-addon-binary\x00\xfe",
        source / "bridge.dll": b"stable-bridge-binary",
        source / "unlisted" / "nr-before-sr.log": b"OUTSIDE-ALLOWLIST",
        source / "_storage_" / "addons" / "third" / "ReShade.log": b"TOO-DEEP",
    }
    for path, data in inputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    with collect(source, fixture / "输出 空格") as archive:
        manifest = json.loads(archive.read("manifest.json"))
        binaries = json.loads(archive.read("binaries.json"))
        check(manifest["status"] == "partial", "capped inputs clearly mark partial package")
        entries = {Path(record["path"]): record for record in manifest["files"] if "entry" in record}
        record = entries[source / "nr-before-sr.log"]
        check(record["status"] == "capped-tail" and archive.read(record["entry"]) == log[-8 * 1024 * 1024:],
              "exact last 8 MiB, including final frame")
        record = entries[source / "nr_before_sr.ini"]
        check(record["status"] == "capped-prefix" and archive.read(record["entry"]) == config[:1024 * 1024],
              "config capped at exactly 1 MiB")
        check(entries[source.parent / "ReShade.log"]["entry"] != entries[source / "ReShade.log"]["entry"],
              "same filenames in two directories remain distinct")
        check(all("OUTSIDE-ALLOWLIST" not in archive.read(name).decode(errors="ignore") and
                  "TOO-DEEP" not in archive.read(name).decode(errors="ignore")
                  for name in archive.namelist() if name.startswith("files/")), "fixed directory boundary")
        check(len(binaries) == 2, "only DLL/addon metadata included")
        for binary in binaries:
            check(binary["sha256"] == hashlib.sha256(Path(binary["path"]).read_bytes()).hexdigest().upper(),
                  "binary hash matches real file")
        check(not any(name.lower().endswith((".dll", ".addon64", ".dmp")) for name in archive.namelist()),
              "no binaries or dumps copied")
        check(not manifest["upload_performed"] and not manifest["configuration_modified"], "local read-only manifest")
        check("gpus" in json.loads(archive.read("system.json")), "GPU query result or explicit unavailability")
    for path, data in inputs.items():
        check(path.read_bytes() == data, "source untouched: " + path.name)

    missing = fixture / "missing game" / "plugins"
    missing.mkdir(parents=True)
    with collect(missing, fixture / "missing-output", unavailable_gpu=True) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        check(manifest["status"] == "partial", "missing logs still produce partial ZIP")
        check(any(r["status"] == "missing-in-all-allowed-directories" and r["path"] == "nr-before-sr.log"
                  for r in manifest["files"]), "missing current NR log is explicit")
        system = json.loads(archive.read("system.json"))
        check(system["gpu_status"] == "unavailable" and "provider unavailable" in system["gpu_error"],
              "unavailable GPU provider still yields partial ZIP")

    capped = fixture / "capped game" / "plugins"
    capped.mkdir(parents=True)
    # A sparse file exercises the 1 GiB preflight without writing/hashing a GiB.
    sparse = capped / "oversized.dll"
    with sparse.open("wb") as handle:
        import msvcrt
        returned = ctypes.c_ulong()
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p,
                                          ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong,
                                          ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
        check(kernel.DeviceIoControl(msvcrt.get_osfhandle(handle.fileno()), 0x900C4,
                                     None, 0, None, 0, ctypes.byref(returned), None) != 0,
              "sparse fixture creation")
        handle.truncate(1024 * 1024 * 1024 + 1)
    with collect(capped, fixture / "oversized-output") as archive:
        manifest = json.loads(archive.read("manifest.json"))
        check(manifest["hash_bytes"] == 0, "oversized binary rejected before hashing")
        check(any(record["status"] == "skipped-total-hash-byte-cap" for record in manifest["files"]),
              "total hash byte cap reported")
    for index in range(40):
        (capped / ("binary%02d.dll" % index)).write_bytes(bytes([index]) * 17)
    with collect(capped, fixture / "capped-output") as archive:
        manifest = json.loads(archive.read("manifest.json"))
        binaries = json.loads(archive.read("binaries.json"))
        check(len(binaries) == 32 and manifest["binary_count"] == 32, "binary count capped at 32")
        check(manifest["hash_bytes"] == 32 * 17, "oversized binary never read for hashing")
        statuses = {record["status"] for record in manifest["files"]}
        check("capped-binary-count" in statuses, "binary count cap reported")

print("Offline feedback: %d checks, 0 failures (Windows PowerShell, Unicode paths, caps, hashes, partial ZIP)." % checks)
