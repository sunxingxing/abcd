#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/guarded_global_call.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "abcd/source/module_exports.h"
#include "record_prelude.h"

namespace {
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
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size),
      fingerprint.size));
  Check(header.profile == abc::ProfileId::kDynamic1206);
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  constexpr abc::EntityOffset kRecord{564};
  constexpr abc::EntityOffset kEntry{1118};
  constexpr std::size_t kGuardStart = 3;
  constexpr std::size_t kGuardEnd = 33;
  const auto record = std::find_if(
      index.records.begin(), index.records.end(),
      [kRecord](const abc::RecordInfo& item) {
        return item.offset == kRecord;
      });
  Check(record != index.records.end());
  const auto position = lookup.Find(kEntry);
  Check(position.has_value());
  const auto& entry = index.methods[position->value];
  Check(entry.code.has_value());
  const auto body = std::get<abc::MethodBody>(
      abc::DecodeMethodBody(file, header.profile, *entry.code));
  const auto guard = analysis::MatchGuardedGlobalCall(
      body, kGuardStart, entry.code->registers);
  Check(guard && guard->first == kGuardStart && guard->after == kGuardEnd);
  const auto context = std::get<source::RecordModuleContext>(
      source::ReadRecordModuleContext(
          file, header, index, *record, abc::FilePackaging::kMerged));
  const auto module = context.ForOwner(file, kRecord);
  Check(module != nullptr);
  source::GlobalScope scope;
  scope.module_slots = std::get<source::ModuleExports>(
      source::BuildModuleExports(file, header, *module)).slot_count();
  std::ostringstream guard_source;
  const auto rendered = source::WriteGuardedGlobalCall(
      file, header, index, lookup, entry, body, *guard, *module,
      roles, scope, guard_source);
  Check(std::holds_alternative<std::size_t>(rendered));
  Check(std::get<std::size_t>(rendered) == guard_source.str().size());
  auto older = header;
  older.profile = abc::ProfileId::kDynamic1102;
  std::ostringstream old_source;
  Check(std::get<source::RecordPreludeError>(source::WriteGuardedGlobalCall(
            file, older, index, lookup, entry, body, *guard, *module,
            roles, scope, old_source)) == source::RecordPreludeError::kProfile);
  Check(old_source.str().empty());
  std::ostringstream output;
  const auto result = source::WriteRecordProgram(
      file, header, index, lookup, *record, *module, roles, output);
  if (const auto* failure =
          std::get_if<source::RecordProgramFailure>(&result)) {
    Check(failure->pc > body.instructions[guard->after - 1].offset);
    Check(output.str().empty());
  } else {
    Check(std::get<std::size_t>(result) == output.str().size());
  }
  if (argc == 3) std::cout << guard_source.str();
}
