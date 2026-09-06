"""Compile the production TXT archive-status expression with independent cases."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OLD = 'text += std::string("zip.status=") + (zip.status == nr_bundle::Status::Failed ? "failed" : zip.required_complete && capture_complete ? "complete" : "partial") + "\\n";'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--negative-control', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'src/nr_feedback_report.inl').read_text(encoding='utf-8')
    lines = [line.strip() for line in source.splitlines() if 'text += std::string("zip.status=")' in line]
    if len(lines) != 1:
        raise RuntimeError('Expected exactly one production archive-status expression')
    expression = OLD if args.negative_control else lines[0]
    cpp = r'''
#include <string>
#include <cstdio>
namespace nr_bundle { enum class Status { Complete, Partial, Failed }; }
struct Result { nr_bundle::Status status; bool required_complete; };
std::string describe(Result zip, [[maybe_unused]] bool capture_complete) {
    std::string text;
    EXPRESSION
    return text;
}
int main() {
    const nr_bundle::Status states[] = {nr_bundle::Status::Complete, nr_bundle::Status::Partial, nr_bundle::Status::Failed};
    const char *expected[] = {"zip.status=complete\n", "zip.status=partial\n", "zip.status=failed\n"};
    unsigned failures = 0, checks = 0;
    for (unsigned s=0; s<3; ++s) for (unsigned required=0; required<2; ++required) for (unsigned capture=0; capture<2; ++capture) {
        ++checks;
        if (describe({states[s], required!=0}, capture!=0) != expected[s]) ++failures;
    }
    if (failures) { std::printf("OPTIONAL_ZIP_STATUS_ORACLE failures=%u\n", failures); return 41; }
    std::printf("production TXT ZIP status: %u checks PASS\n", checks);
}
'''.replace('EXPRESSION', expression)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp)
        (path / 'status.cpp').write_text(cpp, encoding='utf-8')
        subprocess.run(['g++', '-std=c++20', '-Wall', '-Wextra', '-Werror', str(path / 'status.cpp'), '-o', str(path / 'status')], check=True)
        result = subprocess.run([str(path / 'status')], capture_output=True, text=True)
        print(result.stdout, end='')
        if args.negative_control:
            if result.returncode != 41 or result.stdout.strip() != 'OPTIONAL_ZIP_STATUS_ORACLE failures=4':
                raise RuntimeError('Historical status defect was not precisely reproduced')
            print('Historical optional/capture conflation rejected; negative control PASS')
        elif result.returncode != 0:
            raise RuntimeError('Production status regression')


if __name__ == '__main__':
    main()
