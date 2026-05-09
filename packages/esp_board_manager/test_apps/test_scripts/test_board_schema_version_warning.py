import logging
import sys


def test_invalid_board_yaml_schema_version_warning_uses_standard_prefix(bmgr_root, caplog):
    sys.path.insert(0, str(bmgr_root))
    from generators.utils.board_schema_version import warn_if_invalid_board_yaml_schema_version
    logger = logging.getLogger('test_board_schema_version_warning')

    with caplog.at_level(logging.WARNING, logger=logger.name):
        warn_if_invalid_board_yaml_schema_version(logger, '2.0', 'board_peripherals.yaml peripheral #1')

    assert '⚠️  WARNING: Unrecognized board YAML schema `version`' in caplog.text
    assert 'board_peripherals.yaml peripheral #1' in caplog.text
    assert "'1.0.0'" in caplog.text
