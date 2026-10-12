#include "../../../src/source/function_file.h"
#include "../../../src/source/family_file.h"
#include "../../../src/source/module_aliases.h"
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
enum class FixtureCase { kValid, kCleanupEntry, kCleanupOverwrite, kBindingCollision, kExportCollision, kBranch, kArmStore, kFunction, kCapture, kCapturedDefault, kCapturedUnusedDefault, kStringDefault, kPublishedCapture, kReentrantCapture, kPublishedCaptureV1, kPublishedBadSlot, kLocalFunction, kClassFunction, kClassCapture, kGlobal, kGlobalShadow, kGlobalLocalShadow, kAnonymous, kAnonymousFunction, kAnonymousSingleClass, kAnonymousClassRead, kClassCell, kClassNamedCell, kClassNamedShadow, kClassCellHole, kClassCellUninitialized, kUnknownName, kClassBinding, kClassBindingMismatch, kClassBindingAssignment, kModuleWriter, kModuleWriterBranch, kModuleWriterEarly, kModuleWriterLiteral, kModuleWriterCatch, kModuleWriterParentBranch, kModuleWriterParentBranchCatch, kModuleWriterParentBranchRaw, kModuleWriterParentBranchRawCatch };
enum class DirectDefault { kAbsent, kFunction, kClass };
enum class ClassDeclarationCase { kAbsent, kSafe, kOuterRead, kDerived, kDerivedNameConflict };
std::string Recover(FixtureCase mode, abc::ProfileId profile = abc::ProfileId::kDynamic1206, bool inspect_prefix = false,
    DirectDefault direct = DirectDefault::kAbsent, ClassDeclarationCase class_declaration = ClassDeclarationCase::kAbsent,
    unsigned class_copies = 0, bool class_split = false,
    std::optional<source::SourceContract> contract = {}) {
  constexpr std::uint32_t kTypes = 256, kPool = 272, kRecord = 512,
      kMethod = 600, kChild = 640, kConstructor = 680, kLiteral = 3000, kModule = 1024, kCode = 4096, kChildCode = 5000, kConstructorCode = 6000, kFileBytes = 8192;
  const bool parent_raw = mode == FixtureCase::kModuleWriterParentBranchRaw || mode == FixtureCase::kModuleWriterParentBranchRawCatch;
  const bool parent_branch = mode == FixtureCase::kModuleWriterParentBranch || mode == FixtureCase::kModuleWriterParentBranchCatch || parent_raw;
  const bool writer_catch = mode == FixtureCase::kModuleWriterCatch || mode == FixtureCase::kModuleWriterParentBranchCatch || mode == FixtureCase::kModuleWriterParentBranchRawCatch;
  const bool module_writer = mode == FixtureCase::kModuleWriter || mode == FixtureCase::kModuleWriterBranch || mode == FixtureCase::kModuleWriterEarly || mode == FixtureCase::kModuleWriterLiteral || writer_catch || parent_branch;
  const bool child_parameter = module_writer || mode == FixtureCase::kCapturedDefault || mode == FixtureCase::kCapturedUnusedDefault || mode == FixtureCase::kStringDefault;
  const bool class_binding = class_declaration != ClassDeclarationCase::kAbsent || mode == FixtureCase::kClassBinding || mode == FixtureCase::kClassBindingMismatch || mode == FixtureCase::kClassBindingAssignment;
  const bool derived_name_conflict = class_declaration == ClassDeclarationCase::kDerivedNameConflict;
  const bool derived_declaration = class_declaration == ClassDeclarationCase::kDerived || derived_name_conflict;
  const bool named_cell = mode == FixtureCase::kClassNamedCell || mode == FixtureCase::kClassNamedShadow;
  const bool class_cell = named_cell || mode == FixtureCase::kClassCell || mode == FixtureCase::kClassCellHole || mode == FixtureCase::kClassCellUninitialized;
  const bool single_class = class_cell || mode == FixtureCase::kAnonymousSingleClass || mode == FixtureCase::kAnonymousClassRead;
  const bool global = mode == FixtureCase::kGlobal || mode == FixtureCase::kGlobalShadow || mode == FixtureCase::kGlobalLocalShadow;
  const bool anonymous = mode == FixtureCase::kAnonymous || mode == FixtureCase::kAnonymousFunction || single_class;
  const bool cleanup_entry = mode == FixtureCase::kCleanupEntry || mode == FixtureCase::kCleanupOverwrite;
  const bool string_default = mode == FixtureCase::kStringDefault;
  const bool unused_default = mode == FixtureCase::kCapturedUnusedDefault;
  const bool captured_default = unused_default || mode == FixtureCase::kCapturedDefault;
  const bool published_capture = mode == FixtureCase::kPublishedCapture || mode == FixtureCase::kReentrantCapture || mode == FixtureCase::kPublishedCaptureV1 || mode == FixtureCase::kPublishedBadSlot;
  const bool family = direct != DirectDefault::kAbsent || module_writer || class_binding || string_default || captured_default || published_capture || single_class || mode == FixtureCase::kFunction || mode == FixtureCase::kCapture || mode == FixtureCase::kLocalFunction || mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture || mode == FixtureCase::kAnonymousFunction;
  const bool mixed = direct == DirectDefault::kClass || class_binding || single_class || mode == FixtureCase::kClassFunction || mode == FixtureCase::kClassCapture;
  const bool captured = captured_default || published_capture || mode == FixtureCase::kCapture || mode == FixtureCase::kClassCapture;
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
  constexpr std::uint16_t kStringDefaultSlot = 5;
  if (string_default) {
    constexpr std::uint32_t kStringDefaultEntity = kLiteral + 512;
    word(kPool + kStringDefaultSlot * sizeof(std::uint32_t),kStringDefaultEntity);
    text(kStringDefaultEntity,"line \"quote\"\\path\n");
  }
  if (mixed) {
    word(kPool + 5 * sizeof(std::uint32_t), kConstructor);
    word(kPool + 6 * sizeof(std::uint32_t), kLiteral);
    word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
    word(kLiteral + 5, 0); // Empty class buffer and prototype split.
  }
  if (class_binding) word(kPool + 8 * sizeof(std::uint32_t), name(Name::kSnapshot));
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
  const bool sole_default = single_class || direct != DirectDefault::kAbsent;
  emit(sole_default ? 2 : 3); // Adjacent local export aliases share local slot 0.
  emit(name(Name::kAnswer)); emit(name(anonymous ? Name::kDefault : Name::kAnswer));
  if (!sole_default) { emit(name(Name::kAnswer)); emit(name(Name::kAnswerAlias)); }
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
  if (direct != DirectDefault::kAbsent) {
    load(false,3,1); store(1);
    if (direct == DirectDefault::kClass) {
      op(abc::Opcode::kLDHOLE); op(abc::Opcode::kSTA_V8); body.push_back(0);
      op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
      body.insert(body.end(),{0,5,0,6,0,0,0,0});
    } else {
      op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
      body.insert(body.end(),{0,static_cast<std::uint8_t>(guards.size()),0,0});
    }
    store(0);
  } else {
    if (global) {
      op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16);
      body.insert(body.end(), {0, static_cast<std::uint8_t>(guards.size()), 0});
    }
    if (mode == FixtureCase::kBranch || mode == FixtureCase::kArmStore || parent_branch) {
      load(false, 0, 0);
      op(abc::Opcode::kJEQZ_IMM8); const auto branch = body.size(); body.push_back(0);
      if (mode == FixtureCase::kArmStore) store(0);
      else { load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0); }
      body[branch] = static_cast<std::uint8_t>(body.size() - branch + 1);
    }
    load(false, 0, 0); store(0);
    if ((!class_binding || mode == FixtureCase::kClassBindingAssignment) && mode != FixtureCase::kModuleWriterEarly) { load(false, 3, 1); store(1); } // Namespace follows ordinary imports.
    if (!single_class) {
      if (parent_raw) { op(abc::Opcode::kLDLOCALMODULEVAR_IMM8); body.push_back(0); }
      else load(true, 0, 2);
      store(0);
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
        if (derived_declaration) load(false, 0, 0);
        else op(abc::Opcode::kLDHOLE);
        op(abc::Opcode::kSTA_V8); body.push_back(0);
        op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
        body.insert(body.end(), {0, 5, 0, 6, 0, 0, 0, 0});
        for (unsigned i = 0; i < class_copies; ++i)
          body.insert(body.end(), {static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
              static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 0});
        if (class_split) body.insert(body.end(), {static_cast<std::uint8_t>(abc::Opcode::kJMP_IMM8), 2});
        store(1);
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
      if (class_binding) {
        load(true, 1, 8); store(0);
        load(false, 0, 0); store(1);
      }
      if (!single_class && !class_binding) {
        op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8);
        body.insert(body.end(), {0, static_cast<std::uint8_t>(guards.size()), 0, static_cast<std::uint8_t>(module_writer ? 1 : 0)});
        store(mode == FixtureCase::kPublishedBadSlot ? 99 : 0);
        if (mode == FixtureCase::kReentrantCapture) {
          load(false, 1, 3); op(abc::Opcode::kCALLARG0_IMM8); body.push_back(0);
        }
        if (captured_default || mode == FixtureCase::kCapture || published_capture) {
          op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {42, 0, 0, 0});
          op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
        }
      }
    }
    if (mode == FixtureCase::kModuleWriterEarly) { load(false, 3, 1); store(1); }
  }
  std::uint32_t cleanup_end = 0, cleanup_handler = 0;
  constexpr std::uint8_t kPushBytes = 2;
  if (cleanup_entry) {
    body.insert(body.begin(),{static_cast<std::uint8_t>(abc::Opcode::kNEWLEXENV_IMM8),1});
    cleanup_end = static_cast<std::uint32_t>(body.size());
    op(abc::Opcode::kPOPLEXENV);
  }
  op(abc::Opcode::kRETURNUNDEFINED);
  if (cleanup_entry) {
    cleanup_handler = static_cast<std::uint32_t>(body.size());
    if (mode == FixtureCase::kCleanupOverwrite) op(abc::Opcode::kLDUNDEFINED);
    op(abc::Opcode::kPOPLEXENV);
    const auto thrown = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    body.insert(body.end(),{static_cast<std::uint8_t>(thrown),static_cast<std::uint8_t>(thrown >> kByteBits)});
  }
  Check(body.size() < 128);
  bytes[kCode - 4] = mixed ? 1 : 0;
  bytes[kCode - 3] = 3; bytes[kCode - 2] = static_cast<std::uint8_t>(body.size());
  std::copy(body.begin(), body.end(), bytes.begin() + kCode);
  if (cleanup_entry) {
    bytes[kCode-1] = 1;
    const std::array<std::uint8_t,6> table{kPushBytes,static_cast<std::uint8_t>(cleanup_end-kPushBytes),1,0,
        static_cast<std::uint8_t>(cleanup_handler),static_cast<std::uint8_t>(body.size()-cleanup_handler)};
    std::copy(table.begin(),table.end(),bytes.begin()+kCode+body.size());
  }
  std::vector<std::uint8_t> child_body;
  if (family) {
    body.swap(child_body);
    if (module_writer) {
      op(abc::Opcode::kLDA_V8); body.push_back(4);
      if (mode == FixtureCase::kModuleWriterBranch) {
        op(abc::Opcode::kJEQZ_IMM8); body.push_back(4); // Branch and skipped store widths.
      }
      if (mode == FixtureCase::kModuleWriterLiteral) {
        op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(), {7, 0, 0, 0});
      }
      store(1);
      op(abc::Opcode::kLDA_V8); body.push_back(4);
      if (writer_catch) { op(abc::Opcode::kTHROW_PREF_NONE); body.push_back(0); }
    } else if (string_default) {
      op(abc::Opcode::kNEWLEXENV_IMM8); body.push_back(1);
      op(abc::Opcode::kLDA_V8); body.push_back(4);
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); body.push_back(0);
      op(abc::Opcode::kSTA_V8); body.push_back(0);
      op(abc::Opcode::kLDUNDEFINED);
      op(abc::Opcode::kSTRICTEQ_IMM8_V8); body.insert(body.end(),{0,0});
      constexpr std::uint8_t kDefaultArmDistance = 7; // Branch(2), string(3), store(2).
      op(abc::Opcode::kJEQZ_IMM8); body.push_back(kDefaultArmDistance);
      op(abc::Opcode::kLDA_STR_ID16); body.insert(body.end(),{kStringDefaultSlot,0});
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); body.push_back(0);
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); body.push_back(0);
    } else if (captured_default) {
      op(abc::Opcode::kLDA_V8); body.push_back(4);
      op(abc::Opcode::kSTA_V8); body.push_back(0);
      op(abc::Opcode::kLDUNDEFINED);
      op(abc::Opcode::kSTRICTEQ_IMM8_V8); body.insert(body.end(), {0,0});
      op(abc::Opcode::kJEQZ_IMM8); body.push_back(10);
      op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); body.push_back(0);
      const auto guard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
      body.insert(body.end(), {static_cast<std::uint8_t>(guard), static_cast<std::uint8_t>(guard >> kByteBits),0,0});
      op(abc::Opcode::kSTA_V8); body.push_back(4);
      if (unused_default) { op(abc::Opcode::kLDAI_IMM32); body.insert(body.end(),{99,0,0,0}); }
      else { op(abc::Opcode::kLDA_V8); body.push_back(4); }
    } else if (captured) { op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); body.push_back(0); }
    else if (mode == FixtureCase::kLocalFunction || mode == FixtureCase::kAnonymousFunction) load(true, 0, 2);
    else load(false, 0, 0);
    op(abc::Opcode::kRETURN);
    body.swap(child_body);
    bytes[kChildCode - 4] = child_parameter ? 1 : 0;
    bytes[kChildCode - 3] = child_parameter ? 4 : 3; bytes[kChildCode - 2] = static_cast<std::uint8_t>(child_body.size());
    std::copy(child_body.begin(), child_body.end(), bytes.begin() + kChildCode);
    if (writer_catch) {
      bytes[kChildCode - 1] = 1;
      // LDA(2), store(2), LDA(2), THROW(2); Catch observes original thrown acc.
      const std::array<std::uint8_t, 6> table{0, 8, 1, 0, 8, 1};
      std::copy(table.begin(), table.end(), bytes.begin() + kChildCode + child_body.size());
    }
  }
  std::vector<std::uint8_t> constructor_body{static_cast<std::uint8_t>(abc::Opcode::kRETURNUNDEFINED)};
  if (mixed) {
    if (class_binding && class_declaration != ClassDeclarationCase::kSafe && !derived_declaration) {
      const auto guard = static_cast<std::uint16_t>(abc::Opcode::kTHROW_UNDEFINEDIFHOLEWITHNAME_PREF_ID16);
      constructor_body = {static_cast<std::uint8_t>(abc::Opcode::kLDLOCALMODULEVAR_IMM8), 1,
          static_cast<std::uint8_t>(guard), static_cast<std::uint8_t>(guard >> kByteBits),
          8, 0, static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
    }
    if (derived_declaration) {
      constructor_body = {static_cast<std::uint8_t>(abc::Opcode::kCREATEEMPTYARRAY_IMM8), 0,
          static_cast<std::uint8_t>(abc::Opcode::kSTA_V8), 0,
          static_cast<std::uint8_t>(abc::Opcode::kLDA_V8), 1,
          static_cast<std::uint8_t>(abc::Opcode::kSUPERCALLSPREAD_IMM8_V8), 0, 0,
          static_cast<std::uint8_t>(abc::Opcode::kRETURN)};
      bytes[kConstructorCode - 4] = 1;
    }
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
  index.regions.push_back({400, 1000, {1, kTypes}, {static_cast<std::uint32_t>((class_binding || mode == FixtureCase::kClassNamedShadow) ? 9 : named_cell ? 8 : mixed ? 7 : string_default ? 6 : guards.size() + family + global), kPool}, {}, {}});
  abc::RecordInfo record; record.offset = {kRecord}; record.field_count = 1; record.method_count = mixed ? 3 : family ? 2 : 1;
  index.fields.push_back({{700}, record.offset, {abc::kDynamicHeaderSize}, {}, 0,
      abc::FieldValue{abc::FieldValueEncoding::kSignedLeb, kModule}});
  abc::MethodInfo entry; entry.offset = {kMethod}; entry.owner_offset = record.offset;
  entry.name = "func_main_0"; entry.name_utf16_length = entry.name.size();
  entry.code = abc::CodeInfo{{kCode - 4}, {kCode}, mixed ? 1U : 0U, 3, static_cast<std::uint32_t>(body.size()), cleanup_entry ? 1U : 0U};
  index.methods.push_back(entry);
  if (family) {
    auto child = entry; child.offset = {kChild}; child.name = direct == DirectDefault::kFunction ? "default" : "reader"; child.name_utf16_length = child.name.size();
    constexpr unsigned kFunctionKindShift = 8; // Pinned dynamic method access flags.
    child.access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
    child.code = abc::CodeInfo{{kChildCode - 4}, {kChildCode}, child_parameter ? 1U : 0U, child_parameter ? 4U : 3U, static_cast<std::uint32_t>(child_body.size()), writer_catch ? 1U : 0U};
    index.methods.push_back(child);
  }
  if (mixed) {
    auto constructor = entry; constructor.offset = {kConstructor}; constructor.name = derived_name_conflict ? "input" : class_binding && mode != FixtureCase::kClassBindingMismatch ? "snapshot" : "Box"; constructor.name_utf16_length = constructor.name.size();
    constructor.code = abc::CodeInfo{{kConstructorCode - 4}, {kConstructorCode}, derived_declaration ? 1U : 0U, 3, static_cast<std::uint32_t>(constructor_body.size()), 0};
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
  if (derived_declaration) identity.contract = source::SourceContract::kLanguageConstructorsV1;
  if (mode == FixtureCase::kPublishedCaptureV1) identity.contract = source::SourceContract::kLanguageAndHostV1;
  if (contract) identity.contract = *contract;
  if (inspect_prefix) {
    const auto loaded = source::ReadFunctionValues(*file,header,index,{0},identity,
        source::InputCaptureMode::kConditional,{},&context);
    Check(std::holds_alternative<source::FileFunction>(loaded));
    const auto& locals = std::get<source::FileFunction>(loaded).input.Proof().locals;
    Check(std::none_of(locals.unexposed_initializations.begin(),locals.unexposed_initializations.end(),
        [](const auto& value) { return value.has_value(); }));
    const auto count = std::count_if(locals.module_prefix_initializations.begin(),locals.module_prefix_initializations.end(),
        [](const auto& value) { return value.has_value(); });
    Check(count == (mode != FixtureCase::kReentrantCapture ? 1 : 0));
    const auto& function = std::get<source::FileFunction>(loaded);
    const auto proof = source::ModuleCapturePrefix::Build(*file,header,index,lookup,index.methods[0],context,function.input,{});
    Check(std::holds_alternative<std::optional<source::ModuleCapturePrefix>>(proof));
    const auto& prefix = std::get<std::optional<source::ModuleCapturePrefix>>(proof);
    const auto has_proof = prefix && std::any_of(prefix->For(function.input).begin(),prefix->For(function.input).end(),
        [](const auto& value){ return value.has_value(); });
    Check(has_proof == (mode == FixtureCase::kPublishedCapture));
    if (has_proof) {
      const auto other = source::ReadFunctionValues(*file,header,index,{0},identity,source::InputCaptureMode::kConditional,{},&context);
      Check(prefix->For(std::get<source::FileFunction>(other).input).empty());
      analysis::SemanticBudget limited; limited.work = 0;
      Check(std::get<source::ModulePrefixError>(source::ModuleCapturePrefix::Build(
          *file,header,index,lookup,index.methods[0],context,function.input,limited)) == source::ModulePrefixError::kWorkLimit);
    }
    Check(unlink(path) == 0); return {};
  }
  if (module_writer) {
    auto discovered = source::ReadFunctionFamily(*file, header, index, lookup, roles, creations,
        {0}, identity, {}, &context);
    Check(std::holds_alternative<source::FileFamily>(discovered));
    auto& family_input = std::get<source::FileFamily>(discovered);
    Check(family_input.members.size() == 2);
    auto& child = family_input.members[1].function;
    if (const auto* phase = child.Phase()) {
      Check(writer_catch);
      const auto* bindings = phase->Phases().Modules();
      Check(bindings && bindings->evaluations.empty() && bindings->writes.size() == 1 &&
          bindings->aliases.size() == 1 && bindings->aliases[0].access == source::ModuleAccess::kMutable &&
          !bindings->aliases[0].local && !bindings->writes[0].binding.initialize);
    } else {
      Check(!writer_catch);
      auto& input = child.Normal();
      const auto bind = [&](const source::FunctionValues& values, analysis::SemanticBudget budget = {}) {
        return source::BindModuleAliases(*file, header, index, input.input, values, context,
            {kRecord}, {}, budget, source::ModuleAccess::kMutable);
      };
      auto bound = bind(input.values);
      if (mode == FixtureCase::kModuleWriterEarly) {
        Check(std::get<source::ModuleAliasError>(std::get<source::ModuleAliasFailure>(bound).cause) ==
            source::ModuleAliasError::kInitialization);
      } else {
        Check(std::holds_alternative<source::ModuleAliases>(bound));
        const auto& bindings = std::get<source::ModuleAliases>(bound);
        Check(bindings.evaluations.empty() && bindings.writes.size() == 1 && bindings.aliases.size() == 1);
        const auto& write = bindings.writes[0];
        Check(write.binding.slot == analysis::ModuleSlot{1} && !write.binding.initialize &&
            write.binding.target.symbol.value == 5 && bindings.aliases[0].target == write.binding.target &&
            bindings.aliases[0].access == source::ModuleAccess::kMutable && !bindings.aliases[0].local);
        Check(bindings.names[0] == u"snapshot");
        auto& operation = std::get<analysis::ValueModuleWrite>(input.values.graph.ops[write.operation.value].operation);
        operation.slot = {0};
        Check(std::get<source::ModuleAliasError>(std::get<source::ModuleAliasFailure>(bind(input.values)).cause) ==
            source::ModuleAliasError::kCoverage);
        operation.slot = write.binding.slot;
        analysis::SemanticBudget limited; limited.work = bindings.work;
        Check(std::holds_alternative<source::ModuleAliases>(bind(input.values, limited)));
        --limited.work;
        Check(std::holds_alternative<source::ModuleAliasFailure>(bind(input.values, limited)));
      }
    }
  }
  const auto build = [&](source::RecordProgramBudget budget = {}) {
    return source::BuildRecordSource(*file, header, index, lookup, record, context, roles,
        budget, identity, (family || cleanup_entry) ? &creations : nullptr, mixed ? &classes : nullptr);
  };
  auto result = build();
  if (mode == FixtureCase::kCleanupOverwrite || mode == FixtureCase::kModuleWriterEarly) {
    Check(!std::holds_alternative<source::PrintedSource>(result));
    Check(unlink(path) == 0); return {};
  }
  if (mode == FixtureCase::kReentrantCapture || mode == FixtureCase::kPublishedCaptureV1) {
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
  Check(std::get<source::PrintedSource>(result).contract == identity.contract);
  if (cleanup_entry) {
    const auto& sites = std::get<source::PrintedSource>(result).environment_cleanup;
    Check(sites.size() == 2 && sites[0].pc.value == cleanup_handler &&
        sites[0].kind == source::CleanupInstructionKind::kPop && sites[0].allocation == abc::CodeOffset{0});
    Check(sites[1].kind == source::CleanupInstructionKind::kRethrow && !sites[1].allocation);
  }
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
  if (mode == "--contracts") {
    unsigned cases = 0;
    for (auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (auto fixture : {FixtureCase::kValid, FixtureCase::kFunction, FixtureCase::kClassFunction, FixtureCase::kCapture}) {
        const auto baseline = Recover(fixture, profile);
        for (auto numeric : {source::kSourceContract, source::SourceContract::kLanguageArithmeticV1, source::SourceContract::kLanguageNumericV1})
          for (bool constructors : {false, true}) {
            const auto contract = constructors ? source::WithLanguageConstructors(numeric) : numeric;
            Check(Recover(fixture, profile, false, DirectDefault::kAbsent, ClassDeclarationCase::kAbsent, 0, false, contract) == baseline);
            ++cases;
          }
      }
    std::cout << cases << " typed record contracts; neutral source unchanged\n";
    return 0;
  }

  if (mode == "--class-declaration" || mode == "--class-declaration-outer-read" || mode == "--class-declaration-derived" || mode == "--class-derived-name-conflict") {
    const auto profile = argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
        argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
    const auto copies = argc > 3 ? static_cast<unsigned>(std::stoul(argv[3])) : 0;
    Check(copies <= 3);
    std::cout << Recover(FixtureCase::kClassBinding, profile, false, DirectDefault::kAbsent,
        mode == "--class-declaration" ? ClassDeclarationCase::kSafe :
            mode == "--class-declaration-derived" ? ClassDeclarationCase::kDerived :
            mode == "--class-derived-name-conflict" ? ClassDeclarationCase::kDerivedNameConflict : ClassDeclarationCase::kOuterRead,
        copies, argc > 4 && std::string_view(argv[4]) == "split");
    return 0;
  }
  if (mode == "--direct-default-function" || mode == "--direct-default-class") {
    const auto profile = argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
        argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
    std::cout << Recover(FixtureCase::kAnonymous,profile,false,
        mode == "--direct-default-class" ? DirectDefault::kClass : DirectDefault::kFunction);
    return 0;
  }
  const bool anonymous = mode.starts_with("--anonymous");
  if (mode == "--class-binding" || mode == "--class-binding-mismatch" || mode == "--class-binding-assignment") {
    const auto fixture = mode == "--class-binding" ? FixtureCase::kClassBinding : mode == "--class-binding-assignment" ? FixtureCase::kClassBindingAssignment : FixtureCase::kClassBindingMismatch;
    const auto profile = argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
        argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
    const auto generated = Recover(fixture, profile);
    Check(generated.find(mode == "--class-binding" ? "let snapshot = class {" : mode == "--class-binding-assignment" ? "(snapshot = class {" : "let snapshot = (class Box {") != std::string::npos);
    std::cout << generated; return 0;
  }
  if (mode == "--module-writer" || mode == "--module-writer-branch" || mode == "--module-writer-early" || mode == "--module-writer-literal" || mode == "--module-writer-catch" || mode == "--module-writer-parent-branch" || mode == "--module-writer-parent-branch-catch" || mode == "--module-writer-parent-branch-raw" || mode == "--module-writer-parent-branch-raw-catch") {
    const auto fixture = mode == "--module-writer" ? FixtureCase::kModuleWriter : mode == "--module-writer-branch" ? FixtureCase::kModuleWriterBranch : mode == "--module-writer-literal" ? FixtureCase::kModuleWriterLiteral : mode == "--module-writer-catch" ? FixtureCase::kModuleWriterCatch : mode == "--module-writer-parent-branch" ? FixtureCase::kModuleWriterParentBranch : mode == "--module-writer-parent-branch-catch" ? FixtureCase::kModuleWriterParentBranchCatch : mode == "--module-writer-parent-branch-raw" ? FixtureCase::kModuleWriterParentBranchRaw : mode == "--module-writer-parent-branch-raw-catch" ? FixtureCase::kModuleWriterParentBranchRawCatch : FixtureCase::kModuleWriterEarly;
    const auto profile = argc > 2 && std::string_view(argv[2]) == "1202" ? abc::ProfileId::kDynamic1202 :
        argc > 2 && std::string_view(argv[2]) == "1301" ? abc::ProfileId::kDynamic1301 : abc::ProfileId::kDynamic1206;
    const auto generated = Recover(fixture, profile);
    if (fixture != FixtureCase::kModuleWriterEarly) Check(generated.find(fixture == FixtureCase::kModuleWriterLiteral ? "snapshot = 7" : "snapshot = $arg0") != std::string::npos);
    std::cout << generated; return 0;
  }
  if (mode == "--publication-window") {
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
      Recover(FixtureCase::kPublishedCapture,profile,true);
      Recover(FixtureCase::kReentrantCapture,profile,true);
      Recover(FixtureCase::kPublishedCaptureV1,profile,true);
      Recover(FixtureCase::kPublishedBadSlot,profile,true);
    }
    return 0;
  }
  if (mode == "--cleanup-entry") {
    Recover(FixtureCase::kCleanupOverwrite);
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1301})
      Recover(FixtureCase::kCleanupEntry,profile);
    std::cout << Recover(FixtureCase::kCleanupEntry); return 0;
  }
  if (mode == "--string-default") {
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1301})
      Recover(FixtureCase::kStringDefault,profile);
    std::cout << Recover(FixtureCase::kStringDefault); return 0;
  }
  if (mode == "--captured-default" || mode == "--unused-captured-default") {
    const auto fixture = mode == "--captured-default" ? FixtureCase::kCapturedDefault : FixtureCase::kCapturedUnusedDefault;
    for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1301})
      Recover(fixture,profile);
    std::cout << Recover(fixture); return 0;
  }
  if (mode == "--published-capture") {
    Recover(FixtureCase::kReentrantCapture); Recover(FixtureCase::kPublishedCaptureV1);
    std::cout << Recover(FixtureCase::kPublishedCapture); return 0;
  }
  if (mode == "--class-cell") { std::cout << Recover(FixtureCase::kClassCell); return 0; }
  if (mode == "--class-shadow-cell") {
    const auto source = Recover(FixtureCase::kClassNamedShadow);
    Check(source.find("let capturedValue") == std::string::npos);
    std::cout << source; return 0;
  }
  if (mode == "--class-named-cell") {
    const auto source = Recover(FixtureCase::kClassNamedCell);
    std::cout << source << std::flush;
    Check(source.find("let capturedValue = input;") != std::string::npos);
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
