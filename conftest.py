# SPDX-FileCopyrightText: 2022-2023 Espressif Systems (Shanghai) CO LTD
#
# SPDX-License-Identifier: Apache-2.0

import logging
import math
import os
import pathlib
import re
import sys
from datetime import datetime
from typing import Callable, List, Optional, Tuple

import pytest
from _pytest.nodes import Item
from pytest import Config, FixtureRequest, Function, Session
from pytest_embedded.plugin import multi_dut_argument, multi_dut_fixture
from pytest_embedded.utils import to_list

IDF_VERSION = os.environ.get('IDF_VERSION')
PYTEST_ROOT_DIR = str(pathlib.Path(__file__).parent)
DEFAULT_UNITY_CASE_TIMEOUT = 300.0
UNITY_CASE_TIMEOUT_ENV = 'GMF_UNITY_CASE_TIMEOUT'
logging.info(f'Pytest root dir: {PYTEST_ROOT_DIR}')


def _parse_positive_timeout(value: object, source: str) -> float:
    try:
        timeout = float(value)
    except (TypeError, ValueError):
        raise ValueError(f'{source} must be a positive number, got {value!r}')
    if not math.isfinite(timeout) or timeout <= 0:
        raise ValueError(f'{source} must be a positive number, got {value!r}')
    return timeout


def resolve_unity_case_timeout(marker_timeout: Optional[object] = None) -> float:
    env_timeout = os.environ.get(UNITY_CASE_TIMEOUT_ENV)
    if env_timeout is not None:
        return _parse_positive_timeout(env_timeout, UNITY_CASE_TIMEOUT_ENV)
    if marker_timeout is not None:
        return _parse_positive_timeout(marker_timeout, 'unity_case_timeout marker')
    return DEFAULT_UNITY_CASE_TIMEOUT


def _patch_pytest_embedded_serial_redirect() -> None:
    """Drain serial RX faster to prevent USB-Serial-JTAG TX FIFO truncation.

    pytest_embedded_serial's _SerialRedirectThread sleeps 50 ms between
    non-blocking read_all() calls. On USB-Serial-JTAG targets (e.g. ESP32-P4
    on /dev/ttyACM0), the 400 MHz CPU bursts a ~7.8 KB Unity menu in a few
    milliseconds, filling the Linux tty + cdc_acm buffers (~5184 B) while the
    reader thread is asleep. Because the firmware's USB-Serial-JTAG TX flush
    timeout is also 50 ms (TX_FLUSH_TIMEOUT_US), the tail of the menu—including
    "Enter test for running."—gets silently dropped before the host wakes up.

    Polling every 5 ms when idle (and 1 ms while actively receiving data)
    keeps the kernel tty buffer drained well within the 50 ms firmware timeout.
    """
    try:
        import queue
        import time
        import serial as pyserial
        from pytest_embedded_serial import serial as _pes_serial
    except ImportError:
        return

    redirect_cls = getattr(_pes_serial, '_SerialRedirectThread', None)
    if redirect_cls is None:
        return

    idle_interval_s = 0.005
    active_interval_s = 0.001

    def _event_loop(self) -> None:
        while True:
            try:
                _e = self._event_q.get_nowait()
            except queue.Empty:
                _e = 'read'
            except OSError:
                return

            s = b''
            if _e == 'read':
                if self._block_reading:
                    time.sleep(idle_interval_s)
                    continue

                try:
                    s = self._s.read_all()
                except OSError as e:
                    logging.error(f'OSError detected: {e}. Serial connection may be lost.')
                    if self._s.closed:
                        logging.error('Serial port is already closed. Exiting event loop.')
                        return

                    port = self._s.port
                    port_config = {
                        'baudrate': self._s.baudrate,
                        'bytesize': self._s.bytesize,
                        'parity': self._s.parity,
                        'stopbits': self._s.stopbits,
                        'timeout': self._s.timeout,
                        'xonxoff': self._s.xonxoff,
                        'rtscts': self._s.rtscts,
                    }
                    for attempt in range(1, 4):
                        delay = attempt * 1.5
                        logging.warning(
                            f'Attempting to reconnect to serial port {port} (try {attempt}/3) after {delay}s...'
                        )
                        time.sleep(delay)
                        try:
                            self._s.close()
                            self._s = pyserial.serial_for_url(port, **port_config)
                            logging.info(f'Successfully reconnected to serial port {port}.')
                            break
                        except Exception as ex:
                            logging.warning(f'Reconnection attempt {attempt} failed: {ex}')
                    else:
                        logging.error(
                            f'Failed to reconnect to serial port {port} after 3 attempts. Exiting event loop.'
                        )
                        return

                    continue

                except Exception as e:
                    logging.warning(
                        'unknown error: %s.\nRecommend to close the serial process by `dut.serial.close()`', str(e)
                    )
                    return

                try:
                    self._q.put(s)
                except OSError as e:
                    logging.warning(f'OSError. Error msg: {e}')
                    return
                except Exception as e:
                    logging.warning(
                        'unknown error: %s.\nRecommend to close the serial process by `dut.serial.close()`', str(e)
                    )
                    return

            elif _e == 'stop':
                self._block_reading = True
            elif _e == 'start':
                self._block_reading = False
            elif _e == 'end':
                return

            time.sleep(active_interval_s if s else idle_interval_s)

    redirect_cls._event_loop = _event_loop


