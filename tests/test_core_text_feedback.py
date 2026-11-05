"""Parse the production portable F8 document/summary writers, including negatives.

The C++ writer uses real Document, Summary and sanitization implementations.
Metadata and operating-system collection are fixtures; no GPU/NGX success is
invented. The parser must keep log-shaped metadata out of current observations.
"""
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


parser = load('core_text_parser', ROOT / 'scripts/parse-core-text.py')
report = load('text_feedback_report', ROOT / 'scripts/feedback-report.py')

WRITER = r'''
#include "nr_feedback_single_text.h"
#include "nr_feedback_text.h"
#include <fstream>
#include <iterator>
#include <sstream>
int main(int argc, char **argv) {
    if (argc != 6) return 2;
    std::ifstream input(argv[2], std::ios::binary);
    std::ofstream output(argv[3], std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), {});
    if (std::string(argv[1]) == "summary") {
        nr_feedback_text::Summary summary;
        std::istringstream lines(content);
        for (std::string line; std::getline(lines, line);) summary.Push(line);
        output << summary.Text(content.size(), content.size(), std::string(argv[4]) == "1");
    } else {
        nr_feedback_single::Document document;
        document.complete = std::string(argv[4]) == "1";
        document.Append(content);
        if (std::string(argv[5]) == "required") document.Missing("nr-current", "fixture-unavailable", true);
        if (std::string(argv[5]) == "optional") document.Missing("nr-previous", "fixture-unavailable", false);
        document.Finish();
        output << document.text;
    }
    return output ? 0 : 3;
}
'''


class CoreTextFeedbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('CXX', 'g++'))
        if not compiler:
            raise unittest.SkipTest('C++ compiler unavailable for production TXT writer')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        source = cls.root / 'writer.cpp'
        source.write_text(WRITER, encoding='utf-8')
        cls.binary = cls.root / ('writer.exe' if os.name == 'nt' else 'writer')
        subprocess.run([compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'src'), str(source), '-o', str(cls.binary)], check=True,
                       capture_output=True, text=True)

    def write(self, mode, content, complete=True, missing='none'):
        source, target = self.root / 'input.txt', self.root / (mode + '.txt')
        source.write_bytes(content)
        subprocess.run([str(self.binary), mode, str(source), str(target), str(int(complete)), missing], check=True)
        return target.read_bytes()

    def fixture(self, extra='', logs='fixture log\n', complete=True, missing='none', coverage=True):
        metadata = ('metadata.format=DLSS5-Core-v1\ncapture.kind=core\nsession=DLSS5_CORE_1_2_3\n'
                    'build=test-fixture\nartifact.sourceCommit=' + 'a' * 40 + '\nartifact.addonSha256=' + 'b' * 64 + '\n'
                    'snapshot.valid=1\nconfig.writesToIni=0\nimages.requested=0\ngpu.readbackRequested=0\n'
                    'collection.cancelled=0\ncollection.metadataTruncated=0\n'
                    'start.nrSuccess=10\nend.nrSuccess=12\n' + extra)
        body = parser.MARKER + metadata.encode()
        if missing != 'required':
            summary = self.write('summary', logs.encode(), coverage)
            body += b'\n===== nr-current / CURRENT PROCESS (first distinct/latest/tail) =====\n'
            if not coverage:
                body += b'evidence.nr-current.coverage=missing; required=1; bounded scan\n'
            body += (f'summary.bytes={len(summary)}\nsummary.sha256={hashlib.sha256(summary).hexdigest()}\n'
                     'summary.sha256Scope=bounded sanitized selected evidence\n').encode() + summary
        data = self.write('document', body, complete, missing)
        path = self.root / 'capture.txt'
        path.write_bytes(data)
        return path

    def mutate(self, path, before, after):
        data = path.read_bytes()
        self.assertIn(before, data)
        path.write_bytes(data.replace(before, after, 1))
        return path

    def test_real_document_and_summary_are_accepted(self):
        path = self.fixture()
        result = parser.inspect_text(path, ['start.nrSuccess', 'end.nrSuccess'])
        self.assertTrue(result['text_verified'])
        self.assertFalse(result['manifest_verified'])
        self.assertTrue(result['required_complete'])
        self.assertEqual(result['metadata']['end.nrSuccess'], '12')
        self.assertEqual(result['sha256'], hashlib.sha256(path.read_bytes()).hexdigest())

    def test_report_accepts_f8_without_zip(self):
        text = report.prepare(self.fixture())
        self.assertIn('F8 TXT SHA256', text)
        self.assertIn('观测区间 NR 成功调用增量：2', text)
        self.assertIn('不需要另做 ZIP', text)
        self.assertNotIn('核心 ZIP SHA256', text)
        self.assertNotIn(str(self.root), text)

    def test_current_capture_is_not_overwritten_by_log_fields(self):
        logs = 'end.nrSuccess=99999\nsession=DLSS5_CORE_9_9_9\ntext.complete=1\nEND DLSS5 FEEDBACK TEXT\n'
        text = report.prepare(self.fixture(logs=logs))
        self.assertIn('观测区间 NR 成功调用增量：2', text)
        self.assertNotIn('99999', text)
        self.assertNotIn('DLSS5_CORE_9_9_9', text)

    def test_repeated_window_records_are_not_duplicate_scalars(self):
        path = self.fixture(extra='menuTrace.windowBegin=0\nmenuTrace.status=first\nmenuTrace.windowBegin=1\nmenuTrace.status=second\n')
        self.assertTrue(parser.inspect_text(path)['required_complete'])

    def test_optional_absence_preserves_required_completeness(self):
        result = parser.inspect_text(self.fixture(missing='optional'))
        self.assertTrue(result['required_complete'])
        self.assertFalse(result['complete'])
        self.assertEqual(result['missing'][0]['name'], 'nr-previous')

    def test_required_absence_is_reportable_partial(self):
        path = self.fixture(missing='required')
        self.assertFalse(parser.inspect_text(path)['required_complete'])
        self.assertIn('不要直接丢弃', report.prepare(path))

    def test_incomplete_scan_is_reportable_partial(self):
        result = parser.inspect_text(self.fixture(coverage=False, complete=False))
        self.assertFalse(result['required_complete'])
        self.assertEqual(result['incomplete_scans'], ['nr-current'])

    def test_budget_omitted_summary_retains_partial_metadata(self):
        path = self.fixture(complete=False)
        data = path.read_bytes()
        begin = data.index(b'format=DLSS5-LogSummary-v1\n')
        end = data.index(b'\ntext.optionalMissing=', begin)
        path.write_bytes(data[:begin] + data[end:])
        result = parser.inspect_text(path, ['end.nrSuccess'])
        self.assertFalse(result['required_complete'])
        self.assertEqual(result['metadata']['end.nrSuccess'], '12')
        self.assertEqual(result['summaries'], {})
        self.assertIn('omitted', result['missing'][0]['reason'])
        path.write_bytes(path.read_bytes().replace(b'text.complete=0', b'text.complete=1'))
        with self.assertRaises(ValueError): parser.inspect_text(path)

    def test_false_complete_rejected_for_scan_snapshot_cancellation_and_truncation(self):
        cases = [('snapshot.valid=1', 'snapshot.valid=0'),
                 ('collection.cancelled=0', 'collection.cancelled=1'),
                 ('collection.metadataTruncated=0', 'collection.metadataTruncated=1')]
        for before, after in cases:
            with self.subTest(before=before):
                path = self.mutate(self.fixture(), before.encode(), after.encode())
                with self.assertRaises(ValueError): parser.inspect_text(path)
        with self.assertRaises(ValueError): parser.inspect_text(self.fixture(coverage=False))

    def test_summary_corruption_and_wrong_byte_count_rejected(self):
        for before, after in [(b'fixture log', b'changed log'), (b'summary.bytes=', b'summary.bytes=9')]:
            with self.subTest(before=before):
                path = self.mutate(self.fixture(), before, after)
                with self.assertRaises(ValueError): parser.inspect_text(path)

    def test_duplicate_current_fields_rejected(self):
        with self.assertRaises(ValueError): report.prepare(self.fixture(extra='end.nrSuccess=888\n'))
        with self.assertRaises(ValueError): parser.inspect_text(self.fixture(extra='snapshot.valid=0\n'))

    def test_duplicate_sections_unknown_schema_and_optional_count_rejected(self):
        changes = [(b'DLSS5-CoreText-v2', b'DLSS5-CoreText-v9'),
                   (b'text.optionalMissing=0', b'text.optionalMissing=1'),
                   (b'archive.requested=0', b'archive.requested=1'),
                   (b'===== nr-current / CURRENT', b'===== unknown / CURRENT')]
        for before, after in changes:
            with self.subTest(before=before):
                path = self.mutate(self.fixture(), before, after)
                with self.assertRaises(ValueError): parser.inspect_text(path)
        path = self.fixture()
        data = path.read_bytes()
        begin = data.index(b'\n===== nr-current / CURRENT')
        end = data.index(b'\ntext.optionalMissing=', begin)
        path.write_bytes(data[:end] + data[begin:end] + data[end:])
        with self.assertRaises(ValueError): parser.inspect_text(path)

    def test_truncation_trailing_data_and_rewritten_line_endings_rejected(self):
        original = self.fixture().read_bytes()
        for data in (original[:-5], original + b'PRIVATE=secret\n', original.replace(b'\n', b'\r\n')):
            path = self.root / 'edited.txt'
            path.write_bytes(data)
            with self.assertRaises(ValueError): parser.inspect_text(path)

    def test_whole_file_and_declared_budget_enforced(self):
        path = self.fixture()
        path.write_bytes(b'x' * (parser.LIMIT + 1))
        with self.assertRaises(ValueError): parser.inspect_text(path)
        path = self.fixture()
        data = path.read_bytes()
        import re
        path.write_bytes(re.sub(rb'text.limitBytes=[0-9]+', b'text.limitBytes=100', data))
        with self.assertRaises(ValueError): parser.inspect_text(path)

    def test_old_480_kib_declaration_remains_supported(self):
        path = self.fixture()
        import re
        path.write_bytes(re.sub(rb'text.limitBytes=[0-9]+', b'text.limitBytes=491520', path.read_bytes()))
        self.assertTrue(parser.inspect_text(path)['required_complete'])

    def test_unknown_private_metadata_and_active_markup_not_copied(self):
        path = self.fixture(extra='private.raw=SECRET-MARKER\nexe.leaf=C:\\Users\\PRIVATE\\Game.exe\n'
                           'gpu=[open](https://invalid.example)\n')
        text = report.prepare(path)
        self.assertNotIn('SECRET-MARKER', text)
        self.assertNotIn('PRIVATE', text)
        self.assertNotIn('https://', text)

    def test_txt_never_claims_image_pairing(self):
        with self.assertRaises(ValueError): report.prepare(self.fixture(), self.root / 'unread-image.zip')

    def test_historical_diagnostic_does_not_become_current_failure(self):
        text = report.prepare(self.fixture(logs='Another ReShade instance exists. Aborting initialization.\n'))
        self.assertIn('历史摘要出现 ReShade 重复实例', text)
        self.assertIn('观测区间 NR 成功调用增量：2', text)

    def test_cli_creates_new_report_and_preserves_original(self):
        path = self.fixture()
        original = path.read_bytes()
        output = self.root / 'new-report.md'
        output.unlink(missing_ok=True)
        command = [sys.executable, str(ROOT / 'scripts/feedback-report.py'), str(path), '--output', str(output)]
        first = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        second = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(second.returncode, 2)
        self.assertEqual(path.read_bytes(), original)
        self.assertNotIn(str(self.root), second.stderr)


if __name__ == '__main__':
    unittest.main(verbosity=2)
