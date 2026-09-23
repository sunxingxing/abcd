#ifndef ABCD_ANALYSIS_CONSTANT_OBJECT_H_
#define ABCD_ANALYSIS_CONSTANT_OBJECT_H_

#include <variant>

#include "abcd/abc/instruction.h"

namespace abcd::analysis {
enum class ConstantObjectKind {
  kNaN,
  kInfinity,
  kUndefined,
  kNull,
  kSymbolConstructor,
  kGlobalObject,
  kTrue,
  kFalse,
  kHole,
  kLexicalEnvironment,
  kNewTarget,
  kThis,
  kPopLexicalEnvironment,
  kDeprecatedPopLexicalEnvironment,
  kUnmappedArguments,
  kAsyncFunctionEnter,
  kCurrentFunction,
  kDebugger
};
enum class AccumulatorEffect { kValue, kHole, kPreserve };
enum class ContextEffect {
  kNone,
  kPopLexicalEnvironment,
  kDebugger
};
enum class ConstantCompletion { kNormal, kMayThrow, kUnimplemented };
struct ConstantObjectInstruction {
  ConstantObjectKind kind;
  AccumulatorEffect accumulator;
  ContextEffect context;
  ConstantCompletion completion;
};
enum class ConstantObjectError { kUnsupportedOpcode, kMalformedInstruction };
using ConstantObjectResult =
    std::variant<ConstantObjectInstruction, ConstantObjectError>;

// v7 interpreter semantics, not source admission. In particular its LDNEWTARGET
// handler is unimplemented, and deprecated POPLEXENV preserves the accumulator
// despite the ISA metadata declaring an output. Never lower VM sentinels or
// context objects to ordinary JS values without a separate proof.
[[nodiscard]] ConstantObjectResult LiftConstantObjectInstruction(
    const abc::DecodedInstruction& instruction);
}  // namespace abcd::analysis
#endif
