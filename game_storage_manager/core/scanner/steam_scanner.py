from __future__ import annotations

import os
import winreg

from game_storage_manager.core.scanner.game_entry import GameEntry, GameSource
from game_storage_manager.system.filesystem.filesystem import (
    directory_exists,
    join_path,
    list_files_with_prefix_suffix,
    normalize_path,
)


def _read_text_file(path: str) -> str:
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            return f.read()
    except OSError:
        return ""


def _registry_string(root: int, sub_key: str, value_name: str) -> str:
    try:
        key = winreg.OpenKey(root, sub_key)
        value, _ = winreg.QueryValueEx(key, value_name)
        winreg.CloseKey(key)
        return str(value) if value else ""
    except OSError:
        return ""


def _environment_variable(name: str) -> str | None:
    value = os.environ.get(name)
    return value if value else None


def _is_numeric(value: str) -> bool:
    return bool(value) and value.isdigit()


def _unescape_vdf_string(value: str) -> str:
    result = []
    i = 0
    while i < len(value):
        ch = value[i]
        if ch != "\\" or i + 1 >= len(value):
            result.append(ch)
            i += 1
            continue
        i += 1
        nxt = value[i]
        if nxt == "\\":
            result.append("\\")
        elif nxt == '"':
            result.append('"')
        elif nxt == "n":
            result.append("\n")
        elif nxt == "t":
            result.append("\t")
        else:
            result.append(nxt)
        i += 1
    return "".join(result)


def _tokenize_vdf(text: str) -> list[str]:
    tokens: list[str] = []
    i = 0
    while i < len(text):
        ch = text[i]

        if ch.isspace():
            i += 1
            continue

        if ch == "/" and i + 1 < len(text) and text[i + 1] == "/":
            while i < len(text) and text[i] != "\n":
                i += 1
            continue

        if ch in ("{", "}"):
            tokens.append(ch)
            i += 1
            continue

        if ch == '"':
            i += 1
            value_chars: list[str] = []
            while i < len(text):
                current = text[i]
                i += 1
                if current == '"':
                    break
                if current == "\\" and i < len(text):
                    value_chars.append(current)
                    value_chars.append(text[i])
                    i += 1
                    continue
                value_chars.append(current)
            tokens.append(_unescape_vdf_string("".join(value_chars)))
            continue

        bare: list[str] = []
        while i < len(text):
            current = text[i]
            if current.isspace() or current in ("{", "}"):
                break
            bare.append(current)
            i += 1
        tokens.append("".join(bare))

    return tokens


def _add_unique_path(paths: list[str], path: str) -> None:
    normalized = normalize_path(path)
    if not normalized:
        return
    key = normalized.lower()
    for existing in paths:
        if normalize_path(existing).lower() == key:
            return
    paths.append(normalized)


def _add_unique_game(games: list[GameEntry], game: GameEntry) -> None:
    key = (game.source_id + "|" + normalize_path(game.install_path)).lower()
    for existing in games:
        if (existing.source_id + "|" + normalize_path(existing.install_path)).lower() == key:
            return
    games.append(game)


class SteamScanner:
    def scan_installed_games(self) -> list[GameEntry]:
        libraries: list[str] = []
        for steam_root in self.discover_steam_roots():
            for library in self.discover_library_folders(steam_root):
                _add_unique_path(libraries, library)

        games: list[GameEntry] = []
        for library in libraries:
            for game in self.scan_library(library):
                _add_unique_game(games, game)

        games.sort(key=lambda g: g.name)
        return games

    def discover_steam_roots(self) -> list[str]:
        roots: list[str] = []

        _add_unique_path(
            roots, _registry_string(winreg.HKEY_CURRENT_USER, "Software\\Valve\\Steam", "SteamPath"))
        _add_unique_path(
            roots, _registry_string(
                winreg.HKEY_LOCAL_MACHINE, "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"))
        _add_unique_path(
            roots, _registry_string(winreg.HKEY_LOCAL_MACHINE, "SOFTWARE\\Valve\\Steam", "InstallPath"))

        pf_x86 = _environment_variable("ProgramFiles(x86)")
        if pf_x86:
            _add_unique_path(roots, join_path(pf_x86, "Steam"))

        pf = _environment_variable("ProgramFiles")
        if pf:
            _add_unique_path(roots, join_path(pf, "Steam"))

        roots = [r for r in roots if directory_exists(r)]
        return roots

    def discover_library_folders(self, steam_root: str) -> list[str]:
        vdf_path = join_path(join_path(steam_root, "steamapps"), "libraryfolders.vdf")
        text = _read_text_file(vdf_path)
        if not text:
            return []
        return self.parse_library_folders_vdf(steam_root, text)

    def scan_library(self, library_path: str) -> list[GameEntry]:
        steam_apps_path = join_path(library_path, "steamapps")
        games: list[GameEntry] = []
        for manifest_path in list_files_with_prefix_suffix(steam_apps_path, "appmanifest_", ".acf"):
            text = _read_text_file(manifest_path)
            game = self.parse_app_manifest(library_path, text)
            if game is not None:
                games.append(game)
        return games

    @staticmethod
    def parse_library_folders_vdf(steam_root: str, text: str) -> list[str]:
        libraries: list[str] = []
        _add_unique_path(libraries, steam_root)

        tokens = _tokenize_vdf(text)
        i = 0
        while i + 1 < len(tokens):
            token = tokens[i]
            nxt = tokens[i + 1]

            if token == "path" and nxt not in ("{", "}"):
                _add_unique_path(libraries, nxt)
                i += 1
                continue

            if _is_numeric(token) and nxt not in ("{", "}"):
                _add_unique_path(libraries, nxt)

            i += 1

        return libraries

    @staticmethod
    def parse_app_manifest(library_path: str, text: str) -> GameEntry | None:
        tokens = _tokenize_vdf(text)

        app_id = ""
        name = ""
        install_dir = ""

        i = 0
        while i + 1 < len(tokens):
            key = tokens[i]
            value = tokens[i + 1]

            if value in ("{", "}"):
                i += 1
                continue

            if key == "appid":
                app_id = value
            elif key == "name":
                name = value
            elif key == "installdir":
                install_dir = value

            i += 1

        if not app_id or not name or not install_dir:
            return None

        entry = GameEntry()
        entry.source = GameSource.Steam
        entry.source_id = app_id
        entry.name = name
        entry.library_path = normalize_path(library_path)
        entry.install_path = join_path(join_path(entry.library_path, "steamapps\\common"), install_dir)
        entry.installed = True
        return entry
