import ctypes
import ctypes.wintypes
import os
from pathlib import Path


def directory_exists(path: str) -> bool:
    return Path(path).is_dir()


def ensure_directory_exists(path: str) -> bool:
    p = Path(path)
    p.mkdir(parents=True, exist_ok=True)
    return p.is_dir()


def file_exists(path: str) -> bool:
    return Path(path).is_file()


def list_files_with_prefix_suffix(directory: str, prefix: str, suffix: str) -> list[str]:
    result = []
    try:
        for entry in os.scandir(directory):
            if not entry.is_file():
                continue
            name = entry.name
            matches_prefix = not prefix or name.startswith(prefix)
            matches_suffix = not suffix or name.endswith(suffix)
            if matches_prefix and matches_suffix and len(name) >= len(prefix) + len(suffix):
                result.append(entry.path)
    except OSError:
        return []
    result.sort()
    return result


def normalize_path(path: str) -> str:
    normalized = str(Path(path))
    while len(normalized) > 1 and normalized[-1] in ("\\", "/"):
        if len(normalized) == 3 and normalized[1] == ":":
            break
        normalized = normalized[:-1]
    return normalized


def join_path(left: str, right: str) -> str:
    if not left:
        return right
    return str(Path(left) / right)


def file_extension(file_name: str) -> str:
    return Path(file_name).suffix


_kernel32 = ctypes.windll.kernel32

_GetCompressedFileSizeW = _kernel32.GetCompressedFileSizeW
_GetCompressedFileSizeW.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.wintypes.DWORD)]
_GetCompressedFileSizeW.restype = ctypes.wintypes.DWORD

INVALID_FILE_SIZE = 0xFFFFFFFF
INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF
FILE_ATTRIBUTE_COMPRESSED = 0x00000800


def get_compressed_file_size(file_path: str) -> int | None:
    high = ctypes.wintypes.DWORD(0)
    low = _GetCompressedFileSizeW(file_path, ctypes.byref(high))
    if low != INVALID_FILE_SIZE or ctypes.get_last_error() == 0:
        return (high.value << 32) | low
    return None


def get_file_attributes(file_path: str) -> int:
    return _kernel32.GetFileAttributesW(file_path)


def is_ntfs_compressed(file_path: str) -> bool:
    attrs = get_file_attributes(file_path)
    if attrs == INVALID_FILE_ATTRIBUTES:
        return False
    return bool(attrs & FILE_ATTRIBUTE_COMPRESSED)


def directory_size(path: str) -> int:
    total = 0
    p = Path(path)
    if not p.is_dir():
        return total
    try:
        for entry in p.rglob("*"):
            if entry.is_file():
                try:
                    phys = get_compressed_file_size(str(entry))
                    if phys is not None:
                        total += phys
                    else:
                        total += entry.stat().st_size
                except OSError:
                    pass
    except PermissionError:
        pass
    return total
