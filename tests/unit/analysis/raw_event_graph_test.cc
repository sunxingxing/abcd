#include "abcd/analysis/raw_event_graph.h"
#include "../../support/analysis/ssa_fixture.h"
#include <iostream>

using namespace abcd;
using namespace abcd::analysis;
namespace {
constexpr auto kProfile = abc::ProfileId::kDynamic1206;
abc::MethodBody HandlerBody() {
  // CALLARG0, sta v1, jump handler, returnundefined; handler: sta v2, returnundefined.
  auto body = test::Body({0x29, 0, 0x61, 1, 0x4d, 3, 0x65, 0x61, 2, 0x65});
  abc::TryRegion region; region.offset = {0}; region.length = 2; region.handler_count = 1;
  abc::CatchHandler handler; handler.offset = {7}; handler.length = 3;
  body.tries = {region}; body.handlers = {handler};
  return body;
}
void Events() {
  const auto body = HandlerBody();
  RawEventArena arena({}); RawEventGraph graph(body, arena);
  const auto dispatch = graph.Select(kProfile);
  graph.Allocate(); graph.Connect(dispatch, kProfile, 16);
  const auto nodes = graph.Nodes();
  test::Check(nodes.size() == body.instructions.size() * RawEventGraph::kPhases);
  const auto caught = graph.Site({7}) * RawEventGraph::kPhases + 2;
  test::Check(!nodes[0].registers.write && nodes[1].registers.write.has_value());
  test::Check(nodes[0].count == 2 && nodes[0].next[0] == 1 && nodes[0].next[1] == caught);
  test::Check(nodes[caught].phase == RawEventPhase::kCatch);
  test::Check(nodes[caught].registers.write && std::holds_alternative<Accumulator>(*nodes[caught].registers.write));
  test::Check(nodes[caught].next[0] == caught - 2);
  // A normal jump into handler code bypasses the acc-defining Catch entry.
  const auto jump = graph.Site({4}) * RawEventGraph::kPhases;
  test::Check(nodes[jump + 1].next[0] == caught - 2);
  test::Check(nodes[nodes[jump + 1].next[0]].phase == RawEventPhase::kBefore);
  // A register destination kills the old value only at its normal commit.
  const auto store = graph.Site({2}) * RawEventGraph::kPhases;
  test::Check(!nodes[store].registers.write);
  test::Check(nodes[store + 1].registers.write &&
      std::get<abc::RegisterId>(*nodes[store + 1].registers.write) == abc::RegisterId{1});
}
void Extensions() {
  const auto body = test::Body({0xbf, 0x65});  // resumegenerator; returnundefined
  struct Extension final : RawEventExtension {
    bool allow_suspend = false;
    bool resume_effect = true;
    std::optional<RawEventExtensionEffect> Effect(std::size_t i) const override {
      if (i != 0) return {};
      RawEventExtensionEffect effect;
      effect.registers.reads[0] = abc::RegisterId{0};
      effect.registers.read_count = 1;
      effect.registers.write = Accumulator{};
      effect.resume = resume_effect;
      return effect;
    }
    bool Resume(std::size_t i) const override { return allow_suspend && i == 0; }
  };
  Extension extension;
  RawEventArena arena({}); RawEventGraph graph(body, arena);
  bool refused = false;
  try { (void)graph.Nodes(); }
  catch (const RawEventFailure& failure) { refused = failure.kind == RawEventError::kCorrespondence; }
  test::Check(refused);
  const auto dispatch = graph.Select(kProfile);
  graph.Allocate(); graph.Connect(dispatch, kProfile, 16, &extension);
  test::Check(graph.Nodes()[0].phase == RawEventPhase::kResume);
  test::Check(!graph.Nodes()[0].unknown && !graph.Nodes()[0].registers.write);
  test::Check(graph.Nodes()[1].registers.write.has_value());
  RawEventArena ordinary({}); RawEventGraph unowned(body, ordinary);
  const auto other = unowned.Select(kProfile);
  unowned.Allocate(); unowned.Connect(other, kProfile, 16);
  test::Check(unowned.Nodes()[0].unknown);
  refused = false;
  try { graph.Connect(dispatch, kProfile, 16); }
  catch (const RawEventFailure& failure) { refused = failure.kind == RawEventError::kCorrespondence; }
  test::Check(refused);
  const auto suspension = test::Body({0xc3, 0, 0x65});
  extension.allow_suspend = true; extension.resume_effect = false;
  RawEventArena suspended_arena({}); RawEventGraph suspended(suspension, suspended_arena);
  const auto suspend_dispatch = suspended.Select(kProfile);
  suspended.Allocate(); suspended.Connect(suspend_dispatch, kProfile, 16, &extension);
  test::Check(!suspended.Nodes()[0].unknown);
  test::Check(suspended.Nodes()[1].count == 2);
  test::Check(suspended.Nodes()[1].next[0] == RawEventGraph::kPhases);
  test::Check(suspended.Nodes()[1].next[1] == 3);
  extension.allow_suspend = false;
  RawEventArena unknown_arena({}); RawEventGraph unknown(suspension, unknown_arena);
  const auto unknown_dispatch = unknown.Select(kProfile);
  unknown.Allocate(); unknown.Connect(unknown_dispatch, kProfile, 16, &extension);
  test::Check(unknown.Nodes()[0].unknown && unknown.Nodes()[1].count == 0);
  // An exhausted builder must never expose its partially connected nodes.
  RawEventArena exhausted({64 * 1024, 1}); RawEventGraph partial(body, exhausted);
  const auto partial_dispatch = partial.Select(kProfile);
  partial.Allocate(); refused = false;
  try { partial.Connect(partial_dispatch, kProfile, 16, &extension); }
  catch (const RawEventFailure& failure) { refused = failure.kind == RawEventError::kWorkLimit; }
  test::Check(refused); refused = false;
  try { (void)partial.Nodes(); }
  catch (const RawEventFailure& failure) { refused = failure.kind == RawEventError::kCorrespondence; }
  test::Check(refused);

}
void Budgets() {
  for (const auto count : {0u, 200u, 201u, 400u, 800u}) {
    std::vector<std::uint8_t> bytes(count, static_cast<std::uint8_t>(abc::Opcode::kNOP));
    bytes.push_back(0x65);
    const auto body = count ? test::Body(bytes) : HandlerBody();
    const auto run = [&](SemanticBudget budget) {
      RawEventArena arena(budget); RawEventGraph graph(body, arena);
      const auto dispatch = graph.Select(kProfile);
      graph.Allocate(); graph.Connect(dispatch, kProfile, 16);
      return std::pair{arena.Peak(), arena.Work()};
    };
    const auto [peak, work] = run({});
    test::Check(run({peak, work}) == std::pair{peak, work});
    bool bytes_failed = false, work_failed = false;
    try { (void)run({peak - 1, work}); }
    catch (const std::bad_alloc&) { bytes_failed = true; }
    catch (const RawEventFailure& failure) { bytes_failed = failure.kind == RawEventError::kResourceLimit; }
    try { (void)run({peak, work - 1}); }
    catch (const RawEventFailure& failure) { work_failed = failure.kind == RawEventError::kWorkLimit; }
    test::Check(bytes_failed && work_failed);
    std::cout << count << " peak=" << peak << " work=" << work << '\n';
  }
}
}
int main() { Events(); Extensions(); Budgets(); }
