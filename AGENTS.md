# VChar64 for Agents

## Project Overview

VChar64 is a multiplatform (Linux, macOS, Windows) desktop editor for creating Commodore 64, C128, and Atari 8-bit charsets, tilesets, and screen maps.

## Tech Stack

- **Language**: ISO C++20 (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_EXTENSIONS OFF`)
- **Framework**: Qt 6 (`Core`, `Gui`, `Network`, `Widgets`, `LinguistTools`, `Test`)
- **Build System**: CMake (>= 3.16)
- **Formatting & Hooks**: `clang-format` (`.clang-format`, WebKit base style) and `pre-commit` (`.pre-commit-config.yaml`)
- **CI/CD**: GitHub Actions (`.github/workflows/`)

## Layered Target Architecture

`src/CMakeLists.txt` enforces a strict 3-target acyclic dependency graph so core domain logic and network protocols can be unit-tested headlessly without a window server:

1. **`vchar64_core` (`STATIC`)**: Headless C64/VIC-II document model (`State`), `QUndoCommand`s (`commands.*`), cached rasterizers (`CharsetImageCache`, `TileAtlasCache`, `DocumentImageRenderer`), `ClipboardCodec`, file importers/exporters (`StateImport`, `StateExport`, `FileUtils`), `Palette`, and `ActiveDocumentRelay`. Links **only** `Qt6::Core` and `Qt6::Gui` — never `Qt6::Widgets` or `Qt6::Network`.
2. **`vchar64_net` (`STATIC`)**: Headless Contiki TCP live preview (`ServerPreview`, `ILivePreviewSink`, C89 wire protocol in `serverprotocol.h`) and HTTPS `AutoUpdater`. Links **only** `vchar64_core` and `Qt6::Network`.
3. **`vchar64` (`EXECUTABLE`)**: Desktop `Qt6::Widgets` MDI shell (`MainWindow`), canvas/palette widgets, and dialogs.

All library, executable, and test targets link privately against `vchar64_warnings` (`cmake/VChar64CompilerOptions.cmake`), enabling `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual -Wnull-dereference -Wformat=2`. Every change must compile with zero warnings.

## Directory Structure

- `src/`: Application source code (`src/core/`, `src/preview/`, `src/ui/`, widgets, and dialogs).
- `tests/unit_tests/`: Headless Qt Test suites (`tst_state_and_commands`, `tst_stateimport`, `tst_stateexport`, `tst_clipboard_and_network`).
- `translations/`: Qt Linguist translation catalogs (`vchar64_es.ts`).
- `server/`: Commodore 64 / Contiki TCP/IP live-preview receiver.
- `cmake/`: Shared compiler warning, sanitizer, and macOS SDK CMake modules.
- `tools/`: Developer and pre-commit helper scripts (`tools/cmake_run_tests.sh`).
- `examples/`: Sample projects and C64 assets.

## Build & Test Workflows

### Standard Build & Headless Unit Tests

```bash
cmake -B build
cmake --build build --parallel
QT_QPA_PLATFORM=offscreen ctest --test-dir build --output-on-failure
```

You can also run `./tools/cmake_run_tests.sh` from the repository root to auto-detect the newest configured build directory, perform a parallel incremental build, and run the full CTest suite headlessly.

### AddressSanitizer + UndefinedBehaviorSanitizer Build

```bash
cmake -B build_san -DVCHAR64_ENABLE_SANITIZERS=ON
cmake --build build_san --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1 \
QT_QPA_PLATFORM=offscreen ctest --test-dir build_san --output-on-failure
```

### Pre-Commit & Translations

- **Format C++ files**: `clang-format -i $(find src -name '*.cpp' -o -name '*.h')`
- **Verify formatting**: `clang-format --dry-run --Werror $(find src -name '*.cpp' -o -name '*.h')`
- **Run all pre-commit hooks**: `pre-commit run --all-files`
- **Update / release translations**:
    - `cmake --build build --target update_translations`
    - `cmake --build build --target release_translations`

## C++ Readability & Style Conventions

VChar64 pairs `.clang-format` (`BasedOnStyle: WebKit`) layout with **Google C++ Style Guide** readability conventions inside implementation files (`src/**/*.cpp`), while preserving Qt API conventions in headers (`src/**/*.h`):

### 1. Naming Boundary (`.h` vs `.cpp`)

- **Headers (`src/**/*.h`)**: Preserve existing Qt/project API names for classes (`PascalCase`), methods/signals/slots (`camelCase`), and member fields (`_camelCase` or `ui`) so public contracts, Qt `moc`/`uic` bindings, and unit tests remain stable.
- **`.cpp` Internals (`src/**/*.cpp`)**:
    - **Parameters & local variables**: `lower_snake_case` with descriptive domain names (`is_multicolor`, `tile_width`, `char_bytes`, `active_palette`). Avoid cryptic abbreviations (`ismc`, `tw`, `chardef`, `res`, `ret`, `tmp`, standalone `idx`).
    - **File-local helper functions & types**: `PascalCase` (`ResolveCharPixelColorIndex`, `StartupBanner`), with `lower_snake_case` fields on file-local structs.
    - **Constants**: `kCamelCase` (`kAsciiToPetscii`, `kMulticolorMasks`, `ImportedFileType::kVChar64`) strictly for `constexpr` or static-storage-duration constants and file-local `enum class` values. Runtime-computed `const` locals use `lower_snake_case`.

### 2. Internal Linkage, Constants & Includes

- Place all `.cpp`-local constants, lookup tables, helper structs, and free helper functions inside a single unnamed `namespace { ... } // namespace` near the top of the `.cpp` file (after `#include`s and before class/public function definitions), ordered before their callers.
- Prefer unnamed-namespace free functions over `private` helper methods whenever the helper does not need access to `private` class members, and never use file-scope `static` or forward declarations in `.cpp` files.
- Never define namespace-scope or function-local `static` constants with non-trivial destructors (`QString`, `QHash`, `QRegularExpression`, `std::string`, `std::vector`). Use `constexpr` aggregates (`std::array`, `std::string_view`, `QLatin1StringView`) instead.
- Include headers directly for every type used in a `.cpp` file rather than relying on transitive includes, and order `#include` blocks as: matching header first, C++/STL `<...>` headers, Qt `<Q...>` headers, then project `"..."` headers.

### 3. Method Order, Types & Control Flow

- **Declaration-mirrored definitions**: Order method definitions in each `.cpp` file to match their declaration order in the corresponding `.h` file (`public` -> `public slots` -> `protected` -> `private slots` -> `private`).
- **Explicit types over opaque `auto`**: Spell out concrete return types (`const State::CharsetBuffer&`, `vchar64::Result<void>`, `QMdiSubWindow*`) whenever the type is not literally written on the right-hand side. Reserve `auto` (qualified with `const`, `&`, or `*`) for `std::make_unique<T>`, `qobject_cast<T*>`, `new T(...)`, verbose iterators, and structured bindings.
- **Flat control flow & braces**: Prefer early-return / `continue` guard clauses over deeply nested conditionals. Always wrap all branches of `if` / `else` chains in `{ ... }` braces.
- **Type safety & idioms**: Use C++ casts (`static_cast`, `qobject_cast`) — never C-style casts (`-Wold-style-cast`). Compare strings with `==` rather than `.compare() == 0`, avoid `std::endl`, and never write `using namespace ...;`.

### 4. Comments, Licenses & Qt Translations

- **License headers**: Preserve the Apache 2.0 `/*** ... ***/` copyright block at the top of every source file.
- **Google-style `//` prose comments in `.cpp` files**: Below the license header, use `//` line comments written in clear, complete sentences explaining *what* and *why* (including what returned values represent on non-trivial helpers). Do not use Doxygen tags (`@file`, `@brief`, `@param`, `@return`) or `/* ... */` block comments in `.cpp` files.
- **Translation stability**: Never alter string literals or context names passed to `tr(...)`, `QCoreApplication::translate(...)`, or `QT_TR_NOOP(...)` during refactors unless intentionally updating `translations/vchar64_es.ts`. Keep `tr(...)` calls inside `QObject` subclass methods (or qualify explicitly as `ClassName::tr(...)` in helpers) so Qt `lupdate` preserves the translation context.
