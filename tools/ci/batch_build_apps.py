#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
#
# SPDX-License-Identifier: Apache-2.0

"""
Batch-build ESP-IDF projects under example/examples and test/test_apps directories.

Features:
  - Iterate IDF versions and (chip, board) pairs from a YAML config file
  - On build failure: write full log to a file and continue with the next project
  - Per-project logs under <batch-dir>/logs/ are renamed to OK_*.log or FAIL_*.log when done
    (<batch-dir> defaults to .batch_build; override via --batch-dir or YAML batch_build_dir)
  - Build timing: per-app duration, per-batch wall/speedup, batch_build_timing_latest.json
  - Board manager: when config has a board, try idf.py bmgr -x/-b first; only if bmgr
    fails and managed_components/espressif__esp_board_manager is missing, fall back to
    set-target (optional bmgr retry after set-target pulls the component)
  - Before each app build: rm -rf build sdkconfig components/gen_bmgr_codes dependencies.lock in the app tree
  - After the run: HTML report under <batch-dir>/logs/ (batch_build_report_latest.html)
  - Optional parallel builds: --jobs N (process pool; file lock per app source tree)
  - On exit or Ctrl+C: cancel pool workers and terminate tracked idf.py/ninja process groups
  - Per IDF version: ./install.sh, pip install esp-bmgr-assist, then source export.sh

Example:
  export PROJECT_PATH=/path/to/esp-gmf
  python tools/ci/batch_build_apps.py -c tools/ci/batch_build_config.esp-gmf.yml

  # List discovered apps without building
  python tools/ci/batch_build_apps.py -c tools/ci/batch_build_config.esp-gmf.yml --list-apps

  # Same IDF + same board: build apps in parallel (4 processes)
  python tools/ci/batch_build_apps.py -c tools/ci/batch_build_config.esp-gmf.yml --jobs 4
"""

from __future__ import annotations

import argparse
import atexit
import html
import json
import logging
import multiprocessing
import os
import re
import shutil
import signal
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor, as_completed
from contextlib import contextmanager
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Dict, Iterable, Iterator, List, Optional, Sequence, Set, Tuple

try:
    import fcntl
except ImportError:  # pragma: no cover
    fcntl = None  # type: ignore

try:
    import yaml
except ImportError as exc:  # pragma: no cover
    raise SystemExit('PyYAML is required: pip install pyyaml') from exc

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_CONFIG = SCRIPT_DIR / 'batch_build_config.esp-gmf.yml'
BMGR_CMD = 'bmgr'

# Directory (relative to project_path) that holds all batch artifacts:
#   <project>/<BATCH_BUILD_DIR>/logs   - per-app logs + HTML report
#   <project>/<BATCH_BUILD_DIR>/out    - CMake build trees
#   <project>/<BATCH_BUILD_DIR>/locks  - flock files for parallel safety
# Override at runtime via `--batch-dir` CLI flag.
BATCH_BUILD_DIR = '.batch_build'

logging.basicConfig(level=logging.INFO, format='%(message)s')
log = logging.getLogger(__name__)

# Parallel pool + idf.py/ninja children (see _shutdown_all_build_workers).
_ACTIVE_EXECUTOR: Optional[ProcessPoolExecutor] = None
_TRACKED_SUBPROC: Optional[object] = None  # multiprocessing Manager list of PIDs
_MP_MANAGER: Optional[multiprocessing.managers.SyncManager] = None
_WORKER_TRACKED_SUBPROC: Optional[object] = None
_SHUTDOWN_LOCK = False


@dataclass
class TargetBoard:
    chip: str
    board: str = ''


@dataclass
class IdfSettings:
    # Run ./install.sh after git checkout (required before export.sh on a fresh/switched IDF tree)
    run_install: bool = True
    # Comma-separated chip list for install.sh; None = derive from targets[].chip
    install_targets: Optional[str] = None
    # pip install esp-bmgr-assist into the active IDF Python venv (after export.sh)
    install_bmgr_assist: bool = True


@dataclass
class BuildSettings:
    log_dir: str = f'{BATCH_BUILD_DIR}/logs'
    # CMake build trees live here (under project_path), not inside each app directory
    build_root: str = f'{BATCH_BUILD_DIR}/out'
    build_dir_pattern: str = '{idf_version}/{target}/{app_id}'
    sdkconfig_defaults: str = 'sdkconfig.defaults.{target}'
    # Parallel app builds per IDF version (1 = serial). Same app tree is locked.
    parallel_jobs: int = 1
    extra_build_args: List[str] = field(default_factory=list)
    # Remove CMake build tree after a successful build to reclaim disk space.
    # Failed build dirs are always kept for debugging.
    keep_build: bool = False


@dataclass
class BatchConfig:
    project_path: Path
    idf_path: Path
    idf_versions: List[str]
    targets: List[TargetBoard]
    idf_tag_flag: bool = True
    # Path to esp_board_manager for IDF_EXTRA_ACTIONS_PATH (idf.py bmgr extension)
    idf_extra_actions_path: Optional[Path] = None
    idf: IdfSettings = field(default_factory=IdfSettings)
    example_dir_names: Set[str] = field(default_factory=lambda: {'example', 'examples'})
    test_dir_names: Set[str] = field(default_factory=lambda: {'test', 'test_apps'})
    exclude_path_segments: Set[str] = field(
        default_factory=lambda: {'managed_components', BATCH_BUILD_DIR}
    )
    build: BuildSettings = field(default_factory=BuildSettings)
    # Resolved batch artifact dir name (relative to project_path); populated from CLI / YAML.
    batch_build_dir: str = BATCH_BUILD_DIR


@dataclass
class BuildResult:
    app_path: str
    idf_version: str
    target: str
    board: str
    group: str
    success: bool
    log_file: Optional[str] = None
    message: str = ''
    duration_sec: float = 0.0
    # True when the app was deliberately skipped (e.g. unsupported target);
    # skipped results do not count toward Pass/Fail in the summary.
    skipped: bool = False


@dataclass
class BatchTimingRecord:
    """Wall-clock vs sum-of-app-time for one (IDF, board, group) batch."""

    idf_version: str
    target: str
    board: str
    group: str
    parallel_jobs: int
    app_count: int
    wall_sec: float
    sum_app_sec: float

    @property
    def speedup(self) -> float:
        if self.wall_sec <= 0:
            return 0.0
        return self.sum_app_sec / self.wall_sec

    @property
    def avg_app_sec(self) -> float:
        if self.app_count <= 0:
            return 0.0
        return self.sum_app_sec / self.app_count


_TIMING_BATCHES: List[BatchTimingRecord] = []
_RUN_WALL_START: Optional[float] = None


@dataclass(frozen=True)
class BuildTask:
    app_path: str
    idf_version: str
    target_chip: str
    target_board: str
    group: str


def _yaml_str_list_field(raw: dict, key: str, default: Sequence[str]) -> List[str]:
    """Load a YAML string list.

    - Key omitted: use *default*.
    - Key present as ``null``, ``[]``, or comment-only block: empty list (skip discovery).
    """
    if key not in raw:
        return list(default)
    value = raw[key]
    if value is None:
        return []
    if isinstance(value, str):
        return [value] if value else []
    return list(value)


def _resolve_config_path(
    raw_value: object,
    *,
    config_path: Path,
    name: str,
    env_var: Optional[str] = None,
    default: Optional[Path] = None,
) -> Path:
    if raw_value is None or str(raw_value).strip() == '':
        env_value = os.environ.get(env_var) if env_var else None
        if env_value:
            raw_value = env_value
        elif default is not None:
            return default.expanduser().resolve()
        else:
            env_hint = f' or export {env_var}' if env_var else ''
            raise ValueError(f'{name} is not set; configure {name}{env_hint}')

    value = os.path.expandvars(str(raw_value)).strip()
    if '$' in value:
        raise ValueError(f'{name} references an unset environment variable: {raw_value}')

    resolved = Path(value).expanduser()
    if not resolved.is_absolute():
        resolved = config_path.parent / resolved
    return resolved.resolve()


