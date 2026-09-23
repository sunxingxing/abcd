#include "abcd/source/static_fields.h"

#include <cstdlib>
#include <memory>
#include <span>

#include "abcd/abc/method_lookup.h"
#include "abcd/abc/method_target.h"
#include "abcd/analysis/function_creation.h"
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
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header, {256 * 1024 * 1024}));
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  constexpr abc::EntityOffset kV7Constants{7167};
  constexpr abc::EntityOffset kV12Constants{4182};
  const bool historical = header.profile == abc::ProfileId::kDynamic1206;
  Check(historical || header.profile == abc::ProfileId::kDynamic1301);
  const auto target = historical ? kV12Constants : kV7Constants;
  const abc::RecordInfo* record = nullptr;
  for (const auto& candidate : index.records)
    if (candidate.offset == target) record = &candidate;
  Check(record != nullptr);
  const abc::MethodInfo* entry = nullptr;
  for (std::size_t i = record->first_method;
       i < record->first_method + record->method_count; ++i)
    if (index.methods[i].name == "func_main_0") entry = &index.methods[i];
  Check(entry != nullptr && entry->code.has_value());
  const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
      file, header.profile, *entry->code));
  const abc::MethodInfo* initializer = nullptr;
  for (const auto& site : body.instructions) {
    const auto lifted = analysis::LiftFunctionCreation(site.decoded);
    const auto* creation = std::get_if<analysis::FunctionCreation>(&lifted);
    if (!creation || creation->kind != analysis::CreationKind::kMethod)
      continue;
    const auto target = std::get<abc::MethodTarget>(abc::ResolveMethodTarget(
        file, header, index, lookup, entry->offset, creation->method));
    Check(initializer == nullptr);
    initializer = &index.methods[target.position.value];
  }
  Check(initializer != nullptr);
  const auto result = source::RecoverStaticFields(
      file, header, index, *initializer, 16 * 1024 * 1024);
  const auto* fields = std::get_if<source::StaticFields>(&result);
  Check(fields != nullptr && fields->fields.size() == 3);
  Check(fields->fields[0].name == u"FULL_PERCENT");
  Check(std::get<std::u16string>(std::get<source::StaticAtom>(
            fields->values[fields->fields[0].value])) == u"100%");
  if (historical) {
    Check(fields->fields[1].name == u"TEXTAREA_HEIGHT");
    Check(fields->fields[2].name == u"BREAK_POINT_SM");
    Check(std::get<std::u16string>(std::get<source::StaticAtom>(
              fields->values[fields->fields[1].value])) == u"35%");
    Check(std::get<std::u16string>(std::get<source::StaticAtom>(
              fields->values[fields->fields[2].value])) == u"sm");
  } else {
    Check(fields->fields[1].name == u"FONT_WEIGHT_500");
    Check(fields->fields[2].name == u"TEXT_OPACITY");
    Check(std::get<std::int32_t>(std::get<source::StaticAtom>(
              fields->values[fields->fields[1].value])) == 500);
    const auto& opacity = std::get<source::StaticArray>(
        fields->values[fields->fields[2].value]);
    Check(opacity.elements.size() == 4);
    Check(std::get<std::int32_t>(opacity.elements[0]) == 1);
  }
  const auto limited = source::RecoverStaticFields(
      file, header, index, *initializer, 0);
  Check(std::get<source::StaticFieldFailure>(limited).kind ==
        source::StaticFieldError::kResourceLimit);
  Check(!file.CheckUnchanged());
}
