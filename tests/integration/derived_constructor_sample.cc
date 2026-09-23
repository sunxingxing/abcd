#include "abcd/analysis/derived_constructor.h"

#include <array>
#include <cstdlib>
#include <memory>
#include <span>

#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  Check(argc == 2);
  auto opened = corpus::AbcFile::Open(argv[1]);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint =
      std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size),
      fingerprint.size));
  Check(header.profile == abc::ProfileId::kDynamic1301);
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header, {256 * 1024 * 1024}));
  constexpr std::array<abc::EntityOffset, 2> kConstructors{
      abc::EntityOffset{9167}, abc::EntityOffset{9544}};
  for (const auto entity : kConstructors) {
    const abc::MethodInfo* method = nullptr;
    for (const auto& candidate : index.methods)
      if (candidate.offset == entity) method = &candidate;
    Check(method != nullptr && method->code.has_value());
    const auto call = std::get<abc::CallType>(abc::ReadCallType(
        file, header, index, method->annotations));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
        method->code->registers, method->code->arguments,
        abc::DecodeCallFlags(call.value)));
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
        file, header.profile, *method->code));
    Check(std::holds_alternative<analysis::ForwardingDerivedConstructor>(
        analysis::ProveForwardingDerivedConstructor(body, frame)));
  }
  Check(!file.CheckUnchanged());
}
