# Dependency Baseline

## OpenHarmony ISA Generator

- Reference: runtime_core `3e147bb76e0b622cc2e4399ef954b030c0639691`,
  OpenHarmony-v7.0-Release, Apache-2.0.
- Inputs: `isa/isa.yaml`, `isa/isapi.rb`, `isa/gen.rb`; SHA-256 identities are
  enforced at configure time and before build-time generation by
  `cmake/IsaDependency.cmake`. No runtime dependency on Ruby.
- Host Ruby 3.2.3 is pinned for reproducible generation of opcode, operand,
  transfer and test-inventory tables. Generated files stay in the build tree.
- The upstream generator uses deprecated positional `ERB.new` arguments and
  emits Ruby warnings. Reference files are not patched to hide those warnings.
- Project templates interpret upstream identifiers at this generation boundary;
  production decoding and control flow use typed opcodes and fields only.

## nlohmann/json

- Local source: `/home/rabbit/arkruntime/third_party/json`.
- Git commit: `82f689380591b58b99578e6744a4f5afd2162084`, part of the pinned
  OpenHarmony-v7.0-Release reference checkout.
- Header: `single_include/nlohmann/json.hpp`.
- SHA-256: `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`.
- License: MIT, upstream `LICENSE.MIT`, copyright Niels Lohmann 2013-2025.
- Purpose: structured corpus manifest serialization only. JSON strings must not
  replace typed classification, profile, error, or evidence records in the engine.
- Build must verify the header digest, not silently accept another local version.
  The source path may be configured; network fetching is not needed here.

## OpenSSL

- SHA-256 through the EVP API, supplied by the system OpenSSL 3 development package.
- Observed build environment: OpenSSL 3.0.13, 30 Jan 2024.
- License: Apache-2.0 for OpenSSL 3. Keep using system security-maintained packages
  rather than vendoring or freezing an old binary. Record the actual linked
  version in build/validation evidence. EVP calls must check return statuses.
- Hash incrementally in bounded buffers. This avoids both a custom cryptographic
  implementation and whole-file allocation for the multi-gigabyte corpus.

## zlib

- System zlib supplies incremental Adler32, the checksum specified by ABC.
- Seed 1; process bytes from offset 12 through EOF, excluding magic/checksum.
- License: zlib license. CMake records the discovered package version in its
  configure output. Checksum correctness tests use an independent bounded oracle.