def _patch_pytest_embedded_unity_menu_parser() -> None:
    """Tolerate repeated / truncated unity test menus from IDF.

    IDF's unity test runner prints "Here's the test menu, pick your combo:"
    eagerly once after boot (without waiting for input) and re-prints it on
    every stray byte. On USB-Serial-JTAG boards the CDC OUT endpoint can
    accumulate one or more line feeds across reset, so the dut output ends
    up with the menu printed 2-3 times before "Enter test for running.".

    Separately, esp_driver_usb_serial_jtag's console TX path uses a small
    ring buffer (default 256 B) and, after one flush timeout (~50 ms),
    silently drops further bytes until the host drains the FIFO. A long
    unity menu (~7 KB for gmf_core) therefore often loses the tail of the
    first print at a stable offset across runners; pytest-embedded then
    captures "first truncated menu + second complete menu" in one block.

    pytest-embedded-idf <= 2.8 captures everything between the first banner
    and "Enter test for running.", then raises NotImplementedError on banner
    lines or truncated case lines inside that block. We replace the parser
    with a tolerant version: the case/subcase regex stays the same, known
    unity-menu noise and incomplete case lines are skipped, and duplicate
    (index, name) entries from re-prints collapse to a single case. Genuine
    unknown lines still raise so real parser regressions are not hidden.
    """
    try:
        from pytest_embedded_idf import unity_tester as _uut
    except ImportError:
        return

    case_re = re.compile(r'\((\d+)\)\s\"(.+)\"\s(\[.+\])+')
    # Truncated mid-line case prints look like '(81)\t"Shared ..." [ELE'
    # (missing closing quote/tag). Skip them; a later full reprint remains.
    incomplete_case_re = re.compile(r'\(\d+\)\s\"')
    subcase_re = re.compile(r'\t\((\d+)\)\s\"(.+)\"')
    benign_prefixes = (
        "Here's the test menu, pick your combo:",
        'Press ENTER to see the list of tests',
    )
    UnittestMenuCase = _uut.UnittestMenuCase

    def _parse(s):
        cases = []
        for line in s.splitlines():
            stripped = line.strip()
            if not stripped:
                continue
            cm = case_re.match(line)
            if cm is not None:
                index, name, tag_block = cm.groups()
                tags = re.findall(r'\[(.+?)\]', tag_block)
                if 'multi_stage' in tags:
                    _type = 'multi_stage'
                    tags.remove('multi_stage')
                elif 'multi_device' in tags:
                    _type = 'multi_device'
                    tags.remove('multi_device')
                else:
                    _type = 'normal'
                keyword = []
                if 'ignore' in tags:
                    keyword.append('ignore')
                    tags.remove('ignore')
                elif 'disable' in tags:
                    keyword = 'disable'
                    tags.remove('disable')
                attributes = {}
                group = []
                for tag in tags:
                    if '=' in tag:
                        k, v = tag.replace(' ', '').split('=')
                        attributes[k] = v
                    else:
                        group.append(tag)
                cases.append(UnittestMenuCase(
                    index=int(index), name=name, type=_type,
                    keywords=keyword, groups=group,
                    attributes=attributes, subcases=[],
                ))
                continue
            sm = subcase_re.match(line)
            if sm is not None:
                if cases:
                    idx, name = sm.groups()
                    cases[-1].subcases.append({'index': int(idx), 'name': name})
                continue
            if stripped.startswith(benign_prefixes):
                continue
            if incomplete_case_re.match(stripped):
                continue
            raise NotImplementedError('Unrecognized test case:', line)

        # Collapse duplicates produced by re-printed menus; keep the last
        # occurrence so the menu reflects the most recent unity print.
        deduped = {}
        for c in cases:
            deduped[(c.index, c.name)] = c
        return list(deduped.values())

    def _parse_test_menu(
        self,
        ready_line: str = 'Press ENTER to see the list of tests',
        pattern="Here's the test menu, pick your combo:(.+)Enter test for running.",
        trigger: str = '',
    ):
        self.expect_exact(ready_line)
        res = self.confirm_write(trigger, expect_pattern=pattern, timeout=2, retry_times=5)
        return self._parse_unity_menu_from_str(res.group(1).decode('utf8', errors='replace'))

    _uut.IdfUnityDutMixin._parse_unity_menu_from_str = staticmethod(_parse)
    _uut.IdfUnityDutMixin._parse_test_menu = _parse_test_menu


