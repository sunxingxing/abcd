#ifndef ABCD_ANALYSIS_SEMANTIC_IR_H_
#define ABCD_ANALYSIS_SEMANTIC_IR_H_

#include "abcd/analysis/non_executable.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/argument_window.h"
#include "abcd/analysis/array_creation.h"
#include "abcd/analysis/await_region.h"
#include "abcd/analysis/yield_region.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/class_creation.h"
#include "abcd/analysis/define_instruction.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/indexed_write.h"
#include "abcd/analysis/iter_result.h"
#include "abcd/analysis/iterator.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/module_store.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/property_delete.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/rest_arguments.h"
#include "abcd/analysis/array_spread.h"
#include "abcd/analysis/scalar.h"
#include "abcd/analysis/this_property.h"
#include "abcd/analysis/object_spread.h"
#include "abcd/analysis/value_write.h"

namespace abcd::analysis {
enum class LanguageEffect : std::uint16_t {
  kNone = 0,
  kHeapRead = 1 << 0,
  kHeapWrite = 1 << 1,
  kUserCode = 1 << 2,
  kModuleRead = 1 << 3,
  kGlobalAccess = 1 << 4,
  kGuard = 1 << 5,
  kAllocation = 1 << 6,
  kModuleWrite = 1 << 7,
  kLexicalRead = 1 << 8,
  kLexicalWrite = 1 << 9,
  kEnvironmentRead = 1 << 10,
  kEnvironmentWrite = 1 << 11,
  kArgumentsRead = 1 << 12,
  kSuspension = 1 << 13,
  kDebugger = 1 << 14,
  kConcurrentNotification = 1 << 15,
};
constexpr LanguageEffect operator|(LanguageEffect left, LanguageEffect right) {
  return static_cast<LanguageEffect>(static_cast<std::uint16_t>(left) |
                                     static_cast<std::uint16_t>(right));
}
// A lazy binding read may run dependency code before returning or throwing.
// This is a semantic effect contract, not permission to erase a Hole guard.
constexpr LanguageEffect ModuleReadEffects(ModuleLoading loading) {
  switch (loading) {
    case ModuleLoading::kOrdinary: return LanguageEffect::kModuleRead;
    case ModuleLoading::kLazy:
      return LanguageEffect::kModuleRead | LanguageEffect::kHeapRead |
          LanguageEffect::kHeapWrite | LanguageEffect::kUserCode | LanguageEffect::kAllocation;
  }
  std::terminate();
}
enum class ExceptionalCompletion : std::uint8_t {
  kImpossible, kMayThrow, kAlwaysThrow
};
// The pinned adapter owns the cause, independently of whether an operation
// throws. Unclassified includes language exceptions and must never be treated
// as resource-only merely because the operation also allocates.
enum class ExceptionCause : std::uint8_t { kUnclassified, kResourceExhaustion };
struct SemanticEffects {
  RegisterEffect normal_registers;
  LanguageEffect normal = LanguageEffect::kNone;
  LanguageEffect before_throw = LanguageEffect::kNone;
  ExceptionalCompletion exceptional = ExceptionalCompletion::kImpossible;
  ExceptionCause exception_cause = ExceptionCause::kUnclassified;
};
struct BranchOperation {
  UnaryExpression predicate;
  abc::CodeOffset target;
};
struct JumpOperation {
  abc::CodeOffset target;
};
struct ReturnOperation {
  ScalarValue value;
};
struct ThrowOperation {
  ScalarValue value;
};
struct NoOperation {};
// Pinned DEBUGGER notifies the debugger without assigning accumulator state.
// Keep an ordered observation point; never canonicalize this to NoOperation.
struct DebuggerOperation {};
// CALLRUNTIME_NOTIFYCONCURRENTRESULT reads frame.function, not its writable
// ABI register copy. It calls TriggerConcurrentCallback(acc, frame.function),
// has no abrupt-completion dispatch and leaves acc unchanged (OH7 interpreter).
enum class NotificationOwner : std::uint8_t { kInvocationFunction };
struct ConcurrentNotification {
  NotificationOwner owner = NotificationOwner::kInvocationFunction;
};
enum class PoolTextKind : std::uint8_t { kString, kBigInt };
struct StringLoad {
  abc::PoolIndex value;
  PoolTextKind kind = PoolTextKind::kString;
};
// Realm-owned object identity, not a mutable globalThis identifier lookup.
struct GlobalObjectLoad {};
// Pinned LDSYMBOL reads GlobalEnv::SymbolFunction, not the mutable global
// object's Symbol property. Source publication requires an intrinsic binding.
struct SymbolConstructorLoad {};
// AOT value candidate; the C++/asm interpreter disagreement is retained in
// this typed operation. Native source requires the independent V7 predicate.
struct NewTargetLoad {};
// Reads VM frame.thisObj, not the writable optional ABI register copy. Derived
// construction can expose Hole here; source use needs callable-owner evidence.
struct FrameThisLoad {};
// Pinned LDFUNCTION/GetFunction reads InterpretedFrame::function, not its
// writable optional ABI register copy. Source use needs a self-binding proof.
struct FrameFunctionLoad {};
// Each execution copies current slots plus invocation-owned extra arguments
// into a fresh object. Window slots are conditional may-reads; the runtime
// actual count and tail are implicit frame inputs, not fabricated registers.
struct ArgumentSnapshot {
  ArgumentWindow window;
};
// RuntimeAsyncFunctionEnter returns a fresh JSAsyncFuncObject retaining a
// distinct promise and generator context, not the promise itself. Neither
// this operation nor a matching register number proves a source async owner.
struct AsyncEnter {};
enum class AsyncSettlement : std::uint8_t { kResolve, kReject };
struct AsyncSettle {
  AsyncSettlement kind;
  ScalarSlot state;
  ScalarSlot value;
};
struct AwaitValue {
  ScalarValue input;
  AwaitRegion origin;
};
struct YieldValue {
  ScalarValue input;
  YieldRegion origin;
};
using SemanticOperation = std::variant<NoOperation, ScalarAssignment,
                                       NamedRead, NamedWrite, OwnWrite,
                                       ValueWrite, ModuleLoad, HoleGuard,
                                       GlobalAccess, BranchOperation,
                                       JumpOperation, ReturnOperation,
                                       StringLoad, OrdinaryCall, ReceiverCall,
                                       EmptyObjectCreation, BufferedObjectCreation, IterResult,
                                       ConstructorCall, ApplyConstructorCall, ModuleStore,
                                       LexicalLoad, LexicalStore,
                                       PushEnvironment, PopEnvironment,
                                       CurrentEnvironment, FunctionCreation,
                                       LoadHole, SuperCheck, RestCopy,
                                       BufferedArrayCreation, AwaitValue, YieldValue,
                                       ThrowOperation, SuperForward, SuperRange, SuperSpread, ClassCreation,
                                       DefineAccessor, IndexedWrite, GlobalObjectLoad, DebuggerOperation,
                                       FrameThisLoad, FrameFunctionLoad, SymbolConstructorLoad,
                                       ArgumentSnapshot, AsyncEnter, AsyncSettle, ArraySpread, ComputedOwnWrite, PropertyDelete, PropertyIterator, NextPropertyName, AcquireIterator, ObjectCheck, ConcurrentNotification, NewTargetLoad, ThisProperty, CopyProperties>;
// Exact VM-enumerator transport only. Does not prove acquisition identity,
// closed use, loop advancement or source admission.
[[nodiscard]] bool AcceptsPropertyEnumerator(const SemanticOperation& operation);
struct SemanticInstruction {
  abc::CodeOffset pc;
  SemanticOperation operation;
  SemanticEffects effects;
  // Normalized control-flow PC stays above; generated positions use this
  // original bytecode provenance when an owning transformation supplied it.
  std::optional<abc::CodeOffset> origin;
};
struct SemanticMethod {
  std::vector<SemanticInstruction> instructions;
  NormalFlow flow;
  RegisterSsa ssa;
  std::size_t allocated_bytes = 0;
};
enum class SemanticError : std::uint8_t {
  kUnsupportedProfile,
  kUnsupportedInstruction,
  kMalformedInstruction,
  kExceptionRegions,
  kFlow,
  kSsa,
  kResourceLimit,
  kNonExecutableInstruction,
};
struct SemanticFailure {
  SemanticError kind;
  abc::CodeOffset pc;
  std::variant<std::monostate, RegisterUseFailure, IdfError,
               DominatorError, SsaError> ssa_cause;
  std::optional<NonExecutableEvidence> non_executable;
};
// Default source recovery has no configured work cutoff. Keep the machine
// counter range for accounting and legacy callers that explicitly request a
// finite quota; memory and input safety remain independently enforced.
inline constexpr std::uint64_t kDefaultSemanticWork = std::numeric_limits<std::uint64_t>::max();
struct SemanticBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  std::uint64_t work = kDefaultSemanticWork;
};
using SemanticResult = std::variant<SemanticMethod, SemanticFailure>;
using InstructionLift = std::variant<SemanticInstruction, SemanticFailure>;

