#include "manifest_json.h"

#include <array>
#include <map>
#include <set>

namespace abcd::corpus {
namespace {
constexpr std::uint32_t kVersionOffset = 12;
constexpr std::uint32_t kVersionEnd = 16;
constexpr std::uint32_t kChecksumOffset = 8;
constexpr std::uint32_t kLanguageTagWidth = 2;
constexpr std::uint64_t kEtsLanguage = 5;
constexpr abc::AbcVersion kLegacyStatic{{0, 0, 0, 6}};
constexpr abc::AbcVersion kCurrentStatic{{0, 1, 0, 7}};
constexpr abc::AbcVersion kIncompatible1100{{11, 0, 0, 0}};
constexpr abc::AbcVersion kIncompatible1101{{11, 0, 1, 0}};

// This schema table identifies header profiles, not decompilation support.
constexpr std::array kVersionProfiles = {
    std::pair{abc::AbcVersion{{9, 0, 0, 0}}, abc::ProfileId::kDynamic9},
    std::pair{abc::AbcVersion{{11, 0, 2, 0}}, abc::ProfileId::kDynamic1102},
    std::pair{abc::AbcVersion{{12, 0, 2, 0}}, abc::ProfileId::kDynamic1202},
    std::pair{abc::AbcVersion{{12, 0, 6, 0}}, abc::ProfileId::kDynamic1206},
    std::pair{abc::AbcVersion{{13, 0, 1, 0}}, abc::ProfileId::kDynamic1301},
    std::pair{abc::AbcVersion{{24, 0, 0, 0}}, abc::ProfileId::kDynamic2400},
    std::pair{kCurrentStatic, abc::ProfileId::kStatic0107}};

abc::ProfileId ProfileFor(const std::optional<abc::AbcVersion>& version) {
  for (const auto& [value, profile] : kVersionProfiles) {
    if (version == value) return profile;
  }
  return abc::ProfileId::kUnknown;
}

bool VersionEvidence(const Detection& detection, EvidenceRule rule) {
  if (!detection.version) return false;
  constexpr unsigned int kByteBits = 8;
  std::uint32_t packed = 0;
  for (const auto byte : detection.version->bytes) packed = packed << kByteBits | byte;
  return detection.evidence == Evidence{rule, kVersionOffset, packed};
}

bool ValidDetection(const CorpusEntry& entry) {
  const auto& d = entry.detection;
  if (d.version.has_value() != (entry.size >= kVersionEnd)) return false;
  const auto expected_profile = ProfileFor(d.version);
  const bool compatible_size = entry.size >= abc::kDynamicHeaderSize && entry.size <= UINT32_MAX;
  const bool incompatible = d.version == kIncompatible1100 || d.version == kIncompatible1101;
  const bool header_failure = d.header_error &&
      d.evidence == Evidence{EvidenceRule::kHeaderFailure, 0,
                             static_cast<std::uint64_t>(*d.header_error)};
  switch (d.kind) {
    case SampleKind::kDynamic:
      return compatible_size && !d.header_error && d.profile == expected_profile &&
          expected_profile != abc::ProfileId::kUnknown &&
          expected_profile != abc::ProfileId::kStatic0107 &&
          VersionEvidence(d, EvidenceRule::kDynamicHeader);
    case SampleKind::kStatic:
      if (!compatible_size || entry.size < abc::kStaticHeaderSize || d.header_error) return false;
      if (d.version == kCurrentStatic) {
        return d.profile == abc::ProfileId::kStatic0107 &&
            VersionEvidence(d, EvidenceRule::kStaticHeader);
      }
      return d.version == kLegacyStatic && d.profile == abc::ProfileId::kUnknown &&
          d.evidence.rule == EvidenceRule::kEtsClass && d.evidence.observed == kEtsLanguage &&
          d.evidence.offset >= abc::kStaticHeaderSize &&
          d.evidence.offset <= entry.size - kLanguageTagWidth;
    case SampleKind::kUnknown:
      if (!compatible_size || d.profile != abc::ProfileId::kUnknown) return false;
      if (d.header_error) {
        return incompatible && *d.header_error == abc::HeaderError::kIncompatibleVersion &&
            header_failure;
      }
      if (incompatible || expected_profile != abc::ProfileId::kUnknown) return false;
      if (d.version == kLegacyStatic) {
        return (d.evidence.rule == EvidenceRule::kNoStaticWitness ||
                d.evidence.rule == EvidenceRule::kProbeLimit) &&
            d.evidence.offset == 0 && d.evidence.observed == 0;
      }
      return VersionEvidence(d, EvidenceRule::kUnknownVersion);
    case SampleKind::kInvalid:
      if (d.header_error) {
        return header_failure && d.profile == abc::ProfileId::kUnknown &&
            *d.header_error != abc::HeaderError::kIncompatibleVersion &&
            *d.header_error != abc::HeaderError::kStaticCandidateVersion;
      }
      return compatible_size && !incompatible && d.profile == expected_profile &&
          (d.version != kCurrentStatic || entry.size >= abc::kStaticHeaderSize) &&
          d.evidence.rule == EvidenceRule::kChecksumMismatch &&
          d.evidence.offset == kChecksumOffset && d.evidence.observed <= UINT32_MAX;
  }
  return false;
}
}  // namespace

std::optional<ManifestError> ValidateEntries(const std::vector<CorpusEntry>& entries) {
  if (entries.size() > kMaxManifestEntries) return ManifestError::kTooLarge;
  std::set<std::pair<std::string, std::filesystem::path>> paths;
  std::map<Sha256, std::uint64_t> sizes;
  std::map<std::pair<Sha256, std::uint32_t>, Detection> contents;
  constexpr std::size_t kMaxPathBytes = 4096;
  constexpr std::size_t kMaxCorpusIdBytes = 64;
  for (const auto& entry : entries) {
    const auto path = entry.relative_path.generic_string();
    if (entry.corpus_id.empty() || entry.corpus_id.size() > kMaxCorpusIdBytes ||
        entry.corpus_id.find('\0') != std::string::npos || path.empty() ||
        path.size() > kMaxPathBytes || path.find('\0') != std::string::npos ||
        entry.relative_path.is_absolute() || entry.checked_at < 0 ||
        entry.detector_revision == 0 || !ValidDetection(entry)) return ManifestError::kSchema;
    for (const auto& part : entry.relative_path) {
      if (part == ".." || part == "." || part.empty()) return ManifestError::kSchema;
    }
    if (!paths.emplace(entry.corpus_id, entry.relative_path).second) return ManifestError::kSchema;
    const auto [size_at, new_size] = sizes.emplace(entry.sha256, entry.size);
    if (!new_size && size_at->second != entry.size) return ManifestError::kSchema;
    const auto [at, inserted] = contents.emplace(
        std::pair{entry.sha256, entry.detector_revision}, entry.detection);
    if (!inserted && at->second != entry.detection) return ManifestError::kSchema;
  }
  return std::nullopt;
}
}  // namespace abcd::corpus
