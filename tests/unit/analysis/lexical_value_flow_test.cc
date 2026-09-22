#include "abcd/analysis/guarded_reads.h"
#include "analysis/lexical_value_fixture.h"

using namespace abcd;
using namespace analysis;
using namespace abcd::test;

int main() {
  const auto body = Body({0x09, 1, 0, 0x3d, 0, 0x3c, 0, 0x64});
  const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
  IdfQuota quota{UINT64_MAX};
  const auto ssa =
      std::get<RegisterSsa>(RegisterSsa::Build(body, graph, 1, quota));
  const auto environments =
      std::get<EnvironmentFlow>(AnalyzeEnvironments(body, graph, 1));
  const auto escape = std::get<EnvironmentEscape>(
      EnvironmentEscape::Build(body, ssa, environments, 1, quota));
  Check(escape.MayEscape({0}) == false);
  const auto flow =
      std::get<RegisterValueFlow>(RegisterValueFlow::BuildWithLexicals(
          body, ssa, 1, {}, {environments, escape}, quota));
  Check(flow.At(*ssa.Write({3})) ==
        ValueKinds::Hole().Join(ValueKinds::Language()));
  Check(flow.work.cells == 1 && flow.Facts().size() == ssa.Values().size());
  const auto old = std::get<RegisterValueFlow>(
      RegisterValueFlow::Build(body, ssa, 1, {}, quota));
  Check(old.At(*ssa.Write({3})) == ValueKinds::Unknown());
  struct Case {
    std::initializer_list<std::uint8_t> code;
    std::uint32_t load;
    ValueKinds kinds;
    std::size_t cells;
  };
  const auto initialized = ValueKinds::Hole().Join(ValueKinds::Language());
  for (const auto& item :
       {Case{{0x09, 1, 0x3c, 0, 0x64}, 1, ValueKinds::Hole(), 1},
        // A later store affects the category, not the load's reaching value.
        Case{{0x09, 1, 0x3c, 0, 0x61, 0, 0, 0x3d, 0, 0x60, 0, 0x64},
             1,
             initialized,
             1},
        Case{{0x09, 1, 0x3c, 0, 0x3d, 0, 0x64}, 1, ValueKinds::Hole(), 1},
        // Self environment stored in a cell, unobserved and not escaped.
        Case{{0x09, 1, 0x3d, 0, 0x3c, 0, 0x65},
             2,
             ValueKinds::Hole().Join(ValueKinds::Environment()),
             1},
        Case{{0x09, 1, 0x3d, 0, 0x3c, 0, 0x64}, 2, ValueKinds::Unknown(), 0},
        Case{{0x3c, 0, 0x64}, 0, ValueKinds::Unknown(), 0},
        Case{{0x09, 1, 0, 0x3d, 0, 0x33, 0, 0, 0, 0, 0x3c, 0, 0x64},
             4,
             ValueKinds::Unknown(),
             0},
        // Child remains local even when the parent environment escapes.
        Case{{0x09, 1, 0x61, 0, 0x09, 1, 0, 0x3d, 0, 0x3c, 0, 0x60, 0, 0x64},
             5,
             initialized,
             1},
        // Deprecated stores use r0 while acc deliberately holds Hole.
        Case{{0x09, 1, 0, 0x61, 0, 0x70, 0xfc, 0x20, 0, 0, 0x3c, 0, 0x64},
             5,
             initialized,
             1},
        Case{{0xfd, 2, 0xff, 0xff, 0, 0xfd, 0x0d, 0, 0, 0xfe, 0xff, 0xfd, 0x0c,
              0, 0, 0xfe, 0xff, 0x64},
             3,
             initialized,
             1},
        // Allocation is fresh on each iteration; Hole is never dropped.
        Case{{0x09, 1, 0, 0x3d, 0, 0x3c, 0, 0x69, 0x4d, 0xf8},
             3,
             initialized,
             1},
        // Two cells form a dependency cycle with a language seed.
        Case{{0x09, 2, 0x3c, 0x10, 0x3d, 0, 0, 0x3d, 0x10, 0x3c, 0, 0x3d, 0x10,
              0x64},
             5,
             initialized,
             2}}) {
    LexicalValueFixture fixture{Body(item.code)};
    const auto actual = fixture.Values();
    Check(actual.At(*fixture.ssa.Write({item.load})) == item.kinds);
    Check(actual.work.cells == item.cells);
  }
  for (const bool reload : {false, true}) {
    std::vector<std::uint8_t> code{0x09, 1, 0, 0x3d, 0, 0x3c, 0, 0xfe, 9, 0, 0};
    if (reload) code.insert(code.end(), {0x3c, 0});
    code.push_back(0x64);
    LexicalValueFixture fixture{Body(code)};
    const auto categories = fixture.Values();
    const auto refined = std::get<GuardedReads>(
        GuardedReads::Build(fixture.body, fixture.graph, fixture.ssa,
                            categories, fixture.frame_size, fixture.quota));
    Check(refined.At({4}, 0)->kinds == initialized);
    const auto returned = refined.At({reload ? 6U : 5U}, 0);
    Check(returned->kinds == (reload ? initialized : ValueKinds::Language()));
    Check(returned->guard.has_value() == !reload);
  }
  {
    LexicalValueFixture fixture{Body({0x09, 1, 0, 0x3d, 0, 0x09, 1, 0x3c, 0,
                                      0x61, 0, 0x3c, 1, 0x69, 0x3c, 0, 0x64})};
    const auto values = fixture.Values();
    Check(values.work.cells == 2);
    Check(values.At(*fixture.ssa.Write({4})) == ValueKinds::Hole());
    Check(values.At(*fixture.ssa.Write({6})) == initialized);
    Check(values.At(*fixture.ssa.Write({8})) == initialized);
  }
  {
    LexicalValueFixture fixture{
        Body({0x09, 1, 0x60, 0, 0x3d, 0, 0x3c, 0, 0xfe, 9, 0, 0, 0x64})};
    const auto entry = fixture.ssa.Reads({1})[0].value;
    const std::array atoms{ValueKinds::Language(), ValueKinds::Hole(),
                           ValueKinds::Environment(),
                           ValueKinds::OtherInternal()};
    for (std::uint32_t mask = 1; mask < (1U << kValueKindCount); ++mask) {
      ValueKinds supplied, non_hole;
      for (std::size_t bit = 0; bit < atoms.size(); ++bit) {
        if (!(mask & (1U << bit))) continue;
        supplied = supplied.Join(atoms[bit]);
        if (atoms[bit] != ValueKinds::Hole())
          non_hole = non_hole.Join(atoms[bit]);
      }
      const std::array<EntryValueFact, 1> entries{{{entry, supplied}}};
      const auto values = fixture.Values(entries);
      Check(values.At(*fixture.ssa.Write({3})) ==
            supplied.Join(ValueKinds::Hole()));
      const auto refined = std::get<GuardedReads>(
          GuardedReads::Build(fixture.body, fixture.graph, fixture.ssa, values,
                              fixture.frame_size, fixture.quota));
      Check(refined.At({5}, 0)->kinds == non_hole);
    }
  }
  for (std::uint64_t limit = 0; limit < flow.work.units; ++limit) {
    IdfQuota limited{limit};
    Check(std::get<ValueFlowFailure>(
              RegisterValueFlow::BuildWithLexicals(
                  body, ssa, 1, {}, {environments, escape}, limited))
              .kind == ValueFlowError::kWorkLimit);
    Check(limited.Remaining() == 0);
  }
  IdfQuota exact{flow.work.units};
  Check(
      std::get<RegisterValueFlow>(RegisterValueFlow::BuildWithLexicals(
                                      body, ssa, 1, {}, {environments, escape},
                                      exact, {flow.work.peak_bytes}))
          .work.cells == 1);
  Check(exact.Remaining() == 0);
  Check(std::get<ValueFlowFailure>(RegisterValueFlow::BuildWithLexicals(
                                       body, ssa, 1, {}, {environments, escape},
                                       quota, {flow.work.peak_bytes - 1}))
            .kind == ValueFlowError::kResourceLimit);
  auto owned = [] {
    LexicalValueFixture fixture{Body({0x09, 1, 0x3c, 0, 0x64})};
    return fixture.Values();
  }();
  auto moved = std::move(owned);
  Check(!owned.At({0}) && owned.AllocatedBytes() == 0);
  owned = std::move(moved);
  Check(!moved.At({0}) && owned.Facts().back() == ValueKinds::Hole());
  LexicalValueFixture malformed{Body({0x09, 1, 0x3c, 0, 0x64})};
  malformed.environments.bindings[0].binding.slot.value = 1;
  Check(std::get<ValueFlowFailure>(
            RegisterValueFlow::BuildWithLexicals(
                malformed.body, malformed.ssa, malformed.frame_size, {},
                {malformed.environments, malformed.escape}, quota))
            .kind == ValueFlowError::kInvalidLexicals);
  VerifyLexicalValueOracle();
  VerifyLexicalValueScaling();
}
