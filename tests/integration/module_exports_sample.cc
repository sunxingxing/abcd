#include "abcd/source/module_exports.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <span>

#include "abcd/abc/method_body.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  Check(argc == 2);
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
}
