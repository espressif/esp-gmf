"""
Compatibility checks for board manager Python tooling.

These checks intentionally focus on board-manager-maintained Python scripts and
exclude vendored/managed third-party trees.
"""

from pathlib import Path
import ast


EXCLUDED_PATH_PARTS = {
    '__pycache__',
    '.pytest_cache',
    'dist',
    'managed_components',
}

MIN_SUPPORTED_PYTHON = (3, 9)
TYPING_310_PLUS = {
    'Concatenate',
    'LiteralString',
    'NotRequired',
    'ParamSpec',
    'Required',
    'Self',
    'TypeAlias',
    'TypeGuard',
    'TypeVarTuple',
    'Unpack',
}


def _iter_board_manager_python_files(bmgr_root: Path):
    for path in sorted(bmgr_root.rglob('*.py')):
        if any(part in EXCLUDED_PATH_PARTS for part in path.parts):
            continue
        yield path


def _has_future_annotations(tree: ast.AST) -> bool:
    for node in tree.body:
        if isinstance(node, ast.ImportFrom) and node.module == '__future__':
            if any(alias.name == 'annotations' for alias in node.names):
                return True
        elif isinstance(node, ast.Expr) and isinstance(getattr(node, 'value', None), ast.Constant) and isinstance(node.value.value, str):
            continue
        else:
            break
    return False


def _iter_annotations(tree: ast.AST):
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            args = list(node.args.posonlyargs) + list(node.args.args) + list(node.args.kwonlyargs)
            for arg in args:
                if arg.annotation is not None:
                    yield ('arg', arg.arg, arg.annotation)
            if node.args.vararg and node.args.vararg.annotation is not None:
                yield ('vararg', node.args.vararg.arg, node.args.vararg.annotation)
            if node.args.kwarg and node.args.kwarg.annotation is not None:
                yield ('kwarg', node.args.kwarg.arg, node.args.kwarg.annotation)
            if node.returns is not None:
                yield ('return', node.name, node.returns)
        elif isinstance(node, ast.AnnAssign):
            target = getattr(node.target, 'id', None) or getattr(node.target, 'attr', '<target>')
            yield ('annassign', target, node.annotation)


def _contains_bitor(annotation: ast.AST) -> bool:
    return any(isinstance(node, ast.BinOp) and isinstance(node.op, ast.BitOr) for node in ast.walk(annotation))


def _scan_python_compatibility(bmgr_root: Path):
    findings = []

    for path in _iter_board_manager_python_files(bmgr_root):
        source = path.read_text(encoding='utf-8')
        tree = ast.parse(source)
        rel_path = path.relative_to(bmgr_root)
        future_annotations = _has_future_annotations(tree)

        for node in ast.walk(tree):
            if isinstance(node, ast.Match):
                findings.append(f'{rel_path}:{node.lineno}: uses match/case syntax (Python 3.10+)')
            elif isinstance(node, ast.Import):
                for alias in node.names:
                    if alias.name == 'tomllib':
                        findings.append(f'{rel_path}:{node.lineno}: imports tomllib (Python 3.11+)')
            elif isinstance(node, ast.ImportFrom):
                if node.module == 'typing':
                    for alias in node.names:
                        if alias.name in TYPING_310_PLUS:
                            findings.append(
                                f'{rel_path}:{node.lineno}: imports typing.{alias.name} '
                                f'(not guaranteed in Python {MIN_SUPPORTED_PYTHON[0]}.{MIN_SUPPORTED_PYTHON[1]})'
                            )
                elif node.module == 'itertools':
                    for alias in node.names:
                        if alias.name == 'pairwise':
                            findings.append(f'{rel_path}:{node.lineno}: imports itertools.pairwise (Python 3.10+)')
            elif isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id == 'zip':
                if any(keyword.arg == 'strict' for keyword in node.keywords):
                    findings.append(f'{rel_path}:{node.lineno}: uses zip(..., strict=...) (Python 3.10+)')

        for kind, name, annotation in _iter_annotations(tree):
            if _contains_bitor(annotation) and not future_annotations:
                findings.append(
                    f'{rel_path}:{getattr(annotation, "lineno", 1)}: '
                    f'annotation for {kind} {name!r} uses | without __future__.annotations'
                )

    return findings


def test_board_manager_python_scripts_remain_compatible_with_minimum_supported_python(bmgr_root):
    findings = _scan_python_compatibility(bmgr_root)

    assert findings == [], (
        f'Found Python {MIN_SUPPORTED_PYTHON[0]}.{MIN_SUPPORTED_PYTHON[1]} compatibility issues:\n'
        + '\n'.join(findings)
    )
