"""Run the shipped offline collector in Windows PowerShell against bounded fixtures."""
import ctypes
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile


root = Path(__file__).resolve().parents[1]
collector = root / "scripts/collect-feedback.ps1"
crash_collector = root / "scripts/collect-crash-feedback.ps1"
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


def collect_crash(source, output, extra_args=()):
    command = [
        "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
        str(crash_collector), "-NoPause", "-SourceDirectory", str(source),
        "-OutputRoot", str(output), *map(str, extra_args)
    ]
    result = subprocess.run(command, capture_output=True, timeout=60)
    check(result.returncode == 0, "crash collector exits successfully: " +
          result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))
    packages = list(output.glob("*/crash-feedback-offline.zip"))
    check(len(packages) == 1, "one unique crash-feedback package")
    return zipfile.ZipFile(packages[0])


EVENT_NS = "http://schemas.microsoft.com/win/2004/08/events/event"


def write_events(path, events):
    ET.register_namespace("", EVENT_NS)
    root_node = ET.Element("{%s}Events" % EVENT_NS)
    for provider, event_id, record_id, time_utc, data in events:
        event = ET.SubElement(root_node, "{%s}Event" % EVENT_NS)
        system = ET.SubElement(event, "{%s}System" % EVENT_NS)
        ET.SubElement(system, "{%s}Provider" % EVENT_NS, Name=provider)
        ET.SubElement(system, "{%s}EventID" % EVENT_NS).text = str(event_id)
        ET.SubElement(system, "{%s}TimeCreated" % EVENT_NS, SystemTime=time_utc)
        ET.SubElement(system, "{%s}EventRecordID" % EVENT_NS).text = str(record_id)
        event_data = ET.SubElement(event, "{%s}EventData" % EVENT_NS)
        for name, value in data.items():
            ET.SubElement(event_data, "{%s}Data" % EVENT_NS, Name=name).text = value
    ET.ElementTree(root_node).write(path, encoding="utf-8", xml_declaration=True)


def reshade_initialization(executable):
    return ("02:59:31:926 [31432] | INFO  | Initializing crosire's ReShade version "
            "'6.8.0.2155' (64-bit) loaded from 'C:\\Games\\Test\\dxgi.dll' into "
            f"'{executable}' (0xCEC37752) ...\n")


