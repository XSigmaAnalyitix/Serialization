# Build Optimization Guide

This guide explains how to use the build optimization features integrated into the CMake build system.

## Features

### 1. Compiler Caching (ccache, sccache, buildcache)

Compiler caching significantly speeds up rebuild times by caching compilation results.

#### Enabling Compiler Cache

```bash
# Using ccache (recommended for most systems)
cmake -B build -DXSIGMA_CACHE_TYPE=ccache

# Using sccache (Rust-based, good for distributed caching)
cmake -B build -DXSIGMA_CACHE_TYPE=sccache

# Using buildcache (C++-based alternative)
cmake -B build -DXSIGMA_CACHE_TYPE=buildcache

# Disable caching
cmake -B build -DXSIGMA_ENABLE_CACHE=OFF
```

#### Installation

**macOS:**
```bash
brew install ccache
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install ccache
```

**Linux (Fedora/RHEL):**
```bash
sudo dnf install ccache
```

### 2. Clang-Tidy Static Analysis

Clang-tidy performs comprehensive static code analysis and can automatically fix issues.

#### Enabling Clang-Tidy

```bash
# Enable static analysis checks
cmake -B build -DXSIGMA_ENABLE_CLANGTIDY=ON

# Enable with automatic fixes (WARNING: modifies source files)
cmake -B build -DXSIGMA_ENABLE_CLANGTIDY=ON -DXSIGMA_ENABLE_FIX=ON
```

#### Installation

**macOS:**
```bash
brew install llvm
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install clang-tools
```

**Linux (Fedora/RHEL):**
```bash
sudo dnf install clang-tools-extra
```

### 3. Distributed Compilation (Icecream)

Icecream enables distributed compilation across multiple machines.

#### Enabling Icecream

```bash
cmake -B build -DXSIGMA_ENABLE_ICECC=ON
```

#### Installation

**macOS:**
```bash
brew install icecc
```

**Linux (Ubuntu/Debian):**
```bash
sudo apt-get install icecc
```

## Combined Usage Example

```bash
# Clean build with all optimizations enabled
rm -rf build
cmake -B build \
  -DXSIGMA_CACHE_TYPE=ccache \
  -DXSIGMA_ENABLE_CLANGTIDY=ON \
  -DXSIGMA_ENABLE_ICECC=ON

cmake --build build
```

## Performance Tips

1. **First Build**: First builds won't benefit from caching. Subsequent builds will be much faster.
2. **Cache Size**: Monitor ccache size with `ccache -s` and clear if needed with `ccache -C`
3. **Clang-Tidy**: Run with `-DXSIGMA_ENABLE_CLANGTIDY=ON` during development, not in CI/CD
4. **Distributed Compilation**: Most effective for large projects with many files

## Configuration Files

- `cmake/cache.cmake` - Compiler caching configuration
- `cmake/clang_tidy.cmake` - Static analysis configuration

