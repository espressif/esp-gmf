#!/usr/bin/env python3

# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Script to batch process applications:
1. Get application list using build_apps.py
2. Execute idf.py set-target for each application
3. Execute idf.py bmgr for each application (with IDF_EXTRA_ACTIONS_PATH set)
"""

import os
import sys
import json
import argparse
import subprocess
import logging
from typing import Dict, List, Optional
from pathlib import Path
from apply_upstream_component import upstream_repo_dir
from build_apps import (
    get_app_paths,
    APP_TYPE_ALL,
    APP_TYPE_EXAMPLE,
    APP_TYPE_TEST_APPS,
    print_debug,
    print_info,
    print_warning,
    print_error,
)

# Initialize logging for print functions
logging.basicConfig(
    level=logging.INFO,
    format='%(message)s'
)

def _target_is_idf_preview(target: str) -> bool:
    """True if *target* is an ESP-IDF preview chip (requires idf.py --preview)."""
    try:
        from esp_bool_parser import PREVIEW_TARGETS
        return target in PREVIEW_TARGETS
    except Exception:
        return False


def _idf_py_cmd(target: str, *idf_args: str) -> List[str]:
    """Build idf.py argv; insert --preview when *target* is a preview chip."""
    if _target_is_idf_preview(target):
        return ['idf.py', '--preview', *idf_args]
    return ['idf.py', *idf_args]


def get_app_list(
    project_path: str,
    target: str,
    target_dir_type: str = APP_TYPE_ALL,
    no_require_pytest: bool = False,
    enable_preview_targets: bool = True,
    parallel_count: int = 1,
    parallel_index: int = 1,
) -> List[str]:
    """
    Get application list using build_apps.py's get_app_paths API.

    Args:
        project_path: Project root path
        target: Target chip (e.g., esp32, esp32s3, etc.)
        target_dir_type: Application directory type (APP_TYPE_ALL/APP_TYPE_EXAMPLE/APP_TYPE_TEST_APPS)
        no_require_pytest: Whether to require pytest files
                         (Note: get_app_paths internally sets no_require_pytest=True)
        enable_preview_targets: Forwarded to idf_build_apps (preview chips like idf.py --preview).

    Returns:
        List of application paths
    """
    print_info(f'[Step 1] Getting application list (type: {target_dir_type})...')

    # Set environment variable (get_cmake_apps requires PROJECT_PATH)
    # Only set if not exists or value differs to avoid unnecessary overwrite
    current_project_path = os.environ.get('PROJECT_PATH')
    if current_project_path != project_path:
        os.environ['PROJECT_PATH'] = project_path

    try:
        # Directly call get_app_paths API
        # get_app_paths internally sets no_require_pytest=True, so no extra handling needed
        app_list = get_app_paths(
            path=project_path,
            target=target,
            target_dir_type=target_dir_type,
            exclude_apps=None,
            enable_preview_targets=enable_preview_targets,
            parallel_count=parallel_count,
            parallel_index=parallel_index,
        )

        if not app_list:
            print_warning('Warning: No applications found')
            return []

        print_info(f'Found {len(app_list)} applications')
        for app in app_list:
            print_info(f'  - {app}')

        return app_list

    except Exception as e:
        print_error(f'Error: Failed to get application list: {e}')
        import traceback
        traceback.print_exc()
        sys.exit(1)


def _upstream_bmgr_dir() -> Optional[str]:
    """Local checkout from apply_upstream_component.py, when this job is an upstream regression."""
    name = os.environ.get('UPSTREAM_COMPONENT', '').strip()
    root = os.environ.get('CI_PROJECT_DIR', '').strip()
    if not name or not root:
        return None
    repo_root = upstream_repo_dir(root)
    for candidate in (
        repo_root / 'esp_board_manager',
        repo_root / 'components' / 'esp_board_manager',
        repo_root.parent / 'esp_board_manager',
    ):
        if (candidate / 'idf_component.yml').is_file() and (candidate / 'idf_ext.py').is_file():
            return str(candidate)
    return None


def _find_bmgr_actions_path(app_path: str) -> Optional[str]:
    """
    Locate esp_board_manager's idf_ext.py.

    After bmgr moved to the IDF Component Registry, IDF 5.x does not auto-load
    component idf_ext.py unless IDF_EXTRA_ACTIONS_PATH points at the component dir.
    When esp_board_manager is overridden to a local upstream checkout, IDF does not
    copy it into managed_components, so we prioritize the upstream checkout path
    and fall back to managed_components/espressif__esp_board_manager.
    """
    candidates: List[str] = []
    upstream = _upstream_bmgr_dir()
    if upstream:
        candidates.append(upstream)
    candidates.append(
        os.path.join(app_path, 'managed_components', 'espressif__esp_board_manager')
    )
    for path in candidates:
        if os.path.isfile(os.path.join(path, 'idf_ext.py')):
            return path
    return None


def _idf_env_for_app(app_path: str) -> Optional[Dict[str, str]]:
    """Build subprocess env with IDF_EXTRA_ACTIONS_PATH set to the managed bmgr component."""
    bmgr_path = _find_bmgr_actions_path(app_path)
    if not bmgr_path:
        return None
    env = os.environ.copy()
    env['IDF_EXTRA_ACTIONS_PATH'] = bmgr_path
    return env


def _execute_command_for_app(
    app_path: str,
    cmd: List[str],
    dry_run: bool = False,
    use_warning_for_failure: bool = False,
    env: Optional[Dict[str, str]] = None,
) -> None:
    """
    Execute a command for a single application.

    Args:
        app_path: Application path
        cmd: Command to execute (as list of strings)
        dry_run: Whether to simulate execution only
        use_warning_for_failure: If True, use print_warning for failures; otherwise use print_error
        env: Optional environment for subprocess (defaults to os.environ)
    """
    if not os.path.isdir(app_path):
        print_warning(f'  Warning: Directory does not exist, skipping')
        return

    if dry_run:
        print_info(f'  [Dry run] Skipping actual execution')
        return

    try:
        cmd_str = ' '.join(cmd)
        result = subprocess.run(
            cmd,
            cwd=app_path,
            env=env if env is not None else os.environ,
            capture_output=True,
            text=True,
            check=False
        )

        if result.returncode == 0:
            print_info(f'  ✓ Success: {cmd_str}')
        else:
            error_func = print_warning if use_warning_for_failure else print_error
            error_func(f'  ✗ Failed (return code: {result.returncode}): {cmd_str}')
            if result.stderr:
                error_func(f'  Error: {result.stderr[:200]}')

    except Exception as e:
        cmd_str = ' '.join(cmd)
        print_error(f'  ✗ Exception: {cmd_str} - {e}')


def set_target_for_app(app_path: str, target: str, dry_run: bool = False) -> None:
    """
    Execute idf.py set-target for a single application.

    Args:
        app_path: Application path
        target: Target chip
        dry_run: Whether to simulate execution only
    """
    cmd = _idf_py_cmd(target, 'set-target', target)
    _execute_command_for_app(app_path, cmd, dry_run, use_warning_for_failure=False)


def _resolve_numeric_idf_version() -> Optional[str]:
    """Read numeric MAJOR.MINOR.PATCH from $IDF_PATH/tools/cmake/version.cmake."""
    import re
    idf_path = os.environ.get('IDF_PATH', '').strip()
    if not idf_path:
        return None
    ver_cmake = Path(idf_path) / 'tools' / 'cmake' / 'version.cmake'
    if not ver_cmake.is_file():
        return None
    parts: Dict[str, str] = {}
    pat = re.compile(r'^\s*set\s*\(\s*IDF_VERSION_([A-Z]{5})\s+(\d+)')
    try:
        for line in ver_cmake.read_text(encoding='utf-8').splitlines():
            m = pat.match(line)
            if m:
                parts[m.group(1)] = m.group(2)
        return f"{parts['MAJOR']}.{parts['MINOR']}.{parts['PATCH']}"
    except Exception:
        return None


def _solve_depends_on_esp_board_manager(app_path: str, idf_target: str = '') -> Optional[bool]:
    """Resolve the full dependency graph (in-repo + external Registry) via VersionSolver."""
    try:
        from idf_component_tools.manager import ManifestManager
        from idf_component_tools.utils import ProjectRequirements
        from idf_component_manager.version_solver.version_solver import VersionSolver
    except ImportError:
        return None

    app_dir = Path(app_path).resolve()
    candidates = [('main', app_dir / 'main'), (app_dir.name, app_dir)]
    components_dir = app_dir / 'components'
    if components_dir.is_dir():
        for child in sorted(components_dir.iterdir(), key=lambda p: p.name):
            if child.is_dir() and child.name != 'gen_bmgr_codes':
                candidates.append((child.name, child))

    saved_target = os.environ.get('IDF_TARGET')
    saved_ver = os.environ.get('IDF_VERSION')
    try:
        if idf_target:
            os.environ['IDF_TARGET'] = idf_target
        elif not saved_target:
            os.environ['IDF_TARGET'] = 'esp32'
        # ManifestManager.load() evaluates `if: "idf_version ..."` rules using
        # os.environ['IDF_VERSION'], which in CI is a tag/branch name like
        # "v5.5.3" or "release/v6.1". Replace it with the numeric version from
        # version.cmake BEFORE calling ManifestManager.load().
        numeric_ver = _resolve_numeric_idf_version()
        if numeric_ver:
            os.environ['IDF_VERSION'] = numeric_ver
        else:
            os.environ.pop('IDF_VERSION', None)

        manifests = []
        has_any_deps = False
        for name, comp_dir in candidates:
            yml = comp_dir / 'idf_component.yml'
            if not yml.is_file() or not (comp_dir / 'CMakeLists.txt').is_file():
                continue
            loaded = ManifestManager(str(comp_dir), name).load()
            if loaded.requirements:
                has_any_deps = True
                for req in loaded.requirements:
                    if str(getattr(req, 'name', '')).split('/')[-1] == 'esp_board_manager':
                        return True
            manifests.append(loaded)

        if not manifests or not has_any_deps:
            return False

        solution = VersionSolver(ProjectRequirements(manifests)).solve()
        return any(
            str(getattr(dep, 'name', '')).split('/')[-1] == 'esp_board_manager'
            for dep in (solution.dependencies or [])
        )
    except Exception as exc:
        print_warning(f'  Warning: VersionSolver fallback ({exc})')
        return None
    finally:
        if saved_target is None:
            os.environ.pop('IDF_TARGET', None)
        else:
            os.environ['IDF_TARGET'] = saved_target
        if saved_ver is None:
            os.environ.pop('IDF_VERSION', None)
        else:
            os.environ['IDF_VERSION'] = saved_ver


def _is_depending_on_esp_board_manager(app_path: str, idf_target: str = '') -> bool:
    """
    Check if the project depends on esp_board_manager component (directly or transitively).

    Args:
        app_path: Application path
        idf_target: Target chip (e.g., esp32, esp32s3)

    Returns:
        True if esp_board_manager is in the project's dependency graph, False otherwise
    """
    project_desc_path = os.path.join(app_path, 'build', 'project_description.json')

    if os.path.exists(project_desc_path):
        try:
            with open(project_desc_path, 'r', encoding='utf-8') as f:
                project_desc = json.load(f)

            build_components = project_desc.get('build_components', [])
            bmgr_aliases = ('esp_board_manager', 'espressif__esp_board_manager')
            if any(alias in build_components for alias in bmgr_aliases):
                return True
            print_info('  Info: esp_board_manager not found in build_components, skipping gen-bmgr-config')
            return False
        except Exception as e:
            print_warning(f'  Warning: Error reading {project_desc_path}: {e}')

    solved = _solve_depends_on_esp_board_manager(app_path, idf_target=idf_target)
    if solved is not None:
        if not solved:
            print_info('  Info: esp_board_manager not found in solved dependency graph, skipping bmgr board config')
        return solved

    print_info('  Info: esp_board_manager not found in dependency graph, skipping bmgr board config')
    return False


def _external_board_component_dirs(app_path: str) -> List[str]:
    """
    Return resolved dirs of board-pack components that live outside managed_components/components.

    When a board pack such as ``esp_boards`` is redirected via a top-level ``overrides:``
    block in ``main/idf_component.yml``, IDF resolves it in ``project_description.json``
    without copying it into ``managed_components/``. Passing those directories via ``-c``
    lets ``idf.py bmgr`` discover the boards regardless of the installed bmgr version.
    """
    app_abs = os.path.abspath(app_path)
    in_tree_prefixes = (
        os.path.join(app_abs, 'managed_components') + os.sep,
        os.path.join(app_abs, 'components') + os.sep,
    )
    dirs: List[str] = []

    def _add_dir(comp_dir: str) -> None:
        if not comp_dir or not os.path.isdir(comp_dir):
            return
        comp_abs = os.path.abspath(comp_dir)
        if any(comp_abs.startswith(prefix) for prefix in in_tree_prefixes):
            return
        if comp_abs not in dirs:
            dirs.append(comp_abs)

    project_desc_path = os.path.join(app_path, 'build', 'project_description.json')
    if os.path.isfile(project_desc_path):
        try:
            with open(project_desc_path, 'r', encoding='utf-8') as f:
                project_desc = json.load(f)
            comp_info = project_desc.get('build_component_info')
            if isinstance(comp_info, dict):
                for name, info in comp_info.items():
                    if not isinstance(name, str) or not isinstance(info, dict):
                        continue
                    slug = name.lower()
                    if 'board_manager' in slug or 'boards' not in slug:
                        continue
                    _add_dir(info.get('dir', ''))
        except Exception:
            pass

    for rel_yml in ('main/idf_component.yml', 'idf_component.yml'):
        yml_path = Path(app_path) / rel_yml
        if not yml_path.is_file():
            continue
        try:
            import yaml  # type: ignore
            doc = yaml.safe_load(yml_path.read_text(encoding='utf-8')) or {}
        except Exception:
            continue
        if not isinstance(doc, dict):
            continue
        for entry in doc.get('overrides') or []:
            if not isinstance(entry, dict):
                continue
            for name, body in entry.items():
                slug = str(name).lower()
                if 'board_manager' in slug or 'boards' not in slug or not isinstance(body, dict):
                    continue
                with_block = body.get('with')
                if isinstance(with_block, dict):
                    for rep in with_block.values():
                        if isinstance(rep, dict) and rep.get('path'):
                            _add_dir(str((yml_path.parent / str(rep['path'])).resolve()))

    return dirs


def set_board_for_app(app_path: str, board: str, dry_run: bool = False, idf_target: str = '') -> None:
    """
    Execute idf.py bmgr (or legacy gen-bmgr-config) for a single application.
    Only executes if the project depends on esp_board_manager component.

    Args:
        app_path: Application path
        board: Target board
        dry_run: Whether to simulate execution only
        idf_target: Chip target (used to add idf.py --preview for preview SoCs)
    """
    if not _is_depending_on_esp_board_manager(app_path, idf_target=idf_target):
        return

    if not board or not board.strip():
        print_info('  Info: --board is empty, skipping bmgr board config')
        return

    idf_env = _idf_env_for_app(app_path)
    if idf_env is not None:
        bmgr_path = idf_env['IDF_EXTRA_ACTIONS_PATH']
        print_info(f'  Info: IDF_EXTRA_ACTIONS_PATH={bmgr_path}')
    else:
        idf_env = os.environ.copy()
    # CI sets IDF_VERSION to tag/branch names like "v5.5.3" or "release/v6.1",
    # which idf_component_tools rejects during esp-bmgr-assist bootstrap.
    # Unsetting it lets esp-bmgr-assist read the numeric version from version.cmake.
    idf_env.pop('IDF_VERSION', None)
    if idf_target:
        idf_env['IDF_TARGET'] = idf_target

    # Prefer the new action name; gen-bmgr-config remains a legacy alias in idf_ext.py.
    cmd = _idf_py_cmd(idf_target, 'bmgr', '-b', board)
    extra_board_dirs = _external_board_component_dirs(app_path)
    if extra_board_dirs:
        cmd.extend(['-c', ';'.join(extra_board_dirs)])
        if _upstream_bmgr_dir():
            idf_env['ESP_BMGR_SKIP_BOOTSTRAP'] = '1'
    _execute_command_for_app(
        app_path, cmd, dry_run, use_warning_for_failure=True, env=idf_env
    )


def main():
    parser = argparse.ArgumentParser(
        description='Batch process applications: get list, set target, configure board',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Process all applications for esp32s3
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3

  # Dry run only, no actual modifications
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --dry-run

  # Specify project path
  python esp_bmgr_pre_build.py --target esp32 --board esp32_s3_korvo2_v3 --project-path /path/to/project

  # Process only applications in test_apps directories
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --target-dir-type test_apps

  # Process only example applications (excluding test_apps)
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --target-dir-type example

  # Skip set-target step
  python esp_bmgr_pre_build.py --target esp32s3 --board esp32_s3_korvo2_v3 --skip-set-target
'''
    )

    parser.add_argument(
        '--target',
        required=True,
        help='Target chip (e.g., esp32, esp32s3, esp32c3, etc.)'
    )

    parser.add_argument(
        '--board',
        required=True,
        help='Target board (e.g., lyrat_mini_v1_1, esp32_s3_korvo2_v3, esp32_s3_korvo2l, etc.)'
    )

    parser.add_argument(
        '--project-path',
        default=None,
        help='Project root path (default: two levels up from script directory)'
    )

    parser.add_argument(
        '--dry-run',
        action='store_true',
        help='Simulate execution without making actual changes'
    )

    parser.add_argument(
        '--skip-set-target',
        action='store_true',
        help='Skip idf.py set-target step'
    )

    parser.add_argument(
        '--parallel-count',
        default=1,
        type=int,
        help='Number of parallel build jobs (default: 1)'
    )

    parser.add_argument(
        '--parallel-index',
        default=1,
        type=int,
        help='1-based index of the parallel build job (default: 1)'
    )

    parser.add_argument(
        '--no-require-pytest',
        action='store_true',
        help='Do not require pytest files in applications'
    )

    parser.add_argument(
        '--target-dir-type',
        choices=[APP_TYPE_ALL, APP_TYPE_EXAMPLE, APP_TYPE_TEST_APPS],
        default=APP_TYPE_ALL,
        help='Application directory type: "all" (default) - all applications, '
             '"test_apps" - only applications in test_apps directories, '
             '"example" - only example applications (excluding test_apps)'
    )

    _preview = parser.add_mutually_exclusive_group()
    _preview.add_argument(
        '--enable-preview-targets',
        dest='enable_preview_targets',
        action='store_true',
        help='Include IDF preview targets when resolving apps (default: on).',
    )
    _preview.add_argument(
        '--disable-preview-targets',
        dest='enable_preview_targets',
        action='store_false',
        help='Restrict app discovery to non-preview IDF targets only.',
    )
    parser.set_defaults(enable_preview_targets=True)

    args = parser.parse_args()

    # Determine project path
    if args.project_path:
        project_path = os.path.abspath(args.project_path)
    else:
        # Default to two levels up from script directory
        script_dir = Path(__file__).parent.absolute()
        project_path = str(script_dir.parent.parent)

    if not os.path.isdir(project_path):
        print_error(f'Error: Project path does not exist: {project_path}')
        sys.exit(1)

    print_info(f'Project path: {project_path}')

    if args.dry_run:
        print_info('\n===== DRY RUN MODE =====')

    print_info(f'Target chip: {args.target}')
    print_info(f'Application type: {args.target_dir_type}\n')

    # Step 1: Get application list
    app_list = get_app_list(
        project_path,
        args.target,
        args.target_dir_type,
        args.no_require_pytest,
        enable_preview_targets=args.enable_preview_targets,
        parallel_count=args.parallel_count,
        parallel_index=args.parallel_index,
    )

    if not app_list:
        print_warning('No applications found, exiting')
        return

    # Process each application: set target chip (optional) and configure board
    idf_bin = 'idf.py --preview' if _target_is_idf_preview(args.target) else 'idf.py'
    if not args.skip_set_target:
        print_info(f'\n[Step 2] Executing idf.py set-target {args.target} and idf.py bmgr -b {args.board} for applications...')
    else:
        print_info(f'\n[Step 2] Executing idf.py bmgr -b {args.board} for applications...')

    for i, app_path in enumerate(app_list, 1):
        print_info(f'\n[{i}/{len(app_list)}] Processing: {app_path}')

        if not args.skip_set_target:
            set_target_for_app(app_path, args.target, args.dry_run)

        set_board_for_app(app_path, args.board, args.dry_run, idf_target=args.target)

    if args.dry_run:
        print_info('\nThis was a dry run. Remove --dry-run to execute actual changes')


if __name__ == '__main__':
    main()
