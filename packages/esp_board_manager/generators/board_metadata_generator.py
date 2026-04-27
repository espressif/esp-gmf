#!/usr/bin/env python3
"""
# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.
"""

"""Board metadata generator."""

from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

import yaml


class InlineList(list):
    """YAML sequence that should be emitted in flow style."""


class MetadataDumper(yaml.SafeDumper):
    """Custom dumper for board metadata."""


def _represent_inline_list(dumper, data):
    return dumper.represent_sequence('tag:yaml.org,2002:seq', data, flow_style=True)


MetadataDumper.add_representer(InlineList, _represent_inline_list)


def _format_family(format_name: Optional[str]) -> Optional[str]:
    if not format_name:
        return None
    return str(format_name).split('-', 1)[0]


def _normalize_scalar_io(value: Any) -> Optional[int]:
    if isinstance(value, bool) or not isinstance(value, int):
        return None
    if value < 0:
        return None
    return value


def _normalize_io_value(value: Any) -> Optional[Any]:
    scalar_value = _normalize_scalar_io(value)
    if scalar_value is not None:
        return scalar_value

    if isinstance(value, list):
        normalized_list = []
        for item in value:
            normalized_item = _normalize_scalar_io(item)
            if normalized_item is not None:
                normalized_list.append(normalized_item)
        if normalized_list:
            return normalized_list

    return None


def _stable_value_key(value: Any) -> Any:
    if isinstance(value, list):
        return tuple(value)
    return value


def _prune_empty(value: Any) -> Any:
    if isinstance(value, dict):
        pruned = {}
        for key, item in value.items():
            pruned_item = _prune_empty(item)
            if pruned_item in ({}, [], None):
                continue
            pruned[key] = pruned_item
        return pruned

    if isinstance(value, list):
        kept_items = [item for item in value if item not in ({}, [], None)]
        if isinstance(value, InlineList):
            return InlineList(kept_items)
        return kept_items

    return value


def _inline_io_lists(value: Any) -> Any:
    if isinstance(value, dict):
        return {key: _inline_io_lists(item) for key, item in value.items()}

    if isinstance(value, list):
        return InlineList(_inline_io_lists(item) for item in value)

    return value


def _optional_peripheral_role(role: Optional[str]) -> Optional[str]:
    if role in (None, '', 'none'):
        return None
    return role


def _optional_peripheral_format(format_name: Optional[str]) -> Optional[str]:
    if format_name in (None, ''):
        return None
    return format_name


def _format_metadata_yaml(yaml_text: str) -> str:
    lines = yaml_text.splitlines()
    formatted = []
    section_headers = ('devices:', 'peripherals:')

    for index, line in enumerate(lines):
        if (
            formatted
            and line in section_headers
            and formatted[-1] != ''
        ):
            formatted.append('')

        if (
            formatted
            and line.startswith('  ')
            and line.endswith(':')
            and not line.startswith('    ')
            and formatted[-1] != ''
            and formatted[-1] not in section_headers
        ):
            formatted.append('')

        formatted.append(line)

    return '\n'.join(formatted) + '\n'


