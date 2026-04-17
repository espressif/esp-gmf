"""
Tests for idf.py action registration and early CMake validation.
"""

import os
from pathlib import Path
import subprocess
import sys
import textwrap
from types import SimpleNamespace
import pytest


class _Task:
    def __init__(self, name, aliases=None):
        self.name = name
        self.aliases = aliases or []


def _run_component_cmake_check(bmgr_root: Path, project_root: Path, workdir: Path):
    script_path = workdir / 'check_board_manager_component.cmake'
    script_path.write_text(
        textwrap.dedent(
            f'''
            cmake_minimum_required(VERSION 3.5)

            set(PROJECT_ROOT "{project_root.as_posix()}")

            function(idf_build_get_property out_var property_name)
                if(property_name STREQUAL "PROJECT_DIR")
                    set(${{out_var}} "${{PROJECT_ROOT}}" PARENT_SCOPE)
                endif()
            endfunction()

            function(idf_component_register)
            endfunction()

            include("{(bmgr_root / 'CMakeLists.txt').as_posix()}")
            '''
        ).strip()
        + '\n',
        encoding='utf-8',
    )

    return subprocess.run(
        ['cmake', '-P', str(script_path)],
        cwd=str(workdir),
        capture_output=True,
        text=True,
    )


def test_idf_extension_registers_bmgr_action(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    actions = idf_ext.action_extensions({}, str(bmgr_root / 'test_apps'))

    assert 'gen-bmgr-config' in actions['actions']
    assert 'bmgr' in actions['actions']
    bmgr_action = actions['actions']['bmgr']
    assert 'arguments' not in bmgr_action
    custom_option = next(
        option for option in actions['actions']['bmgr']['options']
        if '--customer-path' in option['names']
    )
    assert '--customer-path' in custom_option['names']
    assert '--custom' not in custom_option['names']


def test_idf_extension_rejects_mixed_legacy_and_new_bmgr_styles(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    actions = idf_ext.action_extensions({}, str(bmgr_root / 'test_apps'))
    validation_callback = actions['global_action_callbacks'][0]

    with pytest.raises(idf_ext.FatalError, match='Do not mix legacy and new command styles'):
        validation_callback(
            None,
            {'project_dir': str(bmgr_root / 'test_apps'), 'define_cache_entry': []},
            [_Task('gen-bmgr-config'), _Task('bmgr')],
        )


@pytest.mark.parametrize(
    ('task_names', 'action_name', 'expected_message', 'expected_value'),
    [
        (['gen-bmgr-config', '1'], 'gen-bmgr-config', 'idf.py gen-bmgr-config -b <board>', '1'),
        (['gen-bmgr-config', 'esp32_s3_korvo2_v3'], 'gen-bmgr-config', 'idf.py gen-bmgr-config -b <board>', 'esp32_s3_korvo2_v3'),
        (['bmgr', '1'], 'bmgr', 'idf.py bmgr -b <board>', '1'),
        (['bmgr', 'esp32_s3_korvo2_v3'], 'bmgr', 'idf.py bmgr -b <board>', 'esp32_s3_korvo2_v3'),
    ],
)
def test_idf_extension_rejects_positional_board_value_misuse(
    bmgr_root, task_names, action_name, expected_message, expected_value
):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    actions = idf_ext.action_extensions({}, str(bmgr_root / 'test_apps'))
    original_argv = sys.argv[:]
    sys.argv = ['idf.py', *task_names]

    try:
        callback = actions['actions'][action_name]['callback']
        with pytest.raises(idf_ext.FatalError) as excinfo:
            callback(
                action_name,
                None,
                SimpleNamespace(project_dir=str(bmgr_root / 'test_apps')),
            )
        message = str(excinfo.value)
        assert 'positional argument' in message
        assert expected_value in message
        assert expected_message in message
    finally:
        sys.argv = original_argv


def test_component_cmake_loads_without_gen_bmgr_codes(bmgr_root, tmp_path):
    """CMake configure does not fatal when gen_bmgr_codes is absent (set-target friendly)."""
    project_root = tmp_path / 'project'
    project_root.mkdir()

    result = _run_component_cmake_check(bmgr_root, project_root, tmp_path)

    assert result.returncode == 0, result.stdout + result.stderr


def test_component_cmake_allows_build_when_generated_board_files_exist(bmgr_root, tmp_path):
    project_root = tmp_path / 'project'
    gen_dir = project_root / 'components' / 'gen_bmgr_codes'
    gen_dir.mkdir(parents=True)

    for filename in (
        'CMakeLists.txt',
        'idf_component.yml',
        'board_manager.defaults',
        'gen_board_info.c',
        'gen_board_periph_config.c',
        'gen_board_periph_handles.c',
        'gen_board_device_config.c',
        'gen_board_device_handles.c',
    ):
        (gen_dir / filename).write_text('', encoding='utf-8')

    result = _run_component_cmake_check(bmgr_root, project_root, tmp_path)

    assert result.returncode == 0, result.stdout + result.stderr


def test_idf_extension_rejects_missing_project_dir(bmgr_root, tmp_path):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    missing_project_root = tmp_path / 'missing_project'
    actions = idf_ext.action_extensions({}, str(missing_project_root))
    validation_callback = actions['global_action_callbacks'][0]

    with pytest.raises(idf_ext.FatalError, match='Project directory does not exist'):
        validation_callback(
            None,
            {'project_dir': str(missing_project_root), 'define_cache_entry': []},
            [_Task('build')],
        )


def test_bmgr_new_board_defaults_to_project_components_when_project_dir_differs(
    bmgr_root, tmp_path, monkeypatch
):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    project_root = tmp_path / 'project'
    outside = tmp_path / 'outside'
    project_root.mkdir()
    outside.mkdir()

    recorded = {}

    def _fake_run(self, board_name, create_path, input_args):
        recorded['board_name'] = board_name
        recorded['create_path'] = create_path
        recorded['input_args'] = input_args
        return True

    monkeypatch.setattr(idf_ext.BoardCreator, 'run', _fake_run, raising=True)
    monkeypatch.chdir(outside)

    actions = idf_ext.action_extensions({}, str(project_root))
    callback = actions['actions']['bmgr']['callback']
    callback(
        'bmgr',
        None,
        SimpleNamespace(project_dir=str(project_root)),
        new_board='demo_board',
    )

    assert recorded['board_name'] == 'demo_board'
    assert recorded['input_args'] == 'demo_board'
    assert recorded['create_path'] == (project_root / 'components').resolve()


def test_bmgr_resolves_component_board_using_project_dir_when_cwd_differs(
    bmgr_root, tmp_path, monkeypatch
):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    project_root = tmp_path / 'project'
    outside = tmp_path / 'outside'
    board_dir = project_root / 'components' / 'my_proj_board'
    board_dir.mkdir(parents=True)
    outside.mkdir()

    (board_dir / 'board_info.yaml').write_text('chip: esp32\n', encoding='utf-8')
    (board_dir / 'board_peripherals.yaml').write_text('peripherals: []\n', encoding='utf-8')
    (board_dir / 'board_devices.yaml').write_text('devices: []\n', encoding='utf-8')

    recorded = {}

    def _fake_run(self, args, cached_boards=None):
        recorded['board_name'] = args.board_name
        recorded['cached_boards'] = cached_boards or {}
        recorded['cwd'] = os.getcwd()
        return True

    monkeypatch.setattr(idf_ext.BoardConfigGenerator, 'run', _fake_run, raising=True)
    monkeypatch.chdir(outside)

    actions = idf_ext.action_extensions({}, str(project_root))
    callback = actions['actions']['bmgr']['callback']
    callback(
        'bmgr',
        None,
        SimpleNamespace(project_dir=str(project_root)),
        board='my_proj_board',
    )

    assert recorded['board_name'] == 'my_proj_board'
    assert recorded['cwd'] == str(project_root.resolve())
    assert recorded['cached_boards']['my_proj_board'] == str(board_dir.resolve())


def test_bmgr_resolves_relative_customer_path_from_project_dir_when_cwd_differs(
    bmgr_root, tmp_path, monkeypatch
):
    sys.path.insert(0, str(bmgr_root))
    import idf_ext

    project_root = tmp_path / 'project'
    outside = tmp_path / 'outside'
    customer_root = project_root / 'relative_custom'
    board_dir = customer_root / 'rel_board'
    board_dir.mkdir(parents=True)
    outside.mkdir()

    (board_dir / 'board_info.yaml').write_text('chip: esp32\n', encoding='utf-8')
    (board_dir / 'board_peripherals.yaml').write_text('peripherals: []\n', encoding='utf-8')
    (board_dir / 'board_devices.yaml').write_text('devices: []\n', encoding='utf-8')

    recorded = {}

    def _fake_run(self, args, cached_boards=None):
        recorded['board_name'] = args.board_name
        recorded['board_customer_path'] = args.board_customer_path
        recorded['cached_boards'] = cached_boards or {}
        return True

    monkeypatch.setattr(idf_ext.BoardConfigGenerator, 'run', _fake_run, raising=True)
    monkeypatch.chdir(outside)

    actions = idf_ext.action_extensions({}, str(project_root))
    callback = actions['actions']['bmgr']['callback']
    callback(
        'bmgr',
        None,
        SimpleNamespace(project_dir=str(project_root)),
        board='rel_board',
        customer_path='relative_custom',
    )

    assert recorded['board_name'] == 'rel_board'
    assert recorded['board_customer_path'] == str(customer_root.resolve())
    assert recorded['cached_boards']['rel_board'] == str(board_dir.resolve())
