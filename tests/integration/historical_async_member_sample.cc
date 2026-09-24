#include <array>
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

#include "abcd/abc/call_type.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/class_method.h"
#include "abcd/source/local_lexical_body.h"
#include "abcd/source/module_exports.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/async_completion.h"
#include "class_emission.h"

namespace {
constexpr std::size_t kFixtureMetadataBytes = 256 * 1024 * 1024;

void Check(bool condition) {
  if (!condition) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  Check(argc == 2 || (argc == 3 &&
                      std::string_view(argv[2]) == "--emit"));
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
      abc::BuildMetadataIndex(file, header, {kFixtureMetadataBytes}));
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  constexpr abc::EntityOffset kRecord{5246};
  const auto record = std::find_if(
      index.records.begin(), index.records.end(),
      [kRecord](const abc::RecordInfo& item) {
        return item.offset == kRecord;
      });
  Check(record != index.records.end());
  const auto context = std::get<source::RecordModuleContext>(
      source::ReadRecordModuleContext(file, header, index, *record,
                                      abc::FilePackaging::kMerged));
  const auto module = context.ForOwner(file, kRecord);
  Check(module != nullptr);
  const auto exports = std::get<source::ModuleExports>(
      source::BuildModuleExports(file, header, *module));
  source::GlobalScope scope;
  scope.module_slots = exports.slot_count();
  constexpr abc::EntityOffset kEntry{5409};
  constexpr abc::CodeOffset kClassSite{14};
  const auto entry_position = lookup.Find(kEntry);
  Check(entry_position.has_value());
  const auto& entry = index.methods[entry_position->value];
  Check(entry.code.has_value());
  const auto entry_body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
      file, header.profile, *entry.code));
  const auto site = std::find_if(
      entry_body.instructions.begin(), entry_body.instructions.end(),
      [kClassSite](const abc::BodyInstruction& instruction) {
        return instruction.offset == kClassSite;
      });
  Check(site != entry_body.instructions.end());
  const auto creation = std::get<analysis::ClassCreation>(
      analysis::LiftClassCreation(site->decoded));
  const auto reference = std::get<abc::PoolReference>(
      abc::ResolvePoolReference(file, header, index, kEntry,
                                creation.literal,
                                abc::PoolEntityKind::kLiteralArray));
  const auto literals = std::get<abc::LiteralArray>(
      abc::ReadLiteralArray(file, header, reference.offset));
  const auto members = std::get<abc::ClassBuffer>(
      abc::DecodeClassBuffer(literals));
  constexpr std::array<abc::EntityOffset, 2> kMembers{
      abc::EntityOffset{5437}, abc::EntityOffset{5465}};
  constexpr std::array<std::uint16_t, 2> kLengths{0, 1};
  constexpr std::array<std::u16string_view, 2> kNames{
      u"onBackup", u"onRestore"};
  constexpr std::array<std::string_view, 2> kSelfNames{
      "BackupSelf", "RestoreSelf"};
  std::array<std::string, 2> emitted;
  Check(members.properties.size() == kMembers.size());
  for (std::size_t i = 0; i < kMembers.size(); ++i) {
    const auto& property = members.properties[i];
    const auto* class_method =
        std::get_if<abc::ClassMethod>(&property.value);
    Check(class_method && class_method->target == kMembers[i] &&
          class_method->length == kLengths[i] &&
          class_method->kind == abc::ClassMethodKind::kOrdinary &&
          source::ReadClassKey(file, header, property, {}) == kNames[i]);
    const auto position = lookup.Find(kMembers[i]);
    Check(position.has_value());
    const auto& method = index.methods[position->value];
    Check(method.code.has_value());
    const auto attributes = std::get<abc::MethodAttributes>(
        abc::DecodeMethodAttributes(method.access_flags, header.profile));
    Check(attributes.kind == abc::FunctionKind::kAsync);
    const auto call = std::get<abc::CallType>(abc::ReadCallType(
        file, header, index, method.annotations));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(
        method.code->registers, method.code->arguments,
        abc::DecodeCallFlags(call.value)));
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(
        file, header.profile, *method.code));
    Check(std::holds_alternative<abc::MethodBody>(
        source::ProveAsyncCompletion(
            body, frame, source::LocalLexicalBudget{}.bytes)));
    const auto target = source::ResolveClassMember(
        header, index, lookup, property, &roles);
    Check(target.position == *position &&
          target.attributes.kind == abc::FunctionKind::kAsync);
    auto older = header;
    older.profile = abc::ProfileId::kDynamic1102;
    bool older_rejected = false;
    try {
      static_cast<void>(source::ResolveClassMember(
          older, index, lookup, property, &roles));
    } catch (const source::ClassFailure& failure) {
      const auto* error =
          std::get_if<source::CreatedClassError>(&failure.result);
      older_rejected = error &&
                       *error == source::CreatedClassError::kMembers;
    }
    Check(older_rejected);
    const source::MethodConstants constants(file, header, index,
                                            method.offset, {}, module.get(),
                                            &roles, scope);
    const source::ClassMethod source_method{
        *class_method, attributes, frame,
        abc::ClassPlacement::kPrototype, kNames[i], kSelfNames[i],
        source::EffectiveMethodKind::kOrdinary};
    std::ostringstream output;
    const auto written = source::WriteClassMethod(
        source_method, body, output, {}, &constants, &lookup);
    Check(std::holds_alternative<std::size_t>(written));
    Check(output.str().starts_with("async ["));
    emitted[i] = output.str();
  }
  if (argc == 3) {
    std::cout << "export const events = [];\n"
                 "export const logger = { get info() { "
                 "events.push('get'); return function(...args) { "
                 "events.push([this, ...args]); }; } };\n"
                 "const $import1 = logger;\n"
                 "export class Backup {\n"
              << emitted[0] << '\n' << emitted[1]
              << "\n}\n"
                 "const BackupSelf = Backup.prototype.onBackup;\n"
                 "const RestoreSelf = Backup.prototype.onRestore;\n";
  }
  Check(!file.CheckUnchanged());
}
