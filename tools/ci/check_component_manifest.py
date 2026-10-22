#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: Apache-2.0
"""Check published component roots touched by a merge request diff.

Roots come from .github/workflows/upload_component.yml. A root is checked
when the diff touches it, or when the upload list gains that path. Components
left untouched stay as they are.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

UPLOAD = '.github/workflows/upload_component.yml'
REQUIRED = ('version', 'description', 'url', 'documentation', 'issues', 'tags')
TOP_KEY = re.compile(r'^([A-Za-z_][A-Za-z0-9_]*)\s*:\s*(.*)$')
LIST_ITEM = re.compile(r'^\s*-\s*(.*?)\s*$')
EXAMPLE_PATH = re.compile(r"""path:\s*(?:"([^"]+)"|'([^']+)'|(\S+))""")
UPLOAD_PATH = re.compile(r':\s*(\./\S+)')


def strip_inline_comment(line):
    in_single = False
    in_double = False
    for idx, ch in enumerate(line):
        if ch == "'" and not in_double:
            in_single = not in_single
        elif ch == '"' and not in_single:
            in_double = not in_double
        elif ch == '#' and not in_single and not in_double:
            if idx == 0 or line[idx - 1].isspace():
                return line[:idx].rstrip()
    return line.rstrip()


def _add_upload_root(roots, raw_item):
    root = re.sub(r'^(?:\./)+', '', (raw_item or '').strip()).rstrip('/')
    if root and root not in roots:
        roots.append(root)


def parse_upload_roots(text):
    roots = []
    in_directories = False
    dir_indent = 0
    for raw in text.splitlines():
        line = strip_inline_comment(raw)
        stripped = line.strip()
        if not stripped:
            continue
        indent = len(line) - len(line.lstrip(' \t'))
        if in_directories:
            if indent <= dir_indent:
                in_directories = False
            else:
                for part in stripped.split(';'):
                    _add_upload_root(roots, part)
                continue
        dir_match = re.match(r'^directories\s*:\s*(.*)$', stripped)
        if dir_match:
            rest = dir_match.group(1).strip()
            if rest in ('>', '|', '>-', '|-', ''):
                in_directories = True
                dir_indent = indent
            else:
                for part in unquote(rest).split(';'):
                    _add_upload_root(roots, part)
            continue
        for match in UPLOAD_PATH.finditer(line):
            _add_upload_root(roots, match.group(1))
    return roots


def unquote(value):
    value = strip_inline_comment(value or '').strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value[1:-1].strip()
    if value in ('[]', '{}', '>', '|', '>-', '|-', 'null', '~'):
        return ''
    return value


def parse_manifest(text):
    try:
        import yaml  # type: ignore
    except ImportError:
        yaml = None

    if yaml is not None:
        try:
            doc = yaml.safe_load(text)
        except Exception:
            doc = None
        if isinstance(doc, dict):
            keys = {}
            for k, v in doc.items():
                if isinstance(v, (str, int, float)) and not isinstance(v, bool):
                    keys[str(k)] = str(v).strip()
            tags = []
            raw_tags = doc.get('tags')
            if isinstance(raw_tags, list):
                tags = [str(item).strip() for item in raw_tags if item is not None and str(item).strip()]
            examples = []
            raw_examples = doc.get('examples')
            if isinstance(raw_examples, list):
                for item in raw_examples:
                    if isinstance(item, dict) and item.get('path'):
                        examples.append(str(item['path']).strip())
            return keys, tags, examples

    keys = {}
    tags = []
    examples = []
    section = None
    for raw in text.splitlines():
        cleaned = strip_inline_comment(raw)
        stripped = cleaned.strip()
        if not stripped:
            continue
        if cleaned[0] not in ' \t':
            match = TOP_KEY.match(cleaned)
            section = None
            if not match:
                continue
            section = match.group(1)
            keys[section] = unquote(match.group(2))
            continue
        if section == 'tags':
            item = LIST_ITEM.match(cleaned)
            if item:
                val = unquote(item.group(1))
                if val:
                    tags.append(val)
        elif section == 'examples':
            found = EXAMPLE_PATH.search(cleaned)
            if found:
                examples.append(next(part for part in found.groups() if part))
        elif section and section in REQUIRED and not keys.get(section):
            keys[section] = unquote(stripped)
    return keys, tags, examples


def select_roots(changed, current, previous):
    selected = []
    norm_changed = [re.sub(r'^(?:\./)+', '', p.strip()) for p in changed if p and p.strip()]

    def add(root):
        if root in current and root not in selected:
            selected.append(root)

    for path in norm_changed:
        if not path or path == UPLOAD:
            continue
        match = None
        for root in current:
            if path == root or path.startswith(root + '/'):
                if match is None or len(root) > len(match):
                    match = root
        if match:
            add(match)
    if previous is not None and UPLOAD in norm_changed:
        for root in current:
            if root not in previous:
                add(root)
    return selected


def validate_with_compote(manifest_path, label):
    try:
        import yaml  # type: ignore
        from idf_component_tools.manifest import Manifest  # type: ignore
    except ImportError:
        return []
    idf_path = os.environ.get('IDF_PATH', '')
    if idf_path and not (Path(idf_path) / 'tools' / 'idf.py').is_file():
        os.environ.pop('IDF_PATH', None)
    try:
        doc = yaml.safe_load(manifest_path.read_text()) or {}
        if not isinstance(doc, dict):
            return [f'{label}: idf_component.yml root must be a mapping']
        Manifest.fromdict(doc)
    except Exception as exc:
        detail = ' '.join(
            line.strip()
            for line in str(exc).splitlines()
            if line.strip() and not line.strip().startswith('For further information visit')
        ) or type(exc).__name__
        return [
            f'{label}: compote manifest validation failed: {detail} '
            f'(please do local verify with: python3 tools/ci/check_component_manifest.py {label})'
        ]
    return []


