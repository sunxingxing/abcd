#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <source_location>
#include <vector>
#include "abcd/abc/instruction.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_source.h"
#include "abcd/analysis/method_roles.h"
#include "abcd/analysis/function_creation_sites.h"
#include "abcd/analysis/class_method_sites.h"
#include "abcd/abc/literal_array.h"

namespace {
using namespace abcd;
void Check(bool value, std::source_location at = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << at.line() << '\n'; std::abort(); }
}
enum class FixtureCase { kValid, kBindingCollision, kExportCollision, kBranch, kArmStore, kFunction, kCapture, kLocalFunction, kClassFunction, kClassCapture };
std::string Recover(FixtureCase mode) {
  constexpr std::uint32_t kTypes = 256, kPool = 272, kRecord = 512,
      kMethod = 600, kChild = 640, kConstructor = 680, kLiteral = 3000, kModule = 1024, kCode = 4096, kChildCode = 5000, kConstructorCode = 6000, kFileBytes = 8192;
  const bool family = mode == FixtureCase::kFunction || mode == FixtureCase::kCapture || mode == FixtureCase::kLocalFunction || mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture;
  const bool mixed = mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture;
  const bool captured = mode == FixtureCase::kCapture || mode == FixtureCase::kClassCapture;
  constexpr unsigned kByteBits = 8;
  const std::array<std::string_view, 12> texts{"input", "value", "mutate", "ns", "answer",
      "answerAlias", "snapshot", "live", "*", "depNamespace", "default", "unused"};
  constexpr std::uint32_t kTextBase = 2048, kTextStride = 64;
  enum class Name : std::uint32_t { kInput, kValue, kMutate, kNamespace, kAnswer,
      kAnswerAlias, kSnapshot, kLive, kStar, kNamespaceExport, kDefault, kUnused };
  const auto name = [&](Name id) { return kTextBase + static_cast<std::uint32_t>(id) * kTextStride; };
  std::vector<std::uint8_t> bytes(kFileBytes);
  const auto word = [&](std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < sizeof(value); ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (i * kByteBits));
  };
  const auto text = [&](std::size_t at, std::string_view value) {
    bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at);
  };
  text(abc::kDynamicHeaderSize, "moduleRecordIdx");
  constexpr std::array<std::uint32_t, 3> kRequests{120, 160, 200};
  text(kRequests[0], "./side.mjs"); text(kRequests[1], "./dep.mjs"); text(kRequests[2], "./star.mjs");
  for (std::size_t i = 0; i < texts.size(); ++i) text(kTextBase + i * kTextStride, texts[i]);
  bytes[kTypes] = 5; // Pinned dynamic i32 record field type.
  const std::array<Name, 4> guards{Name::kInput, Name::kNamespace, Name::kAnswer, Name::kMutate};
  for (std::size_t i = 0; i < guards.size(); ++i) word(kPool + i * sizeof(std::uint32_t), name(guards[i]));
  if (family) word(kPool + guards.size() * sizeof(std::uint32_t), kChild);
  if (mixed) {
    word(kPool + 5 * sizeof(std::uint32_t), kConstructor);
    word(kPool + 6 * sizeof(std::uint32_t), kLiteral);
    word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    word(kLiteral + 5, 0); // Empty class buffer and prototype split.
  }
  std::size_t cursor = kModule;
  const auto emit = [&](std::uint32_t value) { word(cursor, value); cursor += sizeof(value); };
  const auto request = [&](std::uint16_t value) {
    bytes[cursor++] = static_cast<std::uint8_t>(value);
    bytes[cursor++] = static_cast<std::uint8_t>(value >> kByteBits);
  };
  emit(0); emit(kRequests.size()); for (const auto offset : kRequests) emit(offset);
  emit(3); // Value, mutator, and an unused default import all keep their bindings.
  emit(name(mode == FixtureCase::kBindingCollision ? Name::kAnswer : Name::kInput)); emit(name(Name::kValue)); request(1);
  emit(name(Name::kMutate)); emit(name(Name::kMutate)); request(1);
  emit(name(Name::kUnused)); emit(name(Name::kDefault)); request(1);
  emit(1); emit(name(Name::kNamespace)); request(1);
  emit(3); // Adjacent local export aliases share local slot 0.
  emit(name(Name::kAnswer)); emit(name(Name::kAnswer));
  emit(name(Name::kAnswer)); emit(name(Name::kAnswerAlias));
  emit(name(Name::kSnapshot)); emit(name(Name::kSnapshot));
  emit(2);
  emit(name(mode == FixtureCase::kExportCollision ? Name::kAnswer : Name::kLive)); emit(name(Name::kValue)); request(1);
  emit(name(Name::kNamespaceExport)); emit(name(Name::kStar)); request(1);
  emit(1); request(2);
  std::vector<std::uint8_t> body;
  const auto op = [&](abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    body.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) body.push_back(static_cast<std::uint8_t>(wire >> kByteBits));
  };
  const auto load = [&](bool local, std::uint8_t slot, std::uint16_t guard) {
    op(local ? abc::Opcode::kLDLOCALMODULEVAR_IMM8 : abc::Opcode::kLDEXTERNALMODULEVAR_IMM8);
    body.push_back(slot); op(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
    body.push_back(static_cast<std::uint8_t>(guard)); body.push_back(static_cast<std::uint8_t>(guard >> kByteBits));
  };
  const auto store = [&](std::uint8_t slot) { op(abc::Opcode::kSTMODULEVAR_IMM8); body.push_back(slot); };
  if (mode == FixtureCase::kBranch || mode == FixtureCase::kArmStore) {
    load(false, 0, 0);
    op(abc::Opcode::kJEQZ_IMM8); const auto branch = body.size(); body.push_back(0);
    if (mode == FixtureCase::kArmStore) store(0);
    else { load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0); }
    body[branch] = static_cast<std::uint8_t>(body.size() - branch + 1);
  }
  load(false, 0, 0); store(0);
  load(false, 3, 1); store(1); // Namespace follows all three ordinary import slots.
  load(true, 0, 2); store(0);
  load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0);
  load(false, 0, 0); store(0); // Fresh live read after the dependency mutates it.
  if (family) {
    if (mixed) {
      op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); body.push_back(0);
      op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
      body.insert(body.end(), {0, 5, 0, 6, 0, 0, 0, 0}); store(1);
      op(abc::Opcode::kSTA_V8); body.push_back(0);
    }
    if (captured) {
      op(abc::Opcode::kNEWLEXENV_IMM8); body.push_back(1);
      if (mixed) { op(abc::Opcode::kLDA_V8); body.push_back(0); }
      else { op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {1, 0, 0, 0}); }
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
    }
    op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
    body.insert(body.end(), {0, static_cast<std::uint8_t>(guards.size()), 0, 0}); store(0);
    if (mode == FixtureCase::kCapture) {
      op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {42, 0, 0, 0});
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
    }
  }
  op(abc::Opcode::kRETURNUNDEFINED);
  Check(body.size() < 128);
  bytes[kCode - 4] = mixed ? 1 : 0;
  bytes[kCode - 3] = 3; bytes[kCode - 2] = static_cast<std::uint8_t>(body.size());
  std::copy(body.begin(), body.end(), bytes.begin() + kCode);
  std::vector<std::uint8_t> child_body;
  if (family) {
    body.swap(child_body);
    if (captured) { op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); body.push_back(0); }
    else if (mode == FixtureCase::kLocalFunction) load(true, 0, 2);
    else load(false, 0, 0);
    op(abc::Opcode::kRETURN);
    body.swap(child_body);
    bytes[kChildCode - 3] = 3; bytes[kChildCode - 2] = static_cast<std::uint8_t>(child_body.size());
    std::copy(child_body.begin(), child_body.end(), bytes.begin() + kChildCode);
  }
  if (mixed) {
    bytes[kConstructorCode - 3] = 3; bytes[kConstructorCode - 2] = 1;
    bytes[kConstructorCode] = static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED);
  }
  char path[] = "/tmp/abcd-record-import-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({400, 1000, {1, kTypes}, {static_cast<std::uint32_t>(mixed ? 7 : guards.size() + family), kPool}, {}, {}});
  abc::RecordInfo record; record.offset = {kRecord}; record.field_count = 1; record.method_count = mixed ? 3 : family ? 2 : 1;
  index.fields.push_back({{700}, record.offset, {abc::kDynamicHeaderSize}, {}, 0,
      abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  abc::MethodInfo entry; entry.offset = {kMethod}; entry.owner_offset = record.offset;
  entry.name = "func_main_0"; entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kCode - 4}, {kCode}, mixed ? 1U : 0U, 3, static_cast<std::uint32_t>(body.size()), 0};
  index.methods.push_back(entry);
  if (family) {
    auto child = entry; child.offset = {kChild}; child.name = "reader"; child.name_utf16_length = child.name.size();
    constexpr unsigned kFunctionKindShift = 8; // Pinned dynamic method access flags.
    child.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
    child.code = abc::CodeInfo{{kChildCode - 4}, {kChildCode}, 0, 3, static_cast<std::uint32_t>(child_body.size()), 0};
    index.methods.push_back(child);
  }
  if (mixed) {
    auto constructor = entry; constructor.offset = {kConstructor}; constructor.name = "Box"; constructor.name_utf16_length = 3;
    constructor.code = abc::CodeInfo{{kConstructorCode - 4}, {kConstructorCode}, 0, 3, 1, 0};
    index.methods.push_back(constructor);
  }
  index.records.push_back(record);
  const auto context = std::get<source::RecordModuleContext>(source::ReadRecordModuleContext(
      *file, header, index, record, abc::FilePackaging::kMerged));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  const auto creations = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(*file, header, index, lookup));
  source::RouteInputIdentity identity; identity.profile = header.profile;
  identity.abc_version = header.version; identity.file_size = header.file_size;
  const auto build = [&](source::RecordProgramBudget budget = {}) {
    return source::BuildRecordSource(*file, header, index, lookup, record, context, roles,
        budget, nullptr, source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &identity, family ? &creations : nullptr, mixed ? &classes : nullptr);
  };
  auto result = build();
  if (mode == FixtureCase::kArmStore) {
    Check(std::get<source::RecordProgramFailure>(result).kind == source::RecordProgramError::kInitializer);
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kBindingCollision || mode == FixtureCase::kExportCollision) {
    Check(std::get<source::RecordProgramFailure>(result).kind == source::RecordProgramError::kExport);
    Check(unlink(path) == 0); return {};
  }
  if (const auto* error = std::get_if<source::RecordProgramFailure>(&result))
    std::cerr << "record error " << static_cast<unsigned>(error->kind) << '\n';
  if (const auto* error = std::get_if<source::RouteRefusal>(&result))
    std::cerr << "route refusal " << unsigned(error->failure.reason) << '\n';
  Check(std::holds_alternative<source::PrintedSource>(result));
  source::RecordProgramBudget small; small.output.bytes = 1;
  Check(std::get<source::RecordProgramFailure>(build(small)).kind == source::RecordProgramError::kOutputLimit);
  source::RecordProgramBudget exhausted; exhausted.bytes = 0;
  Check(std::get<source::RecordProgramFailure>(build(exhausted)).kind == source::RecordProgramError::kResourceLimit);
  Check(unlink(path) == 0);
  return std::get<source::PrintedSource>(std::move(result)).text;
}
}
int main(int argc, char** argv) {
  const std::string_view mode = argc > 1 ? argv[1] : "";
  const auto source = Recover(mode == "--class-capture" ? FixtureCase::kClassCapture : mode == "--class-function" ? FixtureCase::kClassFunction : mode == "--local-function" ? FixtureCase::kLocalFunction : mode == "--function" ? FixtureCase::kFunction : mode == "--capture" ?
      FixtureCase::kCapture : argc > 2 ? FixtureCase::kBranch : FixtureCase::kValid);
  Recover(FixtureCase::kClassCapture); Recover(FixtureCase::kClassFunction); Recover(FixtureCase::kFunction); Recover(FixtureCase::kCapture); Recover(FixtureCase::kLocalFunction);
  Recover(FixtureCase::kBranch); Recover(FixtureCase::kArmStore);
  Recover(FixtureCase::kBindingCollision); Recover(FixtureCase::kExportCollision);
  if (argc > 1) std::cout << source;
}
