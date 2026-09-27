#include "abcd/analysis/constant_object.h"
#include "abcd/analysis/isa_catalog.h"

#include <array>
#include <cstdlib>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
struct Case {
  std::array<std::uint8_t, 2> bytes;
  std::size_t size;
  abcd::analysis::ConstantObjectKind kind;
  abcd::analysis::AccumulatorEffect accumulator;
  abcd::analysis::ContextEffect context;
  bool may_throw;
};
}  // namespace

int main() {
  using namespace abcd;
  using namespace analysis;
  // OpenHarmony-v7.0-Release isa.yaml, "constant object loaders" group.
  constexpr std::array<Case, 18> cases{{
      {{0x6a, 0}, 1, ConstantObjectKind::kNaN, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x6b, 0}, 1, ConstantObjectKind::kInfinity, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x00, 0}, 1, ConstantObjectKind::kUndefined, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x01, 0}, 1, ConstantObjectKind::kNull, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0xad, 0}, 1, ConstantObjectKind::kSymbolConstructor, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x6d, 0}, 1, ConstantObjectKind::kGlobalObject, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x02, 0}, 1, ConstantObjectKind::kTrue, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x03, 0}, 1, ConstantObjectKind::kFalse, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x70, 0}, 1, ConstantObjectKind::kHole, AccumulatorEffect::kHole, ContextEffect::kNone, false},
      {{0xfc, 0x00}, 2, ConstantObjectKind::kLexicalEnvironment, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x6e, 0}, 1, ConstantObjectKind::kNewTarget, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x6f, 0}, 1, ConstantObjectKind::kThis, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0x69, 0}, 1, ConstantObjectKind::kPopLexicalEnvironment, AccumulatorEffect::kPreserve, ContextEffect::kPopLexicalEnvironment, false},
      {{0xfc, 0x01}, 2, ConstantObjectKind::kDeprecatedPopLexicalEnvironment, AccumulatorEffect::kPreserve, ContextEffect::kPopLexicalEnvironment, false},
      {{0x6c, 0}, 1, ConstantObjectKind::kUnmappedArguments, AccumulatorEffect::kValue, ContextEffect::kNone, true},
      {{0xae, 0}, 1, ConstantObjectKind::kAsyncFunctionEnter, AccumulatorEffect::kValue, ContextEffect::kNone, true},
      {{0xaf, 0}, 1, ConstantObjectKind::kCurrentFunction, AccumulatorEffect::kValue, ContextEffect::kNone, false},
      {{0xb0, 0}, 1, ConstantObjectKind::kDebugger, AccumulatorEffect::kPreserve, ContextEffect::kDebugger, false},
  }};
  std::size_t group_count = 0;
  for (const auto& entry : PinnedIsaCatalog())
    if (entry.group == IsaGroup::kConstant) ++group_count;
  Check(group_count == cases.size());
  std::array<abc::Opcode, cases.size()> seen{};
  std::size_t seen_count = 0;
  for (const auto& test : cases) {
    const auto decoded = std::get<abc::DecodedInstruction>(
        abc::DecodeInstruction(std::span(test.bytes).first(test.size),
                               abc::ProfileId::kDynamic1301));
    Check(decoded.size == test.size);
    const auto* spec = FindPinnedInstruction(decoded.opcode);
    Check(spec && spec->group == IsaGroup::kConstant);
    for (std::size_t i = 0; i < seen_count; ++i) Check(seen[i] != decoded.opcode);
    seen[seen_count++] = decoded.opcode;
    const auto lifted = LiftConstantObjectInstruction(decoded);
    const auto& operation = std::get<ConstantObjectInstruction>(lifted);
    Check(operation.kind == test.kind);
    Check(operation.accumulator == test.accumulator);
    Check(operation.context == test.context);
    const auto completion =
        test.kind == ConstantObjectKind::kNewTarget
            ? ConstantCompletion::kUnimplemented
            : test.may_throw ? ConstantCompletion::kMayThrow
                             : ConstantCompletion::kNormal;
    Check(operation.completion == completion);
    auto malformed = decoded;
    malformed.operand_count = 1;
    Check(std::get<ConstantObjectError>(
              LiftConstantObjectInstruction(malformed)) ==
          ConstantObjectError::kMalformedInstruction);
  }
  auto unrelated = std::get<abc::DecodedInstruction>(abc::DecodeInstruction(
      std::array<std::uint8_t, 1>{0x64}, abc::ProfileId::kDynamic1301));
  Check(std::get<ConstantObjectError>(
            LiftConstantObjectInstruction(unrelated)) ==
        ConstantObjectError::kUnsupportedOpcode);
}
