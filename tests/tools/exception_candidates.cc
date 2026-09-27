#include <algorithm>
#include <iostream>
#include <nlohmann/json.hpp>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/corpus/detect.h"

// Candidate discovery only: inspect one selected ABC's method metadata, not
// a corpus decompilation or support census. No source/sidecar inputs are read.
int main(int argc, char** argv) {
  using namespace abcd;
  if (argc != 2) return 1;
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto* owner = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owner) return 1;
  const auto& file = **owner;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return 1;
  const auto parsed = abc::ParseHeader(std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->kind != abc::AbcKind::kDynamic || header->checksum != hash->adler32) return 1;
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return 1;
  const auto lookup = abc::MethodLookup::Build(index->methods);
  const auto* methods = std::get_if<abc::MethodLookup>(&lookup);
  if (!methods) return 1;
  const auto bound = analysis::ClassMethodSites::Build(file, *header, *index, *methods);
  const auto* sites = std::get_if<analysis::ClassMethodSites>(&bound);
  if (!sites) return 1;
  std::vector<const abc::MethodInfo*> candidates;
  for (const auto& method : index->methods)
    if (method.code && method.code->try_count && sites->Find(method.offset).count == 1)
      candidates.push_back(&method);
  std::sort(candidates.begin(), candidates.end(), [](const auto* left, const auto* right) {
    return left->code->instruction_bytes < right->code->instruction_bytes;
  });
  constexpr std::size_t kCandidateLimit = 20;
  if (candidates.size() > kCandidateLimit) candidates.resize(kCandidateLimit);
  for (const auto* method : candidates)
    std::cout << nlohmann::json{{"entity", method->offset.value}, {"name", method->name},
        {"bytes", method->code->instruction_bytes}, {"tries", method->code->try_count}} << '\n';
  return file.CheckUnchanged() ? 1 : 0;
}
