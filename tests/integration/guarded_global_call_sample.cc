#include "abcd/analysis/guarded_global_call.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <span>
#include <string_view>

#include "abcd/abc/metadata_index.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/module_exports.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_program.h"
#include "record_prelude.h"

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
  using namespace abcd;
  Check(argc == 2 ||
        (argc == 3 && std::string_view(argv[2]) == "--emit"));
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
  constexpr abc::EntityOffset kShoppingRecord{11285};
  constexpr std::size_t kExpectedGuardedRecords = 16;
  std::string shopping_source;
  std::size_t matched = 0;
  for (const auto& record : index.records) {
    if (record.method_count == 0) continue;
    const auto first_method = index.methods.begin() + record.first_method;
    const auto last_method = first_method + record.method_count;
    const auto found_entry = std::find_if(
        first_method, last_method,
        [](const abc::MethodInfo& method) {
          return method.name == "func_main_0" && method.code.has_value();
        });
    if (found_entry == last_method) continue;
    const auto& entry = *found_entry;
    const auto body = std::get<abc::MethodBody>(
        abc::DecodeMethodBody(file, header.profile, *entry.code));
    if (body.instructions.size() <= 3) continue;
    const auto matched_guard = analysis::MatchGuardedGlobalCall(
        body, 3, entry.code->registers);
    const auto* found =
        std::get_if<analysis::GuardedGlobalCall>(&matched_guard);
    if (!found) continue;
    ++matched;
    if (record.offset != kShoppingRecord) continue;
    Check(found->first == 3 && found->after == 33);
    Check(found->function == 29);
    Check(found->key.value == 297);
    Check(found->receiver_global.value == 282);
    Check(found->receiver_property.value == 9);
    Check(found->call_global.value == 276);
    Check(found->call_property.value == 327);
    const auto lookup = std::get<abc::MethodLookup>(
        abc::MethodLookup::Build(index.methods));
    const auto roles = std::get<analysis::MethodRoleIndex>(
        analysis::MethodRoleIndex::Build(file, header, index, lookup));
    const auto context = std::get<source::RecordModuleContext>(
        source::ReadRecordModuleContext(
            file, header, index, record, abc::FilePackaging::kMerged));
    const auto module = context.ForOwner(file, record.offset);
    Check(module != nullptr);
    source::GlobalScope scope{};
    scope.module_slots = std::get<source::ModuleExports>(
        source::BuildModuleExports(file, header, *module)).slot_count();
    std::ostringstream rendered;
    const auto result = source::WriteGuardedGlobalCall(
        file, header, index, lookup, entry, body, *found, *module, roles,
        scope, rendered);
    Check(std::holds_alternative<std::size_t>(result));
    shopping_source = rendered.str();
    std::ostringstream record_source;
    const auto record_result = source::WriteRecordProgram(
        file, header, index, lookup, record, *module, roles, record_source);
    Check(std::holds_alternative<source::RecordProgramFailure>(record_result));
    Check(std::get<source::RecordProgramFailure>(record_result).pc >=
          body.instructions[found->after + 2].offset);
    auto malformed = body;
    malformed.instructions[11].branch_target = {};
    Check(!std::holds_alternative<analysis::GuardedGlobalCall>(
        analysis::MatchGuardedGlobalCall(
            malformed, 3, entry.code->registers)));
    malformed = body;
    malformed.instructions[27].decoded.operands[0].raw_bits = 0;
    Check(!std::holds_alternative<analysis::GuardedGlobalCall>(
        analysis::MatchGuardedGlobalCall(
            malformed, 3, entry.code->registers)));
    malformed = body;
    malformed.instructions[34].decoded.opcode = abc::Opcode::kLDA_V8;
    malformed.instructions[34].decoded.operand_count = 1;
    malformed.instructions[34].decoded.operands[0].kind =
        abc::OperandKind::kRegister;
    malformed.instructions[34].decoded.operands[0].raw_bits = 5;
    Check(!std::holds_alternative<analysis::GuardedGlobalCall>(
        analysis::MatchGuardedGlobalCall(
            malformed, 3, entry.code->registers)));
    const auto nop = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(std::array<std::uint8_t, 1>{0xd5},
                               header.profile));
    for (const std::size_t count : {200u, 201u, 400u, 800u}) {
      auto extended = body;
      const auto final = extended.instructions.back();
      extended.instructions.pop_back();
      for (std::size_t i = 0; i < count; ++i)
        extended.instructions.push_back(
            {{final.offset.value + static_cast<std::uint32_t>(i)}, nop, {}});
      auto terminal = final;
      terminal.offset.value += static_cast<std::uint32_t>(count);
      extended.instructions.push_back(terminal);
      Check(std::holds_alternative<analysis::GuardedGlobalCall>(
          analysis::MatchGuardedGlobalCall(
              extended, 3, entry.code->registers)));
    }
  }
  Check(matched == kExpectedGuardedRecords);
  Check(!shopping_source.empty());
  if (argc == 3) std::cout << shopping_source;
}
