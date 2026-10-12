#include "../../../src/source/class_tree.h"
#include "class_names_checks.h"
#include "../../../src/source/tree_constants.h"
#include "../../../src/source/family_print.h"
#include "../../../src/source/family_cells.h"
#include "../../../src/source/tree_structure.h"
#include "../../../src/source/tree_semantics.h"
#include "../../../src/source/class_self_order.h"
#include "abcd/source/global_reference.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/analysis/exception_locals.h"
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <source_location>
#include <nlohmann/json.hpp>
namespace {
using namespace abcd;
using namespace abcd::source;
void Check(bool ok, const std::source_location& at = std::source_location::current()) {
  if (!ok) { std::cerr << "accessor tree check " << at.line() << '\n'; std::abort(); }
}
enum class ObserverFlow { kSameBlock, kSplit, kSelection, kInstallSplit, kInstallDiamond, kReverseInstall, kInitializerSplit, kInitializerDiamond, kInitializerReverse };
enum class FieldBody { kSameBlock, kSplit, kReverse, kReverseString, kCompoundSplit, kCompoundReverse };
enum class PrototypeRead { kNone, kNamed, kComputed };
std::string Recover(bool is_static, bool return_prototype = false, int ordinary_parity = -1,
    bool async_method = false, abc::ProfileId profile = abc::ProfileId::kDynamic1206,
    bool capture_cells = false, bool initialized = true, bool input_gap = false,
    ObserverFlow observers = ObserverFlow::kSameBlock, bool self_initializer = false, bool frame_this = false,
    FieldBody field_body = FieldBody::kSameBlock, PrototypeRead prototype_read = PrototypeRead::kNone, bool name_checks = false) {
  constexpr std::uint32_t kPool = 64, kOwner = 128, kLiteral = 160,
      kCreator = 256, kConstructor = 300, kGetter = 340, kSetter = 380,
      kBufferMethod = 420, kBufferCode = 864, kNestedMethod = 460, kNestedCode = 1000,
      kCreatorCode = 512, kConstructorCode = 768, kGetterCode = 800, kSetterCode = 832,
      kKey = 900, kPrototype = 920, kField = 944, kCaptureKey = 968;
  constexpr std::uint32_t kOtherField = 1150, kInitializerCode = 1200;
  std::vector<std::uint8_t> bytes(1600);
  const auto word = [&](std::size_t at, std::uint32_t n) {
    for (unsigned i = 0; i < sizeof(n); ++i) bytes[at + i] = static_cast<std::uint8_t>(n >> (8 * i));
  };
  const std::uint32_t pool[] = {kConstructor, kLiteral, kGetter, kSetter, kKey, kPrototype, kField, kBufferMethod, kNestedMethod, kOtherField};
  for (std::size_t i = 0; i < std::size(pool); ++i) word(kPool + i * sizeof(std::uint32_t), pool[i]);
  const auto text = [&](std::size_t at, std::string_view value) {
    bytes[at++] = static_cast<std::uint8_t>(value.size() * 2 + 1);
    std::copy(value.begin(), value.end(), bytes.begin() + at);
  };
  text(kKey, "value"); text(kPrototype, "prototype"); text(kField, "_value");
  text(kCaptureKey, "shared");
  text(kOtherField, "_other");
  word(kLiteral, 2); bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger);
  word(kLiteral + 5, 0); // Empty class buffer, prototype split zero.
  if (capture_cells) {
    word(kLiteral, 8);
    bytes[kLiteral + 4] = static_cast<std::uint8_t>(abc::LiteralTag::kString); word(kLiteral + 5, kCaptureKey);
    bytes[kLiteral + 9] = static_cast<std::uint8_t>(abc::LiteralTag::kMethod); word(kLiteral + 10, kBufferMethod);
    bytes[kLiteral + 14] = static_cast<std::uint8_t>(abc::LiteralTag::kMethodAffiliate);
    bytes[kLiteral + 15] = 0; bytes[kLiteral + 16] = 0;
    bytes[kLiteral + 17] = static_cast<std::uint8_t>(abc::LiteralTag::kInteger); word(kLiteral + 18, 1);
  }
  std::vector<std::uint8_t> code;
  const auto op = [&](abc::Opcode value) { code.push_back(static_cast<std::uint8_t>(value)); };
  const auto load = [&](std::uint8_t r) { op(abc::Opcode::kLDA_V8); code.push_back(r); };
  const auto save = [&](std::uint8_t r) { op(abc::Opcode::kSTA_V8); code.push_back(r); };
  const bool initializer = observers == ObserverFlow::kInitializerSplit || observers == ObserverFlow::kInitializerDiamond || observers == ObserverFlow::kInitializerReverse;
  const auto corridor = [&] {
    if (observers == ObserverFlow::kInitializerSplit || observers == ObserverFlow::kInitializerReverse) { op(abc::Opcode::kJMP_IMM8); code.push_back(2); }
    else {
      load(9); op(abc::Opcode::kJEQZ_IMM8); code.push_back(5);
      op(abc::Opcode::kNOP); op(abc::Opcode::kJMP_IMM8); code.push_back(3); op(abc::Opcode::kNOP);
    }
  };
  if (capture_cells) {
    op(abc::Opcode::kNEWLEXENV_IMM8); code.push_back(1);
    if (initialized) {
      op(abc::Opcode::kLDAI_IMM32); code.insert(code.end(), {42, 0, 0, 0});
      op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); code.push_back(0);
    }
  }
  if (self_initializer) { op(abc::Opcode::kNEWLEXENV_IMM8); code.push_back(1); corridor(); }
  op(abc::Opcode::kLDHOLE); save(0);
  op(abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8);
  code.insert(code.end(), {0, 0, 0, 1, 0, 0, 0, 0}); save(1);
  if (self_initializer) { corridor(); load(1); op(abc::Opcode::kSTLEXVAR_IMM4_IMM4); code.push_back(0); }
  if (!is_static) { op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); code.insert(code.end(), {0, 5, 0}); }
  save(2);
  if (observers == ObserverFlow::kInitializerReverse) {
    const auto jump = code.size(); op(abc::Opcode::kJMP_IMM8); code.push_back(0);
    const auto call = code.size(); load(4); op(abc::Opcode::kCALLTHIS0_IMM8_V8); code.insert(code.end(), {0, 1});
    const auto end_jump = code.size(); op(abc::Opcode::kJMP_IMM8); code.push_back(0);
    const auto method = code.size(); load(1); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8);
    code.insert(code.end(), {0, 2, 0, 0}); save(4);
    const auto backward = code.size(); op(abc::Opcode::kJMP_IMM8);
    code.push_back(static_cast<std::uint8_t>(static_cast<std::ptrdiff_t>(call) - static_cast<std::ptrdiff_t>(backward)));
    code[jump + 1] = method - jump; code[end_jump + 1] = code.size() - end_jump;
  } else if (initializer) {
    corridor(); load(1); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8);
    code.insert(code.end(), {0, 2, 0, 0}); save(4);
    corridor(); load(4); op(abc::Opcode::kCALLTHIS0_IMM8_V8); code.insert(code.end(), {0, 1});
  }
  const auto member_start = code.size();
  std::array<std::vector<std::uint8_t>, 4> packets;
  // Repeated definitions test ordering and preserving the opposite descriptor
  // half. Every original method occurrence must still have a prepared body.
  for (unsigned i = 0; i < (initializer ? 0U : 4U); ++i) {
    const auto packet_start = code.size();
    const bool setter = i % 2;
    op(abc::Opcode::kLDA_STR_ID16); code.insert(code.end(), {4, 0}); save(3);
    if (observers == ObserverFlow::kInstallDiamond) {
      load(9); op(abc::Opcode::kJEQZ_IMM8); code.push_back(5);
      op(abc::Opcode::kNOP); op(abc::Opcode::kJMP_IMM8); code.push_back(3); op(abc::Opcode::kNOP);
    }
    load(2); op(abc::Opcode::kDEFINEMETHOD_IMM8_ID16_IMM8);
    code.insert(code.end(), {0, static_cast<std::uint8_t>(setter ? 3 : 2), 0, static_cast<std::uint8_t>(setter)});
    save(setter ? 5 : 4);
    if (observers == ObserverFlow::kInstallSplit) { op(abc::Opcode::kJMP_IMM8); code.push_back(2); }
    if (static_cast<int>(i % 2) == ordinary_parity) {
      op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 4, 0, 2});
    } else {
      op(abc::Opcode::kLDUNDEFINED); save(setter ? 4 : 5);
      op(abc::Opcode::kLDFALSE); op(abc::Opcode::kDEFINEGETTERSETTERBYVALUE_V8_V8_V8_V8);
      code.insert(code.end(), {2, 3, 4, 5});
    }
    packets[i].assign(code.begin() + packet_start, code.end());
  }
  if (observers == ObserverFlow::kReverseInstall) {
    code.resize(member_start);
    const auto first_jump = code.size(); op(abc::Opcode::kJMP_IMM8); code.push_back(0);
    std::array<std::size_t, 4> starts, jumps;
    for (std::size_t i = packets.size(); i; --i) {
      starts[i - 1] = code.size(); code.insert(code.end(), packets[i - 1].begin(), packets[i - 1].end());
      jumps[i - 1] = code.size(); op(abc::Opcode::kJMP_IMM8); code.push_back(0);
    }
    const auto patch = [&](std::size_t jump, std::size_t target) {
      const auto delta = static_cast<std::ptrdiff_t>(target) - static_cast<std::ptrdiff_t>(jump);
      Check(delta >= INT8_MIN && delta <= INT8_MAX); code[jump + 1] = static_cast<std::uint8_t>(delta);
    };
    patch(first_jump, starts[0]);
    for (std::size_t i = 0; i < jumps.size(); ++i) patch(jumps[i], i + 1 < starts.size() ? starts[i + 1] : code.size());
  }
  if (observers == ObserverFlow::kSplit) { op(abc::Opcode::kJMP_IMM8); code.push_back(2); }
  if (prototype_read != PrototypeRead::kNone) {
    load(1);
    if (prototype_read == PrototypeRead::kNamed) {
      op(abc::Opcode::kLDOBJBYNAME_IMM8_ID16); code.insert(code.end(), {0, 5, 0});
    } else {
      save(0); op(abc::Opcode::kLDA_STR_ID16); code.insert(code.end(), {5, 0});
      op(abc::Opcode::kLDOBJBYVALUE_IMM8_V8); code.insert(code.end(), {0, 0});
    }
  }
  // A distinct ordinary object literal in the same graph must still be
  // planned; class method installations must not be claimed as its fields.
  if (!initializer) {
    op(abc::Opcode::kCREATEEMPTYOBJECT); save(0); load(3);
    op(abc::Opcode::kSTOWNBYNAME_IMM8_ID16_V8); code.insert(code.end(), {0, 6, 0, 0});
  }
  if (observers == ObserverFlow::kSelection) {
    // The class/prototype is selected on incoming edges after all installs.
    // Physical argument 9 follows six locals and three implicit ABI arguments.
    load(9); op(abc::Opcode::kJEQZ_IMM8); code.push_back(8);
    load(1); save(0); op(abc::Opcode::kJMP_IMM8); code.push_back(6);
    load(2); save(0); load(0); op(abc::Opcode::kRETURN);
  } else {
    if (self_initializer) { op(abc::Opcode::kLDLEXVAR_IMM4_IMM4); code.push_back(0); }
    else load(return_prototype ? 2 : 1);
    op(abc::Opcode::kRETURN);
  }
  const auto body = [&](std::uint32_t at, std::uint8_t locals, std::uint8_t args, std::vector<std::uint8_t> data,
      std::uint8_t tries = 0, std::uint8_t try_bytes = 0) {
    constexpr std::uint8_t kPayload = 0x7f, kContinuation = 0x80;
    constexpr unsigned kPayloadBits = 7;
    auto cursor = at;
    const auto uleb = [&](std::uint32_t value) {
      do {
        const auto part = static_cast<std::uint8_t>(value & kPayload); value >>= kPayloadBits;
        bytes[cursor++] = part | (value ? kContinuation : 0);
      } while (value);
    };
    const auto size = static_cast<std::uint32_t>(data.size() - try_bytes);
    uleb(locals); uleb(args); uleb(size); uleb(tries);
    std::copy(data.begin(), data.end(), bytes.begin() + cursor);
    return abc::CodeInfo{{at}, {cursor}, locals, args, size, tries};
  };
  const auto opcode = [](abc::Opcode value) { return static_cast<std::uint8_t>(value); };
  abc::MetadataIndex index; index.regions.push_back({kCreator, kCreatorCode, {}, {std::size(pool), kPool}, {}, {}});
  index.methods.resize(capture_cells ? 6 : 4);
  const std::uint32_t offsets[] = {kCreator, kConstructor, kGetter, kSetter, kBufferMethod, kNestedMethod};
  const std::string names[] = {"factory", "Accessors", "value", "value", "value", "nested"};
  for (std::size_t i = 0; i < index.methods.size(); ++i) {
    index.methods[i].offset = {offsets[i]}; index.methods[i].owner_offset = {kOwner};
    index.methods[i].name = names[i]; index.methods[i].name_utf16_length = names[i].size();
  }
  const bool parameter = observers == ObserverFlow::kSelection || observers == ObserverFlow::kInstallDiamond || observers == ObserverFlow::kInitializerDiamond;
  index.methods[0].code = body(kCreatorCode, 6, parameter ? 4 : 3, code);
  index.methods[1].code = body(kConstructorCode, 0, 3, {opcode(abc::Opcode::kRETURNUNDEFINED)});
  if (self_initializer) index.methods[1].code = body(kConstructorCode, 0, 3,
      {opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, opcode(abc::Opcode::kRETURN)});
  index.methods[2].code = body(kGetterCode, 0, 3, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kLDOBJBYNAME_IMM8_ID16), 0, 6, 0, opcode(abc::Opcode::kRETURN)});
  if (initializer) {
    std::vector<std::uint8_t> fields;
    if (frame_this) fields.push_back(opcode(abc::Opcode::kLDTHIS));
    else fields.insert(fields.end(), {opcode(abc::Opcode::kLDA_V8), 3});
    // Private register copies pass through the common SSA canonicalizer.
    fields.insert(fields.end(), {opcode(abc::Opcode::kSTA_V8), 0,
      opcode(abc::Opcode::kLDA_V8), 0, opcode(abc::Opcode::kSTA_V8), 0,
      opcode(abc::Opcode::kLDAI_IMM32), 42, 0, 0, 0,
      opcode(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8), 0, 6, 0, 0,
      opcode(abc::Opcode::kRETURNUNDEFINED)});
    index.methods[2].code = body(kGetterCode, 1, 3, std::move(fields));
    if (field_body != FieldBody::kSameBlock) {
      const bool string_value = field_body == FieldBody::kReverseString;
      const bool compound = field_body == FieldBody::kCompoundSplit || field_body == FieldBody::kCompoundReverse;
      const auto packet = [&](std::uint8_t key, std::uint8_t value) {
        std::vector<std::uint8_t> data;
        if (frame_this) data.push_back(opcode(abc::Opcode::kLDTHIS));
        else data.insert(data.end(), {opcode(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(string_value || compound ? 4 : 3)});
        data.insert(data.end(), {opcode(abc::Opcode::kSTA_V8), 0,
          opcode(abc::Opcode::kLDA_V8), 0, opcode(abc::Opcode::kSTA_V8), 0,
          opcode(abc::Opcode::kLDAI_IMM32), value, 0, 0, 0,
          opcode(abc::Opcode::kDEFINEFIELDBYNAME_IMM8_ID16_V8), 0, key, 0, 0});
        return data;
      };
      const auto first = packet(9, 17);
      auto second = packet(6, 42);
      if (compound) {
        const auto immediate = std::find(second.begin(), second.end(), opcode(abc::Opcode::kLDAI_IMM32));
        Check(immediate != second.end());
        const auto at = static_cast<std::size_t>(immediate-second.begin());
        second.erase(second.begin()+at, second.begin()+at+5);
        // !(typeof "value" === typeof "_value"), with independent expression
        // composition and a saved operand; the oracle sees only generated JS.
        second.insert(second.begin()+at, {opcode(abc::Opcode::kLDA_STR_ID16), 4, 0,
          opcode(abc::Opcode::kTYPEOF_IMM8), 0, opcode(abc::Opcode::kSTA_V8), 1,
          opcode(abc::Opcode::kLDA_STR_ID16), 6, 0, opcode(abc::Opcode::kTYPEOF_IMM8), 0,
          opcode(abc::Opcode::kSTRICTEQ_IMM8_V8), 0, 1, opcode(abc::Opcode::kISFALSE)});
      }
      if (string_value) {
        const auto immediate = std::find(second.begin(), second.end(), opcode(abc::Opcode::kLDAI_IMM32));
        Check(immediate != second.end());
        const auto at = static_cast<std::size_t>(immediate-second.begin());
        second.erase(second.begin()+at, second.begin()+at+5);
        second.insert(second.begin()+at, {opcode(abc::Opcode::kLDA_V8), 1});
      }
      std::vector<std::uint8_t> data;
      if (field_body == FieldBody::kSplit || field_body == FieldBody::kCompoundSplit) {
        data = first; data.insert(data.end(), {opcode(abc::Opcode::kJMP_IMM8), 2});
        data.insert(data.end(), second.begin(), second.end());
      } else {
        data = {opcode(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(second.size()+4)};
        const auto second_at = data.size(); data.insert(data.end(), second.begin(), second.end());
        const auto to_return = data.size(); data.insert(data.end(), {opcode(abc::Opcode::kJMP_IMM8), 0});
        data.insert(data.end(), first.begin(), first.end());
        if (string_value) data.insert(data.end(), {opcode(abc::Opcode::kLDA_STR_ID16), 4, 0,
          opcode(abc::Opcode::kSTA_V8), 1});
        const auto to_second = data.size(); data.insert(data.end(), {opcode(abc::Opcode::kJMP_IMM8),
          static_cast<std::uint8_t>(static_cast<std::ptrdiff_t>(second_at)-static_cast<std::ptrdiff_t>(to_second))});
        data[to_return+1] = static_cast<std::uint8_t>(data.size()-to_return);
      }
      data.push_back(opcode(abc::Opcode::kRETURNUNDEFINED));
      index.methods[2].code = body(kInitializerCode, string_value || compound ? 2 : 1, 3, std::move(data));
    }
  }
  index.methods[3].code = body(kSetterCode, 1, 4, {opcode(abc::Opcode::kLDTHIS),
      opcode(abc::Opcode::kSTA_V8), 0, opcode(abc::Opcode::kLDA_V8), 4,
      opcode(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 6, 0, 0, opcode(abc::Opcode::kRETURNUNDEFINED)});
  if (input_gap) index.methods[2].code = body(kGetterCode, 1, 3, {
      opcode(abc::Opcode::kLDTHIS), opcode(abc::Opcode::kSTA_V8), 0,
      opcode(abc::Opcode::kLDTHIS), opcode(abc::Opcode::kADD2_IMM8_V8), 0, 0,
      opcode(abc::Opcode::kSTA_V8), 0, opcode(abc::Opcode::kRETURN)});
  if (capture_cells) {
    const std::vector<std::uint8_t> captured{opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, opcode(abc::Opcode::kRETURN)};
    index.methods[4].name = "shared"; index.methods[4].name_utf16_length = 6;
    index.methods[1].code = body(kConstructorCode, 0, 3, captured);
    index.methods[2].code = body(kGetterCode, 0, 3, captured);
    index.methods[4].code = body(kBufferCode, 0, 3,
        {opcode(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 8, 0, 0, opcode(abc::Opcode::kRETURN)});
    index.methods[5].code = body(kNestedCode, 0, 3, {
        opcode(abc::Opcode::kLDAI_IMM32), 99, 0, 0, 0,
        opcode(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
        opcode(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, opcode(abc::Opcode::kRETURN)});
    index.methods[3].code = body(kSetterCode, 1, 4, {
        opcode(abc::Opcode::kLDA_V8), 4, opcode(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
        opcode(abc::Opcode::kLDTHIS), opcode(abc::Opcode::kSTA_V8), 0,
        opcode(abc::Opcode::kLDA_V8), 4, opcode(abc::Opcode::kSTOBJBYNAME_IMM8_ID16_V8), 0, 6, 0, 0,
        opcode(abc::Opcode::kRETURNUNDEFINED)});
    constexpr unsigned kFunctionKindShift = 8; // Pinned dynamic method access flags.
    index.methods[5].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor) << kFunctionKindShift;
  }
  if (async_method) {
    // Same private async await/resume wrapper as the class-buffer fixture,
    // now created by actual DEFINE METHOD and installed by STOWNBYNAME.
    constexpr unsigned kFunctionKindShift = 8, kByteBits = 8; // Pinned Panda attrs/encoding.
    constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
    constexpr auto kStore = static_cast<std::uint8_t>(abc::Opcode::kSTA_V8);
    constexpr auto kLoad = static_cast<std::uint8_t>(abc::Opcode::kLDA_V8);
    constexpr auto kReturn = static_cast<std::uint8_t>(abc::Opcode::kRETURN);
    constexpr auto kInteger = static_cast<std::uint8_t>(abc::Opcode::kLDAI_IMM32);
    index.methods[3].access_flags = static_cast<std::uint32_t>(abc::FunctionKind::kAsync) << kFunctionKindShift;
    index.methods[3].code = body(kSetterCode, 3, 4, {
        opcode(abc::Opcode::kASYNCFUNCTIONENTER), kStore, 0,
        kLoad, 6, opcode(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
        opcode(abc::Opcode::kSUSPENDGENERATOR_V8), 0, kLoad, 0,
        opcode(abc::Opcode::kRESUMEGENERATOR), kStore, 1, kLoad, 0,
        opcode(abc::Opcode::kGETRESUMEMODE), kStore, 2,
        kInteger, static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
        opcode(abc::Opcode::kEQ_IMM8_V8), 0, 2, opcode(abc::Opcode::kJEQZ_IMM8), 6,
        kLoad, 1, static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
        kLoad, 1, opcode(abc::Opcode::kLDTHIS), opcode(abc::Opcode::kNOP),
        opcode(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, kReturn,
        kStore, 1, opcode(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0, kReturn,
        3, 37, 1, 0, 40, 5}, 1, 6);
  }
  char path[] = "/tmp/abcd-accessor-tree-XXXXXX";
  const int fd = mkstemp(path); Check(fd >= 0);
  Check(write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size())); Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path); const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header; header.kind = abc::AbcKind::kDynamic; header.profile = profile;
  header.version = profile == abc::ProfileId::kDynamic1202 ? abc::AbcVersion{12, 0, 2, 0} :
      profile == abc::ProfileId::kDynamic1301 ? abc::AbcVersion{13, 0, 1, 0} : abc::AbcVersion{12, 0, 6, 0}; header.layout = abc::DynamicLayout{}; header.file_size = bytes.size();
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto identity = MakeRouteInputIdentity(fingerprint, header);
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
  const auto functions = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
  const auto classes = std::get<analysis::ClassMethodSites>(analysis::ClassMethodSites::Build(file, header, index, lookup));
  if (frame_this) {
    // Raw frame reads need the genuine initializer owner; the same bytes
    // loaded as an ownerless method must not acquire an initialized-this fact.
    const auto detached = ReadFunctionValues(file, header, index, {2}, identity, InputCaptureMode::kClosed);
    Check(std::holds_alternative<FunctionFileFailure>(detached));
    const auto& failure = std::get<FunctionFileFailure>(detached);
    Check(std::holds_alternative<FunctionInputFailure>(failure));
    const auto& input = std::get<FunctionInputFailure>(failure).cause;
    Check(std::holds_alternative<analysis::SemanticProofFailure>(input) &&
        std::get<analysis::SemanticProofFailure>(input).kind == analysis::SemanticProofError::kInternalValue);
  }
  auto loaded = ReadFunctionValues(file, header, index, {0}, identity,
      capture_cells || self_initializer ? InputCaptureMode::kConditional : InputCaptureMode::kClosed);
  if (const auto* failure = std::get_if<FunctionFileFailure>(&loaded)) {
    std::cerr << "self input failure " << failure->index();
    if (const auto* input = std::get_if<FunctionInputFailure>(failure)) std::cerr << " input " << input->cause.index();
    std::cerr << '\n';
  }
  Check(std::holds_alternative<FileFunction>(loaded));
  std::optional<FileFamily> self_family;
  if (self_initializer) self_family.emplace(std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup,
      roles, functions, {0}, identity, {}, nullptr, nullptr, {}, FamilyClassMode::kRetainRoot)));
  const auto& creator = self_family ? self_family->members[0].function.Normal() : std::get<FileFunction>(loaded);
  const auto& graph = creator.values.graph; const auto& definitions = creator.values.definitions.sites;
  if (initializer) {
    Check(definitions.empty() && creator.values.initializers.calls.size() == 1);
    const auto& call = creator.values.initializers.calls.front();
    std::optional<ClassSelfOrder> self_order;
    if (self_initializer) {
      auto child = std::get<ClassInitializerFunction>(ReadClassInitializerFunction(file, header, index, lookup,
          roles, functions, creator, call));
      const ClassInitializerWitness witness{call, child};
      self_order = std::get<ClassSelfOrder>(ClassSelfOrder::Build(creator, {&witness, 1}));
      const auto prefixes = self_order->Prefixes(creator.input, graph);
      const analysis::InstructionId site{graph.origins[graph.ops[call.creation.value].origin.value].site.value};
      Check(prefixes.size() == 1 && prefixes[0] && prefixes[0]->capture == site);
      const auto rebase = [&](auto facts, auto domain, auto at) {
        return ScopeFacts(creator.input.Input(), creator.input.Proof(), facts).Rebase(at, {{0}, {0}}, domain);
      };
      Check(std::holds_alternative<CaptureCell>(rebase(prefixes, analysis::CaptureDomain::kLanguage, site)));
      Check(std::get<ScopeError>(rebase(prefixes, analysis::CaptureDomain::kDefined, site)) == ScopeError::kInitialization);
      auto wrong = std::vector(prefixes.begin(), prefixes.end()); wrong[0]->capture = analysis::InstructionId{site.value + 1};
      Check(std::get<ScopeError>(rebase(wrong, analysis::CaptureDomain::kLanguage, site)) == ScopeError::kInitialization);
    }
    const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject,
        {}, {}, {}, creator.values.initializers.calls, self_initializer ? analysis::ClosureWrites::kLanguage : analysis::ClosureWrites::kUnknown,
        {}, {}, {}, self_order ? &*self_order : nullptr));
    GlobalScope globals{};
    auto layout = std::make_shared<ClassTree>(std::get<ClassTree>(BuildClassLayout(file, header, index,
        lookup, roles, classes, creator, call.creation, identity, {}, &call)));
    auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, {kCreator}, graph));
    constants.operations[call.creation.value] = layout;
    FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {kCreator}; syntax.owner = {0};
    syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
    syntax.parameters.resize(creator.frame.parameters); syntax.names = constants.names; syntax.texts = constants.texts;
    const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
    auto source = std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View()));
    const auto parent_flow = std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, source.tree));
    const CaptureTree parent_capture{source.tree, parent_flow};
    auto prepared = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
        call.creation, identity, &globals, {}, &call, {}, RecoveryMode::kCompleteOnly, {37}, self_initializer ? &parent_capture : nullptr);
    if (const auto* failure = std::get_if<ClassTreeFailure>(&prepared)) {
      std::cerr << "initializer tree failure " << failure->index();
      if (const auto* error = std::get_if<EvaluationError>(failure)) std::cerr << " evaluation " << static_cast<unsigned>(*error);
      std::cerr << '\n';
    }
    Check(std::holds_alternative<ClassTree>(prepared)); *layout = std::get<ClassTree>(std::move(prepared));
    Check(layout->BodiesPrepared());
    // The class layout gained prepared bodies after initial construction.
    // Reverify that final representation, then print with its actual raw facts.
    const auto final_semantics = std::get<TreeSemantics>(VerifyTreeSemantics(
        graph, control, evaluation, source.tree, constants.View()));
    Check(final_semantics.flow.original != nullptr);
    std::ostringstream output;
    const auto printed = PrintFunctionTree(source.tree, output, {}, {}, &globals, final_semantics.flow.original.get());
    if (const auto* text = std::get_if<PrintedFunction>(&printed); text && text->incomplete)
      std::cerr << output.str() << '\n';
    if (const auto* error = std::get_if<TreePrintError>(&printed))
      std::cerr << "field print error " << static_cast<unsigned>(*error) << " after " << output.str() << '\n';
    Check(std::holds_alternative<PrintedFunction>(printed) && !std::get<PrintedFunction>(printed).incomplete);
    if (self_initializer) {
      const auto& flow = final_semantics.flow;
      Check(self_family->members.size() == 1);
      ConstructedFamily probe{{{source.tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}}}, {0}, {std::nullopt}};
      Check(std::holds_alternative<FamilyWrites>(VerifyFamilyWrites(index, *self_family, probe)));
      const auto expect_coverage = [&] {
        const auto checked = VerifyFamilyWrites(index, *self_family, probe);
        Check(std::holds_alternative<FamilyWriteFailure>(checked) &&
            std::get<FamilyWriteFailure>(checked).kind == FamilyWriteError::kCoverage);
      };
      const auto initializer_site = layout->initializer_site;
      layout->initializer_site.reset(); expect_coverage(); layout->initializer_site = initializer_site;
      const auto body_method = layout->initializer->tree.method;
      ++layout->initializer->tree.method.value; expect_coverage(); layout->initializer->tree.method = body_method;
      ++layout->initializer->call.method.value; expect_coverage(); --layout->initializer->call.method.value;
      auto composed = BuildRecordFamilyTrees(file, header, index, std::move(*self_family),
          {source.tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
      if (const auto* failure = std::get_if<FamilyTreeFailure>(&composed)) {
        std::cerr << "self class family failure " << failure->cause.index();
        if (const auto* error = std::get_if<FamilyWriteFailure>(&failure->cause)) std::cerr << " writes " << static_cast<unsigned>(error->kind) << " pc " << (error->pc ? error->pc->value : UINT32_MAX);
        std::cerr << '\n';
      }
      Check(std::holds_alternative<ConstructedFamily>(composed));
      const auto& family = std::get<ConstructedFamily>(composed);
      Check(family.writes && family.writes->stores == 1);
      output.str({}); output.clear();
      const auto emitted = PrintFunctionTree(family.members[0].tree, output, {}, {}, &globals, family.members[0].flow.original.get());
      Check(std::holds_alternative<PrintedFunction>(emitted) && !std::get<PrintedFunction>(emitted).incomplete);
    }
    Check(unlink(path) == 0); return output.str();
  }
  Check(definitions.size() == 4);
  if (capture_cells) {
    const auto class_operation = definitions.front().creation;
    const auto& class_origin = graph.origins[graph.ops[class_operation.value].origin.value];
    const analysis::InstructionId site{class_origin.site.value};
    Check(site.value < creator.input.Proof().environments.before.size() &&
        creator.input.Proof().environments.before[site.value]);
    const auto environment = *creator.input.Proof().environments.before[site.value];
    std::shared_ptr<ClassTree> source_layout;
    std::optional<ConstructedFunction> source_parent;
    std::optional<TreeFlow> source_flow;
    if (initialized) {
      source_layout = std::make_shared<ClassTree>(std::get<ClassTree>(BuildClassLayout(
          file, header, index, lookup, roles, classes, creator, class_operation, identity, {}, nullptr, definitions)));
      auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, creator.input.Method(), graph));
      constants.operations[class_operation.value] = source_layout;
      FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = creator.input.Method(); syntax.owner = {0};
      syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
      syntax.names = constants.names; syntax.texts = constants.texts;
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject,
          {}, {}, {}, {}, analysis::ClosureWrites::kLanguage, definitions));
      source_parent.emplace(std::get<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation,
          std::move(syntax), constants.View())));
      source_flow.emplace(std::get<TreeFlow>(VerifyTreeFlow(graph, control, evaluation, source_parent->tree)));
    }
    const auto check = [&](const FunctionContext& owner, analysis::InstructionId at, CaptureSite occurrence) {
      auto values = ReadFunctionValues(file, header, index, owner.Position(), identity,
          InputCaptureMode::kConditional, {}, nullptr, &owner);
      Check(std::holds_alternative<FileFunction>(values));
      const auto& child = std::get<FileFunction>(values);
      std::optional<FileFamily> forwarding;
      const auto work = [&]() -> CaptureChild {
        if (child.input.Method() != abc::EntityOffset{kBufferMethod}) return {child.input, child.values};
        // Genuine nested creation drives PlanFamilyCells. The class member has
        // no lexical opcode; its incoming cell is exclusively a descendant demand.
        Check(child.input.Captures().empty() && child.input.Children().size() == 1);
        const auto nested_site = child.input.Children().front();
        auto binding = std::get<analysis::CreationBinding>(analysis::BindCreation(file, header, index, lookup,
            child.input.Method(), child.body, child.input.Proof().environments, nested_site.value));
        const auto sites = functions.Find({kNestedMethod}); Check(sites.count == 1);
        FunctionContext nested_owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
            file, header, index, lookup, roles, functions, sites.first, identity))};
        auto nested = std::get<FileFunction>(ReadFunctionValues(file, header, index, nested_owner.Position(), identity,
            InputCaptureMode::kConditional, {}, nullptr, &nested_owner));
        auto incoming = std::get<CaptureCells>(BindCaptureCells(index, {child.input, child.values},
            {nested.input, nested.values}, nested_site, binding));
        forwarding.emplace(); forwarding->members.reserve(2);
        forwarding->members.push_back({FamilyFunction{std::get<FileFunction>(std::move(values))}});
        forwarding->members.push_back({FamilyFunction{std::move(nested)}, std::move(nested_owner),
            FamilyCreation{FileFamily::kRoot, nested_site, binding, std::move(incoming)}});
        Check(std::holds_alternative<FamilyCellCost>(PlanFamilyCells(*forwarding)));
        const auto prepared = forwarding->members[0].function.Capture();
        Check(prepared.OriginalInput().Captures().empty() && prepared.Input().Captures().size() == 1 &&
            prepared.Cells()->cells.size() == 1 && prepared.Cells()->cells[0].count == 0);
        return prepared;
      }();
      auto bound = BindCaptureCells(index, {creator.input, creator.values}, work, at, occurrence);
      const auto read_family = [&](const FileFunction* parent) {
        return ReadFunctionFamily(file, header, index, lookup, roles, functions, owner.Position(),
            identity, {}, nullptr, &owner, {}, FamilyClassMode::kReject, nullptr, {37}, parent);
      };
      const auto closed = read_family(nullptr);
      Check(std::holds_alternative<FamilyFileFailure>(closed) &&
          std::get<CaptureEvaluationError>(std::get<CaptureEvaluationFailure>(std::get<FamilyFileFailure>(closed).cause)) ==
              CaptureEvaluationError::kOuterCapture);
      auto discovered = read_family(&creator);
      Check(std::holds_alternative<FamilyFileFailure>(read_family(&child)));
      if (!initialized) {
        Check(std::holds_alternative<FamilyFileFailure>(discovered) &&
            std::get<CaptureCellFailure>(std::get<FamilyFileFailure>(discovered).cause) == CaptureCellError::kInput);
      } else {
        Check(std::holds_alternative<FileFamily>(discovered));
        auto& family = std::get<FileFamily>(discovered);
        Check(family.root_capture && family.Owner(FileFamily::kRoot) == FunctionOwnerId{37});
        const auto root = family.members.front().function.Capture();
        Check(family.root_capture->targets.size() == 1 &&
            std::holds_alternative<ParentCell>(family.root_capture->targets[0].target));
        Check(std::holds_alternative<std::uint64_t>(VerifyCaptureCells(index,
            {creator.input, creator.values}, root, at, occurrence, *family.root_capture)));
        Check(std::holds_alternative<std::uint64_t>(VerifyClassRootCaptureCells(
            file, header, index, lookup, creator, root, owner, *family.root_capture)));
        auto stale = *family.root_capture; ++stale.child_revision.value;
        Check(std::get<CaptureCellFailure>(VerifyClassRootCaptureCells(
            file, header, index, lookup, creator, root, owner, stale)) == CaptureCellError::kRevision);
        auto& member = family.members.front();
        auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, owner,
            member.function.Body(), root.Graph(), family.Owner(FileFamily::kRoot)));
        const auto control = std::get<ControlPlan>(BuildControlPlan(root.Graph()));
        const auto demands = root.Cells() ? std::span<const CellDemand>(root.Cells()->demands) : std::span<const CellDemand>{};
        const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(root.Graph(), {}, IncomingCells::kConditional,
            member.children, {}, demands, {}, analysis::ClosureWrites::kLanguage));
        IncomingCaptureTree incoming{*family.root_capture, {source_parent->tree, *source_flow},
            ClassCaptureAnchor{class_operation, owner}};
        const auto view = syntax.View();
        auto constructed = BuildFunctionTree(root.Graph(), control, evaluation, std::move(syntax.syntax), view, {}, &incoming);
        Check(std::holds_alternative<ConstructedFunction>(constructed));
        const auto& tree = std::get<ConstructedFunction>(constructed).tree;
        Check(tree.captures.size() == 1 && tree.captures[0].target.owner == source_parent->tree.owner);
        const auto flow = std::get<TreeFlow>(VerifyTreeFlow(root.Graph(), control, evaluation, tree));
        Check(std::holds_alternative<std::uint64_t>(VerifyCaptureTree(*family.root_capture,
            incoming.parent, {tree, flow}, {}, &*incoming.class_anchor)));
        auto changed_tree = tree; ++changed_tree.captures[0].target.symbol.value;
        Check(std::get<CaptureTreeError>(VerifyCaptureTree(*family.root_capture,
            incoming.parent, {changed_tree, flow}, {}, &*incoming.class_anchor)) == CaptureTreeError::kTarget);
        ClassCaptureAnchor wrong{{class_operation.value + 1}, owner};
        Check(std::get<CaptureTreeError>(VerifyCaptureTree(*family.root_capture,
            incoming.parent, {tree, flow}, {}, &wrong)) == CaptureTreeError::kCreation);
        changed_tree = tree;
        const auto capture_name = tree.symbols[tree.captures[0].symbol.value].name;
        changed_tree.names[capture_name.value].append(u"Changed");
        Check(std::get<CaptureTreeError>(VerifyCaptureTree(*family.root_capture,
            incoming.parent, {changed_tree, flow}, {}, &*incoming.class_anchor)) == CaptureTreeError::kName);
        if (const auto* method = owner.ClassSite()) {
          auto& id = source_layout->member_sites[method->property]; const auto saved = id; ++id.value;
          Check(std::get<CaptureTreeError>(VerifyCaptureTree(*family.root_capture,
              incoming.parent, {tree, flow}, {}, &*incoming.class_anchor)) == CaptureTreeError::kCreation);
          id = saved;
        }
        // A bound root is not a closed standalone family. Source integration
        // must supply and verify its actual enclosing ClassExpr and symbols.
        auto draft = BuildFileFamilyTrees(file, header, index, std::move(family), owner,
            {}, nullptr, {}, &lookup);
        Check(std::holds_alternative<FamilyTreeFailure>(draft) &&
            std::get<FamilyTreeError>(std::get<FamilyTreeFailure>(draft).cause) == FamilyTreeError::kContext);
        auto externally_bound = std::get<FileFamily>(read_family(&creator));
        const ExternalFamilyCapture external{creator, {incoming.parent, class_operation}};
        auto contextual = BuildFileFamilyTrees(file, header, index, std::move(externally_bound), owner,
            {}, nullptr, {}, &lookup, RecoveryMode::kCompleteOnly, &external);
        Check(std::holds_alternative<ConstructedFamily>(contextual));
        auto& built = std::get<ConstructedFamily>(contextual);
        Check(built.root_capture && built.root_capture->targets.size() == 1 && built.writes);
        auto input_again = std::get<FileFamily>(read_family(&creator));
        const ClassRootWriteInput actual{file, header, lookup, creator, owner, external.source};
        Check(std::holds_alternative<FamilyWrites>(VerifyFamilyWrites(index, input_again, built, {}, &lookup, &actual)));
        Check(std::get<FamilyWriteFailure>(VerifyFamilyWrites(index, input_again, built, {}, &lookup)).kind == FamilyWriteError::kCapture);
        ++built.root_capture->child_revision.value;
        Check(std::get<FamilyWriteFailure>(VerifyFamilyWrites(index, input_again, built, {}, &lookup, &actual)).kind == FamilyWriteError::kCapture);
        --built.root_capture->child_revision.value;
        if (owner.ClassSite()) Check(built.writes->captured_stores == 1 && built.writes->creations == 1);
      }
      if (!initialized) {
        // kInput keeps the refused capture plan and its rebase as typed causes:
        // without the parent store the captured slot has no local cell.
        const auto& refused = std::get<CaptureCellFailure>(bound);
        const auto& input = std::get<CaptureFailure>(refused.cause);
        Check(refused == CaptureCellError::kInput && input == CaptureError::kScope &&
            std::get<ScopeError>(input.cause) == ScopeError::kCell);
        return;
      }
      Check(std::holds_alternative<CaptureCells>(bound));
      const auto& cells = std::get<CaptureCells>(bound);
      Check(cells.targets.size() == 1 && std::holds_alternative<ParentCell>(cells.targets[0].target));
      Check(std::holds_alternative<std::uint64_t>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, cells)));
      auto changed = cells; ++changed.input.pc.value;
      Check(std::get<CaptureCellFailure>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, changed)) == CaptureCellError::kInput);
      changed = cells; ++std::get<ParentCell>(changed.targets[0].target).cell.value;
      Check(std::get<CaptureCellFailure>(VerifyCaptureCells(index,
          {creator.input, creator.values}, work, at, occurrence, changed)) == CaptureCellError::kBinding);
      if (const auto* cls = std::get_if<ClassCaptureSite>(&occurrence)) {
        const ClassCaptureSite wrong{cls->owner, analysis::CapturedDepth{1}};
        Check(std::get<CaptureCellFailure>(BindCaptureCells(index,
            {creator.input, creator.values}, work, at, wrong)) == CaptureCellError::kInput);
      }
    };
    const FunctionContext constructor{std::get<ClassTreeContext>(ClassTreeContext::ResolveConstructor(
        file, header, index, lookup, roles, creator, class_operation, identity))};
    check(constructor, site, ClassCaptureSite{constructor, environment});
    // The original phase matcher must use this class instruction's Before,
    // never Commit or a neighboring instruction. This is scope-plan evidence,
    // not a claim that a whole phased class-containing body can be emitted.
    const auto phase = std::get<analysis::ExceptionFlow>(analysis::BuildExceptionFlow(
        creator.body, profile, creator.frame.registers));
    const auto phase_env = std::get<analysis::EnvironmentFlow>(analysis::AnalyzeExceptionEnvironments(phase));
    const auto phase_locals = std::get<analysis::ExceptionLocals>(analysis::ProveExceptionLocals(
        phase, phase_env, {}, {}, analysis::ClosureWrites::kLanguage));
    bool before_found = false, commit_found = false;
    const CaptureUse use{{{0}, {0}}, analysis::CaptureDomain::kLanguage};
    for (std::size_t n = 0; n < phase.nodes.size(); ++n) {
      const auto& node = phase.nodes[n];
      if (node.instruction != site) continue;
      const ScopeFacts scopes(ExceptionScopeFacts{phase, phase_env, phase_locals, {static_cast<std::uint32_t>(n)}});
      const ClassCaptureSite occurrence{constructor, environment};
      auto plan = BuildCapturePlan(scopes, site, occurrence, {&use, 1});
      if (node.phase == analysis::ExceptionPhase::kBefore) {
        before_found = true;
        if (initialized) {
          Check(std::holds_alternative<CapturePlan>(plan));
          Check(std::holds_alternative<std::uint64_t>(VerifyCapturePlan(scopes, site, occurrence,
              {&use, 1}, std::get<CapturePlan>(plan))));
        } else Check(std::get<CaptureFailure>(plan) == CaptureError::kScope);
      } else {
        commit_found |= node.phase == analysis::ExceptionPhase::kCommit;
        Check(std::get<CaptureFailure>(plan) == CaptureError::kCreation);
      }
    }
    Check(before_found && commit_found);
    const auto range = classes.Find({kBufferMethod}); Check(range.count == 1);
    const FunctionContext member{std::get<ClassTreeContext>(ClassTreeContext::Resolve(
        file, header, index, lookup, roles, classes, range.first, identity))};
    check(member, site, ClassCaptureSite{member, environment});
    const auto& definition = definitions.front();
    const FunctionContext defined{std::get<ClassTreeContext>(ClassTreeContext::ResolveDefinedMember(
        file, header, index, lookup, roles, functions, creator, definition.definition,
        abc::ClassMethodKind::kGetter, identity))};
    const auto created = *definition.Methods()[0].operation;
    const analysis::InstructionId instruction{graph.origins[graph.ops[created.value].origin.value].site.value};
    const auto binding = std::get<analysis::CreationBinding>(analysis::BindCreation(file, header, index, lookup,
        creator.input.Method(), creator.body, creator.input.Proof().environments, instruction.value));
    check(defined, instruction, binding);
    std::string captured_source;
    if (initialized) {
      GlobalScope globals{};
      const CaptureTree parent{source_parent->tree, *source_flow};
      auto complete = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
          class_operation, identity, &globals, {}, nullptr, definitions, RecoveryMode::kCompleteOnly, {37}, &parent);
      Check(std::holds_alternative<ClassTree>(complete));
      *source_layout = std::get<ClassTree>(std::move(complete));
      std::ostringstream detached;
      const auto detached_result = PrintClassTree(*source_layout, detached, {}, {}, &globals);
      Check(std::holds_alternative<TreePrintError>(detached_result) &&
          std::get<TreePrintError>(detached_result) == TreePrintError::kUnsupportedContext);
      Check(detached.str().empty());
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(source_parent->tree)));
      const auto& constructor = *source_layout->constructor;
      const auto capture = constructor.Family().members[constructor.Family().root.value].tree.captures.front().target.symbol;
      auto sibling = source_parent->tree;
      auto& body = std::get<BlockStmt>(sibling.statements[sibling.body.value].statement);
      Check(body.first_child == 0 && body.child_count == sibling.children.size());
      const ScopeId scope{static_cast<std::uint32_t>(sibling.scopes.size())};
      sibling.scopes.push_back({ScopeKind::kBlock, sibling.body_scope}); sibling.symbols[capture.value].scope = scope;
      std::vector<StmtId> outside, inside;
      for (const auto id : sibling.children) {
        const auto& statement = sibling.statements[id.value].statement;
        const auto* declaration = std::get_if<DeclarationStmt>(&statement);
        const auto* expression = std::get_if<ExpressionStmt>(&statement);
        const auto* assignment = expression ? std::get_if<AssignmentExpr>(&sibling.expressions[expression->expression.value].expression) : nullptr;
        const auto* target = assignment ? std::get_if<SymbolTarget>(&assignment->target) : nullptr;
        ((declaration && declaration->symbol == capture) || (target && target->symbol == capture) ? inside : outside).push_back(id);
      }
      Check(inside.size() == 2);
      const StmtId block{static_cast<std::uint32_t>(sibling.statements.size())}; outside.insert(outside.begin(), block);
      sibling.children = outside; sibling.children.insert(sibling.children.end(), inside.begin(), inside.end());
      body.child_count = outside.size();
      sibling.statements.push_back({BlockStmt{scope, static_cast<std::uint32_t>(outside.size()), static_cast<std::uint32_t>(inside.size())},
          sibling.statements[sibling.body.value].origin});
      const auto class_expression = *source_flow->expressions[class_operation.value];
      const auto empty_layout = std::make_shared<ClassTree>(std::get<ClassTree>(BuildClassLayout(
          file, header, index, lookup, roles, classes, creator, class_operation, identity, {}, nullptr, definitions)));
      auto& class_node = std::get<ClassExpr>(sibling.expressions[class_expression.value].expression);
      class_node.definition = empty_layout;
      Check(std::holds_alternative<TreeStructure>(VerifyTreeStructure(sibling)));
      class_node.definition = source_layout;
      Check(std::get<TreeStructureError>(VerifyTreeStructure(sibling)) == TreeStructureError::kScope);
      const ClassCaptureDestination actual{source_parent->tree, class_expression};
      const FamilyDestination destination{Strictness::kStrict, &globals, FunctionFragment::kClassMember, {}, nullptr, &actual};
      Check(std::holds_alternative<FamilyEnclosure>(constructor.CheckDestination(destination)));
      // The construction flow is not a checked final lexical destination.
      const ClassCaptureParent construction{parent, class_operation};
      auto construction_only = destination;
      construction_only.enclosure = nullptr; construction_only.captures = &construction;
      Check(std::holds_alternative<FamilyPrepareFailure>(constructor.CheckDestination(construction_only)));
      auto wrong_parent = source_parent->tree; ++wrong_parent.value_revision.value;
      const ClassCaptureDestination stale{wrong_parent, class_expression};
      auto bad_destination = destination; bad_destination.enclosure = &stale;
      Check(std::holds_alternative<FamilyPrepareFailure>(constructor.CheckDestination(bad_destination)));
      std::ostringstream output;
      auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, creator.input.Method(), graph));
      constants.operations[class_operation.value] = source_layout;
      const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
      const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(graph, {}, IncomingCells::kReject,
          {}, {}, {}, {}, analysis::ClosureWrites::kLanguage, definitions));
      const auto final_semantics = std::get<TreeSemantics>(VerifyTreeSemantics(
          graph, control, evaluation, source_parent->tree, constants.View()));
      Check(final_semantics.flow.original != nullptr);
      Check(std::holds_alternative<PrintedFunction>(PrintFunctionTree(source_parent->tree, output, {}, {}, &globals,
          final_semantics.flow.original.get())));
      captured_source = output.str();
    }
    Check(unlink(path) == 0); return captured_source;
  }
  auto constants = std::get<TreeConstants>(ResolveTreeConstants(file, header, index, {kCreator}, graph));
  std::vector<ClassPrototypeRead> prototype_reads;
  if (prototype_read != PrototypeRead::kNone) {
    prototype_reads = std::get<ClassPrototypeReads>(FindClassPrototypeReads(graph)).reads;
    std::erase_if(prototype_reads, [&](const auto& site) {
      return !HasClassPrototypeKey(site, constants.View(), constants.names, constants.texts);
    });
    Check(prototype_reads.size() == 2); // Original method home plus the new discarded read.
  }
  auto evaluated = BuildEvaluationPlan(graph, {}, IncomingCells::kReject, {}, {}, {}, {}, analysis::ClosureWrites::kUnknown, definitions, {}, {}, nullptr, prototype_reads);
  if (const auto* error = std::get_if<EvaluationError>(&evaluated)) std::cerr << "evaluation " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<EvaluationPlan>(evaluated)); const auto& evaluation = std::get<EvaluationPlan>(evaluated);
  GlobalScope globals{};
  auto layout = BuildClassLayout(file, header, index, lookup, roles, classes, creator,
      definitions.front().creation, identity, {}, nullptr, definitions);
  Check(std::holds_alternative<ClassTree>(layout));
  std::ostringstream pending;
  Check(std::get<TreePrintError>(PrintClassTree(std::get<ClassTree>(layout), pending, {}, {}, &globals)) ==
      TreePrintError::kUnsupportedContext);
  Check(pending.str().empty());
  auto definition = std::make_shared<ClassTree>(std::get<ClassTree>(std::move(layout)));
  constants.operations[definitions.front().creation.value] = definition;
  FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {kCreator}; syntax.owner = {0};
  syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(creator.frame.parameters);
  syntax.names = constants.names; syntax.texts = constants.texts;
  const auto control = std::get<ControlPlan>(BuildControlPlan(graph));
  auto tree = BuildFunctionTree(graph, control, evaluation, syntax, constants.View());
  if (const auto* error = std::get_if<FunctionTreeFailure>(&tree)) std::cerr << "tree failure " << static_cast<unsigned>(*error) << '\n';
  Check(std::holds_alternative<ConstructedFunction>(tree));
  if (!prototype_reads.empty()) {
    auto wrong = syntax;
    const auto& site = prototype_reads.back();
    if (site.key) {
      const auto text = std::get<TextId>(constants.operations[site.key->value]); wrong.texts[text.value] = u"getter";
    } else {
      const auto name = std::get<NameId>(constants.operations[site.read.value]); wrong.names[name.value] = u"getter";
    }
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, wrong, constants.View())));
    const auto original = definition->operation; definition->operation = site.read;
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
    definition->operation = original;
  }
  auto built = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
      definitions.front().creation, identity, &globals, {}, nullptr, definitions, RecoveryMode::kCompleteOnly, {37});
  if (async_method && ordinary_parity != 1) {
    const auto& failure = std::get<ClassTreeFailure>(built);
    const auto& family = std::get<FamilyPrepareFailure>(failure);
    const auto& context = std::get<ClassContextFailure>(family.cause);
    Check(std::get<ClassContextError>(context.error) == ClassContextError::kKind);
    Check(unlink(path) == 0); return {};
  }
  if (input_gap) {
    Check(std::holds_alternative<ClassTreeFailure>(built));
    built = BuildClassTree(file, header, index, lookup, roles, functions, classes, creator,
        definitions.front().creation, identity, &globals, {}, nullptr, definitions, RecoveryMode::kAllowPartial, {37});
  }
  if (const auto* error = std::get_if<ClassTreeFailure>(&built)) std::cerr << "class failure " << error->index() << '\n';
  Check(std::holds_alternative<ClassTree>(built));
  *definition = std::get<ClassTree>(std::move(built));
  Check(definition->DraftPrepared() && definition->BodiesPrepared() != input_gap);
  if (name_checks) {
    const auto context = [&] { return FunctionContext{std::get<ClassTreeContext>(ClassTreeContext::ResolveConstructor(
        file, header, index, lookup, roles, creator, definitions.front().creation, identity))}; };
    FunctionOwnerId next{37};
    CheckClassNames([&] {
      auto value = std::make_shared<ClassTree>(std::get<ClassTree>(BuildClassTree(file, header, index,
          lookup, roles, functions, classes, creator, definitions.front().creation, identity, &globals,
          {}, nullptr, definitions, RecoveryMode::kCompleteOnly, next)));
      const auto owners = [&](const PreparedFamily& family) {
        for (const auto& member : family.Family().members)
          next.value = std::max(next.value, member.tree.owner.value + 1);
      };
      owners(*value->constructor);
      for (const auto& member : value->members) if (member) owners(*member);
      for (const auto& member : value->definitions)
        for (const auto& body : member.bodies) if (body) owners(*body);
      return value;
    }, context);
  }

  if (input_gap) {
    Check(definition->definitions[0].omissions[0] && definition->definitions[2].omissions[0]);
    Check(!definition->definitions[0].bodies[0] && !definition->definitions[2].bodies[0]);
    auto gap = std::move(definition->definitions[0].omissions[0]);
    definition->definitions[0].omissions[0].reset();
    Check(!definition->DraftPrepared());
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
    definition->definitions[0].omissions[0] = std::move(gap);
    auto& omission = *definition->definitions[0].omissions[0];
    ++omission.root.value;
    Check(!definition->DraftPrepared());
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
    --omission.root.value;
    auto conflict = std::move(definition->definitions[1].bodies[1]);
    definition->definitions[1].bodies[1].reset();
    definition->definitions[0].bodies[0] = std::move(conflict);
    Check(!definition->DraftPrepared());
    Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
    definition->definitions[1].bodies[1] = std::move(definition->definitions[0].bodies[0]);
    definition->definitions[0].bodies[0].reset();
    Check(definition->DraftPrepared());
    Check(std::holds_alternative<ConstructedFunction>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
  }
  // All source occurrences reserve disjoint owners, including phase members.
  // Parent owner zero is never reused for a class root or its descendants.
  std::uint32_t next_owner = 37;
  const auto check_owners = [&](const PreparedFamily& prepared) {
    const auto& family = prepared.Family();
    Check(family.root == FileFamily::kRoot);
    for (const auto& member : family.members) Check(member.tree.owner.value == next_owner++);
  };
  check_owners(*definition->constructor);
  for (const auto& member : definition->members) { Check(member.has_value()); check_owners(*member); }
  for (const auto& site : definition->definitions)
    for (const auto& member : site.bodies) if (member) check_owners(*member);
  // A complete class cannot lose an actual definition body or its proof.
  for (auto& site : definition->definitions) {
    for (auto& body : site.bodies) {
      if (!body) continue;
      auto missing_body = std::move(body); body.reset();
      std::ostringstream rejected;
      Check(std::get<TreePrintError>(PrintClassTree(*definition, rejected, {}, {}, &globals)) ==
          TreePrintError::kUnsupportedContext);
      Check(rejected.str().empty());
      Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
      body = std::move(missing_body);
    }
  }
  // Independent operand checking must reject removal or substitution of the
  // prepared body behind an otherwise well-formed projection certificate.
  auto missing = std::move(definition->definitions.back()); definition->definitions.pop_back();
  Check(std::holds_alternative<FunctionTreeFailure>(BuildFunctionTree(graph, control, evaluation, syntax, constants.View())));
  definition->definitions.push_back(std::move(missing));
  const auto final_semantics = std::get<TreeSemantics>(VerifyTreeSemantics(
      graph, control, evaluation, std::get<ConstructedFunction>(tree).tree, constants.View()));
  Check(final_semantics.flow.original != nullptr);
  std::ostringstream output;
  auto printed = PrintFunctionTree(std::get<ConstructedFunction>(tree).tree, output, {}, {}, &globals,
      final_semantics.flow.original.get());
  Check(std::holds_alternative<PrintedFunction>(printed));
  Check(std::get<PrintedFunction>(printed).class_members.size() == (input_gap ? 3 : 5));
  Check(std::get<PrintedFunction>(printed).incomplete == input_gap);
  const auto read_family = [&] {
    return std::get<FileFamily>(ReadFunctionFamily(file, header, index, lookup, roles, functions, {0}, identity,
        {}, nullptr, nullptr, {}, FamilyClassMode::kRetainRoot));
  };
  const auto& root_tree = std::get<ConstructedFunction>(tree).tree;
  const auto& flow = final_semantics.flow;
  auto incomplete = read_family(); incomplete.members[0].classes.pop_back();
  const auto denied_family = BuildRecordFamilyTrees(file, header, index, std::move(incomplete),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  Check(std::holds_alternative<FamilyTreeFailure>(denied_family));
  auto composed = BuildRecordFamilyTrees(file, header, index, read_family(),
      {root_tree, flow, {}, {}, {}, abc::EntityOffset{kOwner}});
  if (const auto* error = std::get_if<FamilyTreeFailure>(&composed)) std::cerr << "family failure " << error->cause.index() << '\n';
  Check(std::holds_alternative<ConstructedFamily>(composed));
  std::ostringstream module;
  auto module_print = PrintModuleFamilyDraft(std::get<ConstructedFamily>(composed), module, {}, {}, &globals);
  if (parameter) {
    // This fixture is an ordinary function with a real parameter. A module
    // body destination must not discard its signature while printing classes.
    Check(std::get<TreePrintError>(module_print) == TreePrintError::kUnsupportedContext);
    Check(module.str().empty());
    Check(unlink(path) == 0); return output.str();
  }
  Check(std::holds_alternative<PrintedFamilyDraft>(module_print));
  Check(std::get<PrintedFamilyDraft>(module_print).members[0].class_members.size() == (input_gap ? 3 : 5));
  Check(std::get<PrintedFamilyDraft>(module_print).members[0].incomplete == input_gap);
  Check(unlink(path) == 0); return output.str();
}
}
int main(int argc, char** argv) {
  if (argc == 2 && std::string_view(argv[1]) == "--class-names") {
    Recover(false, false, -1, false, abc::ProfileId::kDynamic1206, false, true, false,
        ObserverFlow::kSameBlock, false, false, FieldBody::kSameBlock, PrototypeRead::kNone, true);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--closed-initializers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto outer : {ObserverFlow::kInitializerSplit, ObserverFlow::kInitializerDiamond, ObserverFlow::kInitializerReverse})
        for (const auto body : {FieldBody::kCompoundSplit, FieldBody::kCompoundReverse})
          for (const bool frame : {false, true})
            std::cout << nlohmann::json{{"self", true}, {"fields", true}, {"compound", true}, {"source", Recover(true, false, -1, false, profile,
                false, true, false, outer, true, frame, body)}} << '\n';
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--prototype-read" || std::string_view(argv[1]) == "--computed-prototype-read")) {
    const auto kind = std::string_view(argv[1]) == "--prototype-read" ? PrototypeRead::kNamed : PrototypeRead::kComputed;
    std::cout << Recover(false, false, -1, false, abc::ProfileId::kDynamic1206, false, true, false,
        ObserverFlow::kSplit, false, false, FieldBody::kSameBlock, kind);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--body-initializers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto outer : {ObserverFlow::kInitializerSplit, ObserverFlow::kInitializerDiamond, ObserverFlow::kInitializerReverse})
        for (const auto body : {FieldBody::kSplit, FieldBody::kReverse, FieldBody::kReverseString})
          for (const bool frame : {false, true})
            for (const bool self : {false, true})
              std::cout << nlohmann::json{{"self", self}, {"fields", true}, {"string_value", body == FieldBody::kReverseString}, {"source", Recover(true, false, -1, false, profile,
                  false, true, false, outer, self, frame, body)}} << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--frame-initializers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto flow : {ObserverFlow::kInitializerSplit, ObserverFlow::kInitializerDiamond, ObserverFlow::kInitializerReverse})
        for (const bool self : {false, true})
          std::cout << nlohmann::json{{"self", self}, {"source", Recover(true, false, -1, false, profile, false, true, false, flow, self, true)}} << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--self-initializers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto flow : {ObserverFlow::kInitializerSplit, ObserverFlow::kInitializerDiamond, ObserverFlow::kInitializerReverse})
        std::cout << nlohmann::json{{"source", Recover(true, false, -1, false, profile, false, true, false, flow, true)}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--initializers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto flow : {ObserverFlow::kInitializerSplit, ObserverFlow::kInitializerDiamond, ObserverFlow::kInitializerReverse})
        std::cout << nlohmann::json{{"source", Recover(true, false, -1, false, profile, false, true, false, flow)}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--observers-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool is_static : {false, true})
        for (const auto flow : {ObserverFlow::kSplit, ObserverFlow::kSelection, ObserverFlow::kInstallSplit, ObserverFlow::kInstallDiamond, ObserverFlow::kReverseInstall}) {
          std::cout << nlohmann::json{{"source", Recover(is_static, false, -1, false, profile, false, true, false, flow)},
              {"static", is_static}, {"selection", flow == ObserverFlow::kSelection}}.dump() << '\n';
          if (flow == ObserverFlow::kReverseInstall)
            std::cout << nlohmann::json{{"source", Recover(is_static, false, 1, false, profile, false, true, false, flow)},
                {"static", is_static}, {"selection", false}, {"ordinary", true}}.dump() << '\n';
        }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--partial-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool is_static : {false, true})
        std::cout << nlohmann::json{{"source", Recover(is_static, false, -1, false, profile, false, true, true)},
            {"static", is_static}, {"incomplete", true}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--capture-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool is_static : {false, true})
        std::cout << nlohmann::json{{"source", Recover(is_static, false, -1, false, profile, true)}, {"static", is_static}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--async-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const bool is_static : {false, true})
        std::cout << nlohmann::json{{"source", Recover(is_static, false, 1, true, profile)}, {"static", is_static}}.dump() << '\n';
    return 0;
  }
  if (argc == 2) { std::cout << Recover(std::string_view(argv[1]) == "--static", std::string_view(argv[1]) == "--retained",
      std::string_view(argv[1]) == "--method-last" ? 1 : std::string_view(argv[1]) == "--accessor-last" ? 0 : -1); return 0; }
  for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
    for (const bool is_static : {false, true}) {
      Recover(is_static, false, -1, false, profile, false, true, true);
      Recover(is_static, false, -1, false, profile, true);
      Recover(is_static, false, -1, false, profile, true, false);
      Recover(is_static, false, 1, true, profile);
      Recover(is_static, false, -1, true, profile); // Async setter cannot become native set syntax.
    }
  Recover(false); Recover(true); Recover(false, true); Recover(false, false, 0); Recover(false, false, 1);
}
