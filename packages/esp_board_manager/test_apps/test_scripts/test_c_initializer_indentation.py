"""
Tests for C initializer indentation formatting.
"""

from pathlib import Path
import sys


def _leading_spaces(line: str) -> int:
    return len(line) - len(line.lstrip(' '))


def _find_first_indent(lines, marker: str) -> int:
    for line in lines:
        if marker in line:
            return _leading_spaces(line)
    raise AssertionError(f'Marker not found: {marker}')


def _read_lines(path: Path):
    return path.read_text(encoding='utf-8').splitlines()


def _extract_struct_block(lines, struct_marker: str):
    start = None
    for idx, line in enumerate(lines):
        if struct_marker in line:
            start = idx
            break
    if start is None:
        raise AssertionError(f'Struct marker not found: {struct_marker}')

    for end in range(start + 1, len(lines)):
        if lines[end].strip() == '};':
            return lines[start:end + 1]
    raise AssertionError(f'Struct block not terminated: {struct_marker}')


def test_dict_to_c_initializer_custom_nested_indentation(bmgr_root):
    sys.path.insert(0, str(bmgr_root))
    from generators.config_generator import ConfigGenerator

    cfg = ConfigGenerator(bmgr_root)
    custom_cfg = {
        'name': 'motor_driver',
        'type': 'custom',
        'sub_cfg': {
            'bus': {
                'flags': {
                    'enable_dma': True,
                },
                'nodes': [
                    {
                        'id': 1,
                        'props': {'mode': 'fast'},
                    },
                ],
            },
        },
    }
    lines = cfg.dict_to_c_initializer(custom_cfg, 4)

    assert _find_first_indent(lines, '.sub_cfg = {') == 0
    assert _find_first_indent(lines, '.bus = {') == 4
    assert _find_first_indent(lines, '.flags = {') == 8
    assert _find_first_indent(lines, '.enable_dma = true,') == 12
    assert _find_first_indent(lines, '.nodes = {') == 8
    assert _find_first_indent(lines, '.id = 1,') == 16
    assert _find_first_indent(lines, '.props = {') == 16
    assert _find_first_indent(lines, '.mode = "fast",') == 20


def test_generated_custom_device_indentation_regression(run_bmgr_cmd, bmgr_root):
    test_apps_dir = bmgr_root / 'test_apps'
    result = run_bmgr_cmd(['-b', 'test_board_e'], cwd=test_apps_dir, check=False)
    assert result.returncode == 0, result.stderr

    generated = test_apps_dir / 'components' / 'gen_bmgr_codes' / 'gen_board_device_config.c'
    lines = _read_lines(generated)
    block = _extract_struct_block(lines, 'esp_bmgr_motor_driver_cfg')

    assert _find_first_indent(block, '.level_3 = {') == 4
    assert _find_first_indent(block, '.level_4 = {') == 8
    assert _find_first_indent(block, '.lv4_dict_of_dict = {') == 12
    assert _find_first_indent(block, '.lv4_name = "level_4_2",') == 20


def test_generated_display_lcd_indentation_regression(run_bmgr_cmd, bmgr_root):
    test_apps_dir = bmgr_root / 'test_apps'
    result = run_bmgr_cmd(['-b', 'esp32_s3_korvo2_v3'], cwd=test_apps_dir, check=False)
    assert result.returncode == 0, result.stderr

    generated = test_apps_dir / 'components' / 'gen_bmgr_codes' / 'gen_board_device_config.c'
    lines = _read_lines(generated)
    block = _extract_struct_block(lines, 'esp_bmgr_display_lcd_cfg')

    assert _find_first_indent(block, '.sub_cfg = {') == 4
    assert _find_first_indent(block, '.spi = {') == 8
    assert _find_first_indent(block, '.spi_name = "spi_display",') == 12
    assert _find_first_indent(block, '.panel_config = {') == 12
    assert _find_first_indent(block, '.flags = {') == 16
    assert _find_first_indent(block, '.reset_active_high = false,') == 20
