# CTest Usage Guide

This project now uses CTest for running serialization tests instead of manually executing test binaries.

## Quick Start

### List all available tests
```bash
cd build
ctest -N
```

### Run all tests
```bash
cd build
ctest
```

### Run all tests with verbose output
```bash
cd build
ctest --verbose
```

### Run a specific test
```bash
cd build
ctest -R TestJsonSerialization
```

### Run tests matching a pattern
```bash
cd build
ctest -R "Json"  # Runs all tests with "Json" in the name
```

### Run tests with output on failure
```bash
cd build
ctest --output-on-failure
```

### Re-run only failed tests
```bash
cd build
ctest --rerun-failed --output-on-failure
```

## Available Tests

- **TestJsonSerialization**: Tests JSON serialization/deserialization
- **TestBinarySerialization**: Tests binary serialization/deserialization

## Test Results

Current status:
- ✅ TestJsonSerialization: **PASSED**
- ❌ TestBinarySerialization: **FAILED** (pre-existing assertion failure in binary stream type-tag handling)

## Configuration

CTest is configured in:
- `CMakeLists.txt` - Enables CTest with `enable_testing()`
- `include/Testing/Cxx/CMakeLists.txt` - Registers individual tests with `add_test()`

Each test executable is automatically registered with CTest and can be run through the CTest interface.

## Integration with CI/CD

CTest can be integrated into CI/CD pipelines:
```bash
cd build
cmake ..
make
ctest --output-on-failure
```

The exit code will be non-zero if any tests fail, making it suitable for automated testing.