def _load_config(path: Path) -> BatchConfig:
    with path.open(encoding='utf-8') as fh:
        raw = yaml.safe_load(fh)

    build_raw = raw.get('build', {}) or {}
    build = BuildSettings(
        log_dir=build_raw.get('log_dir', f'{BATCH_BUILD_DIR}/logs'),
        build_root=build_raw.get('build_root', f'{BATCH_BUILD_DIR}/out'),
        build_dir_pattern=build_raw.get(
            'build_dir_pattern', '{idf_version}/{target}/{app_id}'
        ),
        sdkconfig_defaults=build_raw.get('sdkconfig_defaults', 'sdkconfig.defaults.{target}'),
        parallel_jobs=max(1, int(build_raw.get('parallel_jobs', 1) or 1)),
        extra_build_args=list(build_raw.get('extra_build_args', []) or []),
        keep_build=bool(build_raw.get('keep_build', False)),
    )

    targets = []
    for item in raw.get('targets', []) or []:
        if isinstance(item, str):
            targets.append(TargetBoard(chip=item))
        else:
            targets.append(TargetBoard(chip=item['chip'], board=item.get('board', '') or ''))

    idf_raw = raw.get('idf', {}) or {}
    idf_settings = IdfSettings(
        run_install=bool(idf_raw.get('run_install', True)),
        install_targets=idf_raw.get('install_targets'),
        install_bmgr_assist=bool(idf_raw.get('install_bmgr_assist', True)),
    )

    project_path = _resolve_config_path(
        raw.get('project_path'),
        config_path=path,
        name='project_path',
        env_var='PROJECT_PATH',
        default=path.parent.parent.parent,
    )
    idf_path = _resolve_config_path(
        raw.get('idf_path'),
        config_path=path,
        name='idf_path',
        env_var='IDF_PATH',
    )

    idf_extra_actions_path = raw.get('idf_extra_actions_path')
    if idf_extra_actions_path:
        idf_extra_actions_path = Path(idf_extra_actions_path).expanduser()
        if not idf_extra_actions_path.is_absolute():
            idf_extra_actions_path = (project_path / idf_extra_actions_path).resolve()
        else:
            idf_extra_actions_path = idf_extra_actions_path.resolve()
    else:
        default_bmgr = project_path / 'packages' / 'esp_board_manager'
        idf_extra_actions_path = default_bmgr.resolve() if default_bmgr.is_dir() else None

    return BatchConfig(
        project_path=project_path,
        idf_path=idf_path,
        idf_versions=list(raw.get('idf_versions', []) or []),
        targets=targets,
        idf_tag_flag=bool(raw.get('idf_tag_flag', True)),
        idf_extra_actions_path=idf_extra_actions_path,
        idf=idf_settings,
        example_dir_names=set(_yaml_str_list_field(
            raw, 'example_dir_names', ['example', 'examples', 'gmf_examples'],
        )),
        test_dir_names=set(_yaml_str_list_field(
            raw, 'test_dir_names', ['test', 'test_apps'],
        )),
        exclude_path_segments=set(_yaml_str_list_field(
            raw,
            'exclude_path_segments',
            ['managed_components', BATCH_BUILD_DIR],
        )),
        build=build,
        batch_build_dir=str(raw.get('batch_build_dir', BATCH_BUILD_DIR) or BATCH_BUILD_DIR),
    )


def _should_exclude(path: Path, exclude_segments: Set[str]) -> bool:
    return any(part in exclude_segments for part in path.parts)


def _apply_batch_dir_override(cfg: BatchConfig, new_dir: str) -> None:
    """Repoint all batch artifact paths to *new_dir* (relative to project_path).

    Replaces the leading BATCH_BUILD_DIR component in cfg.build.log_dir /
    cfg.build.build_root if they still match the default prefix; otherwise
    keeps the user's YAML value untouched. Always updates cfg.batch_build_dir
    and adjusts exclude_path_segments to skip the new tree during discovery.
    """
    new_dir = new_dir.strip().rstrip('/').lstrip('./') or BATCH_BUILD_DIR

    def _rewrite(value: str, suffix: str) -> str:
        default = f'{BATCH_BUILD_DIR}/{suffix}'
        if value == default or value.startswith(f'{BATCH_BUILD_DIR}/'):
            tail = value[len(BATCH_BUILD_DIR):]  # keeps leading '/' or ''
            return f'{new_dir}{tail}'
        return value  # respect explicit YAML overrides

    cfg.build.log_dir = _rewrite(cfg.build.log_dir, 'logs')
    cfg.build.build_root = _rewrite(cfg.build.build_root, 'out')

    if BATCH_BUILD_DIR in cfg.exclude_path_segments:
        cfg.exclude_path_segments = (
            cfg.exclude_path_segments - {BATCH_BUILD_DIR}
        ) | {new_dir}
    else:
        cfg.exclude_path_segments = cfg.exclude_path_segments | {new_dir}

    cfg.batch_build_dir = new_dir


def _is_idf_project(directory: Path) -> bool:
    cmake = directory / 'CMakeLists.txt'
    if not cmake.is_file():
        return False
    try:
        content = cmake.read_text(encoding='utf-8', errors='ignore')
    except OSError:
        return False
    if 'project(' not in content:
        return False
    # Typical app layout: project CMakeLists.txt + main/ (or components only for libs)
    return (directory / 'main').is_dir() or (directory / 'components').is_dir()


def discover_apps(root: Path, dir_names: Set[str], exclude_segments: Set[str]) -> List[Path]:
    """Find ESP-IDF application roots under *root* whose path contains *dir_names*."""
    if not dir_names:
        return []

    found: List[Path] = []
    seen: Set[Path] = set()

    for current, dirnames, _ in os.walk(root):
        current_path = Path(current)

        if _should_exclude(current_path, exclude_segments):
            dirnames[:] = []
            continue

        if not _should_exclude(current_path.relative_to(root), dir_names):
            continue

        if not _is_idf_project(current_path):
            continue

        resolved = current_path.resolve()
        if resolved in seen:
            continue
        seen.add(resolved)
        found.append(resolved)
        # Do not descend into sub-projects of an app tree
        dirnames[:] = []

    return sorted(found)


def _resolve_idf_version(idf_path: Path, version_tag: str, tag_flag: bool) -> str:
    """Map config version to a git ref (mirrors tools/ci/utils.sh check_idf_version)."""
    if tag_flag:
        return version_tag

    try:
        result = subprocess.run(
            ['git', 'ls-remote', '--heads', 'origin', f'release/{version_tag}'],
            cwd=idf_path,
            capture_output=True,
            text=True,
            check=False,
        )
        if result.stdout.strip():
            return f'release/{version_tag}'
    except OSError:
        pass
    return version_tag


def checkout_idf(idf_path: Path, version_tag: str, tag_flag: bool) -> str:
    """Checkout ESP-IDF at *version_tag*; return resolved ref name."""
    ref = _resolve_idf_version(idf_path, version_tag, tag_flag)
    log.info('Checking out ESP-IDF: %s -> %s', version_tag, ref)

    if tag_flag:
        tag_check = subprocess.run(
            ['git', 'ls-remote', '--tags', 'origin', f'refs/tags/{ref}'],
            cwd=idf_path,
            capture_output=True,
            text=True,
            check=False,
        )
        if tag_check.stdout.strip():
            subprocess.run(['git', 'fetch', 'origin', 'tag', ref, '--depth', '1'], cwd=idf_path, check=True)
            subprocess.run(['git', 'checkout', ref], cwd=idf_path, check=True)
        else:
            subprocess.run(['git', 'fetch', 'origin', ref, '--depth', '1'], cwd=idf_path, check=True)
            subprocess.run(['git', 'checkout', '-B', ref, f'origin/{ref}'], cwd=idf_path, check=True)
    else:
        subprocess.run(['git', 'fetch', 'origin', ref, '--depth', '1'], cwd=idf_path, check=True)
        subprocess.run(['git', 'checkout', '-B', ref, f'origin/{ref}'], cwd=idf_path, check=True)

    mqtt = idf_path / 'components' / 'mqtt' / 'esp-mqtt'
    if mqtt.is_dir():
        shutil.rmtree(mqtt, ignore_errors=True)
    subprocess.run(
        ['git', 'submodule', 'update', '--init', '--recursive', '--depth', '1'],
        cwd=idf_path,
        check=False,
    )

    return ref


def _clean_idf_activation_env(base: Optional[Dict[str, str]] = None) -> Dict[str, str]:
    """Drop stale IDF activation variables before install.sh / export.sh."""
    env = dict(base or os.environ)
    for key in (
        'IDF_PATH',
        'IDF_PYTHON_ENV_PATH',
        'IDF_TOOLS_EXPORT_CMD',
        'OPENOCD_SCRIPTS',
    ):
        env.pop(key, None)
    return env


def _resolve_install_targets(cfg: BatchConfig) -> Optional[str]:
    if cfg.idf.install_targets:
        return cfg.idf.install_targets
    chips = sorted({t.chip for t in cfg.targets})
    return ','.join(chips) if chips else None


