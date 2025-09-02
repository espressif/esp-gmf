# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

"""
Cache utilities for ESP Board Manager
"""

import os
import json
import hashlib
from pathlib import Path
from typing import Any, Dict, Optional
from functools import wraps
import time


class FileCache:
    """Simple file-based cache for expensive operations"""

    def __init__(self, cache_dir: Path = None):
        if cache_dir is None:
            cache_dir = Path.home() / '.gmf_cache'

        self.cache_dir = cache_dir
        self.cache_dir.mkdir(exist_ok=True)

    def _get_cache_key(self, key: str) -> str:
        """Generate cache key"""
        return hashlib.md5(key.encode()).hexdigest()

    def _get_cache_file(self, key: str) -> Path:
        """Get cache file path"""
        cache_key = self._get_cache_key(key)
        return self.cache_dir / f'{cache_key}.json'

    def get(self, key: str, max_age: int = 3600) -> Optional[Any]:
        """Get value from cache"""
        cache_file = self._get_cache_file(key)

        if not cache_file.exists():
            return None

        # Check file age
        file_age = time.time() - cache_file.stat().st_mtime
        if file_age > max_age:
            cache_file.unlink()
            return None

        try:
            with open(cache_file, 'r') as f:
                data = json.load(f)
            return data['value']
        except Exception:
            # If cache file is corrupted, remove it
            cache_file.unlink()
            return None

    def set(self, key: str, value: Any) -> bool:
        """Set value in cache"""
        cache_file = self._get_cache_file(key)

        try:
            data = {
                'key': key,
                'value': value,
                'timestamp': time.time()
            }
            with open(cache_file, 'w') as f:
                json.dump(data, f)
            return True
        except Exception:
            return False

    def clear(self) -> bool:
        """Clear all cache"""
        try:
            for cache_file in self.cache_dir.glob('*.json'):
                cache_file.unlink()
            return True
        except Exception:
            return False


def cached(max_age: int = 3600):
    """Decorator for caching function results"""
    def decorator(func):
        cache = FileCache()

        @wraps(func)
        def wrapper(*args, **kwargs):
            # Create cache key from function name and arguments
            key_parts = [func.__name__]
            key_parts.extend(str(arg) for arg in args)
            key_parts.extend(f'{k}={v}' for k, v in sorted(kwargs.items()))
            key = '|'.join(key_parts)

            # Try to get from cache
            cached_result = cache.get(key, max_age)
            if cached_result is not None:
                return cached_result

            # Execute function and cache result
            result = func(*args, **kwargs)
            cache.set(key, result)
            return result

        return wrapper
    return decorator


class DirectoryScanner:
    """Optimized directory scanner with caching"""

    def __init__(self, cache_dir: Path = None):
        self.cache = FileCache(cache_dir)

    @cached(max_age=1800)  # Cache for 30 minutes
    def scan_directory(self, directory: Path, extensions: set = None) -> list:
        """Scan directory for files with given extensions"""
        if extensions is None:
            extensions = {'.py', '.yml', '.yaml'}

        files = []
        for file_path in directory.rglob('*'):
            if file_path.is_file() and file_path.suffix in extensions:
                files.append(str(file_path))

        return files

    def get_directory_hash(self, directory: Path) -> str:
        """Get hash of directory contents for cache invalidation"""
        if not directory.exists():
            return ''

        hashes = []
        for file_path in sorted(directory.rglob('*')):
            if file_path.is_file():
                hashes.append(f'{file_path}:{file_path.stat().st_mtime}')

        return hashlib.md5('|'.join(hashes).encode()).hexdigest()
