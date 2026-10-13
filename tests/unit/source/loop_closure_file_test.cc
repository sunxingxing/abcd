#include <unistd.h>

#include <nlohmann/json.hpp>
#include <algorithm>
#include <sstream>
#include <string_view>

#include "../../../tools/family_value_inspect.h"
#include "../../../src/source/capture_cells.h"
#include "../../../src/source/family_file.h"
#include "../../../src/source/family_tree.h"
#include "../../../src/source/family_print.h"
#include "abcd/abc/method_attributes.h"
#include "abcd/analysis/coroutine.h"
#include "abcd/source/function_emit_error.h"
#include "../../../src/source/tree_structure.h"
#include "analysis/ssa_fixture.h"
#include "analysis/generator_delegate_fixture.h"
#include "source/preserved_completions.h"

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
// Code headers and exception tables use Panda ULEB128, including large bodies.
void Uleb(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
  constexpr unsigned kPayloadBits = 7;
  constexpr std::uint8_t kMore = 0x80, kPayload = 0x7f;
  do {
    const auto part = static_cast<std::uint8_t>(value & kPayload);
    value >>= kPayloadBits;
    bytes.push_back(part | (value ? kMore : 0));
  } while (value);
}
abc::CodeInfo AppendEncodedCode(std::vector<std::uint8_t>& bytes, std::uint8_t locals,
                     std::uint8_t parameters,
                     std::span<const std::uint8_t> code,
                     std::span<const std::uint8_t> table = {}, std::uint32_t try_count = 1) {
  test::Check(code.size() <= UINT32_MAX);
  const auto offset = static_cast<std::uint32_t>(bytes.size());
  const auto arguments =
      static_cast<std::uint8_t>(parameters + kImplicitArguments);
  const auto tries = table.empty() ? 0 : try_count;
  Uleb(bytes, locals); Uleb(bytes, arguments); Uleb(bytes, static_cast<std::uint32_t>(code.size())); Uleb(bytes, tries);
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
abc::CodeInfo AppendCode(std::vector<std::uint8_t>& bytes, std::uint8_t locals,
                     std::uint8_t parameters, std::span<const std::uint8_t> code,
                     std::initializer_list<std::uint8_t> table = {}, std::uint8_t try_count = 1) {
  return AppendEncodedCode(bytes, locals, parameters, code, {table.begin(), table.size()}, try_count);
}
abc::CodeInfo Append(std::vector<std::uint8_t>& bytes, std::uint8_t locals,
                     std::uint8_t parameters, std::initializer_list<std::uint8_t> code,
                     std::initializer_list<std::uint8_t> table = {}, std::uint8_t tries = 1) {
  return AppendCode(bytes, locals, parameters, code, table, tries);
}
enum class ParentShape { kIteration, kCatch, kNormal, kReceiverCapture, kOuterCatchContinue, kOuterCatchBreak };
enum class CopyEncoding { kV4, kV8, kV16 };
enum class BranchEncoding { k8, k16, k32 };
struct ResumeEncoding {
  BranchEncoding return_mode = BranchEncoding::k8;
  BranchEncoding throw_mode = BranchEncoding::k8;
};
enum class YieldFinalizer { kPreserve, kReturn, kThrow, kDifferentCallee, kDifferentInput, kBodyYield, kSharedBodyYield, kInjectedReturn, kInjectedDifferentCallee, kInjectedCellRead, kInjectedUncaught, kInjectedConditional, kInjectedConditionalUncaught, kInjectedSuspend, kInjectedSuspendUncaught };
enum class YieldOverride { kNone, kReturn, kThrow };
enum class CleanupControl { kSequence, kLoop, kSelect, kConditionCall, kLoopConditionCall, kLoopOverride, kNestedLoopOverride, kPostLoop, kPostLoopOverride };
enum class ChildShape {
  kPlain, kException, kNested, kForwarded, kForwardedNormal, kForwardedLocal,
  kForwardedWriter, kForwardedThrowWriter, kOuterContinue, kOuterBreak, kSharedFinally, kSharedFinallyNested,
  kAwaitCapture, kAwaitClosure, kAwaitCatch, kAwaitFinally, kAwaitFinallySuspend, kAwaitFinallyReuse,
  kAwaitSelf, kAwaitSelfPrivate, kAwaitThis, kAwaitReceiver, kAwaitArguments, kYieldCapture, kYieldResult, kYieldLoop, kYieldCall,
  kYieldCatch, kYieldCatchCapture, kYieldCatchYield, kYieldCatchOverwrite, kYieldCatchLoop,
  kYieldSelf, kYieldSelfPrivate, kYieldThis, kYieldReceiver,
  kYieldCopy, kYieldLexical, kYieldClosure, kYieldFrameCopy, kYieldParameterCell, kYieldParameterFrameCell,
  kYieldFinallyReturn, kYieldDelegate, kYieldDelegateCatch, kAsyncGeneratorPrefix,
  kAsyncGeneratorAwait, kAsyncGeneratorAwaitCatch, kAsyncGeneratorYield, kAsyncGeneratorYieldCatch,
  kAsyncGeneratorYieldAwait, kAsyncGeneratorYieldAwaitCatch
};
enum class MissingCell { kNone, kRead, kWrite, kHoleWriter, kHoleSetter, kAwaitState,
                         kYieldState, kYieldDone, kYieldReturn, kYieldEntry, kYieldCleanup,
                         kYieldCatchResult, kYieldCatchMode, kYieldCatchBoundary, kYieldCatchEntry,
                         kYieldPreludeCall, kYieldPreludeState, kYieldPreludeLocal, kYieldPreludeEntry,
                         kYieldPreludeResult, kYieldPreludeMode, kYieldPreludeCallee,
                         kYieldPreludeCellDepth, kYieldPreludeCellSlot, kYieldPreludeCellLocal, kYieldPreludeCellNoEnv,
                         kYieldReturnBranch, kYieldThrowBranch, kYieldReturnTarget, kYieldThrowTarget,
                         kYieldReturnReadResult, kYieldReturnWriteResult, kYieldReturnReadMode, kYieldReturnWriteMode,
                         kYieldReturnReadState, kYieldReturnWriteState, kYieldReturnReadAccumulator,
                         kYieldCleanupEscape, kYieldCleanupExternalEntry, kYieldCleanupDeadEntry, kYieldInnerState, kYieldInnerResult, kYieldInnerMode,
                         kYieldInnerReturnBranch, kYieldInnerThrowBranch, kYieldInnerEntry,
                         kDelegateName, kDelegatePool, kDelegateNext, kDelegateReceiver, kDelegateState,
                         kDelegateMode, kDelegateBranch, kDelegateEntry, kDelegateResultLeak,
                         kDelegateCatchLeak, kDelegateCloseBoundary, kDelegateUserBoundary, kDelegateCloseMissing, kDelegateTruth,
                         kAsyncGenCallee, kAsyncGenState, kAsyncGenDone, kAsyncGenComplete, kAsyncGenResolve, kAsyncGenReject,
                         kAsyncGenResult, kAsyncGenMode, kAsyncGenEntry, kAsyncGenReturnEntry,
                         kAsyncGenAwaitState, kAsyncGenAwaitMode, kAsyncGenAwaitBranch, kAsyncGenAwaitEntry,
                         kAsyncGenAwaitCatchResult, kAsyncGenAwaitCatchMode,
                         kAsyncYieldState, kAsyncYieldDone, kAsyncYieldResolve, kAsyncYieldReturnMode,
                         kAsyncYieldReturnTarget, kAsyncYieldThrowTarget, kAsyncYieldReturnState,
                         kAsyncYieldReturnValue, kAsyncYieldEntry, kAsyncYieldLeak,
                         kAsyncYieldCatchResult, kAsyncYieldCatchPrivate, kAsyncYieldBoundary };
std::string Emit(abc::ProfileId profile, ParentShape shape, ChildShape child_shape,
                 MissingCell missing = MissingCell::kNone, bool async_arrow = false, std::uint32_t yield_count = 1,
                 CopyEncoding copy_encoding = CopyEncoding::kV8, std::uint32_t copy_count = 1,
                 YieldFinalizer yield_finalizer = YieldFinalizer::kPreserve, ResumeEncoding resume_encoding = {},
                 std::uint32_t return_cleanup_count = 1, YieldOverride yield_override = YieldOverride::kNone,
                 CleanupControl cleanup_control = CleanupControl::kSequence,
                 test::DelegateTruth delegate_truth = test::DelegateTruth::kNative, bool verify_preservation = false) {
  const bool exception_child = child_shape != ChildShape::kPlain && child_shape != ChildShape::kForwardedNormal;
  const bool forwarded_writer = child_shape == ChildShape::kForwardedWriter || child_shape == ChildShape::kForwardedThrowWriter;
  const bool forwarding = child_shape == ChildShape::kForwarded || child_shape == ChildShape::kForwardedNormal ||
      child_shape == ChildShape::kForwardedLocal || forwarded_writer;
  const bool yield_parameter_cell = child_shape == ChildShape::kYieldParameterCell || child_shape == ChildShape::kYieldParameterFrameCell;
  const bool yield_closure = child_shape == ChildShape::kYieldClosure || yield_parameter_cell;
  const bool yield_frame_copy = child_shape == ChildShape::kYieldFrameCopy || child_shape == ChildShape::kYieldParameterFrameCell;
  const bool yield_lexical = child_shape == ChildShape::kYieldLexical || yield_closure;
  const bool yield_copy = child_shape == ChildShape::kYieldCopy || child_shape == ChildShape::kYieldClosure || (yield_frame_copy && !yield_parameter_cell);
  const bool nested = child_shape == ChildShape::kNested || forwarding || child_shape == ChildShape::kAwaitClosure || yield_closure;
  const bool outer_transfer = child_shape == ChildShape::kOuterContinue || child_shape == ChildShape::kOuterBreak;
  const bool shared_finally = child_shape == ChildShape::kSharedFinally || child_shape == ChildShape::kSharedFinallyNested;
  const bool finally_reuse = child_shape == ChildShape::kAwaitFinallyReuse;
  const bool finally_suspend = child_shape == ChildShape::kAwaitFinallySuspend || finally_reuse;
  const bool await_finally = child_shape == ChildShape::kAwaitFinally || finally_suspend;
  const bool await_self = child_shape == ChildShape::kAwaitSelf || child_shape == ChildShape::kAwaitSelfPrivate;
  const bool await_frame = await_self || child_shape == ChildShape::kAwaitThis ||
      child_shape == ChildShape::kAwaitReceiver || child_shape == ChildShape::kAwaitArguments;
  const bool await_capture = child_shape == ChildShape::kAwaitCapture || child_shape == ChildShape::kAwaitClosure ||
      child_shape == ChildShape::kAwaitCatch || await_finally || await_frame;
  const bool yield_catch = child_shape == ChildShape::kYieldCatch || child_shape == ChildShape::kYieldCatchCapture ||
      child_shape == ChildShape::kYieldCatchYield || child_shape == ChildShape::kYieldCatchOverwrite ||
      child_shape == ChildShape::kYieldCatchLoop;
  const bool yield_self = child_shape == ChildShape::kYieldSelf || child_shape == ChildShape::kYieldSelfPrivate;
  const bool yield_frame = yield_self || child_shape == ChildShape::kYieldThis || child_shape == ChildShape::kYieldReceiver;
  const bool yield_delegate = child_shape == ChildShape::kYieldDelegate || child_shape == ChildShape::kYieldDelegateCatch;
  const bool yield_finally = child_shape == ChildShape::kYieldFinallyReturn;
  const bool async_generator_await = child_shape == ChildShape::kAsyncGeneratorAwait || child_shape == ChildShape::kAsyncGeneratorAwaitCatch;
  const bool async_generator_yield = child_shape == ChildShape::kAsyncGeneratorYield || child_shape == ChildShape::kAsyncGeneratorYieldCatch ||
      child_shape == ChildShape::kAsyncGeneratorYieldAwait || child_shape == ChildShape::kAsyncGeneratorYieldAwaitCatch;
  const bool async_generator = child_shape == ChildShape::kAsyncGeneratorPrefix || async_generator_await || async_generator_yield;
  const bool suspended_return = yield_finalizer == YieldFinalizer::kInjectedSuspend ||
      yield_finalizer == YieldFinalizer::kInjectedSuspendUncaught;
  const bool injected_return = yield_finally && (yield_finalizer == YieldFinalizer::kInjectedReturn ||
      yield_finalizer == YieldFinalizer::kInjectedDifferentCallee || yield_finalizer == YieldFinalizer::kInjectedCellRead ||
      yield_finalizer == YieldFinalizer::kInjectedUncaught || yield_finalizer == YieldFinalizer::kInjectedConditional ||
      yield_finalizer == YieldFinalizer::kInjectedConditionalUncaught || suspended_return);
  const bool conditional_return = yield_finalizer == YieldFinalizer::kInjectedConditional ||
      yield_finalizer == YieldFinalizer::kInjectedConditionalUncaught;
  const unsigned override_bytes = yield_override == YieldOverride::kReturn ? 1U : yield_override == YieldOverride::kThrow ? 2U : 0U;
  const bool nested_loop = cleanup_control == CleanupControl::kNestedLoopOverride;
  const bool post_loop = cleanup_control == CleanupControl::kPostLoop || cleanup_control == CleanupControl::kPostLoopOverride;
  const bool loop_override = cleanup_control == CleanupControl::kLoopOverride || nested_loop ||
      cleanup_control == CleanupControl::kPostLoopOverride;
  const bool loop_cleanup = cleanup_control == CleanupControl::kLoop || cleanup_control == CleanupControl::kLoopConditionCall ||
      loop_override || post_loop;
  const bool condition_call = cleanup_control == CleanupControl::kConditionCall || cleanup_control == CleanupControl::kLoopConditionCall;
  const bool select_cleanup = cleanup_control == CleanupControl::kSelect;
  test::Check(!loop_cleanup || (!conditional_return && (loop_override ? yield_override != YieldOverride::kNone : yield_override == YieldOverride::kNone)));
  test::Check(!select_cleanup || (!conditional_return && yield_override != YieldOverride::kNone));
  // OH7 LDLEXVAR imm4/imm4; JEQZ imm32; JMP imm32.
  constexpr unsigned kLoopGuardBytes = 7, kLoopBackBytes = 5;
  constexpr unsigned kLoopWorkBytes = 4; // LDA v8; CALLARG0 imm8 after inner exit.
  constexpr unsigned kNestedLoopBytes = kLoopGuardBytes + kLoopBackBytes + kLoopWorkBytes;
  const bool generator = child_shape == ChildShape::kYieldCapture || child_shape == ChildShape::kYieldResult ||
      child_shape == ChildShape::kYieldLoop || child_shape == ChildShape::kYieldCall || yield_catch || yield_frame || yield_lexical || yield_copy || yield_finally || yield_delegate;
  const std::uint8_t getter_parameters = await_finally || yield_finally ? 2 : await_capture || generator || async_generator ? 1 : outer_transfer ? 5 : shared_finally ? 3 : 0;
  const bool outer_catch = shape == ParentShape::kOuterCatchContinue || shape == ParentShape::kOuterCatchBreak;
  const std::uint8_t parent_parameters = outer_catch ? 5 : 3;
  std::vector<std::uint8_t> bytes(kCodeStart);
  if (yield_delegate) {
    std::uint32_t offset = 160; // File string entities, separate from Code records.
    std::uint32_t slot = 3;
    for (const std::string_view name : {"next", "throw", "return", "done", "value"}) {
      for (unsigned byte = 0; byte < sizeof(offset); ++byte)
        bytes[kPool + slot * sizeof(offset) + byte] = static_cast<std::uint8_t>(offset >> (byte * kByteBits));
      bytes[offset++] = static_cast<std::uint8_t>(name.size() * 2 + 1); // ASCII Panda string header.
      for (const auto letter : name) bytes[offset++] = static_cast<std::uint8_t>(letter);
      bytes[offset++] = 0;
      ++slot;
    }
    if (missing == MissingCell::kDelegateName) bytes[161] = 'p';
    if (missing == MissingCell::kDelegatePool)
      std::fill(bytes.begin() + kPool + 3 * sizeof(offset), bytes.begin() + kPool + 4 * sizeof(offset), 0);
  }
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
      (shape == ParentShape::kNormal || shape == ParentShape::kReceiverCapture) ? Append(bytes, 2, 3, {
      Op(abc::Opcode::kLDA_V8), kNext, Op(abc::Opcode::kCALLARG0_IMM8), 0,
      Op(abc::Opcode::kJEQZ_IMM8), 33,
      Op(abc::Opcode::kNEWLEXENV_IMM8), 1,
      Op(shape == ParentShape::kReceiverCapture ? abc::Opcode::kLDTHIS : abc::Opcode::kLDA_V8),
      shape == ParentShape::kReceiverCapture ? Op(abc::Opcode::kNOP) : kRun,
      Op(shape == ParentShape::kReceiverCapture ? abc::Opcode::kNOP : abc::Opcode::kCALLARG0_IMM8),
      shape == ParentShape::kReceiverCapture ? Op(abc::Opcode::kNOP) : std::uint8_t{0},
      Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
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
    else if (child_shape == ChildShape::kAwaitArguments)
      code.insert(code.end(), {Op(abc::Opcode::kGETUNMAPPEDARGS), Op(abc::Opcode::kNOP)});
    else if (await_self || child_shape == ChildShape::kAwaitThis)
      code.insert(code.end(), {Op(await_self ? abc::Opcode::kLDFUNCTION : abc::Opcode::kLDTHIS),
          Op(abc::Opcode::kNOP)});
    else if (child_shape == ChildShape::kAwaitReceiver)
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 5});
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
      // Resume temporaries either use v6/v7 or reuse v1/v2; neither may alias
      // the pending completion. Both layouts must survive the second suspend.
      constexpr std::size_t kInputOperand = 7, kCleanupOperand = 62,
                            kGuardBranchOperand = 54, kCleanupEnd = 66;
      const std::uint8_t locals = finally_reuse ? 6 : 8;
      const std::uint8_t result = finally_reuse ? 1 : 6, mode = finally_reuse ? 2 : 7;
      code[kInputOperand] = locals + kImplicitArguments;
      code[kCleanupOperand] = locals + kImplicitArguments + 1;
      code[kGuardBranchOperand] = 43;  // Skip call and its 30-byte await protocol.
      code.insert(code.begin() + kCleanupEnd, {
        Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
        Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0,
        Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR),
        Op(abc::Opcode::kSTA_V8), result, Op(abc::Opcode::kLDA_V8), 0,
        Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), mode,
        Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
        Op(abc::Opcode::kEQ_IMM8_V8), 0, mode, Op(abc::Opcode::kJEQZ_IMM8), 6,
        Op(abc::Opcode::kLDA_V8), result,
        static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
        Op(abc::Opcode::kLDA_V8), result});
      return AppendCode(bytes, locals, getter_parameters, code,
          {6, 32, 1, 0, 43, 2, 3, 116, 1, 0, 119, 5}, 2);
    }
    return AppendCode(bytes, 6, getter_parameters, code,
        {6, 32, 1, 0, 43, 2, 3, 86, 1, 0, 89, 5}, 2);
  };
  std::uint32_t generator_yield_pc = 0;
  const auto generator_getter = [&] {
    // OH7 GeneratorFunctionBuilder: initial suspend, then one source yield.
    // Both handshakes distinguish RETURN, THROW and NEXT with the same slots.
    std::vector<std::uint8_t> code;
    // FunctionRegScope assigns mandatory/user parameters to v0..v3; the
    // physical ABI inputs follow locals. CopyFunctionArguments connects them.
    const bool shared_body_yield = yield_finally && yield_finalizer == YieldFinalizer::kSharedBodyYield;
    const std::uint8_t locals = yield_delegate ? test::kDelegateLocals : suspended_return ? 11 : yield_frame_copy ? 9 : shared_body_yield ? 8 : yield_finally ? 6 : 5;
    const std::uint8_t state = yield_frame_copy ? 4 : 0, mode = yield_frame_copy ? 5 : 1,
        result = yield_frame_copy ? 6 : 2, value = yield_frame_copy ? 7 : 3, done = yield_frame_copy ? 8 : 4;
    const std::uint8_t function = yield_frame_copy ? 0 : locals, parameter = locals + kImplicitArguments;
    if (yield_lexical && missing != MissingCell::kYieldPreludeCellNoEnv) code.insert(code.end(), {Op(abc::Opcode::kNEWLEXENV_IMM8), 1});
    const bool private_copy = missing == MissingCell::kYieldPreludeResult || missing == MissingCell::kYieldPreludeMode;
    const std::uint8_t private_slot = missing == MissingCell::kYieldPreludeResult ? 2 : 1;
    if (private_copy) code.insert(code.end(), {Op(abc::Opcode::kMOV_V8_V8), private_slot, 8});
    const auto copy = [&](std::uint8_t destination, std::uint8_t source) {
      // OH7 ISA MOV formats: low/high v4 nibbles, then little-endian v8/v16.
      constexpr unsigned kNibbleBits = 4;
      switch (copy_encoding) {
        case CopyEncoding::kV4:
          code.insert(code.end(), {Op(abc::Opcode::kMOV_V4_V4), static_cast<std::uint8_t>(destination | (source << kNibbleBits))});
          break;
        case CopyEncoding::kV8:
          code.insert(code.end(), {Op(abc::Opcode::kMOV_V8_V8), destination, source});
          break;
        case CopyEncoding::kV16:
          code.insert(code.end(), {Op(abc::Opcode::kMOV_V16_V16), destination, 0, source, 0});
          break;
      }
    };
    if (yield_frame_copy) {
      copy(0, locals); copy(1, locals + 1); copy(2, locals + 2);
    }
    for (std::uint32_t i = 0; yield_copy && i < copy_count; ++i)
      copy(missing == MissingCell::kYieldPreludeState ? 0 : 3,
          missing == MissingCell::kYieldPreludeLocal ? 4 : parameter);
    if (missing == MissingCell::kYieldPreludeCallee) copy(0, parameter);
    if (yield_parameter_cell) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8),
        static_cast<std::uint8_t>(missing == MissingCell::kYieldPreludeCellLocal ? 3 : parameter),
        Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
        static_cast<std::uint8_t>(missing == MissingCell::kYieldPreludeCellDepth ? 1 :
            missing == MissingCell::kYieldPreludeCellSlot ? 0x10 : 0)});
    // A second register copy after the cell pair exercises mixed prefix order;
    // the callee's final copy must still establish the same function identity.
    if (child_shape == ChildShape::kYieldParameterFrameCell) copy(0, locals);
    if (missing == MissingCell::kYieldPreludeCall)
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 8, Op(abc::Opcode::kCALLARG0_IMM8), 0});
    const auto entry = static_cast<std::uint32_t>(code.size());
    code.insert(code.end(), {Op(abc::Opcode::kCREATEGENERATOROBJ_V8), function,
        Op(abc::Opcode::kSTA_V8), state, Op(abc::Opcode::kLDUNDEFINED)});
    std::uint32_t first_cleanup_pc = 0, first_payload_pc = 0;
    // HandleCompletion's handleReturn_ guard suppresses repeated outer
    // finalizer injection at this inner yield. Its result/mode slots cannot
    // alias the caller's pending RETURN; see pinned OH7 functionBuilder.cpp.
    const auto suspended_cleanup = [&] {
      std::vector<std::uint8_t> cleanup;
      if (!suspended_return) return cleanup;
      const std::uint8_t inner_result = missing == MissingCell::kYieldInnerResult ? result : 6;
      const std::uint8_t inner_mode = missing == MissingCell::kYieldInnerMode ? mode : 7;
      const auto branch = [&](BranchEncoding encoding, bool returning) {
        const auto width = encoding == BranchEncoding::k8 ? 1U : encoding == BranchEncoding::k16 ? 2U : 4U;
        const bool inverse = missing == (returning ? MissingCell::kYieldInnerReturnBranch : MissingCell::kYieldInnerThrowBranch);
        cleanup.push_back(Op(encoding == BranchEncoding::k8 ? (inverse ? abc::Opcode::kJNEZ_IMM8 : abc::Opcode::kJEQZ_IMM8) :
            encoding == BranchEncoding::k16 ? (inverse ? abc::Opcode::kJNEZ_IMM16 : abc::Opcode::kJEQZ_IMM16) :
            (inverse ? abc::Opcode::kJNEZ_IMM32 : abc::Opcode::kJEQZ_IMM32)));
        const auto delta = 1U + width + 2U + (returning ? 1U : 2U);
        for (unsigned byte = 0; byte < width; ++byte) cleanup.push_back(static_cast<std::uint8_t>(delta >> (byte * kByteBits)));
      };
      const bool suspended_loop = cleanup_control == CleanupControl::kLoop || cleanup_control == CleanupControl::kPostLoop;
      if (suspended_loop && !post_loop)
        cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM32), 0, 0, 0, 0});
      for (std::uint32_t i = 0; i < return_cleanup_count; ++i) {
        cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(parameter + 1),
            Op(abc::Opcode::kCALLARG0_IMM8), 0,
            Op(abc::Opcode::kSTA_V8), 8, Op(abc::Opcode::kLDFALSE), Op(abc::Opcode::kSTA_V8), 9,
            Op(abc::Opcode::kCREATEITERRESULTOBJ_V8_V8), 8, 9,
            Op(abc::Opcode::kSUSPENDGENERATOR_V8), static_cast<std::uint8_t>(missing == MissingCell::kYieldInnerState ? 5 : state), Op(abc::Opcode::kLDA_V8), state,
            Op(abc::Opcode::kRESUMEGENERATOR), Op(abc::Opcode::kSTA_V8), inner_result,
            Op(abc::Opcode::kLDA_V8), state, Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), inner_mode,
            Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kReturn), 0, 0, 0,
            Op(abc::Opcode::kEQ_IMM8_V8), 0, inner_mode});
        branch(resume_encoding.return_mode, true);
        cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDA_V8), inner_result, Op(abc::Opcode::kRETURN),
            Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
            Op(abc::Opcode::kEQ_IMM8_V8), 0, inner_mode});
        branch(resume_encoding.throw_mode, false);
        cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDA_V8), inner_result,
            static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits), Op(abc::Opcode::kLDA_V8), inner_result});
      }
      if (suspended_loop) {
        if (post_loop) {
          cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM32)});
          constexpr std::uint32_t kExitDelta = sizeof(std::uint8_t) + sizeof(std::uint32_t) + kLoopBackBytes;
          for (unsigned byte = 0; byte < sizeof(kExitDelta); ++byte)
            cleanup.push_back(static_cast<std::uint8_t>(kExitDelta >> (byte * kByteBits)));
        }
        const auto back = static_cast<std::uint32_t>(-std::int64_t(cleanup.size()));
        cleanup.push_back(Op(abc::Opcode::kJMP_IMM32));
        for (unsigned byte = 0; byte < sizeof(back); ++byte)
          cleanup.push_back(static_cast<std::uint8_t>(back >> (byte * kByteBits)));
        if (!post_loop) {
          constexpr std::size_t kConditionPc = 2, kBranchOperand = 3;
          const auto forward = static_cast<std::uint32_t>(cleanup.size() - kConditionPc);
          for (unsigned byte = 0; byte < sizeof(forward); ++byte)
            cleanup[kBranchOperand + byte] = static_cast<std::uint8_t>(forward >> (byte * kByteBits));
        }
      }
      cleanup.insert(cleanup.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kSTA_V8), 10,
          Op(abc::Opcode::kLDA_V8), parameter, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 10});
      if (missing == MissingCell::kYieldInnerEntry) {
        const auto resumed = std::find(cleanup.begin(), cleanup.end(), Op(abc::Opcode::kRESUMEGENERATOR));
        test::Check(resumed != cleanup.end());
        const auto delta = static_cast<std::uint32_t>(std::int64_t(resumed - cleanup.begin() + 1) - std::int64_t(cleanup.size()));
        cleanup.push_back(Op(abc::Opcode::kJMP_IMM32));
        for (unsigned byte = 0; byte < sizeof(delta); ++byte) cleanup.push_back(static_cast<std::uint8_t>(delta >> (byte * kByteBits)));
      }
      return cleanup;
    }();
    const auto resume = [&](bool source_yield) {
      const auto branch = [&](BranchEncoding encoding, bool returning) {
        // OH7 ISA jeqz op_imm_8/16/32. Displacements are from the opcode PC.
        const bool inverse = source_yield && missing == (returning ? MissingCell::kYieldReturnBranch : MissingCell::kYieldThrowBranch);
        abc::Opcode opcode; unsigned width;
        switch (encoding) {
          case BranchEncoding::k8: opcode = inverse ? abc::Opcode::kJNEZ_IMM8 : abc::Opcode::kJEQZ_IMM8; width = 1; break;
          case BranchEncoding::k16: opcode = inverse ? abc::Opcode::kJNEZ_IMM16 : abc::Opcode::kJEQZ_IMM16; width = 2; break;
          case BranchEncoding::k32: opcode = inverse ? abc::Opcode::kJNEZ_IMM32 : abc::Opcode::kJEQZ_IMM32; width = 4; break;
        }
        constexpr unsigned kLoadBytes = 2, kReturnBytes = 1, kThrowBytes = 2, kOpcodeBytes = 1;
        const bool wrong_target = source_yield && missing == (returning ? MissingCell::kYieldReturnTarget : MissingCell::kYieldThrowTarget);
        constexpr unsigned kCallCleanupBytes = 4; // LDA v8; CALLARG0 imm8.
        constexpr unsigned kCellArgumentBytes = 5; // LDLEXVAR; STA v8; extra CALLARG1 operand.
        constexpr unsigned kConditionalBytes = 4; // LDLEXVAR; JEQZ imm8.
        const auto cleanup = source_yield && returning && suspended_return ? static_cast<unsigned>(suspended_cleanup.size()) :
            source_yield && returning && injected_return ? (kCallCleanupBytes +
            (yield_finalizer == YieldFinalizer::kInjectedCellRead ? kCellArgumentBytes :
             conditional_return ? kConditionalBytes : 0U) +
            (select_cleanup ? kConditionalBytes + kCallCleanupBytes + 2U : override_bytes + (loop_override ? 2U : 0U))) * return_cleanup_count +
            (loop_cleanup ? kLoopGuardBytes + kLoopBackBytes : 0U) +
            (nested_loop ? kNestedLoopBytes : 0U) +
            (select_cleanup ? override_bytes : 0U) +
            (missing == MissingCell::kYieldReturnReadAccumulator ? kLoadBytes : 0U) : 0U;
        const auto delta = kOpcodeBytes + width + cleanup + kLoadBytes + (wrong_target ? 0 : returning ? kReturnBytes : kThrowBytes);
        code.push_back(Op(opcode));
        for (unsigned byte = 0; byte < width; ++byte) code.push_back(static_cast<std::uint8_t>(delta >> (byte * kByteBits)));
      };
      code.insert(code.end(), {Op(abc::Opcode::kSUSPENDGENERATOR_V8),
          static_cast<std::uint8_t>(source_yield && missing == MissingCell::kYieldState ? 1 : state),
          Op(abc::Opcode::kLDA_V8), state, Op(abc::Opcode::kRESUMEGENERATOR),
          Op(abc::Opcode::kSTA_V8), result, Op(abc::Opcode::kLDA_V8), state,
          Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), mode,
          Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(source_yield && missing == MissingCell::kYieldReturn ?
              analysis::CoroutineResumeMode::kThrow : analysis::CoroutineResumeMode::kReturn), 0, 0, 0,
          Op(abc::Opcode::kEQ_IMM8_V8), 0, mode});
      branch(resume_encoding.return_mode, true);
      const auto loop_begin = static_cast<std::uint32_t>(code.size());
      if (source_yield && suspended_return) {
        if (!first_cleanup_pc) first_cleanup_pc = static_cast<std::uint32_t>(code.size());
        code.insert(code.end(), suspended_cleanup.begin(), suspended_cleanup.end());
      }
      if (source_yield && injected_return && !suspended_return) {
        if (!first_cleanup_pc) first_cleanup_pc = static_cast<std::uint32_t>(code.size());
        if (loop_cleanup && !post_loop) {
          code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM32)});
          constexpr unsigned kLoadCellBytes = 2, kCallBytes = 4;
          constexpr unsigned kBranchBytes = kLoopGuardBytes - kLoadCellBytes;
          const auto delta = condition_call ? kBranchBytes + 2 :
              kBranchBytes + (kCallBytes + (loop_override ? 2U + override_bytes : 0U)) * return_cleanup_count + kLoopBackBytes +
              (nested_loop ? kNestedLoopBytes : 0U) +
              (missing == MissingCell::kYieldCleanupEscape ? 2U : 0U);
          for (unsigned i = 0; i < sizeof(delta); ++i) code.push_back(static_cast<std::uint8_t>(delta >> (i * kByteBits)));
        }
        const auto inner_begin = static_cast<std::uint32_t>(code.size());
        if (nested_loop) {
          // The inner loop's normal exit runs work once before outer continue.
          // Its return/throw leaves both loops without duplicating that effect.
          code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM32)});
          constexpr unsigned kBranchBytes = 5, kCallBytes = 4;
          const auto delta = kBranchBytes + (kCallBytes + 2U + override_bytes) * return_cleanup_count + kLoopBackBytes;
          for (unsigned i = 0; i < sizeof(delta); ++i) code.push_back(static_cast<std::uint8_t>(delta >> (i * kByteBits)));
        }
        if (missing == MissingCell::kYieldReturnReadAccumulator) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), value});
        for (std::uint32_t i = 0; i < return_cleanup_count; ++i) {
          if (select_cleanup)
            code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM8), 8});
          if (conditional_return)
            code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM8),
                static_cast<std::uint8_t>(condition_call ? 4 :
                    missing == MissingCell::kYieldCleanupEscape ? 9 : 6 + override_bytes)});
          if (yield_finalizer == YieldFinalizer::kInjectedCellRead)
            code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kSTA_V8), 5});
          const auto input = missing == MissingCell::kYieldReturnReadResult ? result : missing == MissingCell::kYieldReturnReadMode ? mode :
              missing == MissingCell::kYieldReturnReadState ? state :
              static_cast<std::uint8_t>(yield_finalizer == YieldFinalizer::kInjectedDifferentCallee ? parameter : parameter + 1);
          code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), input});
          if (missing == MissingCell::kYieldReturnWriteResult || missing == MissingCell::kYieldReturnWriteMode ||
              missing == MissingCell::kYieldReturnWriteState)
            code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(missing == MissingCell::kYieldReturnWriteResult ? result :
                missing == MissingCell::kYieldReturnWriteMode ? mode : state)});
          else if (yield_finalizer == YieldFinalizer::kInjectedCellRead) code.insert(code.end(), {Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, 5});
          else code.insert(code.end(), {Op(abc::Opcode::kCALLARG0_IMM8), 0});
          if (select_cleanup)
            code.insert(code.end(), {Op(abc::Opcode::kJMP_IMM8), 6, Op(abc::Opcode::kLDA_V8), parameter,
                Op(abc::Opcode::kCALLARG0_IMM8), 0});
          if (loop_override)
            code.insert(code.end(), {Op(abc::Opcode::kJEQZ_IMM8), static_cast<std::uint8_t>(2U + override_bytes)});
          if (!select_cleanup || i + 1 == return_cleanup_count) {
            if (yield_override == YieldOverride::kReturn) code.push_back(Op(abc::Opcode::kRETURN));
            else if (yield_override == YieldOverride::kThrow)
              code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
          }
        }
        if (nested_loop) {
          const auto delta = static_cast<std::uint32_t>(std::int64_t(inner_begin) - std::int64_t(code.size()));
          code.push_back(Op(abc::Opcode::kJMP_IMM32));
          for (unsigned i = 0; i < sizeof(delta); ++i) code.push_back(static_cast<std::uint8_t>(delta >> (i * kByteBits)));
          code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), parameter, Op(abc::Opcode::kCALLARG0_IMM8), 0});
        }
        if (post_loop) {
          // do { callback/conditional completion; } while (cell).
          // JEQZ skips the back jump, not the mandatory first iteration.
          code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kJEQZ_IMM32)});
          constexpr std::uint32_t kExitDelta = sizeof(std::uint8_t) + sizeof(std::uint32_t) + kLoopBackBytes;
          for (unsigned i = 0; i < sizeof(kExitDelta); ++i)
            code.push_back(static_cast<std::uint8_t>(kExitDelta >> (i * kByteBits)));
        }
        if (loop_cleanup) {
          const auto delta = static_cast<std::uint32_t>(std::int64_t(loop_begin) - std::int64_t(code.size()));
          code.push_back(Op(abc::Opcode::kJMP_IMM32));
          for (unsigned i = 0; i < sizeof(delta); ++i) code.push_back(static_cast<std::uint8_t>(delta >> (i * kByteBits)));
        }
      }
      if (source_yield && injected_return && !first_payload_pc) first_payload_pc = static_cast<std::uint32_t>(code.size());
      code.insert(code.end(), {
          Op(source_yield && missing == MissingCell::kYieldCleanup ? abc::Opcode::kCALLARG0_IMM8 : abc::Opcode::kLDA_V8),
          result, Op(abc::Opcode::kRETURN),
          Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
          Op(abc::Opcode::kEQ_IMM8_V8), 0, mode});
      branch(resume_encoding.throw_mode, false);
      code.insert(code.end(), {
          Op(abc::Opcode::kLDA_V8), result,
          static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
          Op(abc::Opcode::kLDA_V8), result});
    };
    resume(false);
    if (yield_delegate) {
      const bool caught = child_shape == ChildShape::kYieldDelegateCatch;
      std::vector<test::DelegateFixture> delegates;
      for (std::uint32_t i = 0; i < yield_count; ++i)
        delegates.push_back(test::AppendGeneratorDelegate(code,
            resume_encoding.return_mode == BranchEncoding::k32, delegate_truth));
      generator_yield_pc = delegates.front().create;
      auto& first_delegate = delegates.front();
      if (missing == MissingCell::kDelegateNext) code[first_delegate.next_copy + 2] = 6;
      if (missing == MissingCell::kDelegateReceiver) code[first_delegate.call + 2] = 6;
      if (missing == MissingCell::kDelegateState) code[first_delegate.suspend + 1] = 1;
      if (missing == MissingCell::kDelegateMode) code[first_delegate.mode + 2] = 9;
      if (missing == MissingCell::kDelegateTruth) {
        if (delegate_truth == test::DelegateTruth::kRuntime || delegate_truth == test::DelegateTruth::kRuntimeTrue) {
          constexpr auto opposite = static_cast<std::uint16_t>(abc::Opcode::kCALLRUNTIME_ISFALSE_PREF_IMM8);
          code[first_delegate.closed_truth] = static_cast<std::uint8_t>(opposite);
          code[first_delegate.closed_truth + 1] = static_cast<std::uint8_t>(opposite >> kByteBits);
        } else code[first_delegate.closed_truth] = Op(abc::Opcode::kISFALSE);
      }
      if (missing == MissingCell::kDelegateBranch) code[first_delegate.loop_branch + 1] += 1;
      if (missing == MissingCell::kDelegateEntry) {
        code[first_delegate.create - 2] = Op(abc::Opcode::kJMP_IMM8);
        code[first_delegate.create - 1] = 4; // Into the erased iterator store.
      }
      if (missing == MissingCell::kDelegateResultLeak)
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 9});
      code.push_back(Op(abc::Opcode::kRETURN));
      const auto user_catch = static_cast<std::uint32_t>(code.size());
      if (caught) code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 17, Op(abc::Opcode::kLDA_V8), 17, Op(abc::Opcode::kRETURN)});
      if (missing == MissingCell::kDelegateCatchLeak) code[user_catch + 3] = 9;
      const auto completion = static_cast<std::uint32_t>(code.size());
      code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
      std::vector<std::uint8_t> table;
      for (const auto& fixture : delegates) if (missing != MissingCell::kDelegateCloseMissing)
        for (const auto operand : {fixture.close_begin, fixture.close_end - fixture.close_begin +
                                      (missing == MissingCell::kDelegateCloseBoundary ? fixture.close_catch - fixture.close_end : 0U), 1U, 0U,
                                  fixture.close_catch, fixture.close_catch_end - fixture.close_catch}) Uleb(table, operand);
      const auto user_begin = missing == MissingCell::kDelegateUserBoundary ? first_delegate.create + 2 : first_delegate.create;
      if (caught) for (const auto operand : {user_begin, user_catch - user_begin, 1U, 0U,
                                             user_catch, completion - user_catch}) Uleb(table, operand);
      for (const auto operand : {entry, completion - entry, 1U, 0U, completion, 2U}) Uleb(table, operand);
      return AppendEncodedCode(bytes, locals, getter_parameters, code, table,
          (missing == MissingCell::kDelegateCloseMissing ? 0 : yield_count) + (caught ? 2 : 1));
    }
    if (shared_body_yield) code.insert(code.end(), {Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTA_V8), 6});
    const auto try_begin = static_cast<std::uint32_t>(code.size());
    // The initial handshake overwrites this copy. Keeping it in normalized
    // IR without modeling that handshake would fabricate a source parameter.
    if (private_copy) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), private_slot, Op(abc::Opcode::kSTA_V8), 3});
    if (yield_lexical && !yield_parameter_cell) code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(yield_copy ? 3 : 8),
        Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0});
    std::uint32_t first_yield_pc = 0, first_mode_pc = 0;
    const auto yield = [&] {
      code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), value,
          Op(missing == MissingCell::kYieldDone ? abc::Opcode::kLDTRUE : abc::Opcode::kLDFALSE),
          Op(abc::Opcode::kSTA_V8), done});
      generator_yield_pc = static_cast<std::uint32_t>(code.size());
      code.insert(code.end(), {Op(abc::Opcode::kCREATEITERRESULTOBJ_V8_V8), value, done});
      if (!first_yield_pc) {
        first_yield_pc = generator_yield_pc;
        // suspend(2), lda(2), resume(1), sta(2), lda(2) precede mode.
        constexpr std::uint32_t kModeFromSuspend = 9;
        first_mode_pc = static_cast<std::uint32_t>(code.size()) + kModeFromSuspend;
      }
      resume(true);
    };
    if (yield_finally) {
      // Both corridors preserve v5 across cleanup's suspended v1/v2 result.
      // Original try contains only work(); cleanup is outside its handler.
      constexpr std::uint8_t kPending = 5;
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), parameter, Op(abc::Opcode::kCALLARG0_IMM8), 0});
      if (yield_finalizer == YieldFinalizer::kBodyYield || shared_body_yield) yield();
      if (injected_return) for (std::uint32_t i = 0; i < yield_count; ++i) yield();
      if (missing == MissingCell::kYieldCleanupExternalEntry || missing == MissingCell::kYieldCleanupDeadEntry) {
        code[try_begin] = Op(abc::Opcode::kJMP_IMM8);
        code[try_begin + 1] = static_cast<std::uint8_t>(
            (missing == MissingCell::kYieldCleanupDeadEntry ? first_payload_pc : first_cleanup_pc) - try_begin);
      }
      if (shared_body_yield) {
        const auto try_end = static_cast<std::uint32_t>(code.size());
        // Pinned OH7 shared pending-Hole tail. RETURN injected at the protected
        // yield bypasses it in this raw protocol; do not add a native finally.
        code.insert(code.end(), {Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), kPending,
            Op(abc::Opcode::kJMP_IMM8), 4});
        const auto caught = static_cast<std::uint32_t>(code.size());
        code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kPending,
            Op(abc::Opcode::kLDA_V8), 6, Op(abc::Opcode::kSTA_V8), 7,
            Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kEQ_IMM8_V8), 0, 7,
            Op(abc::Opcode::kJEQZ_IMM8), 8, Op(abc::Opcode::kSTA_V8), 6,
            Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(parameter + 1), Op(abc::Opcode::kCALLARG0_IMM8), 0,
            Op(abc::Opcode::kLDA_V8), kPending, Op(abc::Opcode::kSTA_V8), 7,
            Op(abc::Opcode::kLDHOLE), Op(abc::Opcode::kSTA_V8), 6,
            Op(abc::Opcode::kLDA_V8), 7, Op(abc::Opcode::kNOTEQ_IMM8_V8), 0, 6,
            Op(abc::Opcode::kJEQZ_IMM8), 6, Op(abc::Opcode::kLDA_V8), 7,
            static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits),
            Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kRETURN)});
        const auto handler = static_cast<std::uint32_t>(code.size());
        code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
        std::vector<std::uint8_t> table;
        for (const auto operand : {try_begin, try_end - try_begin, 1U, 0U, caught, 2U,
                                  entry, handler - entry, 1U, 0U, handler, 2U}) Uleb(table, operand);
        return AppendEncodedCode(bytes, locals, getter_parameters, code, table, 2);
      }
      code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kPending});
      const auto try_end = static_cast<std::uint32_t>(code.size());
      const auto cleanup = [&](bool exceptional) {
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8),
            static_cast<std::uint8_t>(exceptional && yield_finalizer == YieldFinalizer::kDifferentCallee ? parameter : parameter + 1),
            Op(abc::Opcode::kCALLARG0_IMM8), 0});
        if (exceptional && yield_finalizer == YieldFinalizer::kDifferentInput)
          code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), kPending});
        if (yield_finalizer != YieldFinalizer::kBodyYield && !injected_return) yield();
        const bool replace = yield_finalizer == YieldFinalizer::kReturn || yield_finalizer == YieldFinalizer::kThrow;
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(replace ? result : kPending)});
        if (yield_finalizer == YieldFinalizer::kThrow || (exceptional && yield_finalizer != YieldFinalizer::kReturn))
          code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
        else code.push_back(Op(abc::Opcode::kRETURN));
      };
      cleanup(false);
      if (yield_finalizer == YieldFinalizer::kInjectedUncaught || yield_finalizer == YieldFinalizer::kInjectedConditionalUncaught ||
          yield_finalizer == YieldFinalizer::kInjectedSuspendUncaught) {
        // No user catch: inline RETURN cleanup throws straight out, and a
        // native THROW must not run either RETURN or normal-path cleanup.
        const auto handler = static_cast<std::uint32_t>(code.size());
        code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
        std::vector<std::uint8_t> table;
        for (const auto operand : {entry, handler - entry, 1U, 0U, handler, 2U}) Uleb(table, operand);
        return AppendEncodedCode(bytes, locals, getter_parameters, code, table, 1);
      }
      const auto catch_begin = static_cast<std::uint32_t>(code.size());
      code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), kPending});
      cleanup(true);
      const auto handler = static_cast<std::uint32_t>(code.size());
      code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
      std::vector<std::uint8_t> table;
      for (const auto operand : {try_begin, try_end - try_begin, 1U, 0U, catch_begin, handler - catch_begin,
                                entry, handler - entry, 1U, 0U, handler, 2U}) Uleb(table, operand);
      return AppendEncodedCode(bytes, locals, getter_parameters, code, table, 2);
    }
    for (std::uint32_t occurrence = 0; occurrence < yield_count; ++occurrence) {
      const auto body_pc = code.size();
      code.insert(code.end(), {Op(child_shape == ChildShape::kYieldLexical || yield_parameter_cell ? abc::Opcode::kLDLEXVAR_IMM4_IMM4 : abc::Opcode::kLDA_V8),
          static_cast<std::uint8_t>(yield_parameter_cell ? 0 : child_shape == ChildShape::kYieldLexical ? 1 : yield_copy ? 3 : parameter)});
      if (child_shape == ChildShape::kYieldCall || yield_catch) code.insert(code.end(), {Op(abc::Opcode::kCALLARG0_IMM8), 0});
      yield();
      if (missing == MissingCell::kYieldEntry) {
        // Enter the retained resume-result store, bypassing the suspension.
        constexpr std::size_t kResultStoreFromSuspend = 5;
        code[body_pc] = Op(abc::Opcode::kJMP_IMM8);
        code[body_pc + 1] = static_cast<std::uint8_t>(generator_yield_pc + 3 + kResultStoreFromSuspend - body_pc);
      }
      if (child_shape == ChildShape::kYieldLoop || child_shape == ChildShape::kYieldCatchLoop) {
        const auto delta = static_cast<std::int32_t>(body_pc) - static_cast<std::int32_t>(code.size() + 2);
        code.insert(code.end(), {Op(abc::Opcode::kJEQZ_IMM8), 4, Op(abc::Opcode::kJMP_IMM8),
            static_cast<std::uint8_t>(delta)});
      }
    }
    if (missing == MissingCell::kYieldPreludeEntry)
      code.insert(code.end(), {Op(abc::Opcode::kJMP_IMM8), static_cast<std::uint8_t>(-static_cast<std::int32_t>(code.size()))});
    if (yield_closure) code.insert(code.end(), {Op(abc::Opcode::kSTLEXVAR_IMM4_IMM4), 0,
        Op(abc::Opcode::kDEFINEFUNC_IMM8_ID16_IMM8), 0, 3, 0, 0});
    else if (yield_self || child_shape == ChildShape::kYieldThis)
      code.push_back(Op(yield_self ? abc::Opcode::kLDFUNCTION : abc::Opcode::kLDTHIS));
    else if (child_shape == ChildShape::kYieldReceiver || yield_frame_copy)
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(yield_frame_copy ? 2 : 7)});
    else code.insert(code.end(), {Op(child_shape == ChildShape::kYieldResult ? abc::Opcode::kLDA_V8 : abc::Opcode::kLDLEXVAR_IMM4_IMM4),
        static_cast<std::uint8_t>(child_shape == ChildShape::kYieldResult ? 2 : 0)});
    code.push_back(Op(abc::Opcode::kRETURN));
    const auto catch_begin = static_cast<std::uint32_t>(code.size());
    if (yield_catch) {
      const bool overwrite = child_shape == ChildShape::kYieldCatchOverwrite;
      code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), static_cast<std::uint8_t>(overwrite ? 2 : 3)});
      if (child_shape == ChildShape::kYieldCatchYield) {
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 3});
        yield();
      }
      const bool capture = child_shape == ChildShape::kYieldCatchCapture || child_shape == ChildShape::kYieldCatchYield;
      const auto catch_slot = missing == MissingCell::kYieldCatchResult ? 2 :
          missing == MissingCell::kYieldCatchMode ? 1 : overwrite ? 2 : 3;
      code.insert(code.end(), {Op(capture ? abc::Opcode::kLDLEXVAR_IMM4_IMM4 : abc::Opcode::kLDA_V8),
          static_cast<std::uint8_t>(capture ? 0 : catch_slot), Op(abc::Opcode::kRETURN)});
    }
    const auto handler = static_cast<std::uint32_t>(code.size());
    code.insert(code.end(), {static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits)});
    std::vector<std::uint8_t> table;
    if (yield_catch) {
      const auto try_end = missing == MissingCell::kYieldCatchBoundary ? first_mode_pc : catch_begin;
      const auto catch_entry = missing == MissingCell::kYieldCatchEntry ? first_mode_pc : catch_begin;
      for (const auto value : {try_begin, try_end - try_begin, 1U, 0U, catch_entry, handler - catch_entry}) Uleb(table, value);
    }
    for (const auto value : {entry, handler - entry, 1U, 0U, handler, 2U}) Uleb(table, value);
    return AppendEncodedCode(bytes, locals, getter_parameters, code, table, yield_catch ? 2 : 1);
  };
  const auto async_generator_getter = [&] {
    // Pinned AsyncGeneratorFunctionBuilder::{Prepare,ImplicitReturn,CleanUp}.
    // Source captures/callbacks live outside the five OH7 Yield private slots.
    const std::uint8_t kLocals = async_generator_yield ? 12 : 6,
        kCallback = async_generator_yield ? 10 : 4, kCell = async_generator_yield ? 9 : 3;
    const auto completed = static_cast<std::uint8_t>(analysis::GeneratorState::kCompleted);
    std::vector<std::uint8_t> code{
      Op(abc::Opcode::kCREATEASYNCGENERATOROBJ_V8), static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenCallee ? kLocals + 1 : kLocals),
      Op(abc::Opcode::kSTA_V8), 0, Op(abc::Opcode::kLDUNDEFINED),
      Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, Op(abc::Opcode::kLDA_V8), 0,
      Op(abc::Opcode::kRESUMEGENERATOR), Op(abc::Opcode::kSTA_V8), 1,
      Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2,
      Op(abc::Opcode::kLDA_V8), static_cast<std::uint8_t>(kLocals + kImplicitArguments), Op(abc::Opcode::kSTA_V8), kCallback};
    const bool private_read = missing == MissingCell::kAsyncGenState || missing == MissingCell::kAsyncGenResult || missing == MissingCell::kAsyncGenMode;
    const auto user_begin = static_cast<std::uint32_t>(code.size());
    const auto prefixed = [&](abc::Opcode opcode) {
      const auto raw = static_cast<std::uint16_t>(opcode);
      code.insert(code.end(), {static_cast<std::uint8_t>(raw), static_cast<std::uint8_t>(raw >> kByteBits)});
    };
    const auto branch = [&](bool wide = false) {
      const auto encoding = wide ? BranchEncoding::k32 : resume_encoding.throw_mode;
      const unsigned width = encoding == BranchEncoding::k8 ? 1 : encoding == BranchEncoding::k16 ? 2 : 4;
      const auto pc = code.size();
      code.push_back(Op(wide ? abc::Opcode::kJMP_IMM32 : encoding == BranchEncoding::k8 ? abc::Opcode::kJEQZ_IMM8 :
          encoding == BranchEncoding::k16 ? abc::Opcode::kJEQZ_IMM16 : abc::Opcode::kJEQZ_IMM32));
      code.insert(code.end(), width, 0); return std::pair{pc, width};
    };
    const auto patch = [&](auto edge, std::size_t target) {
      const auto delta = static_cast<std::int64_t>(target) - static_cast<std::int64_t>(edge.first);
      test::Check(edge.second != 1 || (delta >= INT8_MIN && delta <= INT8_MAX));
      for (unsigned byte = 0; byte < edge.second; ++byte)
        code[edge.first + 1 + byte] = static_cast<std::uint8_t>(delta >> (byte * kByteBits));
    };
    const auto wait = [&](std::uint8_t result, std::uint8_t mode) {
      code.insert(code.end(), {Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8), 0,
        Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, Op(abc::Opcode::kLDA_V8), 0,
        Op(abc::Opcode::kRESUMEGENERATOR), Op(abc::Opcode::kSTA_V8), result,
        Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), mode,
        Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
        Op(abc::Opcode::kEQ_IMM8_V8), 0, mode});
      const auto edge = branch(); code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), result});
      prefixed(abc::Opcode::kTHROW_PREF_NONE); patch(edge, code.size());
      code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), result});
    };
    std::size_t yield_resume = 0;
    for (std::uint32_t i = 0; i < yield_count; ++i) {
      const auto callback_load = code.size() + 4; // LDLEXVAR/STA precede LDA callback.
      code.insert(code.end(), {
      Op(private_read ? abc::Opcode::kLDA_V8 : abc::Opcode::kLDLEXVAR_IMM4_IMM4),
      static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenResult ? 1 : missing == MissingCell::kAsyncGenMode ? 2 : 0),
      Op(abc::Opcode::kSTA_V8), kCell, Op(abc::Opcode::kLDA_V8), kCallback,
      Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, kCell});
      if (async_generator_await) {
        code.insert(code.end(), {Op(abc::Opcode::kASYNCFUNCTIONAWAITUNCAUGHT_V8),
          static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenAwaitState ? 1 : 0),
          Op(abc::Opcode::kSUSPENDGENERATOR_V8), 0, Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR),
          Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kLDA_V8), 0,
          Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2,
          Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
          Op(abc::Opcode::kEQ_IMM8_V8), 0, static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenAwaitMode ? 1 : 2)});
        const auto branch = code.size();
        const auto encoding = resume_encoding.throw_mode;
        const unsigned width = encoding == BranchEncoding::k8 ? 1 : encoding == BranchEncoding::k16 ? 2 : 4;
        code.push_back(Op(encoding == BranchEncoding::k8 ? abc::Opcode::kJEQZ_IMM8 : encoding == BranchEncoding::k16 ?
            abc::Opcode::kJEQZ_IMM16 : abc::Opcode::kJEQZ_IMM32));
        const unsigned delta = 1 + width + 2 + 2; // Branch + load rejected + prefixed throw.
        for (unsigned byte = 0; byte < width; ++byte)
          code.push_back(static_cast<std::uint8_t>((delta - (missing == MissingCell::kAsyncGenAwaitBranch ? 4 : 0)) >> (byte * kByteBits)));
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 1,
          static_cast<std::uint8_t>(kThrow), static_cast<std::uint8_t>(kThrow >> kByteBits), Op(abc::Opcode::kLDA_V8), 1});
        if (missing == MissingCell::kAsyncGenAwaitEntry) {
          // Redirect the retained callback callee load into the private mode save.
          constexpr unsigned kCompareBytes = 3, kModeConstantBytes = 5;
          const auto mode_save = branch - kCompareBytes - kModeConstantBytes - 2;
          const auto jump_delta = std::int64_t(mode_save) - std::int64_t(callback_load);
          test::Check(jump_delta <= INT8_MAX);
          code[callback_load] = Op(abc::Opcode::kJMP_IMM8); code[callback_load + 1] = static_cast<std::uint8_t>(jump_delta);
        }
      }
      if (async_generator_yield) {
        if (child_shape == ChildShape::kAsyncGeneratorYieldAwait || child_shape == ChildShape::kAsyncGeneratorYieldAwaitCatch) {
          wait(6, 7);
          code.insert(code.end(), {Op(abc::Opcode::kLDLEXVAR_IMM4_IMM4), 0, Op(abc::Opcode::kSTA_V8), kCell,
            Op(abc::Opcode::kLDA_V8), kCallback, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0, kCell});
        }
        // Exact AsyncGeneratorFunctionBuilder::Yield: outbound Await, AsyncYield,
        // injected RETURN Await/DirectReturn, THROW and NEXT continuation.
        const auto begin = code.size(); wait(5, 4);
        code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kLDA_V8), 0,
          Op(abc::Opcode::kSETGENERATORSTATE_IMM8), static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldState ?
              analysis::GeneratorState::kExecuting : analysis::GeneratorState::kSuspendedYield),
          Op(missing == MissingCell::kAsyncYieldDone ? abc::Opcode::kLDTRUE : abc::Opcode::kLDFALSE), Op(abc::Opcode::kSTA_V8), 4,
          Op(abc::Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8), 0, static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldResolve ? 3 : 1), 4});
        yield_resume = code.size();
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kRESUMEGENERATOR), Op(abc::Opcode::kSTA_V8), 3,
          Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kGETRESUMEMODE), Op(abc::Opcode::kSTA_V8), 2,
          Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldReturnMode ?
              analysis::CoroutineResumeMode::kThrow : analysis::CoroutineResumeMode::kReturn), 0, 0, 0,
          Op(abc::Opcode::kEQ_IMM8_V8), 0, 2});
        const auto not_return = branch(); code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 3}); wait(3, 2);
        code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 4, Op(abc::Opcode::kLDTRUE), Op(abc::Opcode::kSTA_V8), 5,
          Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kSETGENERATORSTATE_IMM8),
          static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldReturnState ? analysis::GeneratorState::kSuspendedYield : analysis::GeneratorState::kCompleted),
          Op(abc::Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8), 0, static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldReturnValue ? 3 : 4), 5,
          Op(abc::Opcode::kRETURN)});
        patch(not_return, code.size());
        if (missing == MissingCell::kAsyncYieldReturnTarget) patch(not_return, code.size() + 5);
        code.insert(code.end(), {Op(abc::Opcode::kLDAI_IMM32), static_cast<std::uint8_t>(analysis::CoroutineResumeMode::kThrow), 0, 0, 0,
          Op(abc::Opcode::kEQ_IMM8_V8), 0, 2});
        const auto not_throw = branch(); code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 3});
        prefixed(abc::Opcode::kTHROW_PREF_NONE); patch(not_throw, code.size());
        if (missing == MissingCell::kAsyncYieldThrowTarget) patch(not_throw, code.size() - 2);
        code.insert(code.end(), {Op(abc::Opcode::kLDA_V8), 3,
          Op(abc::Opcode::kLDA_V8), kCallback, Op(abc::Opcode::kCALLARG1_IMM8_V8), 0,
          static_cast<std::uint8_t>(missing == MissingCell::kAsyncYieldLeak ? 4 : 3)});
        if (missing == MissingCell::kAsyncYieldEntry) {
          code[callback_load] = Op(abc::Opcode::kJMP_IMM8);
          const auto delta = std::int64_t(begin + 4) - std::int64_t(callback_load);
          test::Check(delta <= INT8_MAX); code[callback_load + 1] = static_cast<std::uint8_t>(delta);
        }
      }
    }
    const bool caught = child_shape == ChildShape::kAsyncGeneratorAwaitCatch || child_shape == ChildShape::kAsyncGeneratorYieldCatch ||
        child_shape == ChildShape::kAsyncGeneratorYieldAwaitCatch;
    std::uint32_t user_end = 0, user_catch = 0, user_catch_end = 0;
    if (caught) {
      user_end = static_cast<std::uint32_t>(code.size());
      const auto jump = code.size(); code.push_back(Op(abc::Opcode::kJMP_IMM32)); code.insert(code.end(), 4, 0);
      user_catch = static_cast<std::uint32_t>(code.size());
      const std::uint8_t exception = async_generator_yield ? 8 : 5;
      code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), exception, Op(abc::Opcode::kLDA_V8), kCallback,
        Op(abc::Opcode::kCALLARG1_IMM8_V8), 0,
        static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenAwaitCatchResult ? 1 : missing == MissingCell::kAsyncGenAwaitCatchMode ? 2 :
            missing == MissingCell::kAsyncYieldCatchResult ? 3 : missing == MissingCell::kAsyncYieldCatchPrivate ? 4 : exception)});
      user_catch_end = static_cast<std::uint32_t>(code.size());
      const auto delta = static_cast<std::uint32_t>(code.size() - jump);
      for (unsigned byte = 0; byte < 4; ++byte) code[jump + 1 + byte] = static_cast<std::uint8_t>(delta >> (byte * kByteBits));
    }
    code.insert(code.end(), {Op(abc::Opcode::kLDUNDEFINED), Op(abc::Opcode::kSTA_V8), 1,
      Op(missing == MissingCell::kAsyncGenDone ? abc::Opcode::kLDFALSE : abc::Opcode::kLDTRUE), Op(abc::Opcode::kSTA_V8), 2,
      Op(abc::Opcode::kLDA_V8), 0, Op(abc::Opcode::kSETGENERATORSTATE_IMM8),
      static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenComplete ? analysis::GeneratorState::kExecuting : analysis::GeneratorState::kCompleted),
      Op(abc::Opcode::kASYNCGENERATORRESOLVE_V8_V8_V8), 0,
      static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenResolve ? 3 : 1), 2, Op(abc::Opcode::kRETURN)});
    const auto handler = static_cast<std::uint32_t>(code.size());
    constexpr std::uint32_t kFirstSource = 17, kInitialValueStore = 10; // Encoded Prepare widths above.
    if (missing == MissingCell::kAsyncGenEntry || missing == MissingCell::kAsyncGenReturnEntry) {
      code[kFirstSource] = Op(abc::Opcode::kJMP_IMM8);
      const auto target = missing == MissingCell::kAsyncGenEntry ? kInitialValueStore : handler - 1;
      const auto delta = std::int64_t(target) - kFirstSource;
      test::Check(delta >= INT8_MIN && delta <= INT8_MAX);
      code[kFirstSource + 1] = static_cast<std::uint8_t>(delta);
    }
    code.insert(code.end(), {Op(abc::Opcode::kSTA_V8), 1, Op(abc::Opcode::kLDA_V8), 0,
      Op(abc::Opcode::kSETGENERATORSTATE_IMM8), completed, Op(abc::Opcode::kLDA_V8), 1,
      Op(abc::Opcode::kASYNCGENERATORREJECT_V8), static_cast<std::uint8_t>(missing == MissingCell::kAsyncGenReject ? 1 : 0),
      Op(abc::Opcode::kRETURN)});
    std::vector<std::uint8_t> table;
    if (caught) for (const auto operand : {user_begin, static_cast<std::uint32_t>(missing == MissingCell::kAsyncYieldBoundary ? yield_resume : user_end) - user_begin,
        1U, 0U, user_catch, user_catch_end - user_catch}) Uleb(table, operand);
    constexpr std::uint32_t kTryBegin = 4; // Label before initial LDUNDEFINED.
    for (const auto operand : {kTryBegin, handler - kTryBegin, 1U, 0U, handler,
                              static_cast<std::uint32_t>(code.size()) - handler}) Uleb(table, operand);
    return AppendEncodedCode(bytes, kLocals, getter_parameters, code, table, caught ? 2 : 1);
  };
  const auto getter = async_generator ? async_generator_getter() : generator ? generator_getter() : await_finally ? finally_getter() :
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
  index.regions.push_back({kCaller, kCodeStart, {}, {yield_delegate ? 8U : forwarded_writer ? 5U : nested ? 4U : 3U, kPool}, {}, {}});
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
      static_cast<std::uint32_t>(async_arrow ? abc::FunctionKind::kAsyncArrow : abc::FunctionKind::kAsync) << kFunctionKindShift;
  if (generator) index.methods[2].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kGenerator) << kFunctionKindShift;
  if (async_generator) index.methods[2].access_flags =
      static_cast<std::uint32_t>(abc::FunctionKind::kAsyncGenerator) << kFunctionKindShift;
  index.methods[1].name = "capture";
  index.methods[1].name_utf16_length = 7;
  const bool private_self = child_shape == ChildShape::kAwaitSelfPrivate || child_shape == ChildShape::kYieldSelfPrivate;
  index.methods[2].name = private_self ? "get value" : "get";
  index.methods[2].name_utf16_length = private_self ? 9 : 3;
  index.methods[3].name = "set";
  index.methods[3].name_utf16_length = 3;
  const auto lookup =
      std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto identity = source::MakeRouteInputIdentity(
      std::get<corpus::FileFingerprint>(file.Fingerprint()), header);
  if (child_shape == ChildShape::kAwaitArguments || verify_preservation) {
    using namespace source;
    auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(file, header, index, lookup));
    auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(file, header, index, lookup));
    const FunctionContext owner{std::get<CreationTreeContext>(CreationTreeContext::Resolve(
        file, header, index, lookup, roles, sites, sites.Find({kParent}).first, identity))};
    auto input = ReadFunctionFamily(file, header, index, lookup, roles, sites, {1}, identity, {}, nullptr, &owner,
        {}, FamilyClassMode::kReject, nullptr, {37});
    test::Check(std::holds_alternative<FileFamily>(input));
    auto built = BuildFileFamilyTrees(file, header, index, std::get<FileFamily>(std::move(input)),
        owner, {}, nullptr, {}, &lookup);
    test::Check(std::holds_alternative<ConstructedFamily>(built));
    const auto& family = std::get<ConstructedFamily>(built);
    test::Check(family.members.size() == 3);
    test::Check(family.root == FileFamily::kRoot);
    for (std::size_t i = 0; i < family.members.size(); ++i)
      test::Check(family.members[i].tree.owner.value == 37 + i);
    if(verify_preservation) {
      std::size_t checked=0;
      for(const auto& member:family.members) {
        test::Check(member.flow.original!=nullptr);
        checked+=test::CheckPreservedCompletions(member.tree,*member.flow.original);
      }
      test::Check(checked>0);
    } else {
    test::Check(family.members[1].tree.callable == CallableKind::kAsyncArrow &&
        family.members[1].tree.invocation == InvocationForm::kArrowMethod);
    auto forged = family.members[1].tree;
    forged.invocation = InvocationForm::kDeclared;
    test::Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(forged)));
    forged = family.members[1].tree;
    forged.callable = CallableKind::kAsyncFunction;
    test::Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(forged)));
    std::ostringstream unbound;
    test::Check(std::holds_alternative<TreePrintFailure>(PrintFamilyDraft(family, unbound)));
    }
    const HostArgumentSnapshotBinding factory{u"snapshotArguments", ArgumentSnapshotAbi::kV7CurrentSlots};
    GlobalScope globals{}; globals.argument_snapshot_binding = &factory;
    std::ostringstream output;
    auto printed = PrintFamilyDraft(family, output, {}, {}, &globals);
    test::Check(std::holds_alternative<PrintedFamilyDraft>(printed));
    for (const auto& member : std::get<PrintedFamilyDraft>(printed).members) test::Check(!member.incomplete);
    test::Check(unlink(path) == 0);
    return output.str();
  }
  std::ostringstream positions;
  const auto report =
      InspectFamilyValues(file, header, index, lookup, {1}, identity, nullptr,
                          positions, FamilyInspectMode::kSourceDraft);
  test::Check(unlink(path) == 0);
  if (generator && missing != MissingCell::kNone) {
    test::Check(!report.value("draft_prepared", false) && !report.contains("draft_source"));
    test::Check(report.at("failed_method") == kGetter);
    if (missing == MissingCell::kDelegatePool) {
      // Corrupt pool metadata must retain its typed metadata failure.
      test::Check(report.at("phase_input_failure") == source::PhasedFileFailure{abc::MetadataError::kBadString}.index());
    } else test::Check(report.at("phase_input_error") == static_cast<unsigned>(source::FunctionEmitError::kKind));
    return {};
  }
  if (async_generator && missing != MissingCell::kNone) {
    test::Check(!report.value("draft_prepared", false) && !report.contains("draft_source"));
    test::Check(report.at("failed_method") == kGetter);
    test::Check(report.at("phase_input_error") == static_cast<unsigned>(source::FunctionEmitError::kKind));
    return {};
  }
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
  if (generator) {
    bool original_yield = false;
    for (const auto& origin : located["draft_positions"][1]["positions"]["origins"])
      original_yield |= origin.contains("original_pc") && origin["original_pc"] == generator_yield_pc;
    test::Check(original_yield);
    if (yield_finally && yield_finalizer != YieldFinalizer::kBodyYield && yield_finalizer != YieldFinalizer::kSharedBodyYield) {
      const bool matched = yield_finalizer != YieldFinalizer::kDifferentCallee && yield_finalizer != YieldFinalizer::kDifferentInput &&
          yield_finalizer != YieldFinalizer::kBodyYield;
      test::Check((report.at("draft_source").get<std::string>().find("finally") != std::string::npos) == matched);
    }
  }
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
    if (await_self || child_shape == ChildShape::kAwaitThis)
      expected.insert(expected.end() - 1, 36);  // The retained padding NOP also has an origin.
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
  if(argc==2&&(std::string_view(argv[1])=="--preserved-delegate-json"||std::string_view(argv[1])=="--preserved-async-yield-json")) {
    const bool delegate=std::string_view(argv[1])=="--preserved-delegate-json";
    for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
      for(const auto parent:{ParentShape::kNormal,ParentShape::kCatch})for(bool caught:{false,true})
        for(unsigned form=0;form<(delegate?1U:2U);++form) {
          const bool awaited=form!=0;
          const auto child=delegate?(caught?ChildShape::kYieldDelegateCatch:ChildShape::kYieldDelegate):
            awaited?(caught?ChildShape::kAsyncGeneratorYieldAwaitCatch:ChildShape::kAsyncGeneratorYieldAwait):
            caught?ChildShape::kAsyncGeneratorYieldCatch:ChildShape::kAsyncGeneratorYield;
          std::cout<<nlohmann::json{{"caught",caught},{"awaited",awaited},{"catch_parent",parent==ParentShape::kCatch},
            {"count",delegate?1:2},{"source",Emit(profile,parent,child,MissingCell::kNone,false,delegate?1:2,
              CopyEncoding::kV8,1,YieldFinalizer::kPreserve,{},1,YieldOverride::kNone,CleanupControl::kSequence,
              test::DelegateTruth::kNative,true)}}<<'\n';
        }
    return 0;
  }

  if(argc==2&&std::string_view(argv[1])=="--preserved-completions-json") {
    for(const auto profile:{abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301})
      for(const auto parent:{ParentShape::kNormal,ParentShape::kCatch})
        for(const auto child:{ChildShape::kAwaitCapture,ChildShape::kAwaitFinallyReuse,ChildShape::kYieldFinallyReturn})
          std::cout<<nlohmann::json{{"kind",child==ChildShape::kAwaitCapture?0:child==ChildShape::kAwaitFinallyReuse?1:2},
            {"caught",parent==ParentShape::kCatch},{"source",Emit(profile,parent,child,MissingCell::kNone,false,1,
            CopyEncoding::kV8,1,YieldFinalizer::kPreserve,{},1,YieldOverride::kNone,CleanupControl::kSequence,
            test::DelegateTruth::kNative,true)}}<<'\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--async-generator-yield-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true}) for (const bool awaited : {false, true})
          for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32}) {
            const auto child = awaited ? (caught ? ChildShape::kAsyncGeneratorYieldAwaitCatch : ChildShape::kAsyncGeneratorYieldAwait) :
                (caught ? ChildShape::kAsyncGeneratorYieldCatch : ChildShape::kAsyncGeneratorYield);
            std::cout << nlohmann::json{{"source", Emit(profile, shape, child, MissingCell::kNone,
                false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding})},
                {"count", 2}, {"caught", caught}, {"awaited", awaited}, {"catch_parent", shape == ParentShape::kCatch}}.dump() << '\n';
          }
    for (const auto count : {200U, 201U, 402U}) for (const bool caught : {false, true}) for (const bool awaited : {false, true}) {
      const auto child = awaited ? (caught ? ChildShape::kAsyncGeneratorYieldAwaitCatch : ChildShape::kAsyncGeneratorYieldAwait) :
          (caught ? ChildShape::kAsyncGeneratorYieldCatch : ChildShape::kAsyncGeneratorYield);
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, child, MissingCell::kNone,
          false, count, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {BranchEncoding::k32, BranchEncoding::k32})},
          {"count", count}, {"caught", caught}, {"awaited", awaited}, {"catch_parent", false}}.dump() << '\n';
    }
    for (const auto missing : {MissingCell::kAsyncYieldState, MissingCell::kAsyncYieldDone, MissingCell::kAsyncYieldResolve,
        MissingCell::kAsyncYieldReturnMode, MissingCell::kAsyncYieldReturnTarget, MissingCell::kAsyncYieldThrowTarget,
        MissingCell::kAsyncYieldReturnState, MissingCell::kAsyncYieldReturnValue, MissingCell::kAsyncYieldEntry,
        MissingCell::kAsyncYieldLeak, MissingCell::kAsyncYieldCatchResult, MissingCell::kAsyncYieldCatchPrivate, MissingCell::kAsyncYieldBoundary})
      for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32}) {
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kAsyncGeneratorYieldCatch, missing, false, 1,
            CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding});
        std::cout << nlohmann::json{{"rejected", true}}.dump() << '\n';
      }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--async-generator-await-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true}) for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
          std::cout << nlohmann::json{{"source", Emit(profile, shape,
              caught ? ChildShape::kAsyncGeneratorAwaitCatch : ChildShape::kAsyncGeneratorAwait,
              MissingCell::kNone, false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding})},
              {"catch_parent", shape == ParentShape::kCatch}, {"count", 2}, {"caught", caught}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U}) for (const bool caught : {false, true})
      for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
            caught ? ChildShape::kAsyncGeneratorAwaitCatch : ChildShape::kAsyncGeneratorAwait,
            MissingCell::kNone, false, count, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding})},
            {"catch_parent", false}, {"count", count}, {"caught", caught}}.dump() << '\n';
    for (const auto missing : {MissingCell::kAsyncGenAwaitState, MissingCell::kAsyncGenAwaitMode, MissingCell::kAsyncGenAwaitBranch,
        MissingCell::kAsyncGenAwaitEntry, MissingCell::kAsyncGenAwaitCatchResult, MissingCell::kAsyncGenAwaitCatchMode})
      for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32}) {
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kAsyncGeneratorAwaitCatch,
            missing, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding});
        std::cout << nlohmann::json{{"rejected", true}}.dump() << '\n';
      }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--async-generator-prefix-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kAsyncGeneratorPrefix,
            MissingCell::kNone, false, 2)}, {"catch_parent", shape == ParentShape::kCatch}, {"count", 2}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          ChildShape::kAsyncGeneratorPrefix, MissingCell::kNone, false, count)}, {"catch_parent", false}, {"count", count}}.dump() << '\n';
    for (const auto missing : {MissingCell::kAsyncGenCallee, MissingCell::kAsyncGenState, MissingCell::kAsyncGenDone,
        MissingCell::kAsyncGenComplete, MissingCell::kAsyncGenResolve, MissingCell::kAsyncGenReject,
        MissingCell::kAsyncGenResult, MissingCell::kAsyncGenMode, MissingCell::kAsyncGenEntry, MissingCell::kAsyncGenReturnEntry}) {
      Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kAsyncGeneratorPrefix, missing);
      std::cout << nlohmann::json{{"rejected", true}}.dump() << '\n';
    }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-delegate-truth-json") {
    for (const auto truth : {test::DelegateTruth::kRuntime, test::DelegateTruth::kRuntimeTrue, test::DelegateTruth::kRuntimeFalse})
      for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
        for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
          for (const bool caught : {false, true}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
            std::cout << nlohmann::json{{"source", Emit(profile, shape,
                caught ? ChildShape::kYieldDelegateCatch : ChildShape::kYieldDelegate, MissingCell::kNone,
                false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding}, 1,
                YieldOverride::kNone, CleanupControl::kSequence, truth)},
                {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}}.dump() << '\n';
    for (const auto truth : {test::DelegateTruth::kRuntime, test::DelegateTruth::kRuntimeTrue, test::DelegateTruth::kRuntimeFalse})
      for (const auto count : {2U, 201U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
            ChildShape::kYieldDelegateCatch, MissingCell::kNone, false, count, CopyEncoding::kV8, 1,
            YieldFinalizer::kPreserve, {encoding, encoding}, 1, YieldOverride::kNone, CleanupControl::kSequence, truth)},
            {"catch_parent", false}, {"caught", true}, {"count", count}}.dump() << '\n';
    for (const auto truth : {test::DelegateTruth::kRuntime, test::DelegateTruth::kRuntimeTrue, test::DelegateTruth::kRuntimeFalse})
      for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32}) {
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldDelegateCatch,
            MissingCell::kDelegateTruth, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding},
            1, YieldOverride::kNone, CleanupControl::kSequence, truth);
        std::cout << nlohmann::json{{"rejected", true}}.dump() << '\n';
      }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-delegate-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
          std::cout << nlohmann::json{{"source", Emit(profile, shape,
              caught ? ChildShape::kYieldDelegateCatch : ChildShape::kYieldDelegate, MissingCell::kNone,
              false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding})},
              {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}}.dump() << '\n';
    for (const auto count : {2U, 200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          ChildShape::kYieldDelegateCatch, MissingCell::kNone, false, count, CopyEncoding::kV8, 1,
          YieldFinalizer::kPreserve, {encoding, encoding})}, {"catch_parent", false}, {"caught", true}, {"count", count}}.dump() << '\n';
    for (const auto missing : {MissingCell::kDelegateName, MissingCell::kDelegatePool, MissingCell::kDelegateNext,
        MissingCell::kDelegateReceiver, MissingCell::kDelegateState, MissingCell::kDelegateMode, MissingCell::kDelegateBranch,
        MissingCell::kDelegateEntry, MissingCell::kDelegateResultLeak, MissingCell::kDelegateCatchLeak,
        MissingCell::kDelegateCloseBoundary, MissingCell::kDelegateUserBoundary, MissingCell::kDelegateCloseMissing})
      for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32}) {
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldDelegateCatch,
            missing, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {encoding, encoding});
        std::cout << nlohmann::json{{"rejected", true}}.dump() << '\n';
      }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-suspend-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn,
              MissingCell::kNone, false, 2, CopyEncoding::kV8, 1,
              caught ? YieldFinalizer::kInjectedSuspend : YieldFinalizer::kInjectedSuspendUncaught)},
              {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}, {"count", 1}, {"stops", 2}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 1, CopyEncoding::kV8, 1,
          YieldFinalizer::kInjectedSuspend, {encoding, encoding}, count)},
          {"catch_parent", false}, {"caught", true}, {"count", count}, {"stops", 1}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-suspend-pair-json") {
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2, CopyEncoding::kV8, 1,
          YieldFinalizer::kInjectedSuspend, {encoding, encoding}, count)},
          {"catch_parent", false}, {"caught", true}, {"count", count}, {"stops", 2}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-return-suspend-loop-json" ||
                   std::string_view(argv[1]) == "--yield-return-suspend-post-loop-json")) {
    const bool post = std::string_view(argv[1]) == "--yield-return-suspend-post-loop-json";
    const auto control = post ? CleanupControl::kPostLoop : CleanupControl::kLoop;
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn,
              MissingCell::kNone, false, 2, CopyEncoding::kV8, 1,
              caught ? YieldFinalizer::kInjectedSuspend : YieldFinalizer::kInjectedSuspendUncaught,
              {}, 1, YieldOverride::kNone, control)},
              {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}, {"count", 1}, {"stops", 2},
              {"loop", true}, {"post", post}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 1, CopyEncoding::kV8, 1,
          YieldFinalizer::kInjectedSuspend, {encoding, encoding}, count, YieldOverride::kNone, control)},
          {"catch_parent", false}, {"caught", true}, {"count", count}, {"stops", 1}, {"loop", true}, {"post", post}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-suspend-negative") {
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto missing : {MissingCell::kYieldInnerState, MissingCell::kYieldInnerResult, MissingCell::kYieldInnerMode,
                                MissingCell::kYieldInnerReturnBranch, MissingCell::kYieldInnerThrowBranch, MissingCell::kYieldInnerEntry})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            missing, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedSuspend, {encoding, encoding});
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-return-loop-override-json" ||
                   std::string_view(argv[1]) == "--yield-return-nested-loop-override-json" ||
                   std::string_view(argv[1]) == "--yield-return-post-loop-override-json")) {
    const bool nested = std::string_view(argv[1]) == "--yield-return-nested-loop-override-json";
    const bool post = std::string_view(argv[1]) == "--yield-return-post-loop-override-json";
    const auto cleanup = nested ? CleanupControl::kNestedLoopOverride : post ? CleanupControl::kPostLoopOverride : CleanupControl::kLoopOverride;
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true}) for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
              CopyEncoding::kV8, 1, caught ? YieldFinalizer::kInjectedReturn : YieldFinalizer::kInjectedUncaught,
              {}, 1, overriding, cleanup)}, {"catch_parent", shape == ParentShape::kCatch},
              {"caught", caught}, {"throwing", overriding == YieldOverride::kThrow}, {"count", 1}, {"nested", nested}, {"post", post}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            MissingCell::kNone, false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn, {encoding, encoding}, count,
            overriding, cleanup)}, {"catch_parent", false}, {"caught", true},
            {"throwing", overriding == YieldOverride::kThrow}, {"count", count}, {"nested", nested}, {"post", post}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-select-many-json") {
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            MissingCell::kNone, false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn, {encoding, encoding}, count,
            overriding, CleanupControl::kSelect)}, {"throwing", overriding == YieldOverride::kThrow}, {"count", count}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-condition-call-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool loop : {false, true})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
              CopyEncoding::kV8, 1, loop ? YieldFinalizer::kInjectedReturn : YieldFinalizer::kInjectedConditional,
              {}, 1, YieldOverride::kNone, loop ? CleanupControl::kLoopConditionCall : CleanupControl::kConditionCall)},
              {"catch_parent", shape == ParentShape::kCatch}, {"loop", loop}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-select-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true}) for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
              CopyEncoding::kV8, 1, caught ? YieldFinalizer::kInjectedReturn : YieldFinalizer::kInjectedUncaught,
              {}, 1, overriding, CleanupControl::kSelect)}, {"catch_parent", shape == ParentShape::kCatch},
              {"caught", caught}, {"throwing", overriding == YieldOverride::kThrow}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-loop-negative") {
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto missing : {MissingCell::kYieldReturnWriteResult, MissingCell::kYieldCleanupEscape,
                                MissingCell::kYieldCleanupExternalEntry, MissingCell::kYieldReturnReadResult,
                                MissingCell::kYieldReturnReadMode, MissingCell::kYieldReturnReadState})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kYieldFinallyReturn,
            missing, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn,
            {encoding, encoding}, 1, YieldOverride::kNone, CleanupControl::kLoop);
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto missing : {MissingCell::kYieldReturnWriteResult, MissingCell::kYieldReturnReadAccumulator,
                                MissingCell::kYieldCleanupExternalEntry, MissingCell::kYieldReturnReadResult,
                                MissingCell::kYieldReturnReadMode, MissingCell::kYieldReturnReadState})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kYieldFinallyReturn,
            missing, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn,
            {encoding, encoding}, 1, YieldOverride::kNone, CleanupControl::kPostLoop);
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-return-loop-json" ||
                   std::string_view(argv[1]) == "--yield-return-post-loop-json")) {
    const bool post = std::string_view(argv[1]) == "--yield-return-post-loop-json";
    const auto cleanup = post ? CleanupControl::kPostLoop : CleanupControl::kLoop;
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true})
          std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
              CopyEncoding::kV8, 1, caught ? YieldFinalizer::kInjectedReturn : YieldFinalizer::kInjectedUncaught,
              {BranchEncoding::k16, BranchEncoding::k32}, 1, YieldOverride::kNone, cleanup)},
              {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}, {"count", 1}, {"post", post}}.dump() << '\n';
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
          MissingCell::kNone, false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn, {encoding, encoding}, count,
          YieldOverride::kNone, cleanup)}, {"catch_parent", false}, {"caught", true}, {"count", count}, {"post", post}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-override-many-json") {
    for (const auto count : {200U, 201U, 402U}) for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
      for (const bool conditional : {false, true}) for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            MissingCell::kNone, false, 2, CopyEncoding::kV8, 1,
            conditional ? YieldFinalizer::kInjectedConditional : YieldFinalizer::kInjectedReturn,
            {encoding, encoding}, count, overriding)}, {"count", count}, {"conditional", conditional},
            {"throwing", overriding == YieldOverride::kThrow}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-override-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool conditional : {false, true}) for (const bool caught : {false, true})
          for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow}) {
            const auto finalizer = conditional ? (caught ? YieldFinalizer::kInjectedConditional : YieldFinalizer::kInjectedConditionalUncaught) :
                (caught ? YieldFinalizer::kInjectedReturn : YieldFinalizer::kInjectedUncaught);
            std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
                CopyEncoding::kV8, 1, finalizer, {}, 1, overriding)}, {"catch_parent", shape == ParentShape::kCatch},
                {"conditional", conditional}, {"caught", caught}, {"throwing", overriding == YieldOverride::kThrow}}.dump() << '\n';
          }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-conditional-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const bool caught : {false, true})
        std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
            CopyEncoding::kV8, 1, caught ? YieldFinalizer::kInjectedConditional : YieldFinalizer::kInjectedConditionalUncaught)},
            {"catch_parent", shape == ParentShape::kCatch}, {"caught", caught}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-conditional-many-json") {
    for (const auto count : {200U, 201U, 402U})
      for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            MissingCell::kNone, false, 2, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedConditional, {encoding, encoding}, count)}, {"count", count}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-uncaught-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 1,
            CopyEncoding::kV8, 1, YieldFinalizer::kInjectedUncaught)}, {"catch_parent", shape == ParentShape::kCatch}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-cell-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 2,
            CopyEncoding::kV8, 1, YieldFinalizer::kInjectedCellRead)}, {"catch_parent", shape == ParentShape::kCatch}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-sites-json") {
    for (const auto count : {200U, 201U, 402U})
      std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
          MissingCell::kNone, false, count, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn)}, {"count", count}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const auto returning : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
          for (const auto throwing : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
            for (const bool different : {false, true})
              std::cout << nlohmann::json{{"source", Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 1,
                  CopyEncoding::kV8, 1, different ? YieldFinalizer::kInjectedDifferentCallee : YieldFinalizer::kInjectedReturn, {returning, throwing})},
                  {"different", different}, {"catch_parent", shape == ParentShape::kCatch}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-many-json") {
    for (const auto count : {200U, 201U, 402U})
      for (const auto encoding : {BranchEncoding::k16, BranchEncoding::k32})
        std::cout << nlohmann::json{{"source", Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn,
            MissingCell::kNone, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn, {encoding, encoding}, count)}, {"count", count}}.dump() << '\n';
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-return-negative") {
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto overriding : {YieldOverride::kReturn, YieldOverride::kThrow})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kIteration, ChildShape::kYieldFinallyReturn,
            MissingCell::kYieldCleanupDeadEntry, false, 1, CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn,
            {encoding, encoding}, 1, overriding);
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto missing : {MissingCell::kYieldReturnReadResult, MissingCell::kYieldReturnWriteResult,
                                MissingCell::kYieldReturnReadMode, MissingCell::kYieldReturnWriteMode,
                                MissingCell::kYieldReturnReadState, MissingCell::kYieldReturnWriteState, MissingCell::kYieldReturnReadAccumulator})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn, missing, false, 1,
            CopyEncoding::kV8, 1, YieldFinalizer::kInjectedReturn, {encoding, encoding});
    for (const auto encoding : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto missing : {MissingCell::kYieldReturnReadAccumulator, MissingCell::kYieldCleanupEscape, MissingCell::kYieldCleanupExternalEntry})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFinallyReturn, missing, false, 1,
            CopyEncoding::kV8, 1, YieldFinalizer::kInjectedConditional, {encoding, encoding});
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-branch-json") {
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        for (const auto returning : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
          for (const auto throwing : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
            for (const auto child : {ChildShape::kYieldResult, ChildShape::kYieldCatchYield, ChildShape::kYieldCatchLoop,
                                    ChildShape::kYieldParameterFrameCell, ChildShape::kYieldFinallyReturn}) {
              const auto mode = child == ChildShape::kYieldResult ? "result" : child == ChildShape::kYieldCatchYield ? "catch-yield" :
                  child == ChildShape::kYieldCatchLoop ? "catch-loop" : child == ChildShape::kYieldParameterFrameCell ? "parameter-cell" : "finally";
              std::cout << nlohmann::json{{"source", Emit(profile, shape, child, MissingCell::kNone, false, 1,
                  CopyEncoding::kV16, 1, YieldFinalizer::kPreserve, {returning, throwing})},
                  {"mode", mode}, {"catch_parent", shape == ParentShape::kCatch}}.dump() << '\n';
            }
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-branch-negative") {
    for (const auto returning : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
      for (const auto throwing : {BranchEncoding::k8, BranchEncoding::k16, BranchEncoding::k32})
        for (const auto missing : {MissingCell::kYieldReturnBranch, MissingCell::kYieldThrowBranch,
                                  MissingCell::kYieldReturnTarget, MissingCell::kYieldThrowTarget})
          Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldResult, missing, false, 1,
              CopyEncoding::kV8, 1, YieldFinalizer::kPreserve, {returning, throwing});
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-finally-return" ||
                   std::string_view(argv[1]) == "--yield-finally-override-return" ||
                   std::string_view(argv[1]) == "--yield-finally-override-throw" ||
                   std::string_view(argv[1]) == "--yield-finally-different-callee" ||
                   std::string_view(argv[1]) == "--yield-finally-different-input" ||
                   std::string_view(argv[1]) == "--yield-finally-body" || std::string_view(argv[1]) == "--yield-finally-shared-body")) {
    const auto mode = std::string_view(argv[1]);
    const auto finalizer = mode == "--yield-finally-override-return" ? YieldFinalizer::kReturn :
        mode == "--yield-finally-override-throw" ? YieldFinalizer::kThrow :
        mode == "--yield-finally-different-callee" ? YieldFinalizer::kDifferentCallee :
        mode == "--yield-finally-different-input" ? YieldFinalizer::kDifferentInput :
        mode == "--yield-finally-shared-body" ? YieldFinalizer::kSharedBodyYield :
        mode == "--yield-finally-body" ? YieldFinalizer::kBodyYield : YieldFinalizer::kPreserve;
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, ChildShape::kYieldFinallyReturn, MissingCell::kNone, false, 1,
            CopyEncoding::kV8, 1, finalizer) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-copy-many") {
    std::cout << "export const functions = [\n";
    for (const auto count : {200U, 201U, 402U})
      std::cout << Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldCopy,
          MissingCell::kNone, false, 1, CopyEncoding::kV16, count) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-prelude-negative") {
    for (const auto encoding : {CopyEncoding::kV4, CopyEncoding::kV8, CopyEncoding::kV16}) {
      for (const auto missing : {MissingCell::kYieldPreludeCall, MissingCell::kYieldPreludeState,
                                MissingCell::kYieldPreludeLocal, MissingCell::kYieldPreludeEntry,
                                MissingCell::kYieldPreludeResult, MissingCell::kYieldPreludeMode})
        Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldClosure, missing, false, 1, encoding);
      Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldFrameCopy,
          MissingCell::kYieldPreludeCallee, false, 1, encoding);
    }
    for (const auto missing : {MissingCell::kYieldPreludeCellDepth, MissingCell::kYieldPreludeCellSlot,
                              MissingCell::kYieldPreludeCellLocal, MissingCell::kYieldPreludeCellNoEnv})
      Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldParameterCell, missing);
    return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-copy" ||
                   std::string_view(argv[1]) == "--yield-copy4" || std::string_view(argv[1]) == "--yield-copy16" ||
                   std::string_view(argv[1]) == "--yield-closure4" || std::string_view(argv[1]) == "--yield-closure16" ||
                   std::string_view(argv[1]) == "--yield-frame-copy" || std::string_view(argv[1]) == "--yield-frame-copy4" ||
                   std::string_view(argv[1]) == "--yield-frame-copy16" ||
                   std::string_view(argv[1]) == "--yield-parameter-cell" ||
                   std::string_view(argv[1]) == "--yield-parameter-frame-cell" ||
                   std::string_view(argv[1]) == "--yield-parameter-frame-cell4" ||
                   std::string_view(argv[1]) == "--yield-parameter-frame-cell16" ||
                   std::string_view(argv[1]) == "--yield-lexical" || std::string_view(argv[1]) == "--yield-closure")) {
    const auto mode = std::string_view(argv[1]);
    const auto encoding = mode.ends_with("4") ? CopyEncoding::kV4 : mode.ends_with("16") ? CopyEncoding::kV16 : CopyEncoding::kV8;
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, mode.starts_with("--yield-parameter-frame-cell") ? ChildShape::kYieldParameterFrameCell :
            mode == "--yield-parameter-cell" ? ChildShape::kYieldParameterCell :
            mode.starts_with("--yield-frame-copy") ? ChildShape::kYieldFrameCopy :
            mode.starts_with("--yield-copy") ? ChildShape::kYieldCopy :
            mode == "--yield-lexical" ? ChildShape::kYieldLexical : ChildShape::kYieldClosure, MissingCell::kNone, false, 1, encoding) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-many" || std::string_view(argv[1]) == "--yield-catch-many")) {
    std::cout << "export const functions = [\n";
    for (const auto count : {200U, 201U, 402U})
      std::cout << Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal,
          std::string_view(argv[1]) == "--yield-catch-many" ? ChildShape::kYieldCatch : ChildShape::kYieldCapture,
          MissingCell::kNone, false, count) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--yield-capture" || std::string_view(argv[1]) == "--yield-result" ||
                   std::string_view(argv[1]) == "--yield-loop" || std::string_view(argv[1]) == "--yield-call" ||
                   std::string_view(argv[1]) == "--yield-catch" || std::string_view(argv[1]) == "--yield-catch-capture" ||
                   std::string_view(argv[1]) == "--yield-catch-yield" || std::string_view(argv[1]) == "--yield-catch-overwrite" ||
                   std::string_view(argv[1]) == "--yield-catch-loop" ||
                   std::string_view(argv[1]) == "--yield-self" || std::string_view(argv[1]) == "--yield-self-private" ||
                   std::string_view(argv[1]) == "--yield-this" || std::string_view(argv[1]) == "--yield-receiver")) {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, std::string_view(argv[1]) == "--yield-result" ?
            ChildShape::kYieldResult : std::string_view(argv[1]) == "--yield-call" ? ChildShape::kYieldCall :
            std::string_view(argv[1]) == "--yield-loop" ? ChildShape::kYieldLoop :
            std::string_view(argv[1]) == "--yield-catch" ? ChildShape::kYieldCatch :
            std::string_view(argv[1]) == "--yield-catch-capture" ? ChildShape::kYieldCatchCapture :
            std::string_view(argv[1]) == "--yield-catch-yield" ? ChildShape::kYieldCatchYield :
            std::string_view(argv[1]) == "--yield-catch-overwrite" ? ChildShape::kYieldCatchOverwrite :
            std::string_view(argv[1]) == "--yield-catch-loop" ? ChildShape::kYieldCatchLoop :
            std::string_view(argv[1]) == "--yield-self" ? ChildShape::kYieldSelf :
            std::string_view(argv[1]) == "--yield-self-private" ? ChildShape::kYieldSelfPrivate :
            std::string_view(argv[1]) == "--yield-this" ? ChildShape::kYieldThis :
            std::string_view(argv[1]) == "--yield-receiver" ? ChildShape::kYieldReceiver :
            ChildShape::kYieldCapture) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--yield-negative") {
    for (const auto missing : {MissingCell::kYieldState, MissingCell::kYieldDone,
                              MissingCell::kYieldReturn, MissingCell::kYieldEntry, MissingCell::kYieldCleanup})
      Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldCapture, missing);
    for (const auto missing : {MissingCell::kYieldCatchResult, MissingCell::kYieldCatchMode,
                              MissingCell::kYieldCatchBoundary, MissingCell::kYieldCatchEntry})
      Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, ChildShape::kYieldCatch, missing);
    return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--arrow-arguments") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      std::cout << Emit(profile, ParentShape::kNormal, ChildShape::kAwaitArguments, MissingCell::kNone, true) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && std::string_view(argv[1]) == "--arrow-this") {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      std::cout << Emit(profile, ParentShape::kReceiverCapture, ChildShape::kAwaitCapture, MissingCell::kNone, true) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--arrow-capture" ||
                   std::string_view(argv[1]) == "--arrow-closure" ||
                   std::string_view(argv[1]) == "--arrow-catch" ||
                   std::string_view(argv[1]) == "--arrow-finally")) {
    const auto child = std::string_view(argv[1]) == "--arrow-closure" ? ChildShape::kAwaitClosure :
        std::string_view(argv[1]) == "--arrow-catch" ? ChildShape::kAwaitCatch :
        std::string_view(argv[1]) == "--arrow-finally" ? ChildShape::kAwaitFinallyReuse : ChildShape::kAwaitCapture;
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206, abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, child, MissingCell::kNone, true) << ",\n";
    std::cout << "];\n"; return 0;
  }
  if (argc == 2 && (std::string_view(argv[1]) == "--await-capture" ||
                    std::string_view(argv[1]) == "--await-closure" ||
                    std::string_view(argv[1]) == "--await-catch" ||
                    std::string_view(argv[1]) == "--await-finally" ||
                    std::string_view(argv[1]) == "--await-finally-suspend" ||
                    std::string_view(argv[1]) == "--await-finally-reuse" ||
                    std::string_view(argv[1]) == "--await-self" ||
                    std::string_view(argv[1]) == "--await-self-private" ||
                    std::string_view(argv[1]) == "--await-this" ||
                    std::string_view(argv[1]) == "--await-receiver")) {
    std::cout << "export const functions = [\n";
    for (const auto profile : {abc::ProfileId::kDynamic1202, abc::ProfileId::kDynamic1206,
                              abc::ProfileId::kDynamic1301})
      for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
        std::cout << Emit(profile, shape, std::string_view(argv[1]) == "--await-closure" ?
            ChildShape::kAwaitClosure : std::string_view(argv[1]) == "--await-catch" ?
            ChildShape::kAwaitCatch : std::string_view(argv[1]) == "--await-finally" ?
            ChildShape::kAwaitFinally : std::string_view(argv[1]) == "--await-finally-suspend" ?
            ChildShape::kAwaitFinallySuspend : std::string_view(argv[1]) == "--await-finally-reuse" ?
            ChildShape::kAwaitFinallyReuse : std::string_view(argv[1]) == "--await-self" ?
            ChildShape::kAwaitSelf : std::string_view(argv[1]) == "--await-self-private" ?
            ChildShape::kAwaitSelfPrivate : std::string_view(argv[1]) == "--await-this" ?
            ChildShape::kAwaitThis : std::string_view(argv[1]) == "--await-receiver" ?
            ChildShape::kAwaitReceiver : ChildShape::kAwaitCapture) << ",\n";
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
  if (!emit) for (const auto shape : {ParentShape::kIteration, ParentShape::kCatch, ParentShape::kNormal})
    Emit(abc::ProfileId::kDynamic1206, shape, ChildShape::kAwaitFinallyReuse);
  if (!emit) for (const auto child : {ChildShape::kAwaitSelf, ChildShape::kAwaitSelfPrivate,
                                    ChildShape::kAwaitThis, ChildShape::kAwaitReceiver})
    Emit(abc::ProfileId::kDynamic1206, ParentShape::kNormal, child);
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
