#include <unistd.h>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <string_view>

#include "../../../tools/family_value_inspect.h"
#include "../../../src/source/capture_cells.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/source/base_function.h"
#include "analysis/ssa_fixture.h"

namespace {
using namespace abcd;
// Real file-backed Code entities; metadata is synthetic, not a parser oracle.
constexpr std::uint32_t kPool = 64, kRecord = 128, kCaller = 256, kParent = 300,
                        kGetter = 320, kSetter = 340, kReader = 360, kWriter = 380, kCodeStart = 400;
constexpr std::uint8_t kImplicitArguments = 3;
constexpr unsigned kByteBits = 8, kFunctionKindShift = 8;
std::uint8_t Op(abc::Opcode opcode) {
  return static_cast<std::uint8_t>(opcode);
}
abc::CodeInfo AppendCode(std::vector<std::uint8_t>& bytes, std::uint8_t locals,
                     std::uint8_t parameters,
                     std::span<const std::uint8_t> code,
                     std::initializer_list<std::uint8_t> table = {}, std::uint8_t try_count = 1) {
  constexpr std::size_t kSingleByteUlebLimit = 128;
  test::Check(code.size() < kSingleByteUlebLimit);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  const auto arguments =
      static_cast<std::uint8_t>(parameters + kImplicitArguments);
  const auto tries = static_cast<std::uint8_t>(table.size() == 0 ? 0 : try_count);
  bytes.insert(bytes.end(), {locals, arguments,
                             static_cast<std::uint8_t>(code.size()), tries});
  const auto start = static_cast<std::uint32_t>(bytes.size());
  bytes.insert(bytes.end(), code.begin(), code.end());
  bytes.insert(bytes.end(), table.begin(), table.end());
  return {{offset},
          {start},
          locals,
          arguments,
          static_cast<std::uint32_t>(code.size()),
          tries};
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::uint8_t locals,
                     std::uint8_t parameters, std::initializer_list<std::uint8_t> code,
                     std::initializer_list<std::uint8_t> table = {}, std::uint8_t tries = 1) {
  return AppendCode(bytes, locals, parameters, code, table, tries);
}
enum class ParentShape { kIteration, kCatch, kNormal, kOuterCatchContinue, kOuterCatchBreak };
enum class ChildShape {
  kPlain, kException, kNested, kForwarded, kForwardedNormal, kForwardedLocal,
  kForwardedWriter, kForwardedThrowWriter, kOuterContinue, kOuterBreak, kSharedFinally, kSharedFinallyNested,
  kAwaitCapture, kAwaitClosure, kAwaitCatch, kAwaitFinally, kAwaitFinallySuspend
};
enum class MissingCell { kNone, kRead, kWrite, kHoleWriter, kHoleSetter, kAwaitState };
std::string Emit(abc::ProfileId profile, ParentShape shape, ChildShape child_shape,
                 MissingCell missing = MissingCell::kNone) {
  const bool exception_child = child_shape != ChildShape::kPlain && child_shape != ChildShape::kForwardedNormal;
  const bool forwarded_writer = child_shape == ChildShape::kForwardedWriter || child_shape == ChildShape::kForwardedThrowWriter;
  const bool forwarding = child_shape == ChildShape::kForwarded || child_shape == ChildShape::kForwardedNormal ||
      child_shape == ChildShape::kForwardedLocal || forwarded_writer;
  const bool nested = child_shape == ChildShape::kNested || forwarding || child_shape == ChildShape::kAwaitClosure;
  const bool outer_transfer = child_shape == ChildShape::kOuterContinue || child_shape == ChildShape::kOuterBreak;
  const bool shared_finally = child_shape == ChildShape::kSharedFinally || child_shape == ChildShape::kSharedFinallyNested;
  const bool finally_suspend = child_shape == ChildShape::kAwaitFinallySuspend;
  const bool await_finally = child_shape == ChildShape::kAwaitFinally || finally_suspend;
  const bool await_capture = child_shape == ChildShape::kAwaitCapture || child_shape == ChildShape::kAwaitClosure ||
      child_shape == ChildShape::kAwaitCatch || await_finally;
  const std::uint8_t getter_parameters = await_finally ? 2 : await_capture ? 1 : outer_transfer ? 5 : shared_finally ? 3 : 0;
  const bool outer_catch = shape == ParentShape::kOuterCatchContinue || shape == ParentShape::kOuterCatchBreak;
  const std::uint8_t parent_parameters = outer_catch ? 5 : 3;
  std::vector<std::uint8_t> bytes(kCodeStart);
  for (unsigned i = 0; i < sizeof(kParent); ++i) {
    bytes[kPool + i] = static_cast<std::uint8_t>(kParent >> (i * kByteBits));
    bytes[kPool + sizeof(kParent) + i] =
        static_cast<std::uint8_t>(kGetter >> (i * kByteBits));
    bytes[kPool + sizeof(kParent) + sizeof(kGetter) + i] =
        static_cast<std::uint8_t>(kSetter >> (i * kByteBits));
    if (nested) bytes[kPool + 3 * sizeof(kParent) + i] = static_cast<std::uint8_t>(kReader >> (i * kByteBits));
    if (forwarded_writer) bytes[kPool + 4 * sizeof(kParent) + i] = static_cast<std::uint8_t>(kWriter >> (i * kByteBits));
  }
  const auto caller = Append(bytes, 0, 0,
                             {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 0,
                              0, parent_parameters, Op(abc::Opcode::kRETURNUNDEFINED)});
  // v0 getter, v1 setter; v5 next, v6 run, v7 collect. Each iteration creates a
  // new environment. The protected call either writes its result or its
  // exception. The escaping getter must retain that iteration, not a
  // function-level cell.
  constexpr std::uint8_t kGet = 0, kSet = 1, kNext = 5, kRun = 6, kCollect = 7;
  constexpr std::uint8_t kExitDelta = 39,
                         kBackDelta = static_cast<std::uint8_t>(-41);
  const auto parent = outer_catch ? Append(bytes, 2, parent_parameters, {
      Op(abc::Opcode::kLDA_V8), 5, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJEQZ_IMM8), 51,
      Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJEQZ_IMM8), 39,
      Op(abc::Opcode::kLDA_V8), 7, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-10),
      Op(abc::Opcode::kSTA_V8), kGet, Op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      Op(abc::Opcode::kLDA_V8), kGet, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 1, 0, getter_parameters,
      Op(abc::Opcode::kSTA_V8), kGet,
      Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 2, 0, 1,
      Op(abc::Opcode::kSTA_V8), kSet,
      Op(abc::Opcode::kLDA_V8), 8, Op(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, kGet, kSet,
      Op(abc::Opcode::kPOPLEXENV),
      Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(shape == ParentShape::kOuterCatchBreak ? 8 : -47),
      Op(abc::Opcode::kLDA_V8), 9, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-53),
      Op(abc::Opcode::kRETURNUNDEFINED)}, {12, 4, 1, 0, 18, 31}) :
      shape == ParentShape::kNormal ? Append(bytes, 2, 3, {
      Op(abc::Opcode::kLDA_V8), kNext, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJEQZ_IMM8), 33,
      Op(abc::Opcode::kNEWLEXENV_IMM8), 1, Op(abc::Opcode::kLDA_V8), kRun,
      Op(abc::Opcode::kCALLARG0_IMM8), 0, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
      Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 1, 0, getter_parameters, Op(abc::Opcode::kSTA_V8), kGet,
      Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 2, 0, 1, Op(abc::Opcode::kSTA_V8), kSet,
      Op(abc::Opcode::kLDA_V8), kCollect, Op(abc::Opcode::kCALLARGS2_IMM8_V8_V8), 0, kGet, kSet,
      Op(abc::Opcode::kPOPLEXENV), Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-35),
      Op(abc::Opcode::kRETURNUNDEFINED)}) : shape == ParentShape::kCatch
                          ? Append(bytes, 2, 3,
                                   {Op(abc::Opcode::kLDA_V8),
                                    kNext,
                                    Op(abc::Opcode::kCALLARG0_IMM8),
                                    0,
                                    Op(abc::Opcode::kJEQZ_IMM8),
                                    kExitDelta,
                                    Op(abc::Opcode::kLDA_V8),
                                    kRun,
                                    Op(abc::Opcode::kCALLARG0_IMM8),
                                    0,
                                    Op(abc::Opcode::kJMP_IMM8),
                                    static_cast<std::uint8_t>(-10),
                                    Op(abc::Opcode::kSTA_V8),
                                    kGet,
                                    Op(abc::Opcode::kNEWLEXENV_IMM8),
                                    1,
                                    Op(abc::Opcode::kLDA_V8),
                                    kGet,
                                    Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
                                    0,
                                    Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8),
                                    0,
                                    1,
                                    0,
                                    getter_parameters,
                                    Op(abc::Opcode::kSTA_V8),
                                    kGet,
                                    Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8),
                                    0,
                                    2,
                                    0,
                                    1,
                                    Op(abc::Opcode::kSTA_V8),
                                    kSet,
                                    Op(abc::Opcode::kLDA_V8),
                                    kCollect,
                                    Op(abc::Opcode::kCALLARGS2_IMM8_V8_V8),
                                    0,
                                    kGet,
                                    kSet,
                                    Op(abc::Opcode::kPOPLEXENV),
                                    Op(abc::Opcode::kJMP_IMM8),
                                    kBackDelta,
                                    Op(abc::Opcode::kRETURNUNDEFINED)},
                                   {6, 4, 1, 0, 12, 31})
                          : Append(bytes, 2, 3,
                                   {Op(abc::Opcode::kLDA_V8),
                                    kNext,
                                    Op(abc::Opcode::kCALLARG0_IMM8),
                                    0,
                                    Op(abc::Opcode::kJEQZ_IMM8),
                                    kExitDelta,
                                    Op(abc::Opcode::kNEWLEXENV_IMM8),
                                    1,
                                    Op(abc::Opcode::kLDA_V8),
                                    kRun,
                                    Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
                                    0,
                                    Op(abc::Opcode::kCALLARG0_IMM8),
                                    0,
                                    Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
                                    0,
                                    Op(abc::Opcode::kJMP_IMM8),
                                    4,
                                    Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
                                    0,
                                    Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8),
                                    0,
                                    1,
                                    0,
                                    getter_parameters,
                                    Op(abc::Opcode::kSTA_V8),
                                    kGet,
                                    Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8),
                                    0,
                                    2,
                                    0,
                                    1,
                                    Op(abc::Opcode::kSTA_V8),
                                    kSet,
                                    Op(abc::Opcode::kLDA_V8),
                                    kCollect,
                                    Op(abc::Opcode::kCALLARGS2_IMM8_V8_V8),
                                    0,
                                    kGet,
                                    kSet,
                                    Op(abc::Opcode::kPOPLEXENV),
                                    Op(abc::Opcode::kJMP_IMM8),
                                    kBackDelta,
                                    Op(abc::Opcode::kRETURNUNDEFINED)},
                                   {12, 4, 1, 0, 18, 2});
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  // IMM4_IMM4 packs depth in the low nibble, slot in the high nibble.
  constexpr unsigned kSlotShift = 4;
  const std::uint8_t read_slot = missing == MissingCell::kRead ? 1U << kSlotShift : 0;
  const std::uint8_t write_slot = missing == MissingCell::kWrite ? 1U << kSlotShift : 0;
  // Forwarding variants have no lexical accesses. One pushes an unrelated
  // environment so its reader must pass through depth 1 to the ancestor cell.
  // await input; return cell. A sibling may change the captured cell while
  // suspended: the load below must not move before the await.
  const auto async_getter = [&] {
    std::vector<std::uint8_t> code{
      Op(abc::Opcode::kASYNCFUNCTIONENTER), Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8),
      static_cast<std::uint8_t>(missing == MissingCell::kAwaitState ? 1 : 0),
      Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0,
      Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR),
      Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kLDA_V8), 0,
      Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2,
      Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
      Op(abc::Opcode::kEQ_IMM8_V8), 0, 2, Op(abc::Opcode::kJEQZ_IMM8), 6,
      Op(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      Op(abc::Opcode::kLDA_V8), 1};
    if (child_shape == ChildShape::kAwaitClosure)
      code.insert(code.end(), {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0});
    else code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot});
    code.insert(code.end(), {Op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, Op(abc::Opcode::kRETURN)});
    if (child_shape == ChildShape::kAwaitCatch)
      code.insert(code.end(), {Op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, Op(abc::Opcode::kRETURN)});
    code.insert(code.end(), {
      Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0,
      Op(abc::Opcode::kRETURN)});
    if (child_shape == ChildShape::kAwaitCatch)
      return AppendCode(bytes, 3, getter_parameters, code,
          {3, 32, 1, 0, 40, 3, 3, 40, 1, 0, 43, 5}, 2);
    const std::uint8_t handler = child_shape == ChildShape::kAwaitClosure ? 43 : 40;
    return AppendCode(bytes, 3, getter_parameters, code,
        {3, static_cast<std::uint8_t>(handler - 3), 1, 0, handler, 5});
  };
  // v7 shared finalizer guard/pending-Hole lowering around a complete await
  // protocol, inside the async completion wrapper. v0 is private async state;
  // v1/v2 resume value/mode; v3 pending throw; v4 guard; v5 scratch.
  const auto finally_getter = [&] {
    std::vector<std::uint8_t> code{
      Op(abc::Opcode::kASYNCFUNCTIONENTER), Op(abc::Opcode::kSTA_V8), 0,
      Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTA_V8), 4,
      Op(abc::Opcode::kLDA_V8), 9, Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
      Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0,
      Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR),
      Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kLDA_V8), 0,
      Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2,
      Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
      Op(abc::Opcode::kEQ_IMM8_V8), 0, 2, Op(abc::Opcode::kJEQZ_IMM8), 6,
      Op(abc::Opcode::kLDA_V8), 1,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      Op(abc::Opcode::kLDA_V8), 1,
      Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 3, Op(abc::Opcode::kJMP_IMM8), 4,
      Op(abc::Opcode::kSTA_V8), 3,
      Op(abc::Opcode::kLDA_V8), 4, Op(abc::Opcode::kSTA_V8), 5,
      Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kEQ_IMM8_V8), 0, 5,
      Op(abc::Opcode::kJEQZ_IMM8), 13, Op(abc::Opcode::kSTA_V8), 4,
      Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot, Op(abc::Opcode::kSTA_V8), 5,
      Op(abc::Opcode::kLDA_V8), 10, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 5,
      Op(abc::Opcode::kLDA_V8), 3, Op(abc::Opcode::kSTA_V8), 5,
      Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 4,
      Op(abc::Opcode::kLDA_V8), 5, Op(abc::Opcode::kNOTEQ_IMM8_V8), 0, 4,
      Op(abc::Opcode::kJEQZ_IMM8), 6, Op(abc::Opcode::kLDA_V8), 5,
      static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
      Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot,
      Op(abc::Opcode::kASYNCFUNCTIONRESOLVE_V8), 0, Op(abc::Opcode::kRETURN),
      Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kASYNCFUNCTIONREJECT_V8), 0,
      Op(abc::Opcode::kRETURN)};
    if (finally_suspend) {
      // Await cleanup(cell) while the pending completion remains in v3.
      // Distinct v6/v7 resume temporaries must not alias that completion.
      constexpr std::size_t kInputOperand = 7, kCleanupOperand = 62,
                            kGuardBranchOperand = 54, kCleanupEnd = 66;
      constexpr std::uint8_t kSuspendingLocals = 8;
      code[kInputOperand] = kSuspendingLocals + kImplicitArguments;
      code[kCleanupOperand] = kSuspendingLocals + kImplicitArguments + 1;
      code[kGuardBranchOperand] = 43;  // Skip call and its 30-byte await protocol.
      code.insert(code.begin() + kCleanupEnd, {
        Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
        Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0,
        Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR),
        Op(abc::Opcode::kSTA_V8), 6, Op(abc::Opcode::kLDA_V8), 0,
        Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 7,
        Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
        Op(abc::Opcode::kEQ_IMM8_V8), 0, 7, Op(abc::Opcode::kJEQZ_IMM8), 6,
        Op(abc::Opcode::kLDA_V8), 6,
        static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
        Op(abc::Opcode::kLDA_V8), 6});
      return AppendCode(bytes, kSuspendingLocals, getter_parameters, code,
          {6, 32, 1, 0, 43, 2, 3, 116, 1, 0, 119, 5}, 2);
    }
    return AppendCode(bytes, 6, getter_parameters, code,
        {6, 32, 1, 0, 43, 2, 3, 86, 1, 0, 89, 5}, 2);
  };
  const auto getter = await_finally ? finally_getter() :
      await_capture ? async_getter() :
      child_shape == ChildShape::kSharedFinallyNested ? Append(bytes, 3, getter_parameters,
      {Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kJMP_IMM8), 4,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
       Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 1,
       Op(abc::Opcode::kJMP_IMM8), 4, Op(abc::Opcode::kSTA_V8), 1,
       Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kEQ_IMM8_V8), 0, 2,
       Op(abc::Opcode::kJEQZ_IMM8), 13, Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDA_V8), 7, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 2,
       Op(abc::Opcode::kLDA_V8), 1, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDA_V8), 2, Op(abc::Opcode::kNOTEQ_IMM8_V8), 0, 0,
       Op(abc::Opcode::kJEQZ_IMM8), 6, Op(abc::Opcode::kLDA_V8), 2,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
       Op(abc::Opcode::kLDA_V8), 8, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kRETURN)}, {3, 4, 1, 0, 9, 2, 3, 8, 1, 0, 16, 2}, 2) :
      child_shape == ChildShape::kSharedFinally ? Append(bytes, 3, getter_parameters,
      {Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 1,
       Op(abc::Opcode::kJMP_IMM8), 4, Op(abc::Opcode::kSTA_V8), 1,
       Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kEQ_IMM8_V8), 0, 2,
       Op(abc::Opcode::kJEQZ_IMM8), 13, Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDA_V8), 7, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 2,
       Op(abc::Opcode::kLDA_V8), 1, Op(abc::Opcode::kSTA_V8), 2,
       Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kLDA_V8), 2, Op(abc::Opcode::kNOTEQ_IMM8_V8), 0, 0,
       Op(abc::Opcode::kJEQZ_IMM8), 6, Op(abc::Opcode::kLDA_V8), 2,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
       Op(abc::Opcode::kLDA_V8), 8, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kRETURN)}, {3, 4, 1, 0, 12, 2}) :
      outer_transfer ? Append(bytes, 0, getter_parameters,
      {Op(abc::Opcode::kLDA_V8), 3, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kJEQZ_IMM8), 26,
       Op(abc::Opcode::kLDA_V8), 4, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kJEQZ_IMM8), 14,
       Op(abc::Opcode::kLDA_V8), 5, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-14),
       Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(child_shape == ChildShape::kOuterBreak ? 8 : -22),
       Op(abc::Opcode::kLDA_V8), 7, Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-28),
       Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot, Op(abc::Opcode::kRETURN)},
      {12, 4, 1, 0, 22, 2}) : child_shape == ChildShape::kForwardedLocal ? Append(bytes, 0, 0,
      {Op(abc::Opcode::kNEWLEXENV_IMM8), 1,
       Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0,
       Op(abc::Opcode::kCALLARG0_IMM8), 0, Op(abc::Opcode::kPOPLEXENV), Op(abc::Opcode::kRETURN),
       Op(abc::Opcode::kPOPLEXENV), Op(abc::Opcode::kRETURN)}, {7, 2, 1, 0, 11, 2}) :
      child_shape == ChildShape::kForwardedNormal ? Append(bytes, 0, 0,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0,
       Op(abc::Opcode::kCALLARG0_IMM8), 0, Op(abc::Opcode::kRETURN)}) :
      forwarding ? Append(bytes, 0, 0,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0,
       Op(abc::Opcode::kCALLARG0_IMM8), 0, Op(abc::Opcode::kRETURN),
       Op(abc::Opcode::kRETURN)}, {5, 2, 1, 0, 8, 1}) : nested ? Append(bytes, 1, 0,
      {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kSTA_V8), 0,
       Op(abc::Opcode::kNEWLEXENV_IMM8), 1, Op(abc::Opcode::kLDA_V8), 0,
       Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
       Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0,
       Op(abc::Opcode::kCALLARG0_IMM8), 0,
       Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kPOPLEXENV),
       Op(abc::Opcode::kRETURN), Op(abc::Opcode::kRETURN)}, {15, 2, 1, 0, 21, 1}) :
      exception_child ? Append(bytes, 0, 0,
      {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), read_slot,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
       Op(abc::Opcode::kRETURN)},
      {0, 4, 1, 0, 4, 1}) : Append(
      bytes, 0, 0,
      {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kRETURN)});
  // Neither forwarding parent accesses the cell. The writer is write-only;
  // its throwing form commits the mutation before the middle handler returns.
  const auto setter = forwarded_writer ? Append(bytes, 0, 1,
      {Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 4, 0, 1,
       Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, kImplicitArguments,
       Op(abc::Opcode::kRETURNUNDEFINED), Op(abc::Opcode::kRETURNUNDEFINED)}, {5, 3, 1, 0, 9, 1}) :
      exception_child ? Append(bytes, 0, 1,
      {Op(abc::Opcode::kLDA_V8), kImplicitArguments,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
       Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), write_slot, Op(abc::Opcode::kRETURNUNDEFINED)},
      {0, 4, 1, 0, 4, 3}) : Append(bytes, 0, 1,
                             {Op(abc::Opcode::kLDA_V8), kImplicitArguments,
                              Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
                              Op(abc::Opcode::kRETURNUNDEFINED)});
  const auto reader = nested ? std::optional{missing == MissingCell::kHoleWriter ? Append(bytes, 0, 0,
      {Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
       Op(abc::Opcode::kRETURNUNDEFINED)}) : Append(bytes, 0, 0,
      {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4),
       static_cast<std::uint8_t>(child_shape == ChildShape::kForwardedLocal ? 1 : 0),
       Op(abc::Opcode::kRETURN)})} : std::nullopt;
  const auto writer = forwarded_writer ? std::optional{missing == MissingCell::kHoleSetter ? Append(bytes, 0, 1,
      {Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
       Op(abc::Opcode::kRETURNUNDEFINED)}) : child_shape == ChildShape::kForwardedThrowWriter ? Append(bytes, 0, 1,
      {Op(abc::Opcode::kLDA_V8), kImplicitArguments, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
       static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)}) : Append(bytes, 0, 1,
      {Op(abc::Opcode::kLDA_V8), kImplicitArguments, Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
       Op(abc::Opcode::kRETURNUNDEFINED)})} : std::nullopt;
  char path[] = "/tmp/abcd-loop-closure-XXXXXX";
  const int fd = mkstemp(path);
  test::Check(fd >= 0 && write(fd, bytes.data(), bytes.size()) ==
                             static_cast<ssize_t>(bytes.size()));
  test::Check(close(fd) == 0);
  auto opened = corpus::AbcFile::Open(path);
  test::Check(std::holds_alternative<std::unique_ptr<corpus::AbcFile>>(opened));
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  abc::Header header;
  header.kind = abc::AbcKind::kDynamic;
  header.layout = abc::DynamicLayout{};
  header.profile = profile;
  switch (profile) {
    case abc::ProfileId::kDynamic1202:
      header.version = {12, 0, 2, 0};
      break;
    case abc::ProfileId::kDynamic1206:
      header.version = {12, 0, 6, 0};
      break;
    case abc::ProfileId::kDynamic1301:
      header.version = {13, 0, 1, 0};
      break;
    default:
      test::Check(false);
  }
  header.file_size = bytes.size();
  abc::MetadataIndex index;
  index.regions.push_back({kCaller, kCodeStart, {}, {forwarded_writer ? 5U : nested ? 4U : 3U, kPool}, {}, {}});
  index.methods.resize(forwarded_writer ? 6 : nested ? 5 : 4);
  index.methods[0].offset = {kCaller};
  index.methods[0].code = caller;
  index.methods[1].offset = {kParent};
  index.methods[1].code = parent;
  index.methods[2].offset = {kGetter};
  index.methods[2].code = getter;
  index.methods[3].offset = {kSetter};
  index.methods[3].code = setter;
  if (nested) {
    index.methods[4].offset = {kReader}; index.methods[4].code = reader;
    index.methods[4].name = "read"; index.methods[4].name_utf16_length = 4;
  }
  if (forwarded_writer) {
    index.methods[5].offset = {kWriter}; index.methods[5].code = writer;
    index.methods[5].name = "write"; index.methods[5].name_utf16_length = 5;
  }
  for (auto& method : index.methods) {
    method.owner_offset = {kRecord};
    method.access_flags =
        static_cast<std::uint32_t>(abc::FunctionKind::kBaseConstructor)
        << kFunctionKindShift;
  }
  if (await_capture) index.methods[2].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kAsync) << kFunctionKindShift;
  index.methods[1].name = "capture";
  index.methods[1].name_utf16_length = 7;
  index.methods[2].name = "get";
  index.methods[2].name_utf16_length = 3;
  index.methods[3].name = "set";
  index.methods[3].name_utf16_length = 3;
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto identity = source::MakeRouteInputIdentity(
      std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  std::ostringstream positions;
  const auto report =
      InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
                          positions, FamilyInspectMode::kSourceDraft);
  test::Check(unlink(path) == 0);
  if (missing == MissingCell::kAwaitState) {
    test::Check(!report.value("draft_prepared", false) && !report.contains("draft_source"));
    test::Check(report.at("failed_method") == kGetter);
    test::Check(report.at("phase_input_error") == static_cast<unsigned>(source::FunctionEmitError::kKind));
    return {};
  }
  if (missing == MissingCell::kHoleWriter || missing == MissingCell::kHoleSetter) {
    test::Check(!report.value("draft_prepared", false) && !report.contains("draft_source"));
    test::Check(report.at("failed_method") == (missing == MissingCell::kHoleSetter ? kWriter : kReader));
    if (!report.contains("input_error")) std::cerr << report << '\n';
    test::Check(report.at("input_error") == static_cast<unsigned>(analysis::SemanticProofError::kInternalValue));
    return {};
  }
  if (missing != MissingCell::kNone) {
    if (!report.contains("family_error")) std::cerr << report << '\n';
    // Conditional Language facts must not invent a parent's missing storage,
    // including write-only captures accessed exclusively from a catch handler.
    test::Check(!report.value("draft_prepared", false) && !report.contains("draft_source"));
    test::Check(report.at("family_error") == static_cast<unsigned>(source::CaptureCellError::kInput));
    test::Check(report.at("failed_method") == (missing == MissingCell::kRead ? kGetter : kSetter));
    return {};
  }
  if (!report.value("draft_prepared", false)) std::cerr << report << '\n';
  test::Check(report.value("draft_prepared", false));
  test::Check(!report.value("draft_incomplete", true));
  const auto located =
      nlohmann::json::parse("{" + positions.str() + "\"ready\":true}");
  test::Check(located["draft_positions"].size() == (forwarded_writer ? 5 : nested ? 4 : 3));
  for (const auto& member : located["draft_positions"])
    test::Check(member["incomplete"] == false);
  if (await_capture) {
    const auto& positions = located["draft_positions"][1]["positions"];
    std::vector<std::uint32_t> original_pcs;
    for (const auto& origin : positions["origins"])
      if (origin.contains("original_pc") && !origin["original_pc"].is_null())
        original_pcs.push_back(origin["original_pc"].get<std::uint32_t>());
    // Compact PCs must not replace the raw await, continuation, lexical load
    // or nested creation PCs in the shared AST's provenance.
    auto expected = std::vector<std::uint32_t>{3, 5, 12, 16, 17, 33, 35,
        child_shape == ChildShape::kAwaitClosure ? 42U : 39U};
    if (child_shape == ChildShape::kAwaitCatch) expected.push_back(42);
    if (await_finally) {
      for (const auto pc : {8U, 43U, 63U, finally_suspend ? 118U : 88U})
        test::Check(std::find(original_pcs.begin(), original_pcs.end(), pc) != original_pcs.end());
      if (finally_suspend)
        test::Check(std::find(original_pcs.begin(), original_pcs.end(), 66U) != original_pcs.end());
      test::Check(report.at("draft_source").get<std::string>().find("finally") != std::string::npos);
    } else test::Check(original_pcs == expected);
    if (child_shape == ChildShape::kAwaitClosure)
      test::Check(report["family_members"][2]["creation_pc"] == 35);
  }
  return report.at("draft_source").get<std::string>();
}
}  // namespace
int main(int argc, char** argv) {
  if (argc == 2 && (std::string_view(argv[1]) == "--await-capture" ||
                    std::string_view(argv[1]) == "--await-closure" ||
                    std::string_view(argv[1]) == "--await-catch" ||
                    std::string_view(argv[1]) == "--await-finally" ||
                    std::string_view(argv[1]) == "--await-finally-suspend")) {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, std::string_view(argv[1]) == "--await-closure" ?
            ChildShape::kAwaitClosure : std::string_view(argv[1]) == "--await-catch" ?
            ChildShape::kAwaitCatch : std::string_view(argv[1]) == "--await-finally" ?
            ChildShape::kAwaitFinally : std::string_view(argv[1]) == "--await-finally-suspend" ?
            ChildShape::kAwaitFinallySuspend : ChildShape::kAwaitCapture) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--shared-finally" ||
                    std::string_view(argv[1]) == "--shared-finally-nested")) {
    const auto child = std::string_view(argv[1]) == "--shared-finally-nested" ?
        ChildShape::kSharedFinallyNested : ChildShape::kSharedFinally;
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, child) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--outer-catch") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      for (const auto parent : {ParentShape::kOuterCatchContinue, ParentShape::kOuterCatchBreak})
        for (const auto child : {ChildShape::kPlain, ChildShape::kException, ChildShape::kForwardedThrowWriter})
          std::cout << Emit(profile, parent, child) << ",\n";
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--outer-transfer") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301}) {
      for (const auto child : {ChildShape::kOuterContinue, ChildShape::kOuterBreak})
        for (const auto parent : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
          std::cout << Emit(profile, parent, child) << ",\n";
    }
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--forwarding") {
    std::cout << "export default " << Emit(abc::ProfileId::kDynamic1206,
        ParentShape::kIteration, ChildShape::kForwarded) << ";\n";
    return 0;
  }
  const bool emit = argc == 2 && std::string_view(argv[1]) == "--emit";
  if (emit) std::cout << "export const functions = [\n";
  for (const auto profile :
       {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
        abc::ProfileId::kDynamic1301}) {
    for (const auto child_shape : {ChildShape::kPlain, ChildShape::kException, ChildShape::kNested,
         ChildShape::kForwarded, ChildShape::kForwardedNormal, ChildShape::kForwardedLocal,
         ChildShape::kForwardedWriter, ChildShape::kForwardedThrowWriter}) {
    for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal}) {
      const auto source = Emit(profile, shape, child_shape);
      if (emit) std::cout << source << ",\n";
    }
    }
  }
  if (emit) std::cout << "];\n";
  if (!emit) for (const auto child : {ChildShape::kOuterContinue, ChildShape::kOuterBreak})
    for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
      Emit(abc::ProfileId::kDynamic1206, shape, child);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kSharedFinally);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitCapture);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitClosure);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitCatch);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitFinally);
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitFinallySuspend);
  if (!emit) for (const auto parent : {ParentShape::kOuterCatchContinue, ParentShape::kOuterCatchBreak})
    for (const auto child : {ChildShape::kPlain, ChildShape::kException, ChildShape::kForwardedThrowWriter})
      Emit(abc::ProfileId::kDynamic1206, parent, child);
  for (const auto missing : {MissingCell::kRead, MissingCell::kWrite})
    Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kException, missing);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kNested, MissingCell::kHoleWriter);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kForwarded, MissingCell::kHoleWriter);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kForwardedWriter, MissingCell::kHoleSetter);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kOuterContinue, MissingCell::kRead);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kSharedFinally, MissingCell::kRead);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kOuterCatchContinue, ChildShape::kException, MissingCell::kRead);
  Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kAwaitCapture, MissingCell::kAwaitState);
}
