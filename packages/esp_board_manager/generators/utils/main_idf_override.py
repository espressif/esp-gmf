# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Resolve local board scan roots from main/idf_component.yml (or .yaml) override_path entries.

Only dependencies whose *component slug* looks like a board/boards pack are considered, so
normal overrides (gmf_core, esp_capture, …) are not scanned.
"""

from pathlib import Path
from typing import Dict, List, Tuple

from .yaml_utils import load_yaml_safe

MAIN_IDF_COMPONENT_FILENAMES = ('idf_component.yml', 'idf_component.yaml')


def dependency_name_suggests_main_override_board_scan(dep_key: str) -> bool:
    """Return True if dependency name should trigger board scan for its local override path.

    Heuristic (slug = part after last '/', lowercased):
    - Contains ``_boards_`` or ends with ``_boards`` → board collection pack.
    - Contains ``_board_`` or ends with ``_board`` → single-board style pack.
    - Slugs containing ``board_manager`` are skipped so ``esp_board_manager`` is never scanned
      via this path (it would duplicate builtin ``boards/``).
    """
    if not isinstance(dep_key, str) or not dep_key.strip():
        return False
    slug = dep_key.split('/')[-1].lower()
    if 'board_manager' in slug:
        return False
    if '_boards_' in slug or slug.endswith('_boards'):
        return True
    if '_board_' in slug or slug.endswith('_board'):
        return True
    return False


def collect_main_override_board_paths(
    project_root: Path,
) -> Tuple[List[Tuple[str, Path]], List[Tuple[str, str]]]:
    """Collect unique resolved directories from main manifest local overrides.

    Resolution rules (cross-platform via :class:`pathlib.Path`):

    - **Relative** paths are resolved against ``{project_root}/main`` (same as typical IDF
      ``override_path`` usage).
    - **Absolute** paths (POSIX ``/...``, Windows ``C:\\...`` / ``C:/...``) ignore ``main/`` and
      resolve from the filesystem root; the right-hand path wins when joining with ``main_dir``.
    - A leading ``~`` or ``~user`` is expanded via :meth:`Path.expanduser` before resolving.

    Only existing directories are returned in the first list. The second list is
    ``(dependency_key, raw_path)`` entries where the board-style name matched but the path is
    missing or not a directory.

    ``override_path`` and ``path`` keys are both considered. Same resolved path is listed once
    (first dependency name wins for ordering).
    """
    main_dir = project_root / 'main'
    seen_resolved: Dict[str, str] = {}
    seen_missing: Dict[str, str] = {}
    ordered: List[Tuple[str, Path]] = []
    missing: List[Tuple[str, str]] = []

    for fname in MAIN_IDF_COMPONENT_FILENAMES:
        yml_path = main_dir / fname
        if not yml_path.is_file():
            continue
        data = load_yaml_safe(yml_path)
        if not data or not isinstance(data, dict):
            continue
        deps = data.get('dependencies')
        if not isinstance(deps, dict):
            continue
        for dep_name, spec in deps.items():
            if not isinstance(dep_name, str) or not isinstance(spec, dict):
                continue
            if not dependency_name_suggests_main_override_board_scan(dep_name):
                continue
            raw = spec.get('override_path')
            if raw is None or raw == '':
                raw = spec.get('path')
            if not isinstance(raw, str) or not raw.strip():
                continue
            raw_s = raw.strip()
            candidate = Path(raw_s).expanduser()
            resolved = (candidate if candidate.is_absolute() else (main_dir / candidate)).resolve()
            key = str(resolved)
            if key in seen_resolved:
                continue
            if not resolved.is_dir():
                if raw_s not in seen_missing:
                    seen_missing[raw_s] = dep_name
                    missing.append((dep_name, raw_s))
                continue
            seen_resolved[key] = dep_name
            ordered.append((dep_name, resolved))

    return ordered, missing
