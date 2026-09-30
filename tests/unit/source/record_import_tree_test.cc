#include <unistd.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>
#include "abcd/abc/instruction.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/record_module_context.h"
#include "abcd/source/record_source.h"
#include "abcd/analysis/method_roles.h"

namespace {
using namespace abcd;
void Check(bool value) { if (!value) std::abort(); }
enum class FixtureCase { kValid, kBindingCollision, kExportCollision };
std::string Recover(FixtureCase mode) {
  constexpr std::uint32_t kTypes = 256, kPool = 272, kRecord = 512,
      kMethod = 600, kModule = 1024, kCode = 4096, kFileBytes = 8192;
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
  load(false, 0, 0); store(0);
  load(false, 3, 1); store(1); // Namespace follows all three ordinary import slots.
  load(true, 0, 2); store(0);
  load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0);
  load(false, 0, 0); store(0); // Fresh live read after the dependency mutates it.
  op(abc::Opcode::kRETURNUNDEFINED);
  Check(body.size() < 128);
  bytes[kCode - 3] = 3; bytes[kCode - 2] = static_cast<std::uint8_t>(body.size());
  std::copy(body.begin(), body.end(), bytes.begin() + kCode);
  char path[] = "/tmp/abcd-record-import-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = abc::ProfileId::kDynamic1206;
  header.version = {12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({400, 1000, {1, kTypes}, {static_cast<std::uint32_t>(guards.size()), kPool}, {}, {}});
  abc::RecordInfo record; record.offset = {kRecord}; record.field_count = 1; record.method_count = 1;
  index.fields.push_back({{700}, record.offset, {abc::kDynamicHeaderSize}, {}, 0,
      abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  abc::MethodInfo entry; entry.offset = {kMethod}; entry.owner_offset = record.offset;
  entry.name = "func_main_0"; entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kCode - 4}, {kCode}, 0, 3, static_cast<std::uint32_t>(body.size()), 0};
  index.methods.push_back(entry); index.records.push_back(record);
  const auto context = std::get<source::RecordModuleContext>(source::ReadRecordModuleContext(
      *file, header, index, record, abc::FilePackaging::kMerged));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  source::RouteInputIdentity identity; identity.profile = header.profile;
  identity.abc_version = header.version; identity.file_size = header.file_size;
  const auto build = [&](source::RecordProgramBudget budget = {}) {
    return source::BuildRecordSource(*file, header, index, lookup, record, context, roles,
        budget, nullptr, source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &identity);
  };
  auto result = build();
  if (mode != FixtureCase::kValid) {
    Check(std::get<source::RecordProgramFailure>(result).kind == source::RecordProgramError::kExport);
    Check(unlink(path) == 0); return {};
  }
  if (const auto* error = std::get_if<source::RecordProgramFailure>(&result))
    std::cerr << "record error " << static_cast<unsigned>(error->kind) << '\n';
  Check(std::holds_alternative<source::PrintedSource>(result));
  source::RecordProgramBudget small; small.output.bytes = 1;
  Check(std::get<source::RecordProgramFailure>(build(small)).kind == source::RecordProgramError::kOutputLimit);
  source::RecordProgramBudget exhausted; exhausted.bytes = 0;
  Check(std::get<source::RecordProgramFailure>(build(exhausted)).kind == source::RecordProgramError::kResourceLimit);
  Check(unlink(path) == 0);
  return std::get<source::PrintedSource>(std::move(result)).text;
}
}
int main(int argc, char**) {
  const auto source = Recover(FixtureCase::kValid);
  Recover(FixtureCase::kBindingCollision); Recover(FixtureCase::kExportCollision);
  if (argc > 1) std::cout << source;
}