def install_idf(idf_path: Path, targets_csv: Optional[str]) -> None:
    """Run ./install.sh in *idf_path* (must run before export.sh)."""
    install_sh = idf_path / 'install.sh'
    if not install_sh.is_file():
        raise FileNotFoundError(f'install.sh not found: {install_sh}')

    env = _clean_idf_activation_env()
    cmd = ['./install.sh']
    if targets_csv:
        cmd.append(targets_csv)

    log.info('Running: cd %s && %s', idf_path, ' '.join(cmd))
    proc = subprocess.run(
        cmd,
        cwd=idf_path,
        env=env,
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return

    combined = (proc.stderr or '') + (proc.stdout or '')
    if targets_csv and 'not supported' in combined.lower():
        log.warning('install.sh rejected target list %r; retrying ./install.sh', targets_csv)
        proc = subprocess.run(
            ['./install.sh'],
            cwd=idf_path,
            env=env,
            capture_output=True,
            text=True,
        )
        if proc.returncode == 0:
            return

    raise RuntimeError(
        f'install.sh failed (exit {proc.returncode}).\n'
        f'{proc.stderr}\n{proc.stdout}'
    )


def load_idf_env(idf_path: Path, idf_extra_actions_path: Optional[Path] = None) -> Dict[str, str]:
    """Return environment dict after ``. ./export.sh`` (requires install.sh first)."""
    export_sh = idf_path / 'export.sh'
    if not export_sh.is_file():
        raise FileNotFoundError(f'export.sh not found: {export_sh}')

    env = _clean_idf_activation_env()
    env['IDF_PATH'] = str(idf_path.resolve())

    log.info('Running: cd %s && . ./export.sh', idf_path)
    proc = subprocess.run(
        ['bash', '-c', f'source "{export_sh}" && env -0'],
        capture_output=True,
        check=False,
        env=env,
    )
    if proc.returncode != 0:
        raise RuntimeError(
            f'export.sh failed (exit {proc.returncode}). '
            f'Ensure install completed: cd {idf_path} && ./install.sh && . ./export.sh\n'
            f'{proc.stderr}\n{proc.stdout}'
        )

    out_env = _clean_idf_activation_env()
    for entry in proc.stdout.split(b'\0'):
        if not entry or b'=' not in entry:
            continue
        key, _, value = entry.partition(b'=')
        out_env[key.decode()] = value.decode()
    out_env.setdefault('IDF_PATH', str(idf_path.resolve()))

    if idf_extra_actions_path and idf_extra_actions_path.is_dir():
        out_env['IDF_EXTRA_ACTIONS_PATH'] = str(idf_extra_actions_path)
    return out_env


def _idf_python(env: Dict[str, str]) -> str:
    """Python interpreter from the active IDF environment."""
    if env.get('PYTHON'):
        return env['PYTHON']
    idf_env = env.get('IDF_PYTHON_ENV_PATH', '')
    if idf_env:
        candidate = Path(idf_env) / 'bin' / 'python'
        if candidate.is_file():
            return str(candidate)
    found = shutil.which('python3') or shutil.which('python')
    return found or sys.executable


def _pip_install_esp_bmgr_assist(env: Dict[str, str]) -> Tuple[bool, str]:
    python = _idf_python(env)
    log.info('Running: %s -m pip install -U esp-bmgr-assist', python)
    proc = subprocess.run(
        [python, '-m', 'pip', 'install', '-U', 'esp-bmgr-assist'],
        env=env,
        capture_output=True,
        text=True,
    )
    if proc.returncode == 0:
        return True, ''
    return False, (proc.stderr or '') + (proc.stdout or '')


def _activate_board_manager_component(env: Dict[str, str], bmgr_path: Path) -> None:
    """
    Manual activation when esp-bmgr-assist is unavailable (esp-board-manager README:
    export IDF_EXTRA_ACTIONS_PATH=<local esp_board_manager>).
    """
    if not bmgr_path.is_dir():
        raise FileNotFoundError(f'esp_board_manager not found: {bmgr_path}')
    env['IDF_EXTRA_ACTIONS_PATH'] = str(bmgr_path.resolve())
    log.info('Set IDF_EXTRA_ACTIONS_PATH=%s', env['IDF_EXTRA_ACTIONS_PATH'])


def _cleanup_all_project_apps(cfg: BatchConfig) -> None:
    """rm -rf build sdkconfig components/gen_bmgr_codes dependencies.lock for every discovered app."""
    apps: List[Path] = []
    seen: Set[Path] = set()
    for dir_names in (cfg.example_dir_names, cfg.test_dir_names):
        for app in discover_apps(cfg.project_path, dir_names, cfg.exclude_path_segments):
            resolved = app.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            apps.append(resolved)
    log.info('Cleaning board-manager artifacts in %d app(s)', len(apps))
    for app in apps:
        _cleanup_app_tree(app)


def install_esp_bmgr_assist(env: Dict[str, str], cfg: BatchConfig) -> None:
    """
    Install esp-bmgr-assist; on failure clean app trees, set IDF_EXTRA_ACTIONS_PATH, retry once.
    """
    ok, err = _pip_install_esp_bmgr_assist(env)
    if ok:
        log.info('esp-bmgr-assist installed/updated in IDF Python environment')
        return

    log.warning(
        'esp-bmgr-assist install failed; cleaning app trees and using manual component activation'
    )
    _cleanup_all_project_apps(cfg)

    bmgr_path = cfg.idf_extra_actions_path
    if not bmgr_path:
        raise RuntimeError(
            'esp-bmgr-assist install failed and idf_extra_actions_path is not configured\n' + err
        )
    _activate_board_manager_component(env, bmgr_path)

    ok, err = _pip_install_esp_bmgr_assist(env)
    if ok:
        log.info('esp-bmgr-assist installed on retry after manual activation')
        return

    raise RuntimeError(
        'esp-bmgr-assist install failed after cleanup and IDF_EXTRA_ACTIONS_PATH activation.\n'
        f'IDF_EXTRA_ACTIONS_PATH={env.get("IDF_EXTRA_ACTIONS_PATH")}\n{err}'
    )


def prepare_idf_environment(
    cfg: BatchConfig,
    *,
    skip_install: bool = False,
    skip_bmgr_assist: bool = False,
) -> Dict[str, str]:
    """Checkout is done separately; this runs install.sh + export.sh + esp-bmgr-assist."""
    if cfg.idf.run_install and not skip_install:
        install_idf(cfg.idf_path, _resolve_install_targets(cfg))
    env = load_idf_env(cfg.idf_path, cfg.idf_extra_actions_path)
    if cfg.idf.install_bmgr_assist and not skip_bmgr_assist:
        install_esp_bmgr_assist(env, cfg)
    return env


def _target_is_idf_preview(target: str) -> bool:
    try:
        from esp_bool_parser import PREVIEW_TARGETS

        return target in PREVIEW_TARGETS
    except Exception:
        return target in {'esp32p4', 'esp32s31'}


def _idf_py_argv(env: Dict[str, str], target: str) -> List[str]:
    """Resolve idf.py from IDF_PATH (required for nohup/non-interactive shells)."""
    idf_root = env.get('IDF_PATH', '')
    if idf_root:
        idf_py = Path(idf_root) / 'tools' / 'idf.py'
        if idf_py.is_file():
            base = [str(idf_py)]
        else:
            base = [shutil.which('idf.py') or 'idf.py']
    else:
        base = [shutil.which('idf.py') or 'idf.py']

    if _target_is_idf_preview(target):
        return base + ['--preview']
    return base


def _log_file_path(
    log_root: Path,
    idf_version: str,
    target: str,
    board: str,
    group: str,
    app_path: Path,
) -> Path:
    """Return the log file path (no OK/FAIL prefix until :func:`_finalize_log_path`).

    The caller is responsible for creating the parent directory before writing.
    """
    safe_ver = re.sub(r'[^\w.\-]+', '_', idf_version)
    safe_target = re.sub(r'[^\w.\-]+', '_', target)
    safe_board = re.sub(r'[^\w.\-]+', '_', board or 'no_board')
    rel = app_path.name
    parent = app_path.parent.name
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    name = f'{safe_ver}_{safe_target}_{safe_board}_{group}_{parent}_{rel}_{stamp}.log'
    return log_root / safe_ver / safe_target / group / name


def _finalize_log_path(log_path: Path, success: bool, *, skipped: bool = False) -> Path:
    """Rename log file so the basename starts with OK_ / FAIL_ / SKIP_."""
    if skipped:
        status = 'SKIP'
    else:
        status = 'OK' if success else 'FAIL'
    stem = log_path.name
    for prefix in ('OK_', 'FAIL_', 'SKIP_'):
        if stem.startswith(prefix):
            stem = stem[len(prefix):]
            break
    final = log_path.with_name(f'{status}_{stem}')
    if final == log_path:
        return log_path
    if log_path.is_file():
        log_path.rename(final)
    elif not final.is_file():
        final.write_text('', encoding='utf-8')
    return final


# --- Target support detection ----------------------------------------------
#
# Honor the IDF-idiomatic mechanisms for declaring which chips an app supports:
#   1. set(SUPPORTED_TARGETS ...) in the project CMakeLists.txt
#   2. `targets:` field in <app>/idf_component.yml
#   3. `targets:` field in each local override_path component referenced by
#      <app>/main/idf_component.yml (e.g. esp_bt_audio declares [esp32, esp32s31])
#
# When any of these excludes the current chip, the app is auto-SKIPped before
# launching idf.py, instead of failing deep inside CMake / component manager.

_SUPPORTED_TARGETS_RE = re.compile(
    r'^\s*set\s*\(\s*SUPPORTED_TARGETS\s+([^)]*)\)',
    re.IGNORECASE | re.MULTILINE,
)


def _read_targets_from_component_yml(yml_path: Path) -> Optional[Set[str]]:
    """Return `targets:` from an idf_component.yml as a set; None if absent."""
    if not yml_path.is_file():
        return None
    try:
        with yml_path.open(encoding='utf-8') as fh:
            data = yaml.safe_load(fh) or {}
    except (yaml.YAMLError, OSError):
        return None
    raw = data.get('targets')
    if not raw:
        return None
    if isinstance(raw, str):
        return {raw.strip()} if raw.strip() else None
    if isinstance(raw, list):
        items = {str(t).strip() for t in raw if str(t).strip()}
        return items or None
    return None


def _read_supported_targets_from_cmake(cmake_path: Path) -> Optional[Set[str]]:
    """Parse `set(SUPPORTED_TARGETS ...)` from a CMakeLists.txt; None if absent."""
    if not cmake_path.is_file():
        return None
    try:
        text = cmake_path.read_text(encoding='utf-8', errors='replace')
    except OSError:
        return None
    match = _SUPPORTED_TARGETS_RE.search(text)
    if not match:
        return None
    tokens = re.split(r'[\s;]+', match.group(1).strip())
    items = {t.strip('"\'') for t in tokens if t.strip(' "\'')}
    return items or None


def _app_supported_targets(app_path: Path) -> Optional[Set[str]]:
    """Discover the set of supported chips for *app_path*.

    Combines constraints from:
      - <app>/CMakeLists.txt set(SUPPORTED_TARGETS ...)
      - <app>/idf_component.yml `targets:`
      - <app>/main/idf_component.yml override_path deps -> their idf_component.yml `targets:`

    All discovered sets are intersected. Returns None if no constraint found.
    """
    constraints: List[Set[str]] = []

    cmake = _read_supported_targets_from_cmake(app_path / 'CMakeLists.txt')
    if cmake is not None:
        constraints.append(cmake)

    top_yml = _read_targets_from_component_yml(app_path / 'idf_component.yml')
    if top_yml is not None:
        constraints.append(top_yml)

    main_yml = app_path / 'main' / 'idf_component.yml'
    if main_yml.is_file():
        try:
            with main_yml.open(encoding='utf-8') as fh:
                manifest = yaml.safe_load(fh) or {}
        except (yaml.YAMLError, OSError):
            manifest = {}
        deps = manifest.get('dependencies') or {}
        if isinstance(deps, dict):
            for spec in deps.values():
                if not isinstance(spec, dict):
                    continue
                override = spec.get('override_path')
                if not override:
                    continue
                local_yml = (main_yml.parent / str(override)).resolve() / 'idf_component.yml'
                local_targets = _read_targets_from_component_yml(local_yml)
                if local_targets is not None:
                    constraints.append(local_targets)

    if not constraints:
        return None
    return set.intersection(*constraints)


def _record_skip(
    task: BuildTask,
    log_root: Path,
    reason: str,
) -> BuildResult:
    """Materialise a SKIP BuildResult, writing a SKIP_*.log with *reason*."""
    log_file = _log_file_path(
        log_root,
        task.idf_version,
        task.target_chip,
        task.target_board,
        task.group,
        Path(task.app_path),
    )
    log_file.parent.mkdir(parents=True, exist_ok=True)
    log_file.write_text(f'SKIPPED: {reason}\n', encoding='utf-8')
    log_file = _finalize_log_path(log_file, success=False, skipped=True)
    return BuildResult(
        app_path=task.app_path,
        idf_version=task.idf_version,
        target=task.target_chip,
        board=task.target_board,
        group=task.group,
        success=False,
        log_file=str(log_file),
        message=reason,
        duration_sec=0.0,
        skipped=True,
    )


def _partition_tasks_by_target_support(
    tasks: List[BuildTask],
    log_root: Path,
) -> Tuple[List[BuildTask], List[BuildResult]]:
    """Split tasks into (to_build, skipped_results) based on target restrictions."""
    to_build: List[BuildTask] = []
    skipped: List[BuildResult] = []
    for task in tasks:
        supported = _app_supported_targets(Path(task.app_path))
        if supported is None or task.target_chip in supported:
            to_build.append(task)
            continue
        reason = (
            f'target {task.target_chip!r} not in declared supported targets '
            f'{sorted(supported)} (CMakeLists.txt SUPPORTED_TARGETS or '
            f'idf_component.yml targets)'
        )
        skipped.append(_record_skip(task, log_root, reason))
    return to_build, skipped


def _pool_worker_init(tracked_pids: object) -> None:
    global _WORKER_TRACKED_SUBPROC
    _WORKER_TRACKED_SUBPROC = tracked_pids


def _track_subprocess_pid(pid: int) -> None:
    if _WORKER_TRACKED_SUBPROC is not None:
        _WORKER_TRACKED_SUBPROC.append(pid)
    elif _TRACKED_SUBPROC is not None:
        _TRACKED_SUBPROC.append(pid)


def _kill_process_tree(pid: int, *, sig: int = signal.SIGTERM) -> None:
    if pid <= 0:
        return
    try:
        os.killpg(os.getpgid(pid), sig)
    except (ProcessLookupError, PermissionError):
        try:
            os.kill(pid, sig)
        except ProcessLookupError:
            pass


def _terminate_tracked_subprocesses() -> None:
    if _TRACKED_SUBPROC is None:
        return
    for pid in list(_TRACKED_SUBPROC):
        _kill_process_tree(int(pid), sig=signal.SIGTERM)
    deadline = time.monotonic() + 5.0
    for pid in list(_TRACKED_SUBPROC):
        while time.monotonic() < deadline:
            try:
                os.kill(int(pid), 0)
            except ProcessLookupError:
                break
            time.sleep(0.1)
        else:
            _kill_process_tree(int(pid), sig=signal.SIGKILL)
    try:
        _TRACKED_SUBPROC[:] = []
    except TypeError:
        pass


def _teardown_executor(executor: ProcessPoolExecutor, *, wait: bool, cancel: bool) -> None:
    """Shutdown a ProcessPoolExecutor and forcibly terminate its worker processes."""
    try:
        executor.shutdown(wait=wait, cancel_futures=cancel)
    except Exception as exc:
        log.warning('ProcessPoolExecutor.shutdown: %s', exc)
    _terminate_executor_processes(executor, wait=wait)


def _cleanup_mp_manager() -> None:
    """Shutdown the multiprocessing Manager and clear the shared PID list."""
    global _MP_MANAGER, _TRACKED_SUBPROC
    if _MP_MANAGER is not None:
        try:
            _MP_MANAGER.shutdown()
        except Exception:
            pass
        _MP_MANAGER = None
    _TRACKED_SUBPROC = None


def _terminate_executor_processes(executor: ProcessPoolExecutor, *, wait: bool) -> None:
    # After shutdown(), _processes may be None.
    procs_map = getattr(executor, '_processes', None) or {}
    processes = list(procs_map.values())
    if not processes:
        return
    for proc in processes:
        if proc.is_alive():
            proc.terminate()
    if wait:
        for proc in processes:
            proc.join()
        return
    deadline = time.monotonic() + 8.0
    for proc in processes:
        remaining = max(0.0, deadline - time.monotonic())
        proc.join(timeout=remaining)
        if proc.is_alive():
            proc.kill()
            proc.join(timeout=2.0)


def _shutdown_all_build_workers(
    *,
    wait: bool = False,
    cancel_pending: bool = True,
) -> None:
    """Stop ProcessPoolExecutor workers and tracked idf.py/ninja process groups."""
    global _ACTIVE_EXECUTOR, _SHUTDOWN_LOCK, _MP_MANAGER, _TRACKED_SUBPROC
    if _SHUTDOWN_LOCK:
        return
    _SHUTDOWN_LOCK = True

    executor = _ACTIVE_EXECUTOR
    _ACTIVE_EXECUTOR = None

    if executor is not None:
        log.info('Shutting down build worker pool (wait=%s, cancel=%s)...', wait, cancel_pending)
        _teardown_executor(executor, wait=wait, cancel=cancel_pending)

    _terminate_tracked_subprocesses()
    _cleanup_mp_manager()


def _install_shutdown_handlers() -> None:
    def _on_signal(signum: int, _frame: object) -> None:
        log.warning('Signal %s: stopping all build jobs...', signum)
        _shutdown_all_build_workers(wait=False, cancel_pending=True)
        raise KeyboardInterrupt

    signal.signal(signal.SIGINT, _on_signal)
    signal.signal(signal.SIGTERM, _on_signal)
    atexit.register(
        lambda: _shutdown_all_build_workers(wait=False, cancel_pending=True),
    )


def _run_cmd(
    cmd: Sequence[str],
    *,
    cwd: Path,
    env: Dict[str, str],
    log_path: Path,
    label: str,
    append_log: bool = False,
) -> Tuple[bool, str]:
    header = f'=== {label} ===\n$ {" ".join(cmd)}\n(cwd: {cwd})\n\n'
    proc = subprocess.Popen(
        list(cmd),
        cwd=str(cwd),
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        start_new_session=True,
    )
    _track_subprocess_pid(proc.pid)
    try:
        stdout, stderr = proc.communicate()
    except KeyboardInterrupt:
        _kill_process_tree(proc.pid)
        proc.wait(timeout=5)
        raise
    if proc.poll() is None:
        _kill_process_tree(proc.pid)
        proc.wait(timeout=5)

    body = stdout or ''
    if stderr:
        body += '\n--- stderr ---\n' + stderr
    body += f'\n\n=== exit code: {proc.returncode} ===\n'
    block = header + body
    if append_log and log_path.is_file():
        with log_path.open('a', encoding='utf-8') as fh:
            fh.write('\n' + block)
    else:
        log_path.write_text(block, encoding='utf-8')
    ok = proc.returncode == 0
    summary = 'OK' if ok else f'failed (exit {proc.returncode})'
    return ok, summary


def _managed_board_manager_dir(app_path: Path) -> Path:
    return app_path / 'managed_components' / 'espressif__esp_board_manager'


def _has_managed_board_manager(app_path: Path) -> bool:
    """Registry-managed esp_board_manager under the app (not override_path-only clones)."""
    return _managed_board_manager_dir(app_path).is_dir()


def _cleanup_app_tree(app_path: Path) -> None:
    """rm -rf build sdkconfig components/gen_bmgr_codes dependencies.lock under the app.

    dependencies.lock is removed so the IDF component manager always re-resolves
    against the current registry; otherwise stale locks may pin transitive deps
    (e.g. esp_board_manager) to old versions that lack the requested board.
    """
    for rel in ('build', 'sdkconfig', 'components/gen_bmgr_codes', 'dependencies.lock'):
        path = app_path / rel
        if not path.exists():
            continue
        if path.is_dir():
            shutil.rmtree(path, ignore_errors=True)
        else:
            path.unlink(missing_ok=True)
        log.info('Removed %s', path)


def _board_manager_defaults_path(app_path: Path) -> Path:
    return app_path / 'components' / 'gen_bmgr_codes' / 'board_manager.defaults'


def _sdkconfig_defaults_for_build(
    app_path: Path,
    target: str,
    cfg: BatchConfig,
    *,
    bmgr_applied: bool,
) -> Optional[str]:
    """
    SDKCONFIG_DEFAULTS list (CMake ';' separator).

    After bmgr -b, board selection lives in components/gen_bmgr_codes/board_manager.defaults
    (mdc: do not rely on legacy CONFIG_ESP32_*_BOARD selectors in sdkconfig).
    """
    names: List[str] = []
    bmgr_defaults = _board_manager_defaults_path(app_path)
    if bmgr_applied and bmgr_defaults.is_file():
        names.append(bmgr_defaults.relative_to(app_path).as_posix())

    project_defaults = cfg.build.sdkconfig_defaults.format(target=target)
    if (app_path / project_defaults).is_file():
        rel = project_defaults
        if rel not in names:
            names.append(rel)

    if not bmgr_applied and bmgr_defaults.is_file():
        rel = bmgr_defaults.relative_to(app_path).as_posix()
        if rel not in names:
            names.append(rel)

    if not names:
        return None
    return ';'.join(names)


def _sanitize_path_token(value: str) -> str:
    return re.sub(r'[^\w.\-]+', '_', value)


def _app_build_slug(app_path: Path, project_root: Path) -> str:
    """Directory name for an app under build_root (path relative to repo, slashes -> _)."""
    try:
        rel = app_path.resolve().relative_to(project_root.resolve())
        return _sanitize_path_token(rel.as_posix().replace('/', '_'))
    except ValueError:
        return _sanitize_path_token(app_path.name)


def _resolve_build_dir(
    app_path: Path,
    cfg: BatchConfig,
    idf_version: str,
    target_chip: str,
) -> Path:
    """Absolute CMake build directory outside the app source tree."""
    app_id = _app_build_slug(app_path, cfg.project_path)
    rel = cfg.build.build_dir_pattern.format(
        idf_version=_sanitize_path_token(idf_version),
        target=target_chip,
        app_id=app_id,
    )
    return (cfg.project_path / cfg.build.build_root / rel).resolve()


def _reset_build_dir(build_dir: Path) -> None:
    """Fresh build dir per matrix cell (avoids stale SDKCONFIG_DEFAULTS in CMakeCache)."""
    if build_dir.is_dir():
        shutil.rmtree(build_dir, ignore_errors=True)


def _bmgr_clean_cmd(idf_base: List[str], build_dir_args: List[str]) -> Tuple[List[str], str]:
    return list(idf_base) + build_dir_args + [BMGR_CMD, '-x'], f'{BMGR_CMD} -x'


def _bmgr_generate_cmd(board: str, idf_base: List[str], build_dir_args: List[str]) -> Tuple[List[str], str]:
    return list(idf_base) + build_dir_args + [BMGR_CMD, '-b', board], f'{BMGR_CMD} -b'


def _run_board_manager_prebuild(
    app_path: Path,
    board: str,
    env: Dict[str, str],
    log_path: Path,
    *,
    append_log: bool,
    idf_base: List[str],
    build_dir_args: List[str],
) -> Tuple[bool, str]:
    """esp-board-manager.mdc: bmgr -x then bmgr -b (summary reports bmgr -b only)."""
    first_append = append_log

    clean_cmd, clean_label = _bmgr_clean_cmd(idf_base, build_dir_args)
    ok, _ = _run_cmd(
        clean_cmd,
        cwd=app_path,
        env=env,
        log_path=log_path,
        label=clean_label,
        append_log=first_append,
    )
    if not ok:
        return False, 'failed (bmgr -x)'

    gen_cmd, gen_label = _bmgr_generate_cmd(board, idf_base, build_dir_args)
    ok, msg = _run_cmd(
        gen_cmd,
        cwd=app_path,
        env=env,
        log_path=log_path,
        label=gen_label,
        append_log=True,
    )
    return ok, msg


def _run_set_target(
    app_path: Path,
    chip: str,
    env: Dict[str, str],
    log_path: Path,
    *,
    append_log: bool,
    idf_base: List[str],
    build_dir_args: List[str],
) -> Tuple[bool, str]:
    return _run_cmd(
        idf_base + build_dir_args + ['set-target', chip],
        cwd=app_path,
        env=env,
        log_path=log_path,
        label='set-target',
        append_log=append_log,
    )


def _configure_target_and_board(
    app_path: Path,
    target: TargetBoard,
    env: Dict[str, str],
    log_path: Path,
    *,
    idf_base: List[str],
    build_dir_args: List[str],
) -> Tuple[bool, List[str], Optional[str]]:
    """
    Try bmgr when board is set; fall back to set-target only if bmgr fails and
    managed_components/espressif__esp_board_manager does not exist.

    Returns (bmgr_applied, messages, error_message). error_message is set on hard failure.
    """
    messages: List[str] = []

    if not target.board:
        ok, msg = _run_set_target(
            app_path, target.chip, env, log_path, append_log=False,
            idf_base=idf_base, build_dir_args=build_dir_args,
        )
        messages.append(f'set-target: {msg}')
        if not ok:
            return False, messages, msg
        return False, messages, None

    log.info('Trying board manager: bmgr -x/-b %s', target.board)
    ok, msg = _run_board_manager_prebuild(
        app_path,
        target.board,
        env,
        log_path,
        append_log=False,
        idf_base=idf_base,
        build_dir_args=build_dir_args,
    )
    if ok:
        messages.append(f'bmgr -b: {msg}')
        return True, messages, None

    if _has_managed_board_manager(app_path):
        messages.append(f'bmgr -b: {msg}')
        return False, messages, (
            f'bmgr -b failed ({_managed_board_manager_dir(app_path)} present): {msg}'
        )

    log.info(
        'bmgr failed and %s missing; project does not use registry board manager yet',
        _managed_board_manager_dir(app_path),
    )
    ok, st_msg = _run_set_target(
        app_path, target.chip, env, log_path, append_log=True,
        idf_base=idf_base, build_dir_args=build_dir_args,
    )
    if not ok:
        messages.append(f'set-target: {st_msg}')
        return False, messages, st_msg

    if not _has_managed_board_manager(app_path):
        messages.append(f'set-target: {st_msg}')
        return False, messages, None

    log.info('board manager component present after set-target; retrying bmgr -x/-b')
    ok, msg = _run_board_manager_prebuild(
        app_path,
        target.board,
        env,
        log_path,
        append_log=True,
        idf_base=idf_base,
        build_dir_args=build_dir_args,
    )
    messages.append(f'bmgr -b: {msg}')
    if not ok:
        return False, messages, msg
    return True, messages, None


@contextmanager
def _app_source_lock(
    app_path: Path,
    project_path: Path,
    batch_build_dir: str = BATCH_BUILD_DIR,
) -> Iterator[None]:
    """One build at a time per app source tree (sdkconfig, managed_components, bmgr)."""
    if fcntl is None:
        yield
        return
    lock_dir = project_path / batch_build_dir / 'locks'
    lock_dir.mkdir(parents=True, exist_ok=True)
    lock_file = lock_dir / f'{_app_build_slug(app_path, project_path)}.lock'
    with open(lock_file, 'w', encoding='utf-8') as fh:
        fcntl.flock(fh.fileno(), fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(fh.fileno(), fcntl.LOCK_UN)


def build_one_app(
    app_path: Path,
    *,
    idf_version: str,
    target: TargetBoard,
    group: str,
    env: Dict[str, str],
    cfg: BatchConfig,
) -> BuildResult:
    with _app_source_lock(app_path, cfg.project_path, cfg.batch_build_dir):
        return _build_one_app_unlocked(
            app_path,
            idf_version=idf_version,
            target=target,
            group=group,
            env=env,
            cfg=cfg,
        )


def _build_one_app_unlocked(
    app_path: Path,
    *,
    idf_version: str,
    target: TargetBoard,
    group: str,
    env: Dict[str, str],
    cfg: BatchConfig,
) -> BuildResult:
    t0 = time.monotonic()
    log_root = (cfg.project_path / cfg.build.log_dir).resolve()
    log_file = _log_file_path(
        log_root,
        idf_version,
        target.chip,
        target.board,
        group,
        app_path,
    )
    log_file.parent.mkdir(parents=True, exist_ok=True)

    build_dir = _resolve_build_dir(app_path, cfg, idf_version, target.chip)

    _cleanup_app_tree(app_path)

    idf_base = _idf_py_argv(env, target.chip)
    build_dir_args = ['-B', str(build_dir)]

    build_dir.parent.mkdir(parents=True, exist_ok=True)
    _reset_build_dir(build_dir)
    log.info('CMake build dir: %s', build_dir)

    bmgr_applied, messages, fatal = _configure_target_and_board(
        app_path,
        target,
        env,
        log_file,
        idf_base=idf_base,
        build_dir_args=build_dir_args,
    )
    if fatal is not None:
        log_file = _finalize_log_path(log_file, False)
        return BuildResult(
            str(app_path), idf_version, target.chip, target.board, group,
            False, str(log_file), '; '.join(messages),
            time.monotonic() - t0,
        )

    sdkconfig_defaults = _sdkconfig_defaults_for_build(
        app_path, target.chip, cfg, bmgr_applied=bmgr_applied,
    )

    build_cmd = list(idf_base)
    build_cmd += build_dir_args
    build_cmd += ['build']
    build_cmd += list(cfg.build.extra_build_args)

    build_env = env
    if sdkconfig_defaults:
        # Semicolon-separated list via env; -D treats ':' as path chars.
        build_env = {**env, 'SDKCONFIG_DEFAULTS': sdkconfig_defaults}

    ok, msg = _run_cmd(
        build_cmd,
        cwd=app_path,
        env=build_env,
        log_path=log_file,
        label='build',
        append_log=True,
    )
    messages.append(f'build: {msg}')

    log_file = _finalize_log_path(log_file, ok)
    if ok and not cfg.build.keep_build:
        shutil.rmtree(build_dir, ignore_errors=True)
    return BuildResult(
        str(app_path),
        idf_version,
        target.chip,
        target.board,
        group,
        ok,
        str(log_file),
        '; '.join(messages),
        time.monotonic() - t0,
    )


def _format_duration(seconds: float) -> str:
    if seconds < 0:
        seconds = 0.0
    if seconds < 60:
        return f'{seconds:.1f}s'
    minutes, sec = divmod(int(seconds), 60)
    if minutes < 60:
        return f'{minutes}m {sec}s'
    hours, minutes = divmod(minutes, 60)
    return f'{hours}h {minutes}m {sec}s'


def _record_batch_timing(
    *,
    idf_version: str,
    target: TargetBoard,
    group: str,
    parallel_jobs: int,
    results: List[BuildResult],
    wall_sec: float,
) -> BatchTimingRecord:
    sum_app = sum(r.duration_sec for r in results)
    rec = BatchTimingRecord(
        idf_version=idf_version,
        target=target.chip,
        board=target.board,
        group=group,
        parallel_jobs=parallel_jobs,
        app_count=len(results),
        wall_sec=wall_sec,
        sum_app_sec=sum_app,
    )
    _TIMING_BATCHES.append(rec)
    mode = f'parallel x{parallel_jobs}' if parallel_jobs > 1 else 'serial'
    log.info(
        'Timing [%s] IDF=%s %s/%s group=%s apps=%d: wall=%s sum_app=%s speedup=%.2fx avg/app=%s',
        mode,
        idf_version,
        target.chip,
        target.board or '(none)',
        group,
        rec.app_count,
        _format_duration(wall_sec),
        _format_duration(sum_app),
        rec.speedup,
        _format_duration(rec.avg_app_sec),
    )
    return rec


def _write_timing_json(
    results: List[BuildResult],
    cfg: BatchConfig,
    *,
    total_wall_sec: float,
    configured_jobs: int,
) -> Path:
    log_root = (cfg.project_path / cfg.build.log_dir).resolve()
    log_root.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    path = log_root / f'batch_build_timing_jobs{configured_jobs}_{stamp}.json'
    latest = log_root / 'batch_build_timing_latest.json'

    batches = [
        {
            'idf_version': b.idf_version,
            'target': b.target,
            'board': b.board,
            'group': b.group,
            'parallel_jobs': b.parallel_jobs,
            'app_count': b.app_count,
            'wall_sec': round(b.wall_sec, 3),
            'sum_app_sec': round(b.sum_app_sec, 3),
            'speedup': round(b.speedup, 3),
            'avg_app_sec': round(b.avg_app_sec, 3),
        }
        for b in _TIMING_BATCHES
    ]
    sum_all_app = sum(r.duration_sec for r in results)
    overall_speedup = sum_all_app / total_wall_sec if total_wall_sec > 0 else 0.0

    payload = {
        'generated_at': datetime.now().isoformat(timespec='seconds'),
        'configured_parallel_jobs': configured_jobs,
        'total_wall_sec': round(total_wall_sec, 3),
        'sum_app_sec': round(sum_all_app, 3),
        'overall_speedup': round(overall_speedup, 3),
        'batches': batches,
        'apps': [
            {
                'app_path': r.app_path,
                'idf_version': r.idf_version,
                'target': r.target,
                'board': r.board,
                'group': r.group,
                'success': r.success,
                'duration_sec': round(r.duration_sec, 3),
            }
            for r in results
        ],
    }
    text = json.dumps(payload, indent=2, ensure_ascii=False) + '\n'
    path.write_text(text, encoding='utf-8')
    latest.write_text(text, encoding='utf-8')
    return path


def _print_timing_summary(results: List[BuildResult], *, configured_jobs: int) -> None:
    if not results and not _TIMING_BATCHES:
        return

    total_wall = 0.0
    if _RUN_WALL_START is not None:
        total_wall = time.monotonic() - _RUN_WALL_START
    sum_app = sum(r.duration_sec for r in results)
    overall = sum_app / total_wall if total_wall > 0 else 0.0

    log.info('\n========== Build timing ==========')
    log.info(
        'Configured jobs: %d  Total wall: %s  Sum of per-app time: %s  Overall speedup: %.2fx',
        configured_jobs,
        _format_duration(total_wall),
        _format_duration(sum_app),
        overall,
    )
    if _TIMING_BATCHES:
        log.info('Per-batch (wall = elapsed for whole group; sum_app = sum of each app):')
        for b in _TIMING_BATCHES:
            mode = f'jobs={b.parallel_jobs}' if b.parallel_jobs > 1 else 'serial'
            log.info(
                '  [%s] IDF=%s %s/%s %s: %d apps wall=%s sum_app=%s speedup=%.2fx',
                mode,
                b.idf_version,
                b.target,
                b.board or '-',
                b.group,
                b.app_count,
                _format_duration(b.wall_sec),
                _format_duration(b.sum_app_sec),
                b.speedup,
            )
    durations = [r.duration_sec for r in results if r.duration_sec > 0]
    if durations:
        log.info(
            'Per-app: min=%s max=%s median=%s',
            _format_duration(min(durations)),
            _format_duration(max(durations)),
            _format_duration(sorted(durations)[len(durations) // 2]),
        )


def _app_display_name(app_path: Path, project_root: Path) -> str:
    try:
        return app_path.resolve().relative_to(project_root.resolve()).as_posix()
    except (OSError, ValueError):
        return str(app_path)


def _log_href(report_path: Path, log_file: Optional[str]) -> str:
    if not log_file:
        return ''
    log_path = Path(log_file)
    if not log_path.is_file():
        return html.escape(log_file)
    try:
        rel = log_path.resolve().relative_to(report_path.resolve().parent)
        return html.escape(rel.as_posix())
    except ValueError:
        return html.escape(log_path.as_uri())


def _write_html_report(results: List[BuildResult], cfg: BatchConfig) -> Path:
    """Write batch_build_report_<timestamp>.html under cfg.build.log_dir."""
    log_root = (cfg.project_path / cfg.build.log_dir).resolve()
    log_root.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime('%Y%m%d_%H%M%S')
    report_path = log_root / f'batch_build_report_{stamp}.html'
    latest_path = log_root / 'batch_build_report_latest.html'

    skipped = [r for r in results if r.skipped]
    passed = [r for r in results if r.success]
    failed = [r for r in results if not r.success and not r.skipped]
    generated_at = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

    total_wall = 0.0
    if _RUN_WALL_START is not None:
        total_wall = time.monotonic() - _RUN_WALL_START
    sum_app = sum(r.duration_sec for r in results)
    overall_speedup = sum_app / total_wall if total_wall > 0 else 0.0

    rows: List[str] = []
    for r in results:
        if r.skipped:
            status = 'SKIP'
            row_class = 'skip'
        elif r.success:
            status = 'PASS'
            row_class = 'pass'
        else:
            status = 'FAIL'
            row_class = 'fail'
        app_name = html.escape(_app_display_name(Path(r.app_path), cfg.project_path))
        log_rel = _log_href(report_path, r.log_file)
        log_cell = (
            f'<a href="{log_rel}">{log_rel}</a>' if log_rel and Path(r.log_file or '').is_file() else '—'
        )
        rows.append(
            f'<tr class="{row_class}">'
            f'<td class="status">{status}</td>'
            f'<td class="app">{app_name}</td>'
            f'<td>{html.escape(r.idf_version)}</td>'
            f'<td>{html.escape(r.target)}</td>'
            f'<td>{html.escape(r.board or "—")}</td>'
            f'<td>{html.escape(r.group)}</td>'
            f'<td class="duration">{html.escape(_format_duration(r.duration_sec))}</td>'
            f'<td class="steps">{html.escape(r.message)}</td>'
            f'<td class="log">{log_cell}</td>'
            f'</tr>'
        )

    timing_rows: List[str] = []
    for b in _TIMING_BATCHES:
        mode = f'parallel ×{b.parallel_jobs}' if b.parallel_jobs > 1 else 'serial'
        timing_rows.append(
            f'<tr>'
            f'<td>{html.escape(mode)}</td>'
            f'<td>{html.escape(b.idf_version)}</td>'
            f'<td>{html.escape(b.target)}</td>'
            f'<td>{html.escape(b.board or "—")}</td>'
            f'<td>{html.escape(b.group)}</td>'
            f'<td>{b.app_count}</td>'
            f'<td>{html.escape(_format_duration(b.wall_sec))}</td>'
            f'<td>{html.escape(_format_duration(b.sum_app_sec))}</td>'
            f'<td>{b.speedup:.2f}×</td>'
            f'</tr>'
        )
    timing_table = ''
    if timing_rows:
        timing_table = f"""
  <h2>Timing</h2>
  <p class="meta">
    Total wall: {html.escape(_format_duration(total_wall))} ·
    Sum of per-app time: {html.escape(_format_duration(sum_app))} ·
    Overall speedup: {overall_speedup:.2f}×
    (speedup = sum_app / wall; ideal parallel ≈ jobs)
  </p>
  <table>
    <thead>
      <tr>
        <th>Mode</th><th>IDF</th><th>Target</th><th>Board</th><th>Group</th>
        <th>Apps</th><th>Wall</th><th>Σ app</th><th>Speedup</th>
      </tr>
    </thead>
    <tbody>
      {''.join(timing_rows)}
    </tbody>
  </table>
"""

    counted = len(results) - len(skipped)
    fail_rate = f'{100.0 * len(failed) / counted:.1f}%' if counted > 0 else '0%'
    idf_versions = ', '.join(html.escape(v) for v in cfg.idf_versions)
    targets_summary = ', '.join(
        html.escape(f'{t.chip}' + (f'/{t.board}' if t.board else '')) for t in cfg.targets
    )

    page = f"""<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>esp-gmf batch build report</title>
  <style>
    :root {{
      --pass: #1a7f37;
      --fail: #cf222e;
      --skip: #6e7781;
      --bg: #f6f8fa;
      --card: #fff;
      --border: #d0d7de;
    }}
    body {{
      font-family: system-ui, -apple-system, Segoe UI, sans-serif;
      margin: 0;
      padding: 24px;
      background: var(--bg);
      color: #1f2328;
    }}
    h1 {{ margin: 0 0 8px; font-size: 1.5rem; }}
    .meta {{ color: #656d76; margin-bottom: 20px; font-size: 0.9rem; }}
    .cards {{
      display: flex;
      flex-wrap: wrap;
      gap: 12px;
      margin-bottom: 24px;
    }}
    .card {{
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 8px;
      padding: 12px 20px;
      min-width: 100px;
    }}
    .card strong {{ display: block; font-size: 1.75rem; }}
    .card.pass strong {{ color: var(--pass); }}
    .card.fail strong {{ color: var(--fail); }}
    .card.skip strong {{ color: var(--skip); }}
    .card span {{ font-size: 0.85rem; color: #656d76; }}
    table {{
      width: 100%;
      border-collapse: collapse;
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 8px;
      overflow: hidden;
      font-size: 0.875rem;
    }}
    th, td {{
      padding: 10px 12px;
      text-align: left;
      border-bottom: 1px solid var(--border);
      vertical-align: top;
    }}
    th {{
      background: #f6f8fa;
      font-weight: 600;
      position: sticky;
      top: 0;
    }}
    tr.pass td.status {{ color: var(--pass); font-weight: 700; }}
    tr.fail td.status {{ color: var(--fail); font-weight: 700; }}
    tr.skip td.status {{ color: var(--skip); font-weight: 700; }}
    tr.fail {{ background: #fff8f8; }}
    tr.skip {{ background: #f6f8fa; color: #6e7781; }}
    td.app {{ font-family: ui-monospace, monospace; max-width: 280px; word-break: break-all; }}
    td.steps {{ max-width: 220px; }}
    td.log a {{ word-break: break-all; }}
    .filter {{
      margin-bottom: 12px;
    }}
    .filter button {{
      margin-right: 8px;
      padding: 6px 12px;
      border: 1px solid var(--border);
      border-radius: 6px;
      background: var(--card);
      cursor: pointer;
    }}
    .filter button.active {{ background: #0969da; color: #fff; border-color: #0969da; }}
  </style>
</head>
<body>
  <h1>esp-gmf batch build report</h1>
  <p class="meta">
    Generated: {html.escape(generated_at)}<br>
    Project: {html.escape(str(cfg.project_path))}<br>
    IDF: {idf_versions} · Targets: {targets_summary}
  </p>
  <div class="cards">
    <div class="card"><strong>{len(results)}</strong><span>Total</span></div>
    <div class="card pass"><strong>{len(passed)}</strong><span>Passed</span></div>
    <div class="card fail"><strong>{len(failed)}</strong><span>Failed</span></div>
    <div class="card skip"><strong>{len(skipped)}</strong><span>Skipped</span></div>
    <div class="card"><strong>{html.escape(fail_rate)}</strong><span>Fail rate (excl. skip)</span></div>
    <div class="card"><strong>{html.escape(_format_duration(total_wall))}</strong><span>Total wall</span></div>
    <div class="card"><strong>{overall_speedup:.2f}×</strong><span>Overall speedup</span></div>
  </div>
  {timing_table}
  <div class="filter">
    <button type="button" class="active" data-filter="all">All</button>
    <button type="button" data-filter="fail">Failed only</button>
    <button type="button" data-filter="pass">Passed only</button>
    <button type="button" data-filter="skip">Skipped only</button>
  </div>
  <table>
    <thead>
      <tr>
        <th>Status</th>
        <th>App</th>
        <th>IDF</th>
        <th>Target</th>
        <th>Board</th>
        <th>Group</th>
        <th>Duration</th>
        <th>Steps</th>
        <th>Log</th>
      </tr>
    </thead>
    <tbody>
      {''.join(rows)}
    </tbody>
  </table>
  <script>
    document.querySelectorAll('.filter button').forEach((btn) => {{
      btn.addEventListener('click', () => {{
        document.querySelectorAll('.filter button').forEach((b) => b.classList.remove('active'));
        btn.classList.add('active');
        const mode = btn.dataset.filter;
        document.querySelectorAll('tbody tr').forEach((row) => {{
          if (mode === 'all') row.style.display = '';
          else if (mode === 'fail') row.style.display = row.classList.contains('fail') ? '' : 'none';
          else if (mode === 'skip') row.style.display = row.classList.contains('skip') ? '' : 'none';
          else row.style.display = row.classList.contains('pass') ? '' : 'none';
        }});
      }});
    }});
  </script>
</body>
</html>
"""
    report_path.write_text(page, encoding='utf-8')
    shutil.copyfile(report_path, latest_path)
    return report_path


def _print_summary(
    results: List[BuildResult],
    report_path: Optional[Path] = None,
    *,
    configured_jobs: int = 1,
    timing_json_path: Optional[Path] = None,
) -> int:
    skipped = [r for r in results if r.skipped]
    failed = [r for r in results if not r.success and not r.skipped]
    passed = [r for r in results if r.success]

    log.info('\n========== Build summary ==========')
    log.info(
        'Total: %d  Passed: %d  Failed: %d  Skipped: %d',
        len(results), len(passed), len(failed), len(skipped),
    )
    if skipped:
        log.info('\nSkipped apps (target not in declared SUPPORTED_TARGETS):')
        for r in skipped:
            log.info('  [SKIP] %s (%s)  %s', r.app_path, r.target, r.message)
    if report_path:
        log.info('HTML report: %s', report_path)
        log.info('Latest report: %s', report_path.parent / 'batch_build_report_latest.html')
    if timing_json_path:
        log.info('Timing JSON: %s', timing_json_path)
        log.info('Latest timing: %s', timing_json_path.parent / 'batch_build_timing_latest.json')

    _print_timing_summary(results, configured_jobs=configured_jobs)

    if failed:
        log.info('\nFailed builds:')
        for r in failed:
            log.info('  [FAIL] %s (%s / %s / %s)', r.app_path, r.idf_version, r.target, r.group)
            if r.log_file:
                log.info('         log: %s', r.log_file)
            if r.message:
                log.info('         %s', r.message)

    return 1 if failed else 0


def _execute_build_task(
    task: BuildTask,
    env: Dict[str, str],
    cfg: BatchConfig,
) -> BuildResult:
    app = Path(task.app_path)
    target = TargetBoard(chip=task.target_chip, board=task.target_board)
    try:
        return build_one_app(
            app,
            idf_version=task.idf_version,
            target=target,
            group=task.group,
            env=env,
            cfg=cfg,
        )
    except Exception as exc:  # pragma: no cover
        log_file = _log_file_path(
            (cfg.project_path / cfg.build.log_dir).resolve(),
            task.idf_version,
            task.target_chip,
            task.target_board,
            task.group,
            app,
        )
        log_file.parent.mkdir(parents=True, exist_ok=True)
        log_file.write_text(f'Unexpected error: {exc}\n', encoding='utf-8')
        log_file = _finalize_log_path(log_file, False)
        return BuildResult(
            task.app_path,
            task.idf_version,
            task.target_chip,
            task.target_board,
            task.group,
            False,
            str(log_file),
            str(exc),
            0.0,
        )


def _log_build_result(result: BuildResult, project_path: Path) -> None:
    app_label = _app_display_name(Path(result.app_path), project_path)
    label = (
        f'{result.idf_version}/{result.target}'
        + (f'/{result.board}' if result.board else '')
        + f' [{result.group}] {app_label}'
    )
    dur = _format_duration(result.duration_sec)
    if result.skipped:
        log.info('  SKIP | %s | %s | %s', label, dur, result.message)
    elif result.success:
        log.info('  PASS | %s | %s | %s', label, dur, result.message)
    else:
        log.error('  FAIL | %s | %s | %s', label, dur, result.message)
        if result.log_file:
            log.error('       log: %s', result.log_file)


def _run_build_tasks(
    tasks: List[BuildTask],
    env: Dict[str, str],
    cfg: BatchConfig,
    parallel_jobs: int,
    *,
    project_path: Path,
    batch_idf: str,
    batch_target: TargetBoard,
    batch_group: str,
) -> List[BuildResult]:
    total = len(tasks)
    if total == 0:
        return []

    batch_t0 = time.monotonic()

    if parallel_jobs <= 1:
        results: List[BuildResult] = []
        for idx, task in enumerate(tasks, 1):
            log.info('[%d/%d] Building %s', idx, total, task.app_path)
            result = _execute_build_task(task, env, cfg)
            _log_build_result(result, project_path)
            results.append(result)
        _record_batch_timing(
            idf_version=batch_idf,
            target=batch_target,
            group=batch_group,
            parallel_jobs=1,
            results=results,
            wall_sec=time.monotonic() - batch_t0,
        )
        return results

    if fcntl is None:
        log.warning('fcntl unavailable; forcing parallel_jobs=1')
        return _run_build_tasks(
            tasks, env, cfg, 1,
            project_path=project_path,
            batch_idf=batch_idf,
            batch_target=batch_target,
            batch_group=batch_group,
        )

    workers = min(parallel_jobs, total)
    log.info('Parallel build: %d worker(s), %d task(s)', workers, total)
    results: List[BuildResult] = []

    global _ACTIVE_EXECUTOR, _TRACKED_SUBPROC, _MP_MANAGER, _SHUTDOWN_LOCK
    _SHUTDOWN_LOCK = False
    _MP_MANAGER = multiprocessing.Manager()
    tracked_pids = _MP_MANAGER.list()
    _TRACKED_SUBPROC = tracked_pids

    pool = ProcessPoolExecutor(
        max_workers=workers,
        initializer=_pool_worker_init,
        initargs=(tracked_pids,),
    )
    _ACTIVE_EXECUTOR = pool
    future_map: Dict[object, BuildTask] = {}
    try:
        future_map = {
            pool.submit(_execute_build_task, task, env, cfg): task for task in tasks
        }
        done = 0
        for future in as_completed(future_map):
            done += 1
            task = future_map[future]
            log.info('[%d/%d] Finished %s', done, total, task.app_path)
            result = future.result()
            _log_build_result(result, project_path)
            results.append(result)
    except KeyboardInterrupt:
        log.warning('Interrupted; cancelling pending build tasks...')
        for future in future_map:
            future.cancel()
        raise
    finally:
        _ACTIVE_EXECUTOR = None
        _teardown_executor(pool, wait=True, cancel=True)
        _terminate_tracked_subprocesses()
        _cleanup_mp_manager()

    _record_batch_timing(
        idf_version=batch_idf,
        target=batch_target,
        group=batch_group,
        parallel_jobs=workers,
        results=results,
        wall_sec=time.monotonic() - batch_t0,
    )
    return results


def _sort_build_results(results: List[BuildResult]) -> List[BuildResult]:
    return sorted(
        results,
        key=lambda r: (r.idf_version, r.target, r.board, r.group, r.app_path),
    )



def run_batch(
    cfg: BatchConfig,
    *,
    list_apps_only: bool = False,
    skip_idf_setup: bool = False,
    parallel_jobs: Optional[int] = None,
) -> int:
    if not cfg.project_path.is_dir():
        log.error('project_path does not exist: %s', cfg.project_path)
        return 1
    if not cfg.idf_path.is_dir():
        log.error('idf_path does not exist: %s', cfg.idf_path)
        return 1
    if not cfg.idf_versions:
        log.error('No idf_versions configured')
        return 1
    if not cfg.targets:
        log.error('No targets configured')
        return 1

    os.environ['PROJECT_PATH'] = str(cfg.project_path)

    if cfg.example_dir_names:
        example_apps = discover_apps(cfg.project_path, cfg.example_dir_names, cfg.exclude_path_segments)
    else:
        log.info('example_dir_names is empty; skipping example/examples discovery')
        example_apps = []

    if cfg.test_dir_names:
        test_apps = discover_apps(cfg.project_path, cfg.test_dir_names, cfg.exclude_path_segments)
    else:
        log.info('test_dir_names is empty; skipping test/test_apps discovery')
        test_apps = []

    app_groups: List[Tuple[str, List[Path]]] = [
        ('examples', example_apps),
        ('test_apps', test_apps),
    ]

    log.info('Project: %s', cfg.project_path)
    log.info('Example/example apps: %d', len(example_apps))
    for p in example_apps:
        log.info('  [examples] %s', p)
    log.info('Test/test_apps apps: %d', len(test_apps))
    for p in test_apps:
        log.info('  [test_apps] %s', p)

    if list_apps_only:
        return 0

    global _SHUTDOWN_LOCK, _TRACKED_SUBPROC, _TIMING_BATCHES
    _SHUTDOWN_LOCK = False
    _TRACKED_SUBPROC = []
    _TIMING_BATCHES.clear()

    idf_versions = list(cfg.idf_versions)
    targets = list(cfg.targets)

    jobs = parallel_jobs if parallel_jobs is not None else cfg.build.parallel_jobs
    jobs = max(1, jobs)
    if jobs > 1:
        log.info(
            'Parallel: %d worker(s) per (IDF, board, group); boards serial; lock: %s/locks/',
            jobs,
            cfg.batch_build_dir,
        )

    results: List[BuildResult] = []

    try:
        return _run_batch_matrix(
            cfg,
            idf_versions=idf_versions,
            targets=targets,
            app_groups=app_groups,
            jobs=jobs,
            skip_idf_setup=skip_idf_setup,
            results=results,
        )
    except KeyboardInterrupt:
        log.error('Batch build interrupted')
        _shutdown_all_build_workers(wait=False, cancel_pending=True)
        results = _sort_build_results(results)
        if results:
            total_wall = (
                time.monotonic() - _RUN_WALL_START if _RUN_WALL_START is not None else 0.0
            )
            report_path = _write_html_report(results, cfg)
            timing_path = _write_timing_json(
                results, cfg, total_wall_sec=total_wall, configured_jobs=jobs,
            )
            _print_summary(
                results, report_path, configured_jobs=jobs, timing_json_path=timing_path,
            )
        return 130
    finally:
        _shutdown_all_build_workers(wait=False, cancel_pending=True)


def _run_batch_matrix(
    cfg: BatchConfig,
    *,
    idf_versions: List[str],
    targets: List[TargetBoard],
    app_groups: List[Tuple[str, List[Path]]],
    jobs: int,
    skip_idf_setup: bool,
    results: List[BuildResult],
) -> int:
    global _RUN_WALL_START
    _RUN_WALL_START = time.monotonic()

    for idf_ver in idf_versions:
        if not skip_idf_setup:
            try:
                checkout_idf(cfg.idf_path, idf_ver, cfg.idf_tag_flag)
            except subprocess.CalledProcessError as exc:
                log.error('IDF checkout failed for %s: %s', idf_ver, exc)
                return 1

        try:
            env = prepare_idf_environment(
                cfg,
                skip_install=skip_idf_setup,
                skip_bmgr_assist=skip_idf_setup,
            )
        except (FileNotFoundError, RuntimeError) as exc:
            log.error('Cannot prepare IDF environment (install.sh + export.sh): %s', exc)
            return 1

        idf_py = Path(env.get('IDF_PATH', cfg.idf_path)) / 'tools' / 'idf.py'
        if not idf_py.is_file():
            log.error('idf.py not found after export: %s', idf_py)
            return 1
        log.info('Using idf.py: %s', idf_py)

        # Boards serial; apps parallel within each (idf, chip, board, group).
        for target in targets:
            log.info(
                '\n>>> Matrix: IDF=%s  target=%s  board=%s',
                idf_ver,
                target.chip,
                target.board or '(none)',
            )
            for group_name, apps in app_groups:
                if not apps:
                    continue
                log.info('\n--- Group: %s (%d apps) ---', group_name, len(apps))
                tasks = [
                    BuildTask(
                        app_path=str(app.resolve()),
                        idf_version=idf_ver,
                        target_chip=target.chip,
                        target_board=target.board,
                        group=group_name,
                    )
                    for app in apps
                ]
                log_root = (cfg.project_path / cfg.build.log_dir).resolve()
                tasks_to_build, skipped_results = _partition_tasks_by_target_support(
                    tasks, log_root,
                )
                for skip in skipped_results:
                    _log_build_result(skip, cfg.project_path)
                results.extend(skipped_results)
                batch_results = _run_build_tasks(
                    tasks_to_build,
                    env,
                    cfg,
                    jobs,
                    project_path=cfg.project_path,
                    batch_idf=idf_ver,
                    batch_target=target,
                    batch_group=group_name,
                )
                results.extend(batch_results)

    results = _sort_build_results(results)
    total_wall = time.monotonic() - _RUN_WALL_START if _RUN_WALL_START is not None else 0.0
    report_path = None
    timing_path = None
    if results:
        report_path = _write_html_report(results, cfg)
        timing_path = _write_timing_json(
            results, cfg, total_wall_sec=total_wall, configured_jobs=jobs,
        )
    return _print_summary(
        results,
        report_path,
        configured_jobs=jobs,
        timing_json_path=timing_path,
    )


def main(argv: Optional[Sequence[str]] = None) -> int:
    _install_shutdown_handlers()
    parser = argparse.ArgumentParser(
        description='Batch build example/examples and test/test_apps ESP-IDF projects.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        '-c', '--config',
        type=Path,
        default=DEFAULT_CONFIG,
        help='YAML configuration file',
    )
    parser.add_argument(
        '--list-apps',
        action='store_true',
        help='Only list discovered applications, do not build',
    )
    parser.add_argument(
        '--skip-idf-setup',
        action='store_true',
        help=(
            'Skip IDF git checkout, install.sh, and esp-bmgr-assist install; '
            'use when the IDF environment is already set up (e.g. local dev loops)'
        ),
    )
    parser.add_argument(
        '-j', '--jobs',
        type=int,
        default=None,
        metavar='N',
        help='Parallel app builds per (IDF version, board, group); 1 = serial (default from config)',
    )
    parser.add_argument(
        '--batch-dir',
        type=str,
        default=None,
        metavar='DIR',
        help=(
            f'Override batch artifacts directory (relative to project_path); '
            f'overrides BATCH_BUILD_DIR constant and YAML build.log_dir/build.build_root '
            f'(default: {BATCH_BUILD_DIR})'
        ),
    )
    args = parser.parse_args(argv)

    try:
        cfg = _load_config(args.config.resolve())
    except ValueError as exc:
        parser.error(str(exc))
    if args.batch_dir:
        _apply_batch_dir_override(cfg, args.batch_dir)
    try:
        return run_batch(
            cfg,
            list_apps_only=args.list_apps,
            skip_idf_setup=args.skip_idf_setup,
            parallel_jobs=args.jobs,
        )
    except KeyboardInterrupt:
        return 130


if __name__ == '__main__':
    sys.exit(main())