def _patch_esptool_progress_bar() -> None:
    """Throttle esptool flash progress output to 20% steps without bar glyphs.

    In non-TTY CI environments (especially on targets using --no-stub where
    esptool writes 1 KB blocks), esptool v5 emits a full ANSI-colored progress
    bar line on every block—producing ~2,500 lines (~440 KB) per 2.5 MB binary
    and over 3 MB across a multi-app test job, which can exceed GitLab's 8 MB
    job log limit.
    """
    step_pct = 20

    try:
        from esptool.logger import EsptoolLogger
    except ImportError:
        EsptoolLogger = None

    if EsptoolLogger is not None and hasattr(EsptoolLogger, 'progress_bar'):
        state = {'prev_iter': -1, 'prev_bucket': -1, 'prev_total': -1}

        def _throttled_progress_bar(
            self,
            cur_iter: int,
            total_iters: int,
            prefix: str = '',
            suffix: str = '',
            bar_length: int = 30,
        ) -> None:
            if getattr(self, '_verbosity', None) in ('silent', 0):
                return
            if total_iters <= 0:
                return
            cur_iter = max(0, min(cur_iter, total_iters))
            pct = 100.0 * cur_iter / total_iters
            bucket = 100 if cur_iter >= total_iters else (int(pct) // step_pct) * step_pct
            is_new_run = (
                state['prev_bucket'] < 0
                or total_iters != state['prev_total']
                or cur_iter < state['prev_iter']
            )
            if not is_new_run and bucket == state['prev_bucket']:
                state['prev_iter'] = cur_iter
                return
            if cur_iter >= total_iters:
                state['prev_iter'] = -1
                state['prev_bucket'] = -1
                state['prev_total'] = -1
            else:
                state['prev_iter'] = cur_iter
                state['prev_bucket'] = bucket
                state['prev_total'] = total_iters
            self.print(f'{prefix}{pct:5.1f}%{suffix}')

        EsptoolLogger.progress_bar = _throttled_progress_bar
        return

    try:
        from esptool import cmds as _esptool_cmds
        from esptool import util as _esptool_util
    except ImportError:
        return

    orig_print_overwrite = getattr(_esptool_util, 'print_overwrite', None)
    if orig_print_overwrite is None:
        return

    pct_re = re.compile(r'^Writing at 0x[0-9a-fA-F]+\.\.\. \((\d+)\s*%\)$')
    state = {'prev_bucket': -1, 'prev_pct': -1}

    def _throttled_print_overwrite(message: str, last_line: bool = False) -> None:
        if not last_line and isinstance(message, str):
            m = pct_re.match(message.strip())
            if m:
                pct = int(m.group(1))
                bucket = 100 if pct >= 100 else (pct // step_pct) * step_pct
                if state['prev_bucket'] >= 0 and pct >= state['prev_pct'] and bucket == state['prev_bucket']:
                    state['prev_pct'] = pct
                    return
                if pct >= 100:
                    state['prev_pct'] = -1
                    state['prev_bucket'] = -1
                else:
                    state['prev_pct'] = pct
                    state['prev_bucket'] = bucket
        orig_print_overwrite(message, last_line=last_line)

    _esptool_util.print_overwrite = _throttled_print_overwrite
    if hasattr(_esptool_cmds, 'print_overwrite'):
        _esptool_cmds.print_overwrite = _throttled_print_overwrite


_patch_pytest_embedded_serial_redirect()
_patch_pytest_embedded_unity_menu_parser()
_patch_esptool_progress_bar()


SUPPORTED_TARGETS = ['esp32', 'esp32s2', 'esp32c3', 'esp32s3', 'esp32c2', 'esp32c6', 'esp32h2', 'esp32p4', 'esp32s31']
PREVIEW_TARGETS: List[str] = []  # this PREVIEW_TARGETS excludes 'linux' target
DEFAULT_SDKCONFIG = 'default'

TARGET_MARKERS = {
    'esp32': 'support esp32 target',
    'esp32s2': 'support esp32s2 target',
    'esp32s3': 'support esp32s3 target',
    'esp32c3': 'support esp32c3 target',
    'esp32c2': 'support esp32c2 target',
    'esp32c6': 'support esp32c6 target',
    'esp32h2': 'support esp32h2 target',
    'esp32p4': 'support esp32p4 target',
    'esp32s31': 'support esp32s31 target',
    'linux': 'support linux target',
}

SPECIAL_MARKERS = {
    'supported_targets': "support all officially announced supported targets ('esp32', 'esp32s2', 'esp32c3', 'esp32s3', 'esp32c2', 'esp32c6')",
    'preview_targets': "support all preview targets ('none')",
    'all_targets': 'support all targets, including supported ones and preview ones',
    'temp_skip_ci': 'temp skip tests for specified targets only in ci',
    'temp_skip': 'temp skip tests for specified targets both in ci and locally',
    'nightly_run': 'tests should be executed as part of the nightly trigger pipeline',
    'host_test': 'tests which should not be built at the build stage, and instead built in host_test stage',
    'qemu': 'build and test using qemu-system-xtensa, not real target',
}

ENV_MARKERS = {
    'GMF_EXAMPLE_GENERIC': 'Tests should be run on GMF_EXAMPLE_GENERIC runners.',
    'ESP_GMF_COMPONENT': 'Tests should be run on ESP_GMF_COMPONENT runners.',
    'ESP_GMF_ELEMENT': 'Tests should be run on random runners.',
    'ESP_GMF_TASK': 'Tests should be run on random runners.',
    'ESP_GMF_IO': 'Tests should be run on random runners.',
    'ESP_GMF_RINGBUF': 'Tests should be run on random runners.',
    'ESP_GMF_PBUF': 'Tests should be run on random runners.',
    'ESP_GMF_BLOCK': 'Tests should be run on random runners.',
    'GMF_LOADER': 'Tests should be run on random runners.',
    'ELEMENT_POOL': 'Tests should be run on random runners.',
    'ELEMENT_PORT': 'Tests should be run on random runners.',
    'ESP_GMF_METHOD': 'Tests should be run on random runners.',
    'AUDIO_LOOPBACK_ENV': 'Audio loopback testing verifies the functionality by comparing the audio input signal with the output signal.',
}

##################
# Help Functions #
##################
def format_case_id(target: Optional[str], config: Optional[str], case: str) -> str:
    return f'{target}.{config}.{case}'


def item_marker_names(item: Item) -> List[str]:
    return [marker.name for marker in item.iter_markers()]


def item_skip_targets(item: Item) -> List[str]:
    def _get_temp_markers_disabled_targets(marker_name: str) -> List[str]:
        temp_marker = item.get_closest_marker(marker_name)

        if not temp_marker:
            return []

        # temp markers should always use keyword arguments `targets` and `reason`
        if not temp_marker.kwargs.get('targets') or not temp_marker.kwargs.get('reason'):
            raise ValueError(
                f'`{marker_name}` should always use keyword arguments `targets` and `reason`. '
                f'For example: '
                f'`@pytest.mark.{marker_name}(targets=["esp32"], reason="IDF-xxxx, will fix it ASAP")`'
            )

        return to_list(temp_marker.kwargs['targets'])  # type: ignore

    temp_skip_ci_targets = _get_temp_markers_disabled_targets('temp_skip_ci')
    temp_skip_targets = _get_temp_markers_disabled_targets('temp_skip')

    # in CI we skip the union of `temp_skip` and `temp_skip_ci`
    if os.getenv('CI_JOB_ID'):
        skip_targets = list(set(temp_skip_ci_targets).union(set(temp_skip_targets)))
    else:  # we use `temp_skip` locally
        skip_targets = temp_skip_targets

    return skip_targets


@pytest.fixture(scope='session', autouse=True)
def idf_version() -> str:
    if os.environ.get('IDF_VERSION'):
        return os.environ.get('IDF_VERSION')
    idf_path = os.environ.get('IDF_PATH')
    if not idf_path:
        logging.warning('Failed to get IDF_VERSION!')
        return ''
    version_path = os.path.join(os.environ['IDF_PATH'], 'tools/cmake/version.cmake')
    regex = re.compile(r'^\s*set\s*\(\s*IDF_VERSION_([A-Z]{5})\s+(\d+)')
    ver = {}
    with open(version_path) as f:
        for line in f:
            m = regex.match(line)
            if m:
                ver[m.group(1)] = m.group(2)
    return '{}.{}'.format(int(ver['MAJOR']), int(ver['MINOR']))


@pytest.fixture(scope='session', autouse=True)
def session_tempdir() -> str:
    _tmpdir = os.path.join(
        os.path.dirname(__file__),
        'pytest_log',
        datetime.now().strftime('%Y-%m-%d_%H-%M-%S'),
    )
    os.makedirs(_tmpdir, exist_ok=True)
    return _tmpdir


@pytest.fixture
@multi_dut_argument
def config(request: FixtureRequest) -> str:
    config_marker = list(request.node.iter_markers(name='config'))
    return config_marker[0].args[0] if config_marker else 'default'


@pytest.fixture
@multi_dut_argument
def app_path(request: FixtureRequest, test_file_path: str) -> str:
    config_marker = list(request.node.iter_markers(name='app_path'))
    if config_marker:
        return config_marker[0].args[0]
    else:
        # compatible with old pytest-embedded parametrize --app_path
        return request.config.getoption('app_path', None) or os.path.dirname(test_file_path)


@pytest.fixture
def test_case_name(request: FixtureRequest, target: str, config: str) -> str:
    if not isinstance(target, str):
        target = '|'.join(sorted(list(set(target))))
    if not isinstance(config, str):
        config = '|'.join(sorted(list(config)))
    return f'{target}.{config}.{request.node.originalname}'


@pytest.fixture
@multi_dut_fixture
def build_dir(
    app_path: str,
    target: Optional[str],
    config: Optional[str],
    idf_version: str
) -> Optional[str]:
    """
    Check local build dir with the following priority:

    1. <app_path>/${IDF_VERSION}/build_<target>_<config>
    2. <app_path>/${IDF_VERSION}/build_<target>
    3. <app_path>/build_<target>_<config>
    4. <app_path>/build
    5. <app_path>

    Args:
        app_path: app path
        target: target
        config: config

    Returns:
        valid build directory
    """

    assert target
    assert config
    check_dirs = []
    if idf_version:
        check_dirs.append(os.path.join(idf_version, f'build_{target}_{config}'))
        check_dirs.append(os.path.join(idf_version, f'build_{target}'))
    check_dirs.append(f'build_{target}_{config}')
    check_dirs.append(f'build_{target}_{config}s')
    check_dirs.append('build')
    check_dirs.append('.')
    for check_dir in check_dirs:
        binary_path = os.path.join(app_path, check_dir)
        if os.path.isdir(binary_path):
            logging.info(f'find valid binary path: {binary_path}')
            return check_dir

        logging.warning(
            f'checking binary path: {binary_path} ... missing ... try another place')

    logging.error(
        f'no build dir. Please build the binary "python tools/build_apps.py {app_path}" and run pytest again')
    sys.exit(1)


@pytest.fixture
def unity_case_timeout(request: FixtureRequest) -> float:
    marker = request.node.get_closest_marker('unity_case_timeout')
    marker_timeout = marker.args[0] if marker else None
    return resolve_unity_case_timeout(marker_timeout)


@pytest.fixture(autouse=True)
@multi_dut_fixture
def junit_properties(
    test_case_name: str, record_xml_attribute: Callable[[str, object], None]
) -> None:
    """
    This fixture is autoused and will modify the junit report test case name to <target>.<config>.<case_name>
    """
    record_xml_attribute('name', test_case_name)


##################
# Hook functions #
##################
_idf_pytest_embedded_key = pytest.StashKey['IdfPytestEmbedded']


def pytest_addoption(parser: pytest.Parser) -> None:
    base_group = parser.getgroup('idf')
    base_group.addoption(
        '--env',
        help='only run tests matching the environment NAME.',
    )


def pytest_configure(config: Config) -> None:
    # Require cli option "--target"
    help_commands = ['--help', '--fixtures', '--markers', '--version']
    for cmd in help_commands:
        if cmd in config.invocation_params.args:
            target = 'unneeded'
            break
        else:
            target = config.getoption('target')
    if not target:
        raise ValueError('Please specify one target marker via "--target [TARGET]"')

    config.stash[_idf_pytest_embedded_key] = IdfPytestEmbedded(
        target=target,
        env_name=config.getoption('env'),
    )
    config.pluginmanager.register(config.stash[_idf_pytest_embedded_key])

    for name, description in {**TARGET_MARKERS, **ENV_MARKERS, **SPECIAL_MARKERS}.items():
        config.addinivalue_line('markers', f'{name}: {description}')

def pytest_unconfigure(config: Config) -> None:
    _pytest_embedded = config.stash.get(_idf_pytest_embedded_key, None)
    if _pytest_embedded:
        del config.stash[_idf_pytest_embedded_key]
        config.pluginmanager.unregister(_pytest_embedded)


class IdfPytestEmbedded:
    def __init__(
        self,
        target: Optional[str] = None,
        env_name: Optional[str] = None,
    ):
        # CLI options to filter the test cases
        self.target = target
        self.env_name = env_name

        self._failed_cases: List[
            Tuple[str, bool, bool]
        ] = []  # (test_case_name, is_known_failure_cases, is_xfail)

    @pytest.hookimpl(tryfirst=True)
    def pytest_sessionstart(self, session: Session) -> None:
        if self.target:
            self.target = self.target.lower()
            session.config.option.target = self.target

    @pytest.hookimpl(tryfirst=True)
    def pytest_collection_modifyitems(self, items: List[Function]) -> None:
        # sort by file path and callspec.config
        # implement like this since this is a limitation of pytest, couldn't get fixture values while collecting
        # https://github.com/pytest-dev/pytest/discussions/9689
        # after sort the test apps, the test may use the app cache to reduce the flash times.
        def _get_param_config(_item: Function) -> str:
            if hasattr(_item, 'callspec'):
                return _item.callspec.params.get('config', DEFAULT_SDKCONFIG)  # type: ignore
            return DEFAULT_SDKCONFIG

        items.sort(key=lambda x: (os.path.dirname(x.path), _get_param_config(x)))

        # set default timeout 10 minutes for each case
        for item in items:
            if 'timeout' not in item.keywords:
                item.add_marker(pytest.mark.timeout(10 * 60))

        # add markers for special markers
        for item in items:
            if 'supported_targets' in item.keywords:
                for _target in SUPPORTED_TARGETS:
                    item.add_marker(_target)
            if 'preview_targets' in item.keywords:
                for _target in PREVIEW_TARGETS:
                    item.add_marker(_target)
            if 'all_targets' in item.keywords:
                for _target in [*SUPPORTED_TARGETS, *PREVIEW_TARGETS]:
                    item.add_marker(_target)

            # add 'xtal_40mhz' tag as a default tag for esp32c2 target
            # only add this marker for esp32c2 cases
            if (
                self.target == 'esp32c2'
                and 'esp32c2' in item_marker_names(item)
                and 'xtal_26mhz' not in item_marker_names(item)
            ):
                item.add_marker('xtal_40mhz')

        # filter all the test cases with "nightly_run" marker
        if os.getenv('INCLUDE_NIGHTLY_RUN') == '1':
            # Do not filter nightly_run cases
            pass
        elif os.getenv('NIGHTLY_RUN') == '1':
            items[:] = [item for item in items if 'nightly_run' in item_marker_names(item)]
        else:
            items[:] = [item for item in items if 'nightly_run' not in item_marker_names(item)]

        # filter all the test cases with target and skip_targets
        filtered_items = []
        for item in items:
            if self.target in item_skip_targets(item):
                continue

            match_target = False
            if self.target in item_marker_names(item):
                match_target = True
            elif hasattr(item, 'callspec') and self.target == item.callspec.params.get('target'):
                match_target = True

            if match_target:
                filtered_items.append(item)

        items[:] = filtered_items
