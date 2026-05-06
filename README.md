# Game Storage Manager

> Optimiza tu librería de juegos automáticamente sin comprometer rendimiento.

**Game Storage Manager** is an intelligent Windows desktop utility that reclaims disk space from your installed games using NTFS compression — without breaking anything, and always reversible.

## How It Works

Game Storage Manager uses Windows' built-in `compact.exe` to apply NTFS compression to game folders. Unlike generic compression tools, it analyzes each game and picks the right algorithm for the job:

| Algorithm | Best For |
|-----------|----------|
| **XPRESS4K** | Light/indie games, fastest decompression |
| **XPRESS8K** | Modern AAA games (default) |
| **XPRESS16K** | Stronger compression with moderate CPU cost |
| **LZX** | Archived or rarely played games, maximum savings |

The app scans your Steam/Epic libraries, analyzes each game's file structure, detects risky patterns (anti-cheat, DirectStorage, already-compressed assets), and recommends what's safe to optimize.

## Core Principles

- **Never break a game.** All compression is reversible with a single click.
- **Never block the UI.** Analysis and compression run in background threads.
- **Intelligent defaults.** No raw algorithm knobs — just clear, contextual recommendations.
- **Full transparency.** See current size, estimated savings, reason codes, and operation history.

## Architecture

```
ui/          ← Qt views, components, controllers (never calls compact.exe directly)
core/        ← Game models, analysis, rules engine, safety logic (testable without UI)
system/      ← Windows interactions, process execution, filesystem adapters
```

Dependency direction: `ui → core → system abstractions`

### Modules

| Module | Responsibility |
|--------|---------------|
| `core/scanner` | Steam library detection, Epic manifests, manual folders |
| `core/analyzer` | Size, file count, extension distribution, compressed asset detection |
| `core/rules_engine` | Recommendation logic, algorithm selection, risk classification |
| `core/compressor` | Compression task coordination, progress tracking |
| `core/safety` | Backup metadata, rollback, access validation |
| `system/process` | Safe `compact.exe` execution, cancellation, error handling |
| `system/filesystem` | Path walking, metadata, validation |

## Roadmap

### Phase 1 — MVP (current)
- [ ] Manual folder selection, analysis, and optimization
- [ ] `compact.exe` integration with background execution
- [x] CMake project with core/system modules
- [x] Analyzer, rules engine, and compact adapter
- [ ] Restore with `compact.exe /u /s`
- [ ] CLI harness for verification

### Phase 2 — Game Scanner
- [ ] Steam library auto-detection (`libraryfolders.vdf`)
- [ ] Epic Games detection
- [ ] Per-game saved space metrics
- [ ] Historical optimization records

### Phase 3 — Smart Rules
- [ ] Profiles: Performance, Balanced, Storage
- [ ] Anti-cheat and DirectStorage risk detection
- [ ] CPU/disk performance warnings
- [ ] Reason codes for every recommendation

### Phase 4 — Polish
- [ ] Qt dark minimal UI
- [ ] Pause/resume task queue
- [ ] Crash-safe task recovery
- [ ] Installer and release packaging

## Build from Source

### Prerequisites

- Windows 10/11
- CMake >= 3.20
- C++17 compiler (MSVC or MinGW GCC >= 9)
- Qt 6 (for UI, not required for current CLI phase)

### Build

```powershell
cmake -B build -G "MinGW Makefiles"
cmake --build build
```

Or with MSVC:

```powershell
cmake -B build
cmake --build build --config Release
```

Run the CLI harness:

```powershell
.\build\gsm_cli.exe
```

## Safety Guarantees

- **No file deletion.** Only NTFS compression flags are modified.
- **Full reversibility.** Restore original state with `compact.exe /u /s`.
- **Validation before and after.** Path existence and file access verified.
- **Risk-aware.** Skips anti-cheat-heavy games, DirectStorage titles, and already-compressed assets by default.
- **Partial failure handling.** If something fails, what succeeded is reported honestly.

## Tech Stack

- **Language:** C++17
- **Build:** CMake
- **UI:** Qt 6 (planned)
- **Compression:** Windows `compact.exe`
- **Platform:** Windows only

## Contributing

This project follows strict separation rules — see `ai-orchestration/` docs for architecture and implementation guidelines. Commits use conventional commit style (`feat:`, `fix:`, `chore:`, etc.).

## License

To be determined.
