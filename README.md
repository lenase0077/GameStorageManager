# Game Storage Manager

> Optimiza tu librería de juegos automáticamente sin comprometer rendimiento.

**Game Storage Manager** es una herramienta de escritorio para Windows que recupera espacio en disco de tus juegos instalados usando compresión NTFS — sin romper nada, y siempre reversible.

## Migración a Python

Este proyecto fue migrado de C++ a Python. La versión original en C++ se encuentra en la rama `cpp-legacy`.

### Cambios principales

- **Lenguaje:** C++17 → Python 3.11+
- **UI:** Qt6 (C++) → PySide6 (Python)
- **Build:** CMake → setuptools + PyInstaller
- **APIs Windows:** Win32 nativo → ctypes + winreg + subprocess
- **Compatibilidad:** 100% compatible con archivos `.gsmmeta` y `library.json` existentes

## Cómo funciona

Game Storage Manager usa `compact.exe` de Windows para aplicar compresión NTFS a carpetas de juegos. A diferencia de herramientas genéricas, analiza cada juego y elige el algoritmo adecuado:

| Algoritmo | Mejor para |
|-----------|------------|
| **XPRESS4K** | Juegos ligeros/indie, descompresión más rápida |
| **XPRESS8K** | Juegos AAA modernos (por defecto) |
| **XPRESS16K** | Compresión más fuerte con costo moderado de CPU |
| **LZX** | Juegos archivados o raramente jugados, máximo ahorro |

La app escanea tus librerías de Steam/Epic, analiza la estructura de archivos de cada juego, detecta patrones riesgosos (anti-cheat, DirectStorage, assets ya comprimidos), y recomienda qué es seguro optimizar.

## Principios fundamentales

- **Nunca romper un juego.** Toda compresión es reversible con un solo clic.
- **Nunca bloquear la UI.** El análisis y la compresión se ejecutan en hilos de fondo.
- **Defaults inteligentes.** Sin perillas de algoritmos crudos — solo recomendaciones claras y contextuales.
- **Transparencia total.** Ve tamaño actual, ahorros estimados, códigos de razón e historial de operaciones.

## Arquitectura

```
game_storage_manager/
  ui/              ← Vistas PySide6, componentes, controladores (nunca llama compact.exe directamente)
  core/            ← Modelos de juegos, análisis, motor de reglas, lógica de seguridad (testeable sin UI)
  system/          ← Interacciones con Windows, ejecución de procesos, adaptadores de filesystem
  app/             ← Entry points (CLI y GUI)
  resources/       ← Iconos SVG, tema QSS
  utils.py         ← Utilidades compartidas
```

Dirección de dependencias: `ui → core → abstracciones de system`

### Módulos

| Módulo | Responsabilidad |
|--------|----------------|
| `core/scanner` | Detección de librerías Steam, manifests Epic, carpetas manuales |
| `core/analyzer` | Tamaño, conteo de archivos, distribución de extensiones, detección de assets comprimidos |
| `core/rules_engine` | Lógica de recomendación, selección de algoritmo, clasificación de riesgo |
| `core/compressor` | Coordinación de tareas de compresión, seguimiento de progreso |
| `core/safety` | Metadatos de backup, rollback, validación de acceso |
| `system/process` | Ejecución segura de `compact.exe`, cancelación, manejo de errores |
| `system/filesystem` | Recorrido de paths, metadatos, validación |

## Instalación

### Desde ejecutable

Descarga `GameStorageManager.exe` desde [Releases](https://github.com/lenase0077/GameStorageManager/releases).

### Desde código fuente

#### Requisitos

- Windows 10/11
- Python >= 3.11
- pip

#### Setup

```powershell
# Clonar el repositorio
git clone https://github.com/lenase0077/GameStorageManager.git
cd GameStorageManager

# Crear entorno virtual
python -m venv venv
.\venv\Scripts\Activate.ps1

# Instalar dependencias
pip install -e ".[dev]"
```

#### Ejecutar

```powershell
# GUI
python -m game_storage_manager.app.gui.main

# CLI
python -m game_storage_manager.app.cli.main analyze <carpeta>
python -m game_storage_manager.app.cli.main scan-steam
python -m game_storage_manager.app.cli.main compact-command <compress|restore> <carpeta> [algoritmo]
```

#### Tests

```powershell
pytest tests/ -v
```

#### Linting

```powershell
ruff check game_storage_manager/ tests/
```

#### Generar ejecutable

```powershell
pyinstaller --onefile --windowed --name "GameStorageManager" --add-data "game_storage_manager/resources;game_storage_manager/resources" game_storage_manager/app/gui/main.py
```

El ejecutable se generará en `dist/GameStorageManager.exe`.

## Roadmap

### Fase 1 — MVP ✓
- [x] Selección manual de carpetas, análisis y optimización
- [x] Integración con `compact.exe` con ejecución en fondo
- [x] Analizador, motor de reglas y adaptador compact
- [x] Restauración con `compact.exe /u /s`
- [x] CLI para verificación

### Fase 2 — Game Scanner ✓
- [x] Auto-detección de librerías Steam (`libraryfolders.vdf`)
- [ ] Detección de Epic Games (Planeado)
- [x] Métricas de espacio ahorrado por juego
- [x] Registros históricos de optimización (Metadatos JSON persistentes)

### Fase 3 — Reglas inteligentes ✓
- [x] Perfiles: Rápido, Balanceado, Fuerte, Máximo
- [x] Mecanismos de cancelación segura y verificaciones de fallback
- [ ] Detección de riesgo de anti-cheat y DirectStorage
- [x] Códigos de razón para cada recomendación

### Fase 4 — Pulido (Actual)
- [x] UI oscura minimalista con Qt (estilo Catppuccin Mocha)
- [x] Sistema de cola para análisis por lotes
- [ ] Pausar/reanudar cola de tareas
- [x] Integración de iconos SVG modernos (Lucide)
- [ ] Instalador y empaquetado de release
- [x] Migración completa a Python

## Garantías de seguridad

- **Sin eliminación de archivos.** Solo se modifican flags de compresión NTFS.
- **Reversibilidad total.** Restaura el estado original con `compact.exe /u /s`.
- **Validación antes y después.** Existencia de paths y acceso a archivos verificados.
- **Consciente de riesgos.** Omite juegos con anti-cheat pesado, títulos DirectStorage y assets ya comprimidos por defecto.
- **Manejo de fallos parciales.** Si algo falla, lo que succeeded se reporta honestamente.

## Stack técnico

- **Lenguaje:** Python 3.11+
- **UI:** PySide6 (Qt6)
- **Compresión:** Windows `compact.exe`
- **Empaquetado:** PyInstaller
- **Testing:** pytest
- **Linting:** ruff
- **Plataforma:** Solo Windows

## Contribuir

Este proyecto sigue reglas estrictas de separación — ver documentación de arquitectura. Los commits usan estilo conventional commit (`feat:`, `fix:`, `chore:`, etc.).

## Licencia

Por determinar.
