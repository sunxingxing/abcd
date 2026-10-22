#include "../../../src/source/script_scope.h"

#include <cstdlib>
#include <iostream>
using namespace abcd;
using namespace abcd::analysis;
using namespace abcd::source;
namespace {
void Check(bool value) {
  if (!value) std::abort();
}
struct Fixture {
  ValueProgram graph;
  TreeConstants constants;
  Fixture(unsigned count) {
    constants.method = {42};
    constants.value_revision = graph.revision;
    constants.cfg_revision = graph.cfg_revision;
    for (unsigned i = 0; i < count; ++i) {
      auto name = std::string("x") + std::to_string(i);
      constants.names.emplace_back(name.begin(), name.end());
      constants.operations.emplace_back(NameId{i});
      ValueOp op{};
      op.operation = ValueGlobalWrite{GlobalOp::kInitMutable, {i}};
      op.first_use = {i};
      op.use_count = 1;
      op.life = ValueLife::kLive;
      graph.ops.push_back(op);
      OperandUse use{};
      use.operand = SpecialValue::kUndefined;
      graph.uses.push_back(use);
    }
    graph.blocks.push_back({{0}, count, {0}, 0});
  }
};
}  // namespace
int main() {
  // The entry effect precedes a store even when its operation ID is greater:
  // first-store admission must use original edges, never physical op order.
  Fixture reordered(1);
  std::get<ValueGlobalWrite>(reordered.graph.ops[0].operation).operation =
      GlobalOp::kWriteVar;
  reordered.graph.ops[0].block = {1};
  ValueOp observation{};
  observation.operation = ValueDebugger{};
  observation.normal = LanguageEffect::kDebugger;
  observation.block = {0};
  observation.life = ValueLife::kLive;
  reordered.graph.ops.push_back(observation);
  reordered.graph.blocks = {{{1}, 1, {0}, 1}, {{0}, 1, {1}, 0}};
  reordered.graph.edges = {{{0}, {1}, FlowEdgeKind::kFallthrough, 0}};
  Check(std::get<ScriptScopeError>(BuildScriptScope(
            reordered.graph, reordered.constants, {7}, {42},
            SourceContract::kLanguageSourceV9)) == ScriptScopeError::kOrder);

  for (unsigned count : {1, 200, 201, 400, 800}) {
    Fixture input(count);
    const auto build = [&](SemanticBudget budget = {}) {
      return BuildScriptScope(input.graph, input.constants, {7}, {42},
                              SourceContract::kLanguageSourceV9, budget);
    };
    auto result = build();
    Check(std::holds_alternative<ScriptScope>(result));
    auto scope = std::get<ScriptScope>(std::move(result));
    Check(scope.declarations.size() == count);
    Check(std::holds_alternative<std::uint64_t>(
        VerifyScriptScope(input.graph, input.constants, scope, {7},
                          SourceContract::kLanguageSourceV9)));
    auto changed = scope;
    changed.record = {8};
    Check(std::get<ScriptScopeError>(VerifyScriptScope(
              input.graph, input.constants, changed, {7},
              SourceContract::kLanguageSourceV9)) == ScriptScopeError::kOwner);
    changed = scope;
    changed.entry = {43};
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    changed = scope;
    changed.declarations.back().value = SpecialValue::kNull;
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    changed = scope;
    changed.declarations.back().handler = ValueHandler{{{1}, {2}, {3}}, {4}};
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    changed = scope;
    changed.declarations.back().exceptional = ExceptionalCompletion::kMayThrow;
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    changed = scope;
    changed.declarations.pop_back();
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    changed = scope;
    changed.declarations.back().kind = GlobalDeclaration::kConst;
    Check(std::get<ScriptScopeError>(
              VerifyScriptScope(input.graph, input.constants, changed, {7},
                                SourceContract::kLanguageSourceV9)) ==
          ScriptScopeError::kCorrespondence);
    changed = scope;
    changed.declarations.back().name = {static_cast<std::uint32_t>(count + 1)};
    Check(std::holds_alternative<ScriptScopeError>(
        VerifyScriptScope(input.graph, input.constants, changed, {7},
                          SourceContract::kLanguageSourceV9)));
    Check(std::get<ScriptScopeError>(
              BuildScriptScope(input.graph, input.constants, {7}, {42},
                               SourceContract::kLanguageSourceV8)) ==
          ScriptScopeError::kContract);
    std::size_t low = 0, high = 1u << 24;
    while (low < high) {
      auto mid = low + (high - low) / 2;
      if (std::holds_alternative<ScriptScope>(
              build({mid, kDefaultSemanticWork})))
        high = mid;
      else
        low = mid + 1;
    }
    Check(low > 0 && std::holds_alternative<ScriptScope>(
                         build({low, kDefaultSemanticWork})));
    Check(std::get<ScriptScopeError>(build({low - 1, kDefaultSemanticWork})) ==
          ScriptScopeError::kResourceLimit);
    auto work = scope.work;
    Check(std::holds_alternative<ScriptScope>(build({1u << 24, work})));
    Check(std::get<ScriptScopeError>(build({1u << 24, work - 1})) ==
          ScriptScopeError::kWorkLimit);
    std::cout << count << " bytes=" << low << " work=" << work << '\n';
  }
}
