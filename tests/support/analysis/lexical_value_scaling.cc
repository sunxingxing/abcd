#include "analysis/lexical_value_fixture.h"

namespace abcd::test {
void VerifyLexicalValueScaling() {
  using namespace analysis;
  const auto expected = ValueKinds::Hole().Join(ValueKinds::Language());
  for (const std::size_t count : {200, 201, 4096, 65536}) {
    for (const bool distinct : {false, true}) {
      std::vector<std::uint8_t> code{0, 0x61, 0};
      if (!distinct) code.insert(code.end(), {0x09, 1});
      for (std::size_t i = 0; i < count; ++i) {
        if (distinct) code.insert(code.end(), {0x09, 1});
        code.insert(code.end(), {0x60, 0, 0x3d, 0, 0x3c, 0, 0x61, 0});
        if (distinct) code.push_back(0x69);
      }
      code.insert(code.end(), {0x60, 0, 0x64});
      LexicalValueFixture fixture{Body(code)};
      const auto flow = fixture.Values();
      Check(flow.work.cells == (distinct ? count : 1));
      for (const auto& site : fixture.environments.bindings) {
        if (const auto value = fixture.ssa.Write({site.instruction}))
          Check(flow.At(*value) == expected);
      }
      Check(flow.work.changes <=
            kValueKindCount * (fixture.ssa.Values().size() + flow.work.cells));
      Check(flow.work.edge_visits <= kValueKindCount * flow.work.dependencies);
      Check(flow.work.units < 300 * fixture.body.instructions.size());
    }
  }
}
}  // namespace abcd::test
