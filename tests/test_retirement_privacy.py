"""Actual retirement formatter rows through the production privacy policy."""
from pathlib import Path
import hashlib
import json
import os
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def formatter_rows():
    source = (ROOT / 'src/nr_retirement_feedback.inl').read_text(encoding='utf-8-sig')
    rows = []
    for literal in re.findall(r'"([^"\n]*)"', source):
        if not re.match(r'%s\.retirement\.(sequence=|context\.%u[.=]|recording\.%u=|binding\.%u=|slot\.%u=|%s\.%u=)', literal):
            continue
        strings = iter(('end', 'privateFlight' if '.%s.' in literal else 'context-recording'))
        # Full-width semantic IDs deliberately look like pointer-sized numbers.
        # The schema and field order come from the actual runtime formatter.
        row = re.sub(r'%(?:llu|llX|u|d|s)', lambda m: {
            '%llu': '123456789012345', '%llX': '0000ABCDEF123456',
            '%u': '42', '%d': '0', '%s': None,
        }[m[0]] if m[0] != '%s' else next(strings), literal)
        row = row.removesuffix(r'\n')
        row = re.sub(r'(end\.retirement\.[A-Za-z]+\.)42(?=[.=])', r'\g<1>0', row)
        rows.append(row)
        if '.privateFlight.' in row:
            rows.append(row.replace('.privateFlight.', '.temporalFlight.'))
    if len(rows) != 8:
        raise AssertionError(f'formatter rows changed: expected 8, got {len(rows)}')
    return rows


HARNESS = r'''
#include "nr_feedback_text.h"
#include <iostream>
int checks=0;
void Check(bool ok,const char* label) {
    ++checks;
    if(!ok){std::cerr<<"FAIL "<<label<<'\n';std::exit(3);}
}
int main(){
    using namespace nr_feedback_text;
    const std::string rows[]={ROWS};
    for(const auto& row:rows){
        auto expected=row;
        for(auto at=expected.find("0x0000ABCDEF123456");at!=std::string::npos;
            at=expected.find("0x0000ABCDEF123456"))expected.replace(at,18,"[address]");
        Check(SanitizeMetadata(row)==expected,"formatter semantic IDs survive while pointers are redacted");
        Check(Sanitize(row).find("123456789012345")==std::string::npos,"raw logs keep pointer-sized-number redaction");
    }
    const auto sequence=rows[0];
    Check(sequence.find(".sequence=")!=std::string::npos,"fixture starts with actual sequence format");
    auto invalid=sequence;
    invalid.replace(0,3,"start");
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"start schema is not whitelisted");
    invalid=sequence+",extra:123456789012345";
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"unknown field invalidates full row");
    invalid=sequence+",tick:123456789012345";
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"duplicate field invalidates full row");
    invalid=sequence.substr(0,sequence.rfind(','));
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"truncated row is not whitelisted");
    invalid=sequence;
    invalid.replace(invalid.find('=')+1,15,"012345678901234");
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"noncanonical numeric field invalidates whole row");
    for(const auto& row:rows){
        if(row.find(".sequence=")!=std::string::npos)continue;
        invalid=row;
        const auto at=invalid.find(".0");
        invalid.replace(at,2,".999");
        Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"out-of-range record index has no exemption");
    }
    invalid=sequence;
    invalid.replace(invalid.find('=')+1,15,"18446744073709551616");
    const auto begin=invalid.find('=')+1;
    Check(!RetirementMetadataNumber(invalid,begin,invalid.find(',',begin)),"overflow is not a semantic counter");
    const std::string maximum="end.retirement.sequence=18446744073709551615,tick:123456789012345,observations:0,suppressed:0";
    Check(SanitizeMetadata(maximum)==maximum,"uint64 max keeps unknown sentinel and later semantic IDs");
    const auto context=rows[1];
    Check(context.find(",first:")!=std::string::npos,"fixture uses actual context format");
    invalid=context;
    invalid.replace(invalid.find(",sampled:0"),10,",sampled:2");
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"invalid boolean invalidates full row");
    invalid=context;
    invalid.replace(invalid.find("context-recording"),17,"arbitrary-kind");
    Check(SanitizeMetadata(invalid).find("123456789012345")==std::string::npos,"unknown first blocker invalidates row");
    invalid=sequence+",path:C:\\Users\\SECRET-NAME\\Game";
    Check(SanitizeMetadata(invalid).find("SECRET-NAME")==std::string::npos,"paths stay private");
    Check(SanitizeMetadata(sequence+",password=PRIVATE")=="[sensitive log line omitted]","secret line stays private");
    std::cout<<checks<<" retirement privacy checks passed\n";
}
'''


class RetirementPrivacy(unittest.TestCase):
    def test_formatter_rows_and_old_policy_negative(self):
        compiler = os.environ.get('CXX') or shutil.which('clang++') or shutil.which('g++')
        if os.environ.get('NR_LLVM_BIN'):
            compiler = str(Path(os.environ['NR_LLVM_BIN']) / ('clang++.exe' if os.name == 'nt' else 'clang++'))
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        evidence = os.environ.get('DLSS5_PRIVACY_TEST_EVIDENCE_DIR')
        if evidence:
            folder = Path(evidence)
            folder.mkdir(parents=True, exist_ok=False)
            self.run_cases(compiler, folder)
        else:
            with tempfile.TemporaryDirectory(prefix='nr-retirement-privacy-') as directory:
                self.run_cases(compiler, Path(directory))

    def run_cases(self, compiler, folder):
        header = (ROOT / 'src/nr_feedback_text.h').read_text(encoding='utf-8-sig')
        marker = '            if (typed_metadata && RetirementMetadataNumber(line, i, end)) { i = end; continue; }'
        self.assertEqual(header.count(marker), 1)
        harness = HARNESS.replace('ROWS', ',\n'.join(json.dumps(row) for row in formatter_rows()))
        report = {'schema': 'nr-retirement-privacy-v1', 'status': 'running', 'gpu': False,
                  'header_sha256': hashlib.sha256(header.encode()).hexdigest(),
                  'formatter_sha256': hashlib.sha256((ROOT / 'src/nr_retirement_feedback.inl').read_bytes()).hexdigest(),
                  'runs': []}
        try:
            for name, text, expected in (('candidate', header, 0), ('old-policy', header.replace(marker, ''), 3)):
                case = folder / name
                case.mkdir()
                (case / 'nr_feedback_text.h').write_text(text, encoding='utf-8')
                (case / 'privacy.cpp').write_text(harness, encoding='utf-8')
                exe = case / ('privacy.exe' if os.name == 'nt' else 'privacy')
                flags = ['-std=c++20', '-O1', '-Wall', '-Wextra', '-Werror']
                if os.name == 'nt':
                    flags.append('-static')
                commands = ([compiler, *flags, '-I' + str(case), str(case / 'privacy.cpp'), '-o', str(exe)], [str(exe)])
                for index, command in enumerate(commands):
                    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=90)
                    (case / f'{index}.log').write_text(result.stdout + result.stderr, encoding='utf-8')
                    report['runs'].append({'name': name, 'step': index, 'command': command, 'exit': result.returncode})
                    self.assertEqual(result.returncode, expected if index else 0, result.stdout + result.stderr)
                    if index and expected:
                        self.assertIn('FAIL formatter semantic IDs survive while pointers are redacted', result.stderr)
                    elif index:
                        print(result.stdout.strip(), flush=True)
            report['status'] = 'passed'
        finally:
            (folder / 'validation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')


if __name__ == '__main__':
    unittest.main()
