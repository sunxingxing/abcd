#include "../../../src/source/function_file.h"
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
enum class FixtureCase { kValid, kBindingCollision, kExportCollision, kBranch, kArmStore, kFunction, kCapture, kPublishedCapture, kReentrantCapture, kLocalFunction, kClassFunction, kClassCapture, kGlobal, kGlobalShadow, kGlobalLocalShadow, kAnonymous, kAnonymousFunction, kAnonymousSingleClass, kAnonymousClassRead, kClassCell, kClassNamedCell, kClassNamedShadow, kClassCellHole, kClassCellUninitialized, kUnknownName };
std::string Recover(FixtureCase mode, abc::ProfileId profile = abc::ProfileId::kDynamic1206, bool inspect_prefix = false) {
  constexpr std::uint32_t kTypes = 256, kPool = 272, kRecord = 512,
      kMethod = 600, kChild = 640, kConstructor = 680, kLiteral = 3000, kModule = 1024, kCode = 4096, kChildCode = 5000, kConstructorCode = 6000, kFileBytes = 8192;
  const bool named_cell = mode == FixtureCase::kClassNamedCell || mode == FixtureCase::kClassNamedShadow;
  const bool class_cell = named_cell || mode == FixtureCase::kClassCell || mode == FixtureCase::kClassCellHole || mode == FixtureCase::kClassCellUninitialized;
  const bool single_class = class_cell || mode == FixtureCase::kAnonymousSingleClass || mode == FixtureCase::kAnonymousClassRead;
  const bool global = mode == FixtureCase::kGlobal || mode == FixtureCase::kGlobalShadow || mode == FixtureCase::kGlobalLocalShadow;
  const bool anonymous = mode == FixtureCase::kAnonymous || mode == FixtureCase::kAnonymousFunction || single_class;
  const bool published_capture = mode == FixtureCase::kPublishedCapture || mode == FixtureCase::kReentrantCapture;
  const bool family = published_capture || single_class || mode == FixtureCase::kFunction || mode == FixtureCase::kCapture || mode == FixtureCase::kLocalFunction || mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture || mode == FixtureCase::kAnonymousFunction;
  const bool mixed = single_class || mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture;
  const bool captured = published_capture || mode == FixtureCase::kCapture || mode == FixtureCase::kClassCapture;
  constexpr unsigned kByteBits = 8;
  std::array<std::string_view, 12> texts{"input", "value", "mutate", "ns", "answer",
      "answerAlias", "snapshot", "live", "*", "depNamespace", "default", "unused"};
  constexpr std::uint32_t kTextBase = 2048, kTextStride = 64;
  enum class Name : std::uint32_t { kInput, kValue, kMutate, kNamespace, kAnswer,
      kAnswerAlias, kSnapshot, kLive, kStar, kNamespaceExport, kDefault, kUnused };
  if (anonymous) {
    texts[static_cast<std::size_t>(Name::kAnswer)] = "*default*";
    texts[static_cast<std::size_t>(Name::kSnapshot)] = "$default$$$0";
  }
  if (mode == FixtureCase::kUnknownName) texts[static_cast<std::size_t>(Name::kAnswer)] = "*unknown*";
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
  const auto snapshot_export = kTextBase + static_cast<std::uint32_t>(texts.size()) * kTextStride;
  if (anonymous) text(snapshot_export, "snapshot");
  bytes[kTypes] = 5; // Pinned dynamic i32 record field type.
  const std::array<Name, 4> guards{Name::kInput, Name::kNamespace, Name::kAnswer, Name::kMutate};
  for (std::size_t i = 0; i < guards.size(); ++i) word(kPool + i * sizeof(std::uint32_t), name(guards[i]));
  if (global) {
    text(kTextBase + texts.size() * kTextStride, "external");
    word(kPool + guards.size() * sizeof(std::uint32_t), mode == FixtureCase::kGlobalShadow ? name(Name::kInput) : mode == FixtureCase::kGlobalLocalShadow ? name(Name::kAnswer) : kTextBase + texts.size() * kTextStride);
  }
  if (family) word(kPool + guards.size() * sizeof(std::uint32_t), kChild);
  if (mixed) {
    word(kPool + 5 * sizeof(std::uint32_t), kConstructor);
    word(kPool + 6 * sizeof(std::uint32_t), kLiteral);
    word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    word(kLiteral + 5, 0); // Empty class buffer and prototype split.
  }
  if (named_cell) {
    constexpr std::uint32_t kScopeLiteral = kLiteral + 64, kCellName = kLiteral + 128;
    word(kPool + 7 * sizeof(std::uint32_t), kScopeLiteral);
    text(kCellName, "capturedValue");
    if (mode == FixtureCase::kClassNamedShadow) word(kPool + 8 * sizeof(std::uint32_t), kCellName);
    // Pinned scope literal: leading count, followed by name/slot pairs.
    word(kScopeLiteral, 6);
    bytes[kScopeLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kScopeLiteral + 5, 1);
    bytes[kScopeLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(kScopeLiteral + 10, kCellName);
    bytes[kScopeLiteral + 14] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kScopeLiteral + 15, 0);
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
  emit(single_class ? 2 : 3); // Adjacent local export aliases share local slot 0.
  emit(name(Name::kAnswer)); emit(name(anonymous ? Name::kDefault : Name::kAnswer));
  if (!single_class) { emit(name(Name::kAnswer)); emit(name(Name::kAnswerAlias)); }
  emit(name(Name::kSnapshot)); emit(anonymous ? snapshot_export : name(Name::kSnapshot));
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
  if (global) {
    op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16);
    body.insert(body.end(), {0, static_cast<std::uint8_t>(guards.size()), 0});
  }
  if (mode == FixtureCase::kBranch || mode == FixtureCase::kArmStore) {
    load(false, 0, 0);
    op(abc::Opcode::kJEQZ_IMM8); const auto branch = body.size(); body.push_back(0);
    if (mode == FixtureCase::kArmStore) store(0);
    else { load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0); }
    body[branch] = static_cast<std::uint8_t>(body.size() - branch + 1);
  }
  load(false, 0, 0); store(0);
  load(false, 3, 1); store(1); // Namespace follows all three ordinary import slots.
  if (!single_class) {
    load(true, 0, 2); store(0);
    load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0);
    load(false, 0, 0); store(0); // Fresh live read after the dependency mutates it.
  }
  if (family) {
    if (class_cell) {
      if (named_cell) {
        op(abc::Opcode::kNEWLEXENVWITHNAME_IMM8_ID16); body.insert(body.end(), {1, 7, 0});
      } else { op(abc::Opcode::kNEWLEXENV_IMM8); body.push_back(1); }
      if (mode != FixtureCase::kClassCellUninitialized) {
        load(false, 0, 0);
        op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
      }
    }
    if (mixed) {
      op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); body.push_back(0);
      op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
      body.insert(body.end(), {0, 5, 0, 6, 0, 0, 0, 0}); store(1);
      op(abc::Opcode::kSTA_V8); body.push_back(0);
    }
    if (captured) {
      op(abc::Opcode::kNEWLEXENV_IMM8); body.push_back(1);
      if (!published_capture) {
        if (mixed) { op(abc::Opcode::kLDA_V8); body.push_back(0); }
        else { op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {1, 0, 0, 0}); }
        op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
      }
    }
    if (!single_class) {
      op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
      body.insert(body.end(), {0, static_cast<std::uint8_t>(guards.size()), 0, 0}); store(0);
      if (mode == FixtureCase::kReentrantCapture) {
        load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0);
      }
      if (mode == FixtureCase::kCapture || published_capture) {
        op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {42, 0, 0, 0});
        op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
      }
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
    else if (mode == FixtureCase::kLocalFunction || mode == FixtureCase::kAnonymousFunction) load(true, 0, 2);
    else load(false, 0, 0);
    op(abc::Opcode::kRETURN);
    body.swap(child_body);
    bytes[kChildCode - 3] = 3; bytes[kChildCode - 2] = static_cast<std::uint8_t>(child_body.size());
    std::copy(child_body.begin(), child_body.end(), bytes.begin() + kChildCode);
  }
  std::vector<std::uint8_t> constructor_body{static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)};
  if (mixed) {
    if (class_cell) {
      constructor_body = {static_cast<std::uint8_t>(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0,
          static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
      if (mode == FixtureCase::kClassNamedShadow)
        constructor_body.insert(constructor_body.begin(), {static_cast<std::uint8_t>(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16), 0, 8, 0});
      if (mode == FixtureCase::kClassCellHole)
        constructor_body = {static_cast<std::uint8_t>(abc::Opcode::kLDHOLE),
            static_cast<std::uint8_t>(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
            static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)};
    }
    if (mode == FixtureCase::kAnonymousClassRead) {
      // The sole default store is unchanged. Only a nested class constructor
      // reads that slot; syntax selection must inspect its owned family.
      constructor_body = {static_cast<std::uint8_t>(abc::Opcode::kLDLOCALMODULEVAR_IMM8), 0,
          static_cast<std::uint8_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16),
          static_cast<std::uint8_t>(static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16) >> kByteBits),
          2, 0, static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
    }
    bytes[kConstructorCode - 3] = 3; bytes[kConstructorCode - 2] = static_cast<std::uint8_t>(constructor_body.size());
    std::copy(constructor_body.begin(), constructor_body.end(), bytes.begin() + kConstructorCode);
  }
  char path[] = "/tmp/abcd-record-import-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = profile;
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0};
  header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({400, 1000, {1, kTypes}, {static_cast<std::uint32_t>(mode == FixtureCase::kClassNamedShadow ? 9 : named_cell ? 8 : mixed ? 7 : guards.size() + family + global), kPool}, {}, {}});
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
    constructor.code = abc::CodeInfo{{kConstructorCode - 4}, {kConstructorCode}, 0, 3, static_cast<std::uint32_t>(constructor_body.size()), 0};
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
  if (inspect_prefix) {
    const auto loaded = source::ReadFunctionValues(*file,header,index,{0},identity,
        source::InputCaptureMode::kConditional,{},&context);
    Check(std::holds_alternative<source::FileFunction>(loaded));
    const auto& locals = std::get<source::FileFunction>(loaded).input.Proof().locals;
    Check(std::none_of(locals.unexposed_initializations.begin(),locals.unexposed_initializations.end(),
        [](const auto& value) { return value.has_value(); }));
    const auto count = std::count_if(locals.module_prefix_initializations.begin(),locals.module_prefix_initializations.end(),
        [](const auto& value) { return value.has_value(); });
    Check(count == (mode == FixtureCase::kPublishedCapture ? 1 : 0));
    Check(unlink(path) == 0); return {};
  }
  const auto build = [&](source::RecordProgramBudget budget = {}) {
    return source::BuildRecordSource(*file, header, index, lookup, record, context, roles,
        budget, nullptr, source::RoutePolicy::kRequireValueTree, nullptr, nullptr, &identity, family ? &creations : nullptr, mixed ? &classes : nullptr);
  };
  auto result = build();
  if (mode == FixtureCase::kReentrantCapture) {
    Check(!std::holds_alternative<source::PrintedSource>(result));
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kClassCellHole || mode == FixtureCase::kClassCellUninitialized) {
    Check(!std::holds_alternative<source::PrintedSource>(result));
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kGlobalShadow || mode == FixtureCase::kGlobalLocalShadow) {
    Check(std::get<source::RecordProgramFailure>(result).kind == source::RecordProgramError::kValue);
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kArmStore) {
    Check(std::get<source::RecordProgramFailure>(result).kind == source::RecordProgramError::kInitializer);
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kBindingCollision || mode == FixtureCase::kExportCollision || mode == FixtureCase::kUnknownName) {
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
  const bool anonymous = mode.starts_with("--anonymous");
  if (mode == "--publication-window") {
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
      Recover(FixtureCase::kPublishedCapture,profile,true);
      Recover(FixtureCase::kReentrantCapture,profile,true);
    }
    return 0;
  }
  if (mode == "--published-capture") {
    Recover(FixtureCase::kReentrantCapture);
    std::cout << Recover(FixtureCase::kPublishedCapture); return 0;
  }
  if (mode == "--class-cell") { std::cout << Recover(FixtureCase::kClassCell); return 0; }
  if (mode == "--class-shadow-cell") {
    const auto source = Recover(FixtureCase::kClassNamedShadow);
    Check(source.find("let capturedValue;") == std::string::npos);
    std::cout << source; return 0;
  }
  if (mode == "--class-named-cell") {
    const auto source = Recover(FixtureCase::kClassNamedCell);
    std::cout << source << std::flush;
    Check(source.find("let capturedValue;") != std::string::npos);
    Check(source.find("return capturedValue;") != std::string::npos);
    return 0;
  }
  const auto profile = anonymous && argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
      anonymous && argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
  const auto source = Recover(mode == "--anonymous-single-class" ? FixtureCase::kAnonymousSingleClass : mode == "--anonymous-class-read" ? FixtureCase::kAnonymousClassRead : mode == "--anonymous" ? FixtureCase::kAnonymous : mode == "--anonymous-function" ? FixtureCase::kAnonymousFunction : mode == "--global" ? FixtureCase::kGlobal : mode == "--class-capture" ? FixtureCase::kClassCapture : mode == "--class-function" ? FixtureCase::kClassFunction : mode == "--local-function" ? FixtureCase::kLocalFunction : mode == "--function" ? FixtureCase::kFunction : mode == "--capture" ?
      FixtureCase::kCapture : argc > 2 ? FixtureCase::kBranch : FixtureCase::kValid, profile);
  if (anonymous) {
    if (mode == "--anonymous-single-class") Check(source.find("export default input;") != std::string::npos);
    if (mode == "--anonymous-class-read") {
      Check(source.find("export default") == std::string::npos);
      Check(source.find("return $default$$$$0;") != std::string::npos);
    }
    std::cout << source; return 0;
  }
  for (const auto candidate : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301}) {
    Recover(FixtureCase::kClassNamedCell, candidate); Recover(FixtureCase::kClassNamedShadow, candidate);
    Recover(FixtureCase::kClassCell, candidate); Recover(FixtureCase::kClassCellHole, candidate); Recover(FixtureCase::kClassCellUninitialized, candidate);
  }
  Recover(FixtureCase::kUnknownName);
  Recover(FixtureCase::kClassCapture); Recover(FixtureCase::kClassFunction); Recover(FixtureCase::kFunction); Recover(FixtureCase::kCapture); Recover(FixtureCase::kLocalFunction);
  Recover(FixtureCase::kGlobal); Recover(FixtureCase::kGlobalShadow); Recover(FixtureCase::kGlobalLocalShadow);
  Recover(FixtureCase::kBranch); Recover(FixtureCase::kArmStore);
  Recover(FixtureCase::kBindingCollision); Recover(FixtureCase::kExportCollision);
  if (argc > 1) std::cout << source;
}
