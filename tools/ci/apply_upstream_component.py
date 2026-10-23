#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""Point this job's apps at an upstream component commit.

No-op unless all three variables are set:

  UPSTREAM_COMPONENT       component name(s), comma-separated, or 'auto' to
                           detect modified components from the MR diff (falling
                           back to all components in the upstream repository)
  UPSTREAM_PROJECT_PATH    GitLab project path, for example adf/esp-board-manager
  UPSTREAM_SHA             commit to fetch

Optional variables:

  UPSTREAM_BASE_SHA        MR diff base SHA ($CI_MERGE_REQUEST_DIFF_BASE_SHA)
                           used when UPSTREAM_COMPONENT is 'auto'
  UPSTREAM_APP             one app relative to the repo, instead of the whole tree

The commit is cloned into ``$CI_PROJECT_DIR/../.upstream/src`` so app scanners
walking ``$CI_PROJECT_DIR`` do not pick up the upstream repository's own
examples or test_apps. Each app that this job will build gets an ``overrides``
entry in its own ``main/idf_component.yml``. Dependencies are left unchanged.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import Dict, List, Mapping, Optional, Sequence, Union

REQUEST_KEYS = ('UPSTREAM_COMPONENT', 'UPSTREAM_PROJECT_PATH', 'UPSTREAM_SHA')
AUTO_TOKENS = ('auto', '*')
NULL_SHA = '0' * 40


def read_request(env: Mapping[str, str]) -> Optional[Dict[str, str]]:
    """Return the upstream request values, None when none are set."""
    values = {key: env.get(key, '').strip() for key in REQUEST_KEYS}
    if not any(values.values()):
        return None
    missing = [key for key, value in values.items() if not value]
    if missing:
        raise SystemExit(
            'upstream component variables are incomplete, missing: ' + ', '.join(missing)
        )
    values['UPSTREAM_BASE_SHA'] = env.get('UPSTREAM_BASE_SHA', '').strip()
    return values


def parse_components(raw: str) -> List[str]:
    """Split a comma-separated component list while preserving order."""
    items: List[str] = []
    for part in raw.split(','):
        name = part.strip()
        if name and name not in items:
            items.append(name)
    return items


def upstream_repo_dir(project_dir: str) -> Path:
    """Checkout directory outside CI_PROJECT_DIR so app scanners do not walk into it."""
    return Path(project_dir).resolve().parent / '.upstream' / 'src'


def _link_root_as_component(repo_root: Path, component: str) -> Path:
    """Expose a single-component repo root under a directory named after the component."""
    link = repo_root.parent / component
    if link.is_symlink() or link.is_file():
        link.unlink()
    elif link.is_dir():
        import shutil
        shutil.rmtree(link)
    link.symlink_to(repo_root)
    return link


def upstream_component_dir(project_dir: str, component: str) -> Path:
    """Directory that contains the component's CMakeLists.txt and idf_component.yml."""
    repo_root = upstream_repo_dir(project_dir)
    sub = repo_root / component
    if (sub / 'idf_component.yml').is_file():
        return sub
    if (repo_root / 'idf_component.yml').is_file():
        return _link_root_as_component(repo_root, component)
    return sub


def discover_repo_components(repo_root: Path, project_path: str = '') -> List[str]:
    """Return all component names present in repo_root."""
    discovered = sorted(
        child.name
        for child in repo_root.iterdir()
        if child.is_dir()
        and not child.name.startswith('.')
        and (child / 'idf_component.yml').is_file()
        and (child / 'CMakeLists.txt').is_file()
    )
    if discovered:
        return discovered
    if (repo_root / 'idf_component.yml').is_file():
        fallback = Path(project_path).name.replace('-', '_')
        if not fallback:
            raise SystemExit('cannot infer component name for root idf_component.yml')
        return [fallback]
    raise SystemExit(f'no component with idf_component.yml found under {repo_root}')


