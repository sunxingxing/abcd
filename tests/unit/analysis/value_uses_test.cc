#include "abcd/analysis/value_ir.h"
#include "../../../src/analysis/value_edits.h"

#include <cstdlib>

namespace {
void Check(bool condition) {
  if (!condition) std::abort();
}
}

int main() {
  using namespace abcd::analysis;
  ValueProgram graph;
  graph.blocks.push_back({{0}, 1, {0}, 0});
  graph.values.push_back({EntryDefinition{ParameterEntry{0}},
                           ValueKinds::Language(), UseId{0}});
  graph.values.push_back({OpResult{OpId{0}}, ValueKinds::Language(), {}});
  graph.ops.push_back({ValueBinary{BinaryOperator::kStrictEqual}, {0}, 0,
      {0}, 2, ValueId{1}, LanguageEffect::kNone, LanguageEffect::kNone,
      ExceptionalCompletion::kImpossible, {0}});
  graph.uses.push_back({OpId{0}, OperandRole::kLeft, ReadPurpose::kOperand,
      ValueId{0}, {}, InputRead{{0}, 0}, {}, UseId{1}});
  graph.uses.push_back({OpId{0}, OperandRole::kRight, ReadPurpose::kOperand,
      ValueId{0}, {}, InputRead{{0}, 1}, UseId{0}, {}});
  Check(!VerifyValueUses(graph, graph.uses.size()));
  Check(VerifyValueUses(graph, 0) == ValueGraphError::kResourceLimit);
  graph.uses[1].next = UseId{0};
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kUseLink);
  graph.uses[1].next.reset();
  graph.uses[1].previous.reset();
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kUseLink);
  graph.uses[1].previous = UseId{0};
  graph.uses[1].operand = ValueId{1};
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kUseLink);
  graph.uses[1].operand = ValueId{0};
  graph.ops[0].use_count = 1;
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kUseRange);
  graph.ops[0].use_count = 2;
  graph.ops[0].result.reset();
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kDefinition);
  graph.ops[0].result = ValueId{1};
  graph.origins.push_back({{0}, {0}, {}});
  graph.values.push_back({EntryDefinition{ParameterEntry{1}},
                         ValueKinds::Language(), {}});
  Check(!ReplaceUse(graph, UseId{0}, ValueId{2}));
  Check(!VerifyValueUses(graph, graph.uses.size()));
  Check(graph.values[0].first_use == UseId{1});
  Check(graph.values[2].first_use == UseId{0});
  Check(!ReplaceUse(graph, UseId{1}, ValueId{2}));
  Check(!VerifyValueUses(graph, graph.uses.size()));
  Check(!graph.values[0].first_use);
  graph.ops[0].normal = LanguageEffect::kHeapRead;
  Check(ErasePureOp(graph, OpId{0}) == ValueEditError::kObservable);
  graph.ops[0].normal = LanguageEffect::kNone;
  Check(!ErasePureOp(graph, OpId{0}));
  Check(!VerifyValueUses(graph, graph.uses.size()));
  Check(!graph.values[2].first_use);
  Check(graph.origins[0].disposition == OriginDisposition::kElidedPure);
  Check(ReplaceUse(graph, UseId{0}, ValueId{0}) == ValueEditError::kInvalidUse);
  graph.uses[0].life = ValueLife::kLive;
  Check(VerifyValueUses(graph, graph.uses.size()) == ValueGraphError::kUseOwner);
}