def check_dependency_paths(manifest_path, label, bound):
    try:
        import yaml  # type: ignore
        doc = yaml.safe_load(manifest_path.read_text()) or {}
    except Exception:
        doc = None

    dep_paths = []
    if isinstance(doc, dict) and isinstance(doc.get('dependencies'), dict):
        for dep_name, spec in doc['dependencies'].items():
            if not isinstance(spec, dict):
                continue
            for field in ('path', 'override_path'):
                val = spec.get(field)
                if val and '$' not in str(val):
                    dep_paths.append((str(dep_name), field, str(val).strip()))
    else:
        in_deps = False
        cur_dep = ''
        for raw in manifest_path.read_text().splitlines():
            cleaned = strip_inline_comment(raw)
            stripped = cleaned.strip()
            if not stripped:
                continue
            indent = len(cleaned) - len(cleaned.lstrip(' \t'))
            if indent == 0:
                in_deps = stripped.startswith('dependencies:')
                cur_dep = ''
                continue
            if not in_deps:
                continue
            key_match = re.match(r'^([A-Za-z0-9_./-]+)\s*:\s*(.*)$', stripped)
            if not key_match:
                continue
            k, v = key_match.group(1), unquote(key_match.group(2))
            if k in ('path', 'override_path') and v and '$' not in v:
                dep_paths.append((cur_dep or 'dependency', k, v))
            elif not v:
                cur_dep = k

    errors = []
    base_dir = manifest_path.parent
    for dep_name, field, rel_path in dep_paths:
        dest = (base_dir / rel_path).resolve()
        try:
            dest.relative_to(bound)
        except ValueError:
            errors.append(f'{label}: {dep_name} {field} escapes repository root: {rel_path}')
            continue
        if not dest.is_dir():
            errors.append(f'{label}: {dep_name} {field} not found: {rel_path}')
    return errors


def check_root(root, label, repo=None):
    errors = []
    license_path = root / 'LICENSE'
    if not license_path.is_file() or license_path.stat().st_size == 0:
        errors.append(f'{label}: missing LICENSE')
    manifest_path = root / 'idf_component.yml'
    if not manifest_path.is_file() or manifest_path.stat().st_size == 0:
        errors.append(f'{label}: missing idf_component.yml')
        return errors
    keys, tags, examples = parse_manifest(manifest_path.read_text())
    for name in REQUIRED:
        if name == 'tags':
            values = [item for item in tags if item] or ([keys.get('tags')] if keys.get('tags') else [])
            if not values:
                errors.append(f'{label}/idf_component.yml: missing tags')
            continue
        if not keys.get(name):
            errors.append(f'{label}/idf_component.yml: missing {name}')
    errors.extend(validate_with_compote(manifest_path, f'{label}/idf_component.yml'))
    bound = (repo or root).resolve()
    errors.extend(check_dependency_paths(manifest_path, f'{label}/idf_component.yml', bound))
    for sub_manifest in sorted(root.glob('**/idf_component.yml')):
        if sub_manifest == manifest_path or 'managed_components' in sub_manifest.parts:
            continue
        rel_label = f'{label}/{sub_manifest.relative_to(root)}'
        errors.extend(validate_with_compote(sub_manifest, rel_label))
        errors.extend(check_dependency_paths(sub_manifest, rel_label, bound))
    for example in examples:
        dest = (root / example).resolve()
        try:
            dest.relative_to(bound)
        except ValueError:
            errors.append(f'{label}: example path escapes component root: {example}')
            continue
        if not dest.is_dir():
            errors.append(f'{label}: example path not found: {example}')
    return errors


def changed_files():
    if len(sys.argv) > 1:
        return sys.argv[1:]
    return os.environ.get('GIT_DIFF_OUTPUT', '').split()


def previous_roots(repo):
    base = os.environ.get('CI_MERGE_REQUEST_DIFF_BASE_SHA', '').strip()
    if not base or set(base) == {'0'}:
        base = os.environ.get('CI_COMMIT_BEFORE_SHA', '').strip()
    if not base or set(base) == {'0'}:
        return None
    try:
        text = subprocess.check_output(
            ['git', 'show', f'{base}:{UPLOAD}'],
            cwd=repo,
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except subprocess.CalledProcessError:
        return []
    return parse_upload_roots(text)


def main():
    repo = Path(os.environ.get('CI_PROJECT_DIR', Path.cwd()))
    upload = repo / UPLOAD
    if not upload.is_file():
        print(f'missing {UPLOAD}', file=sys.stderr)
        return 1
    changed = [path.strip() for path in changed_files() if path.strip()]
    current = parse_upload_roots(upload.read_text())
    previous = None
    if UPLOAD in changed:
        previous = previous_roots(repo)
        if previous is None:
            print('upload list changed but base revision is unavailable; checking every published component')
            previous = []
    roots = select_roots(changed, current, previous)
    if not roots:
        print('No published component to check.')
        return 0
    errors = []
    for root in roots:
        errors.extend(check_root(repo / root, root, repo=repo))
    if errors:
        print('\n'.join(errors), file=sys.stderr)
        print(
            f'Please do local verify with: python3 tools/ci/check_component_manifest.py {" ".join(roots)}',
            file=sys.stderr,
        )
        return 1
    print('Checked: ' + ', '.join(roots))
    return 0


if __name__ == '__main__':
    sys.exit(main())