class BoardMetadataGenerator:
    """Build and write unified board metadata YAML."""

    def _get_module_io_spec(self, parse_func, component_type: str, kind: str) -> Any:
        prefix = 'DEV' if kind == 'device' else 'PERIPH'
        constant_name = f'{prefix}_{component_type.upper()}_IO_LIST'
        return parse_func.__globals__.get(constant_name)

    def _get_custom_extractor(self, parse_func):
        return parse_func.__globals__.get('extract_metadata')

    def _resolve_io_fields(self, artifact: Dict[str, Any], kind: str) -> List[str]:
        parse_func = artifact.get('parse_func')
        if parse_func is None:
            return []

        io_spec = self._get_module_io_spec(parse_func, artifact.get('type', ''), kind)
        if io_spec is None:
            return []

        if isinstance(io_spec, (list, tuple, set)):
            return list(io_spec)

        if not isinstance(io_spec, dict):
            raise ValueError(
                f'Unsupported IO spec type for {kind} {artifact.get("name")}: '
                f'{type(io_spec).__name__}'
            )

        if kind == 'device':
            sub_type = artifact.get('sub_type')
            selected = io_spec.get(sub_type)
            if selected is None:
                selected = io_spec.get('default', [])
            return list(selected)

        role = artifact.get('role')
        format_name = artifact.get('format')
        format_family = _format_family(format_name)
        for key in (role, format_name, format_family, 'default'):
            if key is None:
                continue
            if key in io_spec:
                return list(io_spec[key])
        return []

    def _collect_matching_io(self, struct_init: Any, target_fields: Iterable[str]) -> Dict[str, Any]:
        targets = set(target_fields)
        if not targets:
            return {}

        matches: Dict[str, List[Any]] = {}

        def record(key: str, value: Any) -> None:
            normalized = _normalize_io_value(value)
            if normalized is None:
                return
            matches.setdefault(key, []).append(normalized)

        def walk(node: Any) -> None:
            if isinstance(node, dict):
                for key, value in node.items():
                    if key in targets:
                        record(key, value)

                    if isinstance(value, list):
                        if value and all(not isinstance(item, (dict, list)) for item in value):
                            for index, item in enumerate(value):
                                synthetic_key = f'{key}_{index}'
                                if synthetic_key in targets:
                                    record(synthetic_key, item)
                    walk(value)
                return

            if isinstance(node, list):
                for item in node:
                    walk(item)

        walk(struct_init)

        resolved: Dict[str, Any] = {}
        for key, values in matches.items():
            unique_values: Dict[Any, Any] = {}
            for value in values:
                unique_values[_stable_value_key(value)] = value

            if len(unique_values) > 1:
                raise ValueError(
                    f"Ambiguous IO field '{key}' produced multiple values in metadata extraction"
                )

            resolved[key] = next(iter(unique_values.values()))

        return resolved

    def _build_device_metadata(self, artifact: Dict[str, Any], context: Dict[str, Any]) -> Dict[str, Any]:
        extractor = self._get_custom_extractor(artifact.get('parse_func'))
        if extractor:
            custom_result = extractor(artifact['name'], artifact.get('raw', {}), artifact['result'], context)
            if not isinstance(custom_result, dict):
                raise ValueError(
                    f"Custom metadata extractor for device '{artifact['name']}' must return a dict"
                )
            io_metadata = _prune_empty(custom_result.get('io', {}))
        else:
            io_fields = self._resolve_io_fields(artifact, kind='device')
            io_metadata = self._collect_matching_io(artifact['result'].get('struct_init', {}), io_fields)
        io_metadata = _inline_io_lists(io_metadata)

        return _prune_empty({
            'type': artifact.get('type'),
            'sub_type': artifact.get('sub_type'),
            'peripherals': list(artifact.get('peripherals', [])),
            'dependencies': artifact.get('dependencies', {}) or {},
            'io': io_metadata,
        })

    def _build_peripheral_metadata(self, artifact: Dict[str, Any], context: Dict[str, Any]) -> Dict[str, Any]:
        extractor = self._get_custom_extractor(artifact.get('parse_func'))
        if extractor:
            custom_result = extractor(artifact['name'], artifact.get('raw', {}), artifact['result'], context)
            if not isinstance(custom_result, dict):
                raise ValueError(
                    f"Custom metadata extractor for peripheral '{artifact['name']}' must return a dict"
                )
            io_metadata = _prune_empty(custom_result.get('io', {}))
        else:
            io_fields = self._resolve_io_fields(artifact, kind='peripheral')
            io_metadata = self._collect_matching_io(artifact['result'].get('struct_init', {}), io_fields)
        io_metadata = _inline_io_lists(io_metadata)

        return _prune_empty({
            'type': artifact.get('type'),
            'role': _optional_peripheral_role(artifact.get('role')),
            'format': _optional_peripheral_format(artifact.get('format')),
            'io': io_metadata,
        })

    def generate_metadata_dict(
        self,
        board_name: str,
        chip_name: str,
        device_artifacts: List[Dict[str, Any]],
        peripheral_artifacts: List[Dict[str, Any]],
    ) -> Dict[str, Any]:
        context = {
            'board': board_name,
            'chip': chip_name,
        }

        devices = {}
        for artifact in device_artifacts:
            devices[artifact['name']] = self._build_device_metadata(artifact, context)

        peripherals = {}
        for artifact in peripheral_artifacts:
            peripherals[artifact['name']] = self._build_peripheral_metadata(artifact, context)

        return {
            'version': 1,
            'board': board_name,
            'chip': chip_name,
            'devices': devices,
            'peripherals': peripherals,
        }

    def write_metadata_file(
        self,
        output_path: str,
        board_name: str,
        chip_name: str,
        device_artifacts: List[Dict[str, Any]],
        peripheral_artifacts: List[Dict[str, Any]],
    ) -> Dict[str, Any]:
        metadata = self.generate_metadata_dict(
            board_name=board_name,
            chip_name=chip_name,
            device_artifacts=device_artifacts,
            peripheral_artifacts=peripheral_artifacts,
        )

        output = Path(output_path)
        output.parent.mkdir(parents=True, exist_ok=True)
        yaml_text = yaml.dump(
            metadata,
            Dumper=MetadataDumper,
            sort_keys=False,
            allow_unicode=False,
        )
        with output.open('w', encoding='utf-8') as f:
            f.write(_format_metadata_yaml(yaml_text))

        return metadata
