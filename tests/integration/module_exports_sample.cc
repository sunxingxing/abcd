#include "abcd/source/module_exports.h"
#include "abcd/source/record_program.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <streambuf>
#include <string_view>

#include "abcd/abc/method_body.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
class RejectBuffer : public std::streambuf {
  std::streamsize xsputn(const char*, std::streamsize) override { return 0; }
  int_type overflow(int_type) override { return traits_type::eof(); }
};
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  Check(argc == 2 || (argc == 3 && std::string_view(argv[2]) == "--emit"));
  auto opened = corpus::AbcFile::Open(argv[1]);
  Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto hash = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  const auto index = std::get<abc::MetadataIndex>(
      abc::BuildMetadataIndex(file, header, {256 * 1024 * 1024}));
  constexpr abc::EntityOffset kBreakpointRecord{7395};
  const auto found = std::find_if(index.records.begin(), index.records.end(),
                                  [&](const abc::RecordInfo& record) {
                                    return record.offset == kBreakpointRecord;
                                  });
  Check(found != index.records.end() && found->method_count != 0);
  const auto context = std::get<source::RecordModuleContext>(
      source::ReadRecordModuleContext(file, header, index, *found,
                                      abc::FilePackaging::kMerged));
  const auto module = context.ForOwner(file, found->offset);
  Check(module != nullptr);
  const auto exports = std::get<source::ModuleExports>(
      source::BuildModuleExports(file, header, *module));
  Check(exports.slot_count() == 1);
  std::ostringstream aliases;
  Check(std::holds_alternative<std::size_t>(exports.WriteAliases(aliases)));
  Check(aliases.str() ==
        "export { $local0 as \"BreakpointType\" };\n");
  const auto& entry = index.methods[found->first_method];
  Check(entry.name == "func_main_0" && entry.code.has_value());
  const auto body = std::get<abc::MethodBody>(
      abc::DecodeMethodBody(file, header.profile, *entry.code));
  std::size_t stores = 0;
  for (const auto& instruction : body.instructions) {
    const auto lifted = analysis::LiftModuleStore(instruction.decoded);
    if (const auto* store = std::get_if<analysis::ModuleStore>(&lifted)) {
      Check(store->slot.value == 0);
      ++stores;
    }
  }
  Check(stores == 1 && !file.CheckUnchanged());
  const auto lookup = std::get<abc::MethodLookup>(
      abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(file, header, index, lookup));
  std::ostringstream source_text;
  const auto emitted = source::WriteRecordProgram(
      file, header, index, lookup, *found, *module, roles, source_text);
  Check(std::holds_alternative<std::size_t>(emitted));
  Check(std::get<std::size_t>(emitted) == source_text.str().size());
  Check(source_text.str().find("class BreakpointType") != std::string::npos);
  Check(source_text.str().find("let $local0 = $acc;") != std::string::npos);
  const auto rejected = [&](const abc::Header& selected_header,
                            const abc::RecordInfo& selected_record,
                            const abc::ModuleInfo& selected_module,
                            source::RecordProgramBudget budget,
                            source::RecordProgramError expected) {
    std::ostringstream text;
    const auto result = source::WriteRecordProgram(
        file, selected_header, index, lookup, selected_record,
        selected_module, roles, text, budget);
    Check(std::get<source::RecordProgramFailure>(result).kind == expected);
    Check(text.str().empty());
  };
  auto wrong_profile = header;
  wrong_profile.profile = abc::ProfileId::kDynamic1202;
  rejected(wrong_profile, *found, *module, {},
           source::RecordProgramError::kProfile);
  auto missing_entry = *found;
  ++missing_entry.first_method;
  --missing_entry.method_count;
  rejected(header, missing_entry, *module, {},
           source::RecordProgramError::kEntry);
  auto missing_store = *module;
  missing_store.local_exports.push_back(
      {entry.name_offset, entry.name_offset});
  rejected(header, *found, missing_store, {},
           source::RecordProgramError::kExport);
  source::RecordProgramBudget short_output;
  short_output.output.bytes = 1;
  rejected(header, *found, *module, short_output,
           source::RecordProgramError::kOutputLimit);
  source::RecordProgramBudget no_scratch;
  no_scratch.bytes = 0;
  rejected(header, *found, *module, no_scratch,
           source::RecordProgramError::kResourceLimit);
  RejectBuffer reject_buffer;
  std::ostream reject_stream(&reject_buffer);
  const auto write_failure = source::WriteRecordProgram(
      file, header, index, lookup, *found, *module, roles, reject_stream);
  Check(std::get<source::RecordProgramFailure>(write_failure).kind ==
        source::RecordProgramError::kWrite);
  if (argc == 3) std::cout << source_text.str();
}
