#ifndef ABCD_ANALYSIS_SEMANTIC_IR_H_
#define ABCD_ANALYSIS_SEMANTIC_IR_H_

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "abcd/analysis/branch_condition.h"
#include "abcd/analysis/constructor_call.h"
#include "abcd/analysis/derived_constructor.h"
#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/global_access.h"
#include "abcd/analysis/hole_guard.h"
#include "abcd/analysis/lexical.h"
#include "abcd/analysis/module_load.h"
#include "abcd/analysis/module_store.h"
#include "abcd/analysis/named_read.h"
#include "abcd/analysis/named_invocation.h"
#include "abcd/analysis/named_write.h"
#include "abcd/analysis/normal_flow.h"
#include "abcd/analysis/own_write.h"
#include "abcd/analysis/object_creation.h"
#include "abcd/analysis/register_effect.h"
#include "abcd/analysis/register_ssa.h"
#include "abcd/analysis/rest_arguments.h"
#include "abcd/analysis/scalar.h"
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
};
constexpr LanguageEffect operator|(LanguageEffect left, LanguageEffect right) {
  return static_cast<LanguageEffect>(static_cast<std::uint16_t>(left) |
                                     static_cast<std::uint16_t>(right));
}
enum class ExceptionalCompletion : std::uint8_t { kImpossible, kMayThrow };
struct SemanticEffects {
  RegisterEffect normal_registers;
  LanguageEffect normal = LanguageEffect::kNone;
  LanguageEffect before_throw = LanguageEffect::kNone;
  ExceptionalCompletion exceptional = ExceptionalCompletion::kImpossible;
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
struct NoOperation {};
struct StringLoad {
  abc::PoolIndex value;
};
using SemanticOperation = std::variant<NoOperation, ScalarAssignment,
                                       NamedRead, NamedWrite, OwnWrite,
                                       ValueWrite, ModuleLoad, HoleGuard,
                                       GlobalAccess, BranchOperation,
                                       JumpOperation, ReturnOperation,
                                       StringLoad, OrdinaryCall, ReceiverCall,
                                       EmptyObjectCreation, BufferedObjectCreation,
                                       ConstructorCall, ModuleStore,
                                       LexicalLoad, LexicalStore,
                                       PushEnvironment, PopEnvironment,
                                       CurrentEnvironment, FunctionCreation,
                                       LoadHole, SuperCheck, RestCopy>;
struct SemanticInstruction {
  abc::CodeOffset pc;
  SemanticOperation operation;
  SemanticEffects effects;
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
};
struct SemanticFailure {
  SemanticError kind;
  abc::CodeOffset pc;
  std::variant<std::monostate, RegisterUseFailure, IdfError,
               DominatorError, SsaError> ssa_cause;
};
inline constexpr std::uint64_t kDefaultSemanticWork = 1'000'000;
struct SemanticBudget {
  std::size_t bytes = 64 * 1024 * 1024;
  std::uint64_t work = kDefaultSemanticWork;
};
using SemanticResult = std::variant<SemanticMethod, SemanticFailure>;

// One shared admission boundary for typed lifting, normal CFG and register
// SSA. It does not claim source equivalence, exception-flow SSA, or that a
// register entry value is JS undefined. Unsupported instructions are fatal to
// this path and may be handled by a separately verified legacy writer.
[[nodiscard]] SemanticResult BuildSemanticMethod(
    const abc::MethodBody& body, abc::ProfileId profile,
    std::uint64_t frame_size, SemanticBudget budget = {});
}  // namespace abcd::analysis
#endif  // ABCD_ANALYSIS_SEMANTIC_IR_H_
