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
enum class AbiObservation : std::uint8_t { kUnobserved, kFrameIdentity };
constexpr std::string_view ParameterLayoutName(ParameterLayout layout) {
  switch (layout) {
    case ParameterLayout::kSimple: return "simple";
    case ParameterLayout::kLiteralDefaults: return "literal_defaults";
  }
  return {};
}
constexpr std::string_view AbiObservationName(AbiObservation observation) {
  switch (observation) {
    case AbiObservation::kUnobserved: return "unobserved";
    case AbiObservation::kFrameIdentity: return "frame_identity";
  }
  return {};
}
struct FunctionAbi {
  // Formal ordinal order, including unused parameters. These are source
  // symbols, never local-register aliases requiring a second set of lets.
  std::vector<ParameterAbiUse> parameters;
  AbiReads receiver;
  // Implicit frame.thisObj observations have no IR operand occurrence.
  std::uint64_t frame_receiver_reads = 0;
  std::uint64_t frame_function_reads = 0;
  std::uint64_t self_references = 0;
  ParameterLayout layout = ParameterLayout::kSimple;
  // Number of formals before the first initializer, not the ABI slot count.
  std::uint32_t length = 0;
  // Frame identity may be observed through a verified immutable self binding.
  // Admission still excludes other callable ABI inputs, argument-list operations and
  // dynamic scope. Child/capture obligations require separate family proofs;
  // they are not established by these per-function absence facts.
  AbiObservation callable = AbiObservation::kUnobserved;
  AbiObservation new_target = AbiObservation::kUnobserved;
  AbiObservation arguments = AbiObservation::kUnobserved;
};
}  // namespace abcd::source
#endif
