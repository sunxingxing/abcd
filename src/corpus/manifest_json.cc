#include "manifest_json.h"

#include <array>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string_view>

namespace abcd::corpus {
namespace {
using Json = nlohmann::json;
using namespace std::literals;

// Text is restricted to this serialization boundary; engine state is typed.
constexpr std::array kKinds = {std::pair{SampleKind::kDynamic, "dynamic"sv},
                               std::pair{SampleKind::kStatic, "static"sv},
                               std::pair{SampleKind::kUnknown, "unknown"sv},
                               std::pair{SampleKind::kInvalid, "invalid"sv}};
constexpr std::array kProfiles = {
    std::pair{abc::ProfileId::kUnknown, "unknown"sv},
    std::pair{abc::ProfileId::kDynamic9, "dynamic_9_0_0_0"sv},
    std::pair{abc::ProfileId::kDynamic1102, "dynamic_11_0_2_0"sv},
    std::pair{abc::ProfileId::kDynamic1202, "dynamic_12_0_2_0"sv},
    std::pair{abc::ProfileId::kDynamic1206, "dynamic_12_0_6_0"sv},
    std::pair{abc::ProfileId::kDynamic1301, "dynamic_13_0_1_0"sv},
    std::pair{abc::ProfileId::kDynamic2400, "dynamic_24_0_0_0"sv},
    std::pair{abc::ProfileId::kStatic0107, "static_0_1_0_7"sv}};
constexpr std::array kRules = {
    std::pair{EvidenceRule::kDynamicHeader, "dynamic_header"sv},
    std::pair{EvidenceRule::kStaticHeader, "static_header"sv},
    std::pair{EvidenceRule::kEtsClass, "ets_class"sv},
    std::pair{EvidenceRule::kUnknownVersion, "unknown_version"sv},
    std::pair{EvidenceRule::kHeaderFailure, "header_failure"sv},
    std::pair{EvidenceRule::kChecksumMismatch, "checksum_mismatch"sv},
    std::pair{EvidenceRule::kNoStaticWitness, "no_static_witness"sv},
    std::pair{EvidenceRule::kProbeLimit, "probe_limit"sv}};
constexpr std::array kHeaderErrors = {
    std::pair{abc::HeaderError::kTruncated, "truncated"sv},
    std::pair{abc::HeaderError::kBadMagic, "bad_magic"sv},
    std::pair{abc::HeaderError::kActualSizeOverflow, "actual_size_overflow"sv},
    std::pair{abc::HeaderError::kFileSizeMismatch, "file_size_mismatch"sv},
    std::pair{abc::HeaderError::kForeignRange, "foreign_range"sv},
    std::pair{abc::HeaderError::kIncompatibleVersion, "incompatible_version"sv},
    std::pair{abc::HeaderError::kStaticCandidateVersion,
              "static_candidate_version"sv},
    std::pair{abc::HeaderError::kIndexAlignment, "index_alignment"sv},
    std::pair{abc::HeaderError::kIndexRange, "index_range"sv},
    std::pair{abc::HeaderError::kReservedLiteralFields,
              "reserved_literal_fields"sv}};

template <class Enum, std::size_t N>
std::string_view Label(
    Enum value,
    const std::array<std::pair<Enum, std::string_view>, N>& labels) {
  for (const auto& [candidate, label] : labels)
    if (candidate == value) return label;
  throw ManifestError::kSchema;
}

template <class Enum, std::size_t N>
Enum ParseLabel(
    const Json& json,
    const std::array<std::pair<Enum, std::string_view>, N>& labels) {
  const auto& text = json.get_ref<const std::string&>();
  for (const auto& [value, label] : labels)
    if (text == label) return value;
  throw ManifestError::kSchema;
}

std::uint64_t Unsigned(const Json& json, std::uint64_t maximum) {
  if (!json.is_number_unsigned()) throw ManifestError::kSchema;
  const auto value = json.get<std::uint64_t>();
  if (value > maximum) throw ManifestError::kSchema;
  return value;
}

std::string Hex(const Sha256& hash) {
  constexpr std::string_view kHexDigits = "0123456789abcdef";
  constexpr unsigned int kNibbleBits = 4;
  constexpr unsigned int kNibbleMask = 0xf;
  std::string result;
  result.reserve(hash.bytes.size() * 2);
  for (auto byte : hash.bytes) {
    result += kHexDigits[byte >> kNibbleBits];
    result += kHexDigits[byte & kNibbleMask];
  }
  return result;
}

Sha256 ParseHex(const Json& json) {
  const auto& text = json.get_ref<const std::string&>();
  Sha256 result;
  if (text.size() != result.bytes.size() * 2) throw ManifestError::kSchema;
  auto nibble = [](char digit) -> std::uint8_t {
    if (digit >= '0' && digit <= '9') return digit - '0';
    if (digit >= 'a' && digit <= 'f') return digit - 'a' + 10;
    throw ManifestError::kSchema;
  };
  for (std::size_t i = 0; i < result.bytes.size(); ++i) {
    result.bytes[i] = static_cast<std::uint8_t>(nibble(text[2 * i]) * 16 +
                                                nibble(text[2 * i + 1]));
  }
  return result;
}

Json EncodeEntry(const CorpusEntry& entry) {
  const auto& detection = entry.detection;
  Json version = nullptr;
  if (detection.version) version = detection.version->bytes;
  Json header_error = nullptr;
  if (detection.header_error)
    header_error = Label(*detection.header_error, kHeaderErrors);
  return {{"corpus_id", entry.corpus_id},
          {"relative_path", entry.relative_path.generic_string()},
          {"size", entry.size},
          {"sha256", Hex(entry.sha256)},
          {"kind", Label(detection.kind, kKinds)},
          {"abc_version", version},
          {"profile", Label(detection.profile, kProfiles)},
          {"detector_revision", entry.detector_revision},
          {"evidence",
           {{"rule", Label(detection.evidence.rule, kRules)},
            {"offset", detection.evidence.offset},
            {"observed", detection.evidence.observed}}},
          {"header_error", header_error},
          {"checked_at", entry.checked_at}};
}

CorpusEntry DecodeEntry(const Json& json) {
  constexpr std::size_t kEntryFields = 11;
  constexpr std::size_t kEvidenceFields = 3;
  if (!json.is_object() || json.size() != kEntryFields)
    throw ManifestError::kSchema;
  CorpusEntry entry;
  entry.corpus_id = json.at("corpus_id").get<std::string>();
  entry.relative_path = json.at("relative_path").get<std::string>();
  entry.size = Unsigned(json.at("size"), UINT64_MAX);
  entry.sha256 = ParseHex(json.at("sha256"));
  auto& detection = entry.detection;
  detection.kind = ParseLabel(json.at("kind"), kKinds);
  detection.profile = ParseLabel(json.at("profile"), kProfiles);
  const auto& version = json.at("abc_version");
  if (!version.is_null()) {
    abc::AbcVersion value;
    if (!version.is_array() || version.size() != value.bytes.size())
      throw ManifestError::kSchema;
    for (std::size_t i = 0; i < value.bytes.size(); ++i) {
      value.bytes[i] =
          static_cast<std::uint8_t>(Unsigned(version[i], UINT8_MAX));
    }
    detection.version = value;
  }
  entry.detector_revision = static_cast<std::uint32_t>(
      Unsigned(json.at("detector_revision"), UINT32_MAX));
  entry.checked_at =
      static_cast<std::int64_t>(Unsigned(json.at("checked_at"), INT64_MAX));
  const auto& evidence = json.at("evidence");
  if (!evidence.is_object() || evidence.size() != kEvidenceFields)
    throw ManifestError::kSchema;
  detection.evidence = {
      ParseLabel(evidence.at("rule"), kRules),
      static_cast<std::uint32_t>(Unsigned(evidence.at("offset"), UINT32_MAX)),
      Unsigned(evidence.at("observed"), UINT64_MAX)};
  if (!json.at("header_error").is_null()) {
    detection.header_error = ParseLabel(json.at("header_error"), kHeaderErrors);
  }
  return entry;
}
}  // namespace

ManifestResult DecodeManifest(std::string_view bytes) {
  if (bytes.size() > kMaxManifestBytes) return ManifestError::kTooLarge;
  try {
    const auto json = Json::parse(bytes, nullptr, false);
    constexpr std::size_t kRootFields = 2;
    if (!json.is_object() || json.size() != kRootFields ||
        Unsigned(json.at("schema_version"), UINT32_MAX) != kManifestSchema ||
        !json.at("entries").is_array())
      return ManifestError::kSchema;
    if (json.at("entries").size() > kMaxManifestEntries)
      return ManifestError::kTooLarge;
    std::vector<CorpusEntry> entries;
    entries.reserve(json.at("entries").size());
    for (const auto& value : json.at("entries"))
      entries.push_back(DecodeEntry(value));
    if (const auto error = ValidateEntries(entries)) throw *error;
    return entries;
  } catch (ManifestError error) {
    return error;
  } catch (const Json::exception&) {
    return ManifestError::kSchema;
  }
}

std::variant<std::string, ManifestError> EncodeManifest(
    const std::vector<CorpusEntry>& entries) {
  try {
    if (const auto error = ValidateEntries(entries)) throw *error;
    Json array = Json::array();
    for (const auto& entry : entries) array.push_back(EncodeEntry(entry));
    auto text =
        Json{{"schema_version", kManifestSchema}, {"entries", std::move(array)}}
            .dump(2);
    text += '\n';
    if (text.size() > kMaxManifestBytes) return ManifestError::kTooLarge;
    return text;
  } catch (ManifestError error) {
    return error;
  } catch (const Json::exception&) {
    return ManifestError::kSchema;
  }
}
}  // namespace abcd::corpus