// Same raw-instruction lifting used by BuildSemanticMethod, without normal
// CFG/SSA admission. Does not remove handlers or infer async ownership.
// frame is borrowed for this call and enables implicit argument-slot reads;
// it must describe frame_size exactly. It is not a callable-owner certificate.
[[nodiscard]] InstructionLift LiftSemanticInstruction(
    const abc::BodyInstruction& site, abc::ProfileId profile,
    std::uint64_t frame_size, const abc::FrameLayout* frame = nullptr);

// One shared admission boundary for typed lifting, normal CFG and register
// SSA. It does not claim source equivalence, exception-flow SSA, or that a
// register entry value is JS undefined. Unsupported instructions are fatal to
// this path and may be handled by a separately verified legacy writer.
// AwaitSite entries are sorted, owner-proven async normalization certificates,
// not permissions inferred from raw await opcodes. They change only the source
// operation and its normal register effects, preserving original region PCs.
// Optional frame is borrowed only during construction. Argument snapshots own
// a validated compact window, not a pointer to caller state or captured values.
[[nodiscard]] SemanticResult BuildSemanticMethod(
    const abc::MethodBody& body, abc::ProfileId profile,
    std::uint64_t frame_size, SemanticBudget budget = {},
    std::span<const AwaitSite> awaits = {},
    const abc::FrameLayout* frame = nullptr);
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_IR_H_
