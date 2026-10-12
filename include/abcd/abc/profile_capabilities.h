#ifndef ABCD_ABC_PROFILE_CAPABILITIES_H_
#define ABCD_ABC_PROFILE_CAPABILITIES_H_

#include "abcd/abc/header.h"

namespace abcd::abc {
// Profile capability table (S4): one row per container profile, one column
// per separately evidenced capability. Semantic stages query a column, never
// a hand-written list of profiles.
//
// Evidence (pinned OpenHarmony-v7.0-Release, see
// docs/reference/openharmony-v7.0-release.lock.json):
// - runtime_core isa/isa.yaml:109-120: current dynamic version 24.0.0.0
//   (API 24+); API 18-23 map to 13.0.1.0, API 12-17 to 12.0.6.0;
//   11.0.0.0/11.0.1.0 are incompatible.
// - ets_runtime: the interpreter dispatches by opcode only. The file-version
//   dependent behaviours are JSPandaFile::IsNewVersion (versions above
//   0.0.0.2) and libpandafile ContainsLiteralArrayInHeader (file.h:546,
//   versions up to 12.0.6.0); the latter is a container layout difference
//   handled by the abc layer.
// Therefore an opcode admitted for 13.0.1.0 has the same runtime semantics in
// 24.0.0.0. Opcodes that exist only in a newer ISA are still refused
// instruction by instruction until lifted.
struct ProfileCapabilities {
  // Header, metadata, string/literal and method-body decoding (src/abc).
  bool container_decoding = false;
  // Same per-opcode semantics as the pinned v7 interpreter; admits semantic
  // lifting and value-tree source recovery.
  bool pinned_runtime_semantics = false;
  // Literal arrays referenced from the file header (<= 12.0.6.0).
  bool literal_arrays_in_header = false;
};

constexpr ProfileCapabilities CapabilitiesOf(ProfileId profile) {
  switch (profile) {
    case ProfileId::kDynamic1202:
    case ProfileId::kDynamic1206:
      return {true, true, true};
    case ProfileId::kDynamic1301:
    case ProfileId::kDynamic2400:
      return {true, true, false};
    // Decoded, but historical lowering is not evidenced against the pinned
    // runtime: semantic recovery stays refused (typed kProfile failure).
    case ProfileId::kDynamic9:
    case ProfileId::kDynamic1102:
      return {true, false, true};
    case ProfileId::kStatic0107:
    case ProfileId::kUnknown:
      return {};
  }
  return {};
}

constexpr bool HasPinnedRuntimeSemantics(ProfileId profile) {
  return CapabilitiesOf(profile).pinned_runtime_semantics;
}
}  // namespace abcd::abc

#endif  // ABCD_ABC_PROFILE_CAPABILITIES_H_
