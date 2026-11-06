<!--
SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only

Copyright (c) 2026 xiphonics, inc.

Inherited BSD-licensed material remains BSD-3-Clause.
New original contributions by xiphonics, inc. are GPL-3.0-only.
Previously granted BSD permissions are preserved. See LICENSING.md.
-->

# Host Tests
This directory contains host-side unit and integration tests for the `picoTracker` project. The tests use the `doctest` framework and are designed to run on your development machine (host) rather than the target STM32 hardware.

## Building the Tests

The host tests are built using CMake. Follow these steps to configure and build them:

```bash
# Navigate to the tests directory
cd tests

# Create a build directory
mkdir -p build

# Configure the build with CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Compile the test targets
cmake --build build
```

## Test Targets

The build system produces several test executables in `tests/build/`:

| Target | Description |
| :--- | :--- |
| `picoTracker_tests` | General application tests (audio, MIDI, model, etc.) |
| `picoTracker_launchpad_tests` | Tests specifically for the launchpad MIDI control surface |
| `picoTracker_boundary_tests` | Boundary/stubbed tests for system-level components |

## Running Tests

You can run the test executables directly.

### Running all tests in a target
```bash
# Run all tests in the main test suite
./build/picoTracker_tests

# Run launchpad tests
./build/picoTracker_launchpad_tests

# Run boundary tests
./build/picoTracker_boundary_tests
```

### Filtering Tests (doctest)
Since the tests use `doctest`, you can use various filters to run specific subsets of tests.

#### Common Filters
*   `-tc, --test-case=<filter>`: Filter tests by their name.
*   `-tce, --test-case-exclude=<filter>`: Exclude tests matching the filter.
*   `-sf, --source-file=<filter>`: Filter tests by their source file.
*   `-lts, --list-test-suites`: Lists all registered test suites.

#### Examples
```bash
# List all registered tests
./build/picoTracker_tests --list-test-cases

# Run a specific test by name
./build/picoTracker_tests --test-case="midi_instrument_tests"

# Run tests that match a certain pattern (e.g., all voice tests)
./build/picoTracker_tests --test-case="*voice*"

# Run all tests except those matching a pattern
./build/picoTracker_tests --test-case-exclude="*audio*"
```

#### Useful Flags
| Flag | Description |
| :--- | :--- |
| `-s, --success` | Include successful assertions in the output (useful for debugging) |
| `-q, --quiet` | Minimal output (only failures) |
| `-d, --duration` | Prints the time duration of each test |
| `-m, --minimal` | Minimal console output (only failures) |
