# Dependency Baseline

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
