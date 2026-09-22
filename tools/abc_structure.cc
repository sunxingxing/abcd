#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

#include "abcd/corpus/detect.h"
#include "abcd/structure/record_path.h"
#include "abcd/structure/record_writer.h"

namespace {
using namespace abcd;
enum class ExportError {
  kOpen,
  kHash,
  kKind,
  kHeader,
  kIndex,
  kPackaging,
  kPath,
  kRecord,
  kOutput,
  kChanged,
  kDestination
};
int Fail(ExportError kind, std::uint32_t entity = 0) {
  std::cerr << nlohmann::json{{"error", static_cast<unsigned>(kind)},
                              {"entity", entity}}
            << '\n';
  return EXIT_FAILURE;
}
class StagedTree {
 public:
  explicit StagedTree(const std::filesystem::path& destination) {
    auto pattern = destination.string() + ".partial.XXXXXX";
    if (mkdtemp(pattern.data())) path = pattern;
  }
  ~StagedTree() {
    if (!path.empty()) {
      std::error_code ignored;
      std::filesystem::remove_all(path, ignored);
    }
  }
  bool Publish(const std::filesystem::path& destination) {
    // Linux no-replace rename: a racing pre-existing destination, including a
    // symlink, cannot be overwritten. All children were made in our fresh tree.
    if (renameat2(AT_FDCWD, path.c_str(), AT_FDCWD, destination.c_str(),
                  RENAME_NOREPLACE) != 0)
      return false;
    path.clear();
    return true;
  }
  std::filesystem::path path;
};
std::string Hex(const corpus::Sha256& hash) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string result;
  result.reserve(hash.bytes.size() * 2);
  for (const auto byte : hash.bytes) {
    result += digits[byte >> 4];
    result += digits[byte & 0xf];
  }
  return result;
}
int Export(const std::filesystem::path& input,
           const std::filesystem::path& output) {
  if (output.filename().empty() ||
      std::filesystem::exists(std::filesystem::symlink_status(output)))
    return Fail(ExportError::kDestination);
  auto opened = corpus::AbcFile::Open(input);
  const auto* owned = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owned) return Fail(ExportError::kOpen);
  const auto& file = **owned;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return Fail(ExportError::kHash);
  const auto detected = corpus::DetectAbc(file, *hash);
  const auto* detection = std::get_if<corpus::Detection>(&detected);
  if (!detection || detection->kind != corpus::SampleKind::kDynamic)
    return Fail(ExportError::kKind);
  const auto parsed = abc::ParseHeader(
      std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->checksum != hash->adler32)
    return Fail(ExportError::kHeader);
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return Fail(ExportError::kIndex);
  const auto packed = abc::DetectFilePackaging(file, *header, *index);
  const auto* packaging = std::get_if<abc::FilePackaging>(&packed);
  if (!packaging) return Fail(ExportError::kPackaging);
  const auto parent = output.parent_path();
  if (!parent.empty()) std::filesystem::create_directories(parent);
  StagedTree tree(output);
  if (tree.path.empty()) return Fail(ExportError::kOutput);
  std::ofstream manifest(tree.path / "manifest.json", std::ios::binary);
  manifest << "{\"schema\":1,\"view\":\"declarations_only\",\"sha256\":\""
           << Hex(hash->sha256)
           << "\",\"packaging\":" << static_cast<unsigned>(*packaging)
           << ",\"records\":[";
  structure::RecordCounts totals;
  bool comma = false;
  for (const auto& record : index->records) {
    const auto planned = structure::PlanRecordPath(record);
    const auto* path = std::get_if<structure::RecordPath>(&planned);
    if (!path) return Fail(ExportError::kPath, record.offset.value);
    const auto stem = std::filesystem::path("records") / path->stem;
    std::filesystem::create_directories((tree.path / stem).parent_path());
    const auto metadata_path = stem.string() + ".record.json";
    const auto declaration_path = stem.string() + ".d.ts";
    std::ofstream metadata(tree.path / metadata_path, std::ios::binary);
    std::ofstream declarations(tree.path / declaration_path, std::ios::binary);
    if (!metadata || !declarations)
      return Fail(ExportError::kOutput, record.offset.value);
    const auto result = structure::WriteRecordStructure(
        file, *header, *index, record, *packaging, metadata, declarations);
    const auto* counts = std::get_if<structure::RecordCounts>(&result);
    if (!counts) {
      const auto& failure = std::get<structure::StructureFailure>(result);
      std::cerr << nlohmann::json{{"failure_variant", failure.error.index()},
                                  {"entity", failure.entity.value}}
                << '\n';
      return Fail(ExportError::kRecord, record.offset.value);
    }
    metadata.close();
    declarations.close();
    if (!metadata || !declarations)
      return Fail(ExportError::kOutput, record.offset.value);
    totals.methods += counts->methods;
    totals.fields += counts->fields;
    totals.requests += counts->requests;
    totals.imports += counts->imports;
    totals.exports += counts->exports;
    if (comma) manifest << ',';
    comma = true;
    manifest << nlohmann::json{{"entity", record.offset.value},
                               {"metadata", metadata_path},
                               {"declarations", declaration_path}};
    if (!manifest) return Fail(ExportError::kOutput);
  }
  const nlohmann::json counts{
      {"records", index->records.size()}, {"methods", totals.methods},
      {"record_fields", totals.fields},   {"module_requests", totals.requests},
      {"module_imports", totals.imports}, {"module_exports", totals.exports}};
  manifest << "],\"counts\":" << counts << "}\n";
  manifest.close();
  if (!manifest) return Fail(ExportError::kOutput);
  if (file.CheckUnchanged()) return Fail(ExportError::kChanged);
  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);
  // Validate all report text before publishing; a non-UTF-8 Linux path must
  // not turn a successfully published tree into a reported serialization error.
  const auto report = nlohmann::json{
      {"output", output.string()},
      {"counts", counts},
      {"peak_rss_bytes",
       static_cast<std::uint64_t>(usage.ru_maxrss) *
           1024}}.dump();
  if (!tree.Publish(output)) return Fail(ExportError::kDestination);
  std::cout << report << '\n';
  return EXIT_SUCCESS;
}
}  // namespace
int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: abc_structure INPUT.abc OUTPUT_DIR\n";
    return EXIT_FAILURE;
  }
  constexpr rlim_t kProcessBytes = 1000000000;
  rlimit limit{};
  if (getrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  limit.rlim_cur = std::min(limit.rlim_cur, kProcessBytes);
  if (setrlimit(RLIMIT_AS, &limit) != 0) return EXIT_FAILURE;
  try {
    return Export(argv[1], argv[2]);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
