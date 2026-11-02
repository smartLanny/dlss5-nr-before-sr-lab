"""Evaluate the checked-in CI job expressions against real event-shaped data.

This validates the current router, not GitHub approval settings or a malicious
PR that changes the workflow itself. No GitHub Actions jobs are dispatched.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


def jobs(source):
    return dict(re.findall(r'^  ([\w-]+):\n(.*?)(?=^  [\w-]+:|\Z)',
                           source[source.index('jobs:'):], re.M | re.S))


def condition(job):
    found = re.search(r'^    if: (.*)$', job, re.M)
    if not found:
        raise ValueError('Job has no explicit admission expression')
    if found.group(1) not in ('>-', '>'):
        return found.group(1)
    after = job[found.end():].splitlines()
    parts = []
    for line in after:
        if line and not line.startswith('      '):
            break
        parts.append(line.strip())
    return ' '.join(parts).strip()


class Expression:
    """Small fail-closed interpreter for the operators used by the production if.

    It tokenizes the source expression; it does not reconstruct the repository's
    authorization decision. Unsupported syntax fails the test, never admits.
    """
    token = re.compile(r"\s*(\&\&|\|\||==|!=|[(),]|'(?:[^']|'')*'|[A-Za-z_][\w.]*(?:\*\.[\w.]+)?)")

    def __init__(self, source, context):
        self.items = []
        position = 0
        while position < len(source):
            found = self.token.match(source, position)
            if not found:
                raise ValueError('Unsupported expression syntax: ' + source[position:])
            self.items.append(found.group(1))
            position = found.end()
        self.at = 0
        self.context = context

    def accept(self, token):
        if self.at < len(self.items) and self.items[self.at] == token:
            self.at += 1
            return True
        return False

    def require(self, token):
        if not self.accept(token):
            raise ValueError('Expected ' + token)

    def value(self, path):
        values = [self.context]
        projected = False
        for key in path.split('.'):
            if key == '*':
                values = [item for value in values if isinstance(value, list) for item in value]
                projected = True
            else:
                values = [value.get(key, '') if isinstance(value, dict) else '' for value in values]
        return values if projected else values[0]

    @staticmethod
    def equal(left, right):
        # Actions ignores case for strings and compares unlike scalar types
        # numerically. These gates use strings, booleans and missing properties.
        if isinstance(left, str) and isinstance(right, str):
            return left.casefold() == right.casefold()
        def number(value):
            if value is None or value == '':
                return 0
            try:
                return float(value)
            except (TypeError, ValueError):
                return float('nan')
        return number(left) == number(right)

    def primary(self):
        if self.accept('('):
            value = self.disjunction()
            self.require(')')
            return value
        if self.at >= len(self.items):
            raise ValueError('Missing expression operand')
        item = self.items[self.at]
        self.at += 1
        if item == 'contains':
            self.require('(')
            source = self.disjunction()
            self.require(',')
            needle = self.disjunction()
            self.require(')')
            if isinstance(source, list):
                return any(self.equal(value, needle) for value in source)
            return str(needle).casefold() in str(source).casefold()
        if item in ('true', 'false', 'null'):
            return {'true': True, 'false': False, 'null': None}[item]
        if item.startswith("'"):
            return item[1:-1].replace("''", "'")
        if not item.startswith(('github.', 'vars.', 'inputs.')):
            raise ValueError('Unsupported context: ' + item)
        return self.value(item)

    def comparison(self):
        left = self.primary()
        if self.accept('=='):
            return self.equal(left, self.primary())
        if self.accept('!='):
            return not self.equal(left, self.primary())
        return left

    def conjunction(self):
        value = self.comparison()
        while self.accept('&&'):
            right = self.comparison()  # Parse both sides, including false branches.
            value = right if value else value
        return value

    def disjunction(self):
        value = self.conjunction()
        while self.accept('||'):
            right = self.conjunction()
            value = value if value else right
        return value

    def evaluate(self):
        result = self.disjunction()
        if self.at != len(self.items):
            raise ValueError('Trailing expression tokens')
        return bool(result)


def event(*, private=False, fork=False, author='OWNER', actor='maintainer',
          head='maintainer/core', action='opened', labels=(), label='',
          name='pull_request', base='main', enabled='true', full_build=False):
    return {
        'vars': {'ENABLE_SELF_HOSTED_CI': enabled, 'ENABLE_HOSTED_CI': 'false'},
        'inputs': {'full_build': full_build},
        'github': {'event_name': name, 'repository': 'maintainer/core',
                   'repository_owner': 'maintainer', 'actor': actor, 'base_ref': base,
                   'event': {'repository': {'private': private}, 'action': action,
                             'label': {'name': label}, 'pull_request': {
                                 'head': {'repo': {'full_name': head, 'fork': fork}},
                                 'author_association': author,
                                 'labels': [{'name': value} for value in labels]}}}}


class PublicCiPolicy(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.source = (ROOT / '.github/workflows/pull-request-validation.yml').read_text(encoding='utf-8')
        cls.jobs = jobs(cls.source)
        cls.expressions = {name: condition(cls.jobs[name]) for name in
                           ('public-portable', 'self-hosted-tests', 'self-hosted-canonical-build')}

    def admitted(self, job, context):
        return Expression(self.expressions[job], context).evaluate()

    def assert_jobs(self, context, portable, tests, build):
        for job, expected in zip(self.expressions, (portable, tests, build)):
            self.assertEqual(self.admitted(job, context), expected, (job, context))

    def test_public_private_and_manual_matrix(self):
        for private in (False, True):
            self.assert_jobs(event(private=private), not private, True, False)
            self.assert_jobs(event(private=private, enabled='false'), not private, False, False)
            self.assert_jobs(event(private=private, name='push'), False, False, False)
            self.assert_jobs(event(private=private, name='workflow_dispatch'), not private, True, False)
            self.assert_jobs(event(private=private, name='workflow_dispatch', full_build=True), not private, True, True)
            self.assert_jobs(event(private=private, name='workflow_dispatch', full_build=True, enabled=''), not private, False, False)

    def test_untrusted_pr_cannot_reach_either_self_hosted_job(self):
        for private in (False, True):
            for action, label in (('opened', ''), ('synchronize', ''), ('reopened', ''), ('labeled', 'full-build')):
                for changes in (
                    {'fork': True, 'head': 'outsider/core', 'author': 'CONTRIBUTOR', 'actor': 'outsider'},
                    {'fork': True, 'head': 'outsider/core'},  # Owner author/actor cannot make a fork trusted.
                    {'fork': True},  # Even malformed same-name fork data is rejected.
                    {'head': 'outsider/core'},  # Do not rely solely on the fork bit.
                    {'author': 'COLLABORATOR'},
                    {'author': 'MEMBER'},
                    {'author': 'NONE'},
                    {'actor': 'collaborator'},
                    {'base': 'release'},
                ):
                    with self.subTest(private=private, action=action, **changes):
                        context = event(private=private, action=action, label=label,
                                        labels=('full-build',), **changes)
                        self.assert_jobs(context, not private, False, False)

    def test_full_build_label_and_actor_matrix(self):
        self.assert_jobs(event(labels=('full-build',)), True, True, True)
        self.assert_jobs(event(action='synchronize', labels=('full-build',)), True, True, True)
        self.assert_jobs(event(action='labeled', label='full-build', labels=('full-build',)), True, False, True)
        self.assert_jobs(event(action='labeled', label='documentation', labels=('full-build',)), True, False, False)
        self.assert_jobs(event(action='labeled', label='full-build'), True, False, False)
        self.assert_jobs(event(action='labeled', label='full-build', labels=('full-build',), actor='outsider'), True, False, False)

    def test_missing_pr_data_fails_closed_and_dispatch_uses_its_own_input(self):
        context = event(labels=('full-build',))
        del context['github']['event']['pull_request']
        self.assert_jobs(context, True, False, False)
        context['github']['event_name'] = 'workflow_dispatch'
        self.assert_jobs(context, True, True, False)
        context['inputs']['full_build'] = True
        self.assert_jobs(context, True, True, True)
        # Current manual-dispatch policy delegates authorization to GitHub's
        # write-access gate; the PR-only owner test is not a manual owner gate.
        context['github']['actor'] = 'collaborator'
        self.assert_jobs(context, True, True, True)

    def test_each_production_guard_is_observable_by_a_negative_control(self):
        cases = (
            ("github.event.pull_request.head.repo.full_name == github.repository", {'head': 'outsider/core'}),
            ("github.event.pull_request.head.repo.fork == false", {'fork': True}),
            ("github.event.pull_request.author_association == 'OWNER'", {'author': 'CONTRIBUTOR'}),
            ("github.actor == github.repository_owner", {'actor': 'outsider'}),
            ("vars.ENABLE_SELF_HOSTED_CI == 'true'", {'enabled': 'false'}),
        )
        for job in ('self-hosted-tests', 'self-hosted-canonical-build'):
            for guard, changes in cases:
                with self.subTest(job=job, removed=guard):
                    original = self.expressions[job]
                    self.assertEqual(original.count(guard), 1)
                    context = event(labels=('full-build',), **changes)
                    self.assertFalse(Expression(original, context).evaluate())
                    self.assertTrue(Expression(original.replace(guard, 'true'), context).evaluate(),
                                    'Removing this real guard must expose its denied event')

    def test_public_runner_does_not_persist_credentials_or_request_write_permissions(self):
        public = self.jobs['public-portable']
        self.assertIn('runs-on: ubuntu-24.04', public)
        self.assertNotIn('self-hosted', public)
        self.assertIn('persist-credentials: false', public)
        self.assertRegex(self.source, r'(?m)^permissions:\n  contents: read\n')
        self.assertNotRegex(self.source, r'(?m)^\s+[\w-]+: write\s*$')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path)
    args, rest = parser.parse_known_args()
    if args.repo:
        ROOT = args.repo.resolve()
    unittest.main(argv=[sys.argv[0]] + rest)
