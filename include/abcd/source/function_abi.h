#ifndef ABCD_SOURCE_FUNCTION_ABI_H_
#define ABCD_SOURCE_FUNCTION_ABI_H_

#include <string_view>

#include "abcd/source/source_tree.h"

namespace abcd::source {
// Counts of retained static operand/reference occurrences, NOT invocation or
// runtime read counts. Pure predicate expansion can repeat a source reference
// without adding an IR occurrence; the two counts must not be conflated.
struct AbiReads {
  std::uint64_t operands = 0;
  std::uint64_t references = 0;
};
struct ParameterAbiUse {
  SymbolId symbol;
  AbiReads reads;
  std::uint64_t writes = 0;
};
enum class ParameterLayout : std::uint8_t { kSimple, kLiteralDefaults };
enum class AbiObservation : std::uint8_t { kUnobserved };
constexpr std::string_view ParameterLayoutName(ParameterLayout layout) {
  switch (layout) {
    case ParameterLayout::kSimple: return "simple";
    case ParameterLayout::kLiteralDefaults: return "literal_defaults";
  }
  return {};
}
constexpr std::string_view AbiObservationName(AbiObservation observation) {
  switch (observation) { case AbiObservation::kUnobserved: return "unobserved"; }
  return {};
}
struct FunctionAbi {
  // Formal ordinal order, including unused parameters. These are source
  // symbols, never local-register aliases requiring a second set of lets.
  std::vector<ParameterAbiUse> parameters;
  AbiReads receiver;
  ParameterLayout layout = ParameterLayout::kSimple;
  // Number of formals before the first initializer, not the ABI slot count.
  std::uint32_t length = 0;
  // G1 admission excludes observing these inputs, argument-list operations,
  // dynamic scope and child/capture operations. Later stages must extend the
  // typed census/proof, not silently retain these absence facts.
  AbiObservation callable = AbiObservation::kUnobserved;
  AbiObservation new_target = AbiObservation::kUnobserved;
  AbiObservation arguments = AbiObservation::kUnobserved;
};
}  // namespace abcd::source
#endif
