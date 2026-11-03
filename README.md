# SwiftPack

**A high-performance, LZ4-inspired lossless compression library and CLI written in C.**

SwiftPack is a systems programming project focused on building a fast, lightweight compression engine from scratch. It explores hash-based match finding, efficient memory handling, parallel processing, and performance optimization.

The project aims to investigate the trade-offs between compression speed, decompression speed, and compression ratio by benchmarking against the reference LZ4 implementation.

> 🚧 **Status:** Under development

## Features

- [x] Lossless compression and decompression (initial format)
- [x] Hash-based match finding
- [x] Command-line interface (CLI)
- [x] Round-trip correctness tests
- [x] Deterministic fuzz smoke test (10,000 inputs)
- [x] Basic malformed-input validation
- [x] SSE2 match comparison with a portable scalar fallback
- [ ] Multithreaded block compression
- [x] CRC32 checksums for data integrity
- [ ] Performance benchmarks against LZ4

## Goals

- Build a compression engine from scratch using C.
- Understand LZ77-style matching and back-reference encoding.
- Explore low-level memory management and CPU-bound optimization.
- Investigate SIMD and parallel processing techniques.
- Measure compression throughput, decompression throughput, and compression ratio.
- Develop a maintainable library with a simple CLI.

## Tech Stack

- **Language:** C
- **Build System:** Make
- **Version Control:** Git and GitHub
- **Testing:** Automated round-trip tests, fuzzing, and sanitizers
- **Benchmarking:** Custom benchmarks against LZ4

## Project Structure

```text
SwiftPack/
├── include/       # Public library headers
├── src/           # Compression and decompression implementation
├── tests/         # Correctness and fuzz tests
├── benchmarks/    # Benchmarking tools and results
├── examples/      # Example usage
├── Makefile
├── README.md
└── LICENSE

```

## Build and use

Build with a C11 compiler and Make:

```sh
make
make test
make fuzz
./swiftpack.exe -c input.bin input.swp
./swiftpack.exe -d input.swp restored.bin
```

The `-c` operation compresses a file; `-d` decompresses it. New SwiftPack v2 streams begin with `SWPK`, a format version, and the original size, followed by literal runs, length/offset matches, and a CRC32 checksum of the uncompressed data. The decoder also accepts v1 streams, which do not contain a checksum. SwiftPack files are not compatible with LZ4 files.