with tempfile.TemporaryDirectory(prefix="dlss5-offline-fixture-") as temporary:
    fixture = Path(temporary)

    # Regression from the real FH6 beta0.4.1 feedback: the original package had
    # a usable ReShade process identity but no exception/event/dump evidence.
    fh6_fixture = root / "tests/fixtures/fh6-missing-crash-evidence"
    fh6_source = fixture / "FH6 missing evidence"
    fh6_source.mkdir()
    (fh6_source / "ReShade.log").write_bytes((fh6_fixture / "ReShade.log").read_bytes())
    expected = json.loads((fh6_fixture / "expected.json").read_text(encoding="utf-8"))
    with collect_crash(fh6_source, fixture / "fh6-output", (
            "-EventLogXmlPath", fh6_fixture / "empty-events.xml",
            "-WerRoots", fixture / "empty-wer", "-DumpRoots", fixture / "empty-dumps")) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        crash = manifest["crash_evidence"]
        check(crash["target_executable"] == expected["target_executable"],
              "FH6 target executable derived exactly from ReShade initialization")
        check(crash["status"] == expected["expected_collection_status"],
              "FH6 missing crash evidence remains explicit rather than looking complete")

    now = datetime.now(timezone.utc)
    fresh = now.isoformat().replace("+00:00", "Z")
    old = (now - timedelta(days=4)).isoformat().replace("+00:00", "Z")
    crash_source = fixture / "crash evidence exact matching"
    crash_source.mkdir()
    target_path = r"C:\Games\Forza Horizon 6\forzahorizon6.exe"
    (crash_source / "ReShade.log").write_text(reshade_initialization(target_path), encoding="utf-8")
    events_xml = fixture / "application-events.xml"
    write_events(events_xml, [
        ("Application Error", 1000, 101, fresh, {
            "AppName": "forzahorizon6.exe", "ModuleName": "nvwgf2umx.dll",
            "ExceptionCode": "c0000005", "AppPath": target_path,
        }),
        ("Windows Error Reporting", 1001, 102, fresh, {
            "EventName": "APPCRASH", "P1": "forzahorizon6.exe", "P5": "c0000005",
            "ReportId": "11111111-2222-3333-4444-555555555555",
        }),
        ("Application Error", 1000, 103, fresh, {
            "AppName": "forzahorizon6helper.exe", "ExceptionCode": "BAD-SUBSTRING-MATCH",
        }),
        ("Unrelated Provider", 1000, 104, fresh, {
            "AppName": "forzahorizon6.exe", "ExceptionCode": "BAD-PROVIDER-MATCH",
        }),
        ("Application Error", 1000, 105, old, {
            "AppName": "forzahorizon6.exe", "ExceptionCode": "BAD-OLD-MATCH",
        }),
    ])
    wer_root = fixture / "WER root"
    matched_report = wer_root / "AppCrash_forzahorizon6_exact"
    unmatched_report = wer_root / "AppCrash_forzahorizon6helper"
    nested_report = matched_report / "nested-must-not-be-read"
    for path in (matched_report, unmatched_report, nested_report):
        path.mkdir(parents=True, exist_ok=True)
    matched_wer = matched_report / "Report.wer"
    matched_wer.write_text(
        "EventType=APPCRASH\nAppName=forzahorizon6.exe\n"
        f"AppPath={target_path}\nReportIdentifier=exact-report\n", encoding="utf-16")
    (matched_report / "triage.dmp").write_bytes(b"MATCHED-WER-DUMP")
    (unmatched_report / "Report.wer").write_text(
        "EventType=APPCRASH\nAppName=forzahorizon6helper.exe\n"
        "ReportIdentifier=must-not-collect\n", encoding="utf-16")
    (unmatched_report / "unrelated.dmp").write_bytes(b"UNRELATED-WER-DUMP")
    (nested_report / "Report.wer").write_text(
        "AppName=forzahorizon6.exe\nReportIdentifier=too-deep\n", encoding="utf-16")
    dump_root = fixture / "CrashDumps"
    dump_root.mkdir()
    (dump_root / "forzahorizon6.exe.4242.dmp").write_bytes(b"EXACT-LOCAL-DUMP")
    (dump_root / "forzahorizon6helper.exe.4242.dmp").write_bytes(b"BAD-SUBSTRING-DUMP")
    old_dump = dump_root / "forzahorizon6.exe.3131.dmp"
    old_dump.write_bytes(b"BAD-OLD-DUMP")
    old_timestamp = (now - timedelta(days=4)).timestamp()
    os.utime(old_dump, (old_timestamp, old_timestamp))
    with collect_crash(crash_source, fixture / "crash-output", (
            "-EventLogXmlPath", events_xml, "-WerRoots", wer_root, "-DumpRoots", dump_root)) as archive:
        manifest = json.loads(archive.read("manifest.json"))
        crash = manifest["crash_evidence"]
        check(manifest["package_kind"] == "crash-feedback-offline", "crash package identity")
        check(crash["target_status"] == "identified" and
              crash["target_executable"].lower() == "forzahorizon6.exe", "strict target identity")
        check(crash["status"] == "matching-crash-evidence-collected", "matching evidence status")
        check(crash["matches"] == {
            "event_summaries": 2, "wer_reports_found": 1, "wer_reports_collected": 1,
            "dumps_found": 2, "dumps_collected": 2,
        }, "only exact and recent event/WER/dump evidence counted")
        summaries = json.loads(archive.read("crash/events.json"))
        check({event["event_id"] for event in summaries} == {1000, 1001},
              "Application 1000 and WER 1001 summaries")
        check(any(event["data"].get("ExceptionCode") == "c0000005" for event in summaries),
              "exception code retained in bounded summary")
        evidence_records = [record for record in crash["files"] if record["status"] == "collected"]
        payload = b"".join(archive.read(record["entry"]) for record in evidence_records)
        check(b"MATCHED-WER-DUMP" in payload and b"EXACT-LOCAL-DUMP" in payload,
              "matched existing dumps copied")
        check(b"UNRELATED-WER-DUMP" not in payload and b"BAD-SUBSTRING-DUMP" not in payload and
              b"BAD-OLD-DUMP" not in payload, "substring, unrelated and old dumps rejected")
        check(b"too-deep" not in payload, "WER traversal remains one level deep")
        check(not crash["registry_modified"] and not crash["dump_creation_performed"] and
              not crash["upload_performed"], "crash mode remains local and read-only")
        check(manifest["dumps_collected"], "base manifest truthfully reports included dump")

    ambiguous_source = fixture / "ambiguous ReShade target"
    ambiguous_source.mkdir()
    (ambiguous_source / "ReShade.log").write_text(
        reshade_initialization(target_path) +
        reshade_initialization(r"C:\Games\Forza Horizon 6\launcher.exe"), encoding="utf-8")
    with collect_crash(ambiguous_source, fixture / "ambiguous-output", (
            "-EventLogXmlPath", events_xml, "-WerRoots", wer_root, "-DumpRoots", dump_root)) as archive:
        crash = json.loads(archive.read("crash-evidence.json"))
        check(crash["status"] == "target-ambiguous" and crash["target_executable"] == "",
              "ambiguous ReShade process identity fails closed")
        check(crash["matches"] == {"event_summaries": 0, "wer_reports_found": 0,
                                   "wer_reports_collected": 0, "dumps_found": 0,
                                   "dumps_collected": 0},
              "ambiguous target never queries or collects crash evidence")

    oversized_source = fixture / "oversized dump target"
    oversized_source.mkdir()
    (oversized_source / "ReShade.log").write_text(reshade_initialization(target_path), encoding="utf-8")
    oversized_dump_root = fixture / "oversized CrashDumps"
    oversized_dump_root.mkdir()
    oversized_dump = oversized_dump_root / "forzahorizon6.exe.9999.dmp"
    with oversized_dump.open("wb") as handle:
        import msvcrt
        returned = ctypes.c_ulong()
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.DeviceIoControl.argtypes = [ctypes.c_void_p, ctypes.c_ulong, ctypes.c_void_p,
                                          ctypes.c_ulong, ctypes.c_void_p, ctypes.c_ulong,
                                          ctypes.POINTER(ctypes.c_ulong), ctypes.c_void_p]
        check(kernel.DeviceIoControl(msvcrt.get_osfhandle(handle.fileno()), 0x900C4,
                                     None, 0, None, 0, ctypes.byref(returned), None) != 0,
              "sparse dump fixture creation")
        handle.truncate(192 * 1024 * 1024 + 1)
    empty_wer = fixture / "empty WER root"
    empty_wer.mkdir()
    with collect_crash(oversized_source, fixture / "oversized-crash-output", (
            "-EventLogXmlPath", fh6_fixture / "empty-events.xml",
            "-WerRoots", empty_wer, "-DumpRoots", oversized_dump_root)) as archive:
        crash = json.loads(archive.read("crash-evidence.json"))
        check(crash["matches"]["dumps_found"] == 1 and crash["matches"]["dumps_collected"] == 0,
              "oversized matching dump discovered but not copied")
        check(any(record["status"] == "skipped-individual-byte-cap" for record in crash["files"]),
              "oversized dump cap is explicit")
        check(not any(name.lower().endswith(".dmp") for name in archive.namelist()),
              "oversized dump is absent from package")

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
