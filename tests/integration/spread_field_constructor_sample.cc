#include "spread_field_constructor.h"

#include <cstdlib>
#include <memory>
#include <span>
#include <sstream>

#include "abcd/abc/call_type.h"

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
  Check(header.profile == abc::ProfileId::kDynamic1206);
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header, {256 * 1024 * 1024}));
  constexpr abc::EntityOffset kConstructor{5083};
  const abc::MethodInfo* method = nullptr;
  for (const auto& candidate : index.methods)
    if (candidate.offset == kConstructor) method = &candidate;
  Check(method && method->code);
  const auto call = std::get<abc::CallType>(abc::ReadCallType(
      file, header, index, method->annotations));
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
      method->code->registers, method->code->arguments,
      abc::DecodeCallFlags(call.value)));
  const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
      file, header.profile, *method->code));
  const auto result = analysis::ProveSpreadFieldDerivedConstructor(
      body, frame);
  const auto* proof =
      std::get_if<analysis::SpreadFieldDerivedConstructor>(&result);
  Check(proof && proof->writes.size() == 1);
  const source::MethodConstants constants(file, header, index,
                                          kConstructor, {});
  std::ostringstream output;
  const auto emitted = source::WriteSpreadFieldConstructor(
      *proof, 0, constants, output);
  Check(std::holds_alternative<std::size_t>(emitted));
  Check(output.str() ==
        "constructor(...$rest) { super(...$rest); this[\"curBp\"] = \"\"; }");
  Check(!file.CheckUnchanged());
}
