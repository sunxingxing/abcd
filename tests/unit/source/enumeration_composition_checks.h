#ifndef ABCD_TEST_ENUMERATION_COMPOSITION_CHECKS_H_
#define ABCD_TEST_ENUMERATION_COMPOSITION_CHECKS_H_
#include "../../../src/source/for_in_emission.h"
namespace {
void CheckEnumerationComposition(const abcd::analysis::ValueProgram& graph,
    const abcd::source::ForOfEvaluationInput& source,
    const abcd::source::EvaluationPlan& evaluation) {
  using namespace abcd::source;
  using namespace abcd::analysis;
  test::Check(evaluation.for_in && evaluation.for_in_events && evaluation.for_of);
  const auto& projection = source.ownership.Forwarding();
  const auto build = [&](SemanticBudget budget = {}) {
    return BuildForInEmissionView(graph, source.regions, source.control,
        *evaluation.for_in, *evaluation.for_in_events, budget, &projection);
  };
  const auto result = build();
  test::Check(std::holds_alternative<ForInEmissionView>(result));
  const auto& view = std::get<ForInEmissionView>(result);
  const auto verify = [&](const ForInEmissionView& candidate) {
    return VerifyForInEmissionView(graph, source.regions, source.control,
        *evaluation.for_in, *evaluation.for_in_events, candidate, {}, &projection);
  };
  test::Check(std::holds_alternative<std::uint64_t>(verify(view)));
  std::size_t shared = 0;
  for (const auto& loop : view.loops) {
    const auto* route = std::get_if<ForwardedExceptionRoute>(&loop.acquisition);
    if (!route || route->owner != ForwardingOwner::kProtocolProjection) continue;
    ++shared;
    test::Check(!view.forwarded_blocks[route->handler.value]);
    auto bad = projection;
    bad.exits[route->exit.value].reset();
    test::Check(std::holds_alternative<ForInFailure>(BuildForInEmissionView(graph,
        source.regions, source.control, *evaluation.for_in, *evaluation.for_in_events, {}, &bad)));
    bad = projection;
    bad.regions[projection.blocks[route->handler.value]->value]->payload = ValueId{UINT32_MAX};
    test::Check(std::holds_alternative<ForInFailure>(BuildForInEmissionView(graph,
        source.regions, source.control, *evaluation.for_in, *evaluation.for_in_events, {}, &bad)));
  }
  test::Check(shared > 0);
  test::Check(std::holds_alternative<ForInFailure>(BuildForInEmissionView(graph,
      source.regions, source.control, *evaluation.for_in, *evaluation.for_in_events)));
  auto forged = view;
  for (auto& loop : forged.loops) if (auto* route = std::get_if<ForwardedExceptionRoute>(&loop.acquisition))
    route->owner = ForwardingOwner::kEnumeration;
  test::Check(std::holds_alternative<ForInFailure>(verify(forged)));
  forged = view; forged.forwarded_operations[0] = 0;
  test::Check(std::holds_alternative<ForInFailure>(verify(forged)));
  forged = view; forged.continuations[0] = EnumerationContinuation{ControlId{0}};
  test::Check(std::holds_alternative<ForInFailure>(verify(forged)));
  const auto missing = VerifyPhasedEvaluationPlan(graph, evaluation);
  test::Check(std::get<EvaluationFailure>(missing).evaluation.protocol_context == ProtocolContextCheck::kMissingInput);
  test::Check(std::holds_alternative<std::uint64_t>(VerifyPhasedEvaluationPlan(graph, evaluation, {}, &source)));
  test::Check(std::holds_alternative<ForInEmissionView>(build({SemanticBudget{}.bytes, view.work})));
  test::Check(std::get<ForInFailure>(build({SemanticBudget{}.bytes, view.work - 1})).kind == ForInError::kWorkLimit);
  std::size_t low = 0, high = SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto candidate = build({mid, SemanticBudget{}.work});
    if (std::holds_alternative<ForInEmissionView>(candidate)) high = mid;
    else { test::Check(std::get<ForInFailure>(candidate).kind == ForInError::kResourceLimit); low = mid + 1; }
  }
  test::Check(std::holds_alternative<ForInEmissionView>(build({low, SemanticBudget{}.work})));
  test::Check(low && std::get<ForInFailure>(build({low - 1, SemanticBudget{}.work})).kind == ForInError::kResourceLimit);
  std::cout << "shared-forwarding=" << shared << " work=" << view.work << " bytes=" << low << '\n';
}
}
#endif