def changed_components_from_diff(
    repo_root: Path,
    base_sha: str,
    head_sha: str,
    all_components: Sequence[str],
) -> List[str]:
    """Return the subset of all_components touched between base_sha and head_sha."""
    if not base_sha or base_sha == NULL_SHA or base_sha == head_sha:
        return []
    result = subprocess.run(
        ['git', 'diff', '--name-only', base_sha, head_sha],
        cwd=repo_root,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return []
    touched = {
        Path(line.strip()).parts[0]
        for line in result.stdout.splitlines()
        if line.strip() and Path(line.strip()).parts
    }
    return [name for name in all_components if name in touched]


def resolve_component_dirs(
    project_dir: str,
    raw_components: str,
    project_path: str = '',
    base_sha: str = '',
    head_sha: str = '',
) -> Dict[str, Path]:
    """Resolve explicit component names or 'auto' to their local directories."""
    repo_root = upstream_repo_dir(project_dir)
    names = parse_components(raw_components)
    if len(names) == 1 and names[0].lower() in AUTO_TOKENS:
        all_components = discover_repo_components(repo_root, project_path)
        if len(all_components) == 1:
            names = all_components
        else:
            changed = changed_components_from_diff(repo_root, base_sha, head_sha, all_components)
            if changed:
                print(f"Detected modified component(s) from diff: {', '.join(changed)}")
                names = changed
            else:
                print(
                    'No component directory touched in diff; '
                    f"falling back to all repository components: {', '.join(all_components)}"
                )
                names = all_components

    resolved: Dict[str, Path] = {}
    for name in names:
        comp_dir = upstream_component_dir(project_dir, name)
        if not (comp_dir / 'idf_component.yml').is_file():
            raise SystemExit(f"component manifest not found: {comp_dir / 'idf_component.yml'}")
        resolved[name] = comp_dir
    return resolved


def public_repo_url(server_url: str, project_path: str) -> str:
    """HTTPS URL with no credentials. The job token is supplied via GIT_ASKPASS."""
    return server_url.rstrip('/') + '/' + project_path.strip('/') + '.git'


def _askpass_script(dest: Path) -> Path:
    """Askpass that reads CI_JOB_TOKEN from the environment. The script itself has no secret."""
    script = dest.parent / 'git-askpass.sh'
    script.write_text(
        '#!/bin/sh\n'
        'case "$1" in\n'
        "  *Username*) printf '%s\\n' gitlab-ci-token ;;\n"
        '  *) printf \'%s\\n\' "$CI_JOB_TOKEN" ;;\n'
        'esac\n'
    )
    script.chmod(0o700)
    return script


def _override_block(overrides: Mapping[str, Union[str, Path]]) -> str:
    lines = ['overrides:\n']
    for component, component_dir in overrides.items():
        lines.append(
            f'  - espressif/{component}:\n'
            '      with:\n'
            f'        {component}:\n'
            f'          path: {component_dir}\n'
        )
    return ''.join(lines)


def apply_overrides(manifest: Path, overrides: Mapping[str, Union[str, Path]]) -> None:
    """Add or replace the overrides block for one or more components."""
    block = _override_block(overrides)
    if not manifest.is_file():
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(block)
        return

    text = manifest.read_text()
    lines = text.splitlines(keepends=True)
    start = None
    for index, line in enumerate(lines):
        if line.startswith('overrides:'):
            start = index
            break
    if start is None:
        if text and not text.endswith('\n'):
            text += '\n'
        manifest.write_text(text + '\n' + block)
        return

    end = start + 1
    while end < len(lines):
        stripped = lines[end].strip()
        if stripped == '':
            nxt = end + 1
            if nxt < len(lines) and lines[nxt].strip() and not lines[nxt][0].isspace():
                break
            end += 1
            continue
        if lines[end][0].isspace():
            end += 1
            continue
        break
    prefix = ''.join(lines[:start])
    if prefix and not prefix.endswith('\n'):
        prefix += '\n'
    suffix = ''.join(lines[end:])
    if suffix and not suffix.startswith('\n'):
        suffix = '\n' + suffix
    manifest.write_text(prefix + block + suffix)


def apply_override(manifest: Path, component: str, component_dir: Union[str, Path]) -> None:
    """Add or replace a single-component override block."""
    apply_overrides(manifest, {component: component_dir})


def clone_sha(url: str, sha: str, dest: Path, token: str = '', base_sha: str = '') -> None:
    """Clone the requested commit (and optional diff base commit) into dest."""
    import shutil

    parent = dest.parent
    if dest.exists():
        shutil.rmtree(dest)
    parent.mkdir(parents=True, exist_ok=True)
    env = os.environ.copy()
    env['GIT_TERMINAL_PROMPT'] = '0'
    if token:
        env['CI_JOB_TOKEN'] = token
        askpass = _askpass_script(dest)
        env['GIT_ASKPASS'] = str(askpass)
        env['GIT_CONFIG_COUNT'] = '1'
        env['GIT_CONFIG_KEY_0'] = 'credential.helper'
        env['GIT_CONFIG_VALUE_0'] = ''

    def run(args: Sequence[str], check: bool = True) -> subprocess.CompletedProcess:
        return subprocess.run(args, cwd=dest if dest.exists() else parent, check=check, env=env)

    dest.mkdir(parents=True, exist_ok=True)
    run(['git', 'init', '-q'])
    run(['git', 'remote', 'add', 'origin', url])
    run(['git', 'fetch', '--depth', '1', 'origin', sha])
    run(['git', 'checkout', '-q', '--detach', 'FETCH_HEAD'])
    if base_sha and base_sha != NULL_SHA and base_sha != sha:
        res = run(['git', 'fetch', '--depth', '1', 'origin', base_sha], check=False)
        if res.returncode != 0:
            print(f'Warning: failed to fetch UPSTREAM_BASE_SHA {base_sha}; diff detection will fall back.')


def require_apps(apps, project_path: str, target: str):
    """Fail when a requested override would not be applied to any app."""
    if not apps:
        raise SystemExit(
            f'No apps under {project_path} for target {target}; '
            'refusing to build without the upstream override'
        )


def _has_any_app_dir(project_path: str, target_dir_type: str) -> bool:
    from build_apps import CMakeApp, find_apps_with_filter

    base = Path(project_path)
    if not base.exists():
        return False
    if CMakeApp.is_app(str(base)):
        return True
    filtered, _ = find_apps_with_filter([project_path], target_dir_type=target_dir_type)
    return any(CMakeApp.is_app(str(p)) for p in filtered)


def _apps_for_job(project_path: str, target: str, target_dir_type: str):
    from build_apps import get_app_paths

    if not _has_any_app_dir(project_path, target_dir_type):
        raise SystemExit(
            f'No apps under {project_path} for target {target}; '
            'refusing to build without the upstream override'
        )
    return get_app_paths(
        path=project_path,
        target=target,
        target_dir_type=target_dir_type,
        enable_preview_targets=True,
    )


def main(argv=None) -> int:
    request = read_request(os.environ)
    if request is None:
        print('No upstream component variables; leaving published dependencies in place.')
        return 0

    parser = argparse.ArgumentParser(description='Override upstream component(s) for this build.')
    parser.add_argument('--project-path', required=True)
    parser.add_argument('--target', required=True)
    parser.add_argument('--target-dir-type', default='all')
    args = parser.parse_args(argv)

    project_dir = os.environ.get('CI_PROJECT_DIR', '').strip()
    server = os.environ.get('CI_SERVER_URL', '').strip()
    token = os.environ.get('CI_JOB_TOKEN', '').strip()
    if not project_dir or not server or not token:
        raise SystemExit('CI_PROJECT_DIR, CI_SERVER_URL and CI_JOB_TOKEN are required to fetch the component')

    repo_root = upstream_repo_dir(project_dir)
    base_sha = request.get('UPSTREAM_BASE_SHA', '')
    print(f"Fetching {request['UPSTREAM_PROJECT_PATH']} @ {request['UPSTREAM_SHA']}")
    clone_sha(
        public_repo_url(server, request['UPSTREAM_PROJECT_PATH']),
        request['UPSTREAM_SHA'],
        repo_root,
        token=token,
        base_sha=base_sha,
    )
    resolved = resolve_component_dirs(
        project_dir,
        request['UPSTREAM_COMPONENT'],
        project_path=request['UPSTREAM_PROJECT_PATH'],
        base_sha=base_sha,
        head_sha=request['UPSTREAM_SHA'],
    )

    apps = _apps_for_job(args.project_path, args.target, args.target_dir_type)
    if not apps:
        if os.environ.get('UPSTREAM_APP', '').strip():
            print(f'Skipping {args.project_path}: filtered out for target {args.target} by build rules.')
            return 0
        require_apps(apps, args.project_path, args.target)

    summary = ', '.join(f'{name} -> {path}' for name, path in resolved.items())
    for app in apps:
        manifest = Path(app) / 'main' / 'idf_component.yml'
        if not (Path(app) / 'main').is_dir():
            raise SystemExit(f'app has no main component directory: {app}')
        apply_overrides(manifest, resolved)
        print(f'override {summary} in {manifest}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
