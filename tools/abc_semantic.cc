#include <algorithm>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/abc/method_body.h"
#include "abcd/analysis/semantic_ir.h"
#include "abcd/corpus/abc_file.h"

namespace {
using namespace abcd;
enum class InspectError : std::uint8_t {
  kArguments,
  kOpen,
  kHash,
  kHeader,
  kIndex,
  kMethod,
  kBody,
  kSemantic,
};
int Fail(InspectError kind, std::uint32_t pc = 0) {
  std::cerr << nlohmann::json{{"error", static_cast<unsigned>(kind)},
                              {"pc", pc}}
            << '\n';
  return EXIT_FAILURE;
}
int Inspect(const std::filesystem::path& path, std::uint32_t entity) {
  auto opened = corpus::AbcFile::Open(path);
  const auto* owned = std::get_if<std::unique_ptr<corpus::AbcFile>>(&opened);
  if (!owned) return Fail(InspectError::kOpen);
  const auto& file = **owned;
  const auto fingerprint = file.Fingerprint();
  const auto* hash = std::get_if<corpus::FileFingerprint>(&fingerprint);
  if (!hash) return Fail(InspectError::kHash);
  const auto parsed = abc::ParseHeader(
      std::span(hash->prefix).first(hash->prefix_size), hash->size);
  const auto* header = std::get_if<abc::Header>(&parsed);
  if (!header || header->checksum != hash->adler32)
    return Fail(InspectError::kHeader);
  constexpr std::size_t kIndexBytes = 256 * 1024 * 1024;
  const auto indexed = abc::BuildMetadataIndex(file, *header, {kIndexBytes});
  const auto* index = std::get_if<abc::MetadataIndex>(&indexed);
  if (!index) return Fail(InspectError::kIndex);
  const auto method = std::find_if(
      index->methods.begin(), index->methods.end(),
      [entity](const abc::MethodInfo& item) {
        return item.offset.value == entity;
      });
  if (method == index->methods.end() || !method->code)
    return Fail(InspectError::kMethod);
  const auto& code = *method->code;
  if (code.registers >
      std::numeric_limits<std::uint32_t>::max() - code.arguments)
    return Fail(InspectError::kBody);
  const auto decoded = abc::DecodeMethodBody(file, header->profile, code);
  const auto* body = std::get_if<abc::MethodBody>(&decoded);
  if (!body) {
    const auto& failure = std::get<abc::BodyFailure>(decoded);
    return Fail(InspectError::kBody, failure.offset.value);
  }
  const auto built = analysis::BuildSemanticMethod(
      *body, header->profile, code.registers + code.arguments);
  const auto* method_ir = std::get_if<analysis::SemanticMethod>(&built);
  if (!method_ir) {
    const auto& failure = std::get<analysis::SemanticFailure>(built);
    std::cerr << nlohmann::json{
                     {"error", static_cast<unsigned>(InspectError::kSemantic)},
                     {"semantic_error", static_cast<unsigned>(failure.kind)},
                     {"pc", failure.pc.value}}
              << '\n';
    return EXIT_FAILURE;
  }
  std::cout << nlohmann::json{
                   {"entity", entity},
                   {"instructions", method_ir->instructions.size()},
                   {"blocks", method_ir->flow.blocks.size()},
                   {"ssa_values", method_ir->ssa.Values().size()}}
            << '\n';
  return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: abc_semantic INPUT.abc METHOD_ENTITY\n";
    return Fail(InspectError::kArguments);
  }
  std::uint32_t entity = 0;
  const std::string_view token{argv[2]};
  const auto number = std::from_chars(
      token.data(), token.data() + token.size(), entity);
  if (number.ec != std::errc{} ||
      number.ptr != token.data() + token.size())
    return Fail(InspectError::kArguments);
  return Inspect(argv[1], entity);
}
