#ifndef ABCD_CORPUS_DETECT_H_
#define ABCD_CORPUS_DETECT_H_

#include <cstdint>
#include <optional>
#include <variant>

#include "abcd/corpus/abc_file.h"

namespace abcd::corpus {

enum class SampleKind { kDynamic, kStatic, kUnknown, kInvalid };
enum class EvidenceRule {
  kDynamicHeader, kStaticHeader, kEtsClass, kUnknownVersion,
  kHeaderFailure, kChecksumMismatch, kNoStaticWitness, kProbeLimit
};

struct Evidence {
  EvidenceRule rule = EvidenceRule::kUnknownVersion;
  std::uint32_t offset = 0;
  std::uint64_t observed = 0;
  bool operator==(const Evidence&) const = default;
};

struct Detection {
  SampleKind kind = SampleKind::kUnknown;
  std::optional<abc::AbcVersion> version;
  abc::ProfileId profile = abc::ProfileId::kUnknown;
  Evidence evidence;
  std::optional<abc::HeaderError> header_error;
  bool operator==(const Detection&) const = default;
};

inline constexpr std::uint32_t kDetectorRevision = 1;
using DetectResult = std::variant<Detection, FileError>;

[[nodiscard]] DetectResult DetectAbc(const AbcFile& file,
                                     const FileFingerprint& fingerprint);

}  // namespace abcd::corpus
#endif  // ABCD_CORPUS_DETECT_H_
