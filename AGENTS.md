# VChar64 for Agents

## Project Overview
VChar64 is a multiplatform (Linux, macOS, Windows) editor for creating Commodore 64 charsets. It is a desktop application built with C++ and Qt 6.

## Tech Stack
- **Languages**: C++17
- **Framework**: Qt 6 (Widgets)
- **Build System**: CMake (>= 3.16)
- **Formatting**: Clang-Format (configuration in `.clang-format`)
- **CI/CD**: GitHub Actions

## Directory Structure
- `src/`: Core application source code.
- `tests/`: Unit tests and verification.
- `translations/`: Translation files (.ts).
- `server/`: TCP/IP server implementation for live preview.
- `examples/`: Example files.
- `.github/workflows/`: CI/CD configurations.

## Build Instructions
The project allows standard CMake build workflow.

### Prerequisites
- CMake
- Qt 6 (and `qt6-languagetools` or equivalent for translations)
- C++ Compiler supporting C++17

### Build Steps
```bash
mkdir build
cd build
cmake ..
make -j$(nproc)
```

## Testing
Unit tests are enabled via CTest.

```bash
cd build
ctest --output-on-failure
```

or using `make`:

```bash
cd build
make test
```

## Code Style
The project uses `clang-format`. Please ensure code is formatted before submitting changes.

```bash
clang-format -i src/**/*.cpp src/**/*.h
```

## Key Workflows
- **Translations Update**: `cmake --build . --target update_translations`
- **Translations Release**: `cmake --build . --target release_translations`
