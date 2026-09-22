#include <algorithm>
#include <random>

#include "abcd/analysis/register_value_flow.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
void VerifyValueFlow() {
  using namespace analysis;
  constexpr std::uint32_t kBlocks = 8, kBlockBytes = 7, kRegisters = 4;
  enum class Recipe {
    kHole,
    kLanguage,
    kEnvironment,
    kCopy,
    kLoad,
    kStore,
    kBoolean,
    kCount
  };
  std::mt19937 random{64318};
  const std::array categories{ValueKinds::Language(), ValueKinds::Hole(),
                              ValueKinds::Environment(), ValueKinds::Unknown()};
  for (std::size_t trial = 0; trial < 500; ++trial) {
    std::vector<std::uint8_t> code;
    for (std::size_t block = 0; block < kBlocks; ++block) {
      const auto reg = static_cast<std::uint8_t>(random() % kRegisters);
      const auto source = static_cast<std::uint8_t>(random() % kRegisters);
      const auto recipe = static_cast<Recipe>(
          random() % static_cast<std::uint32_t>(Recipe::kCount));
      switch (recipe) {
        case Recipe::kHole:
          code.insert(code.end(), {0x70, 0xd5, 0xd5});
          break;
        case Recipe::kLanguage:
          code.insert(code.end(), {0, 0xd5, 0xd5});
          break;
        case Recipe::kEnvironment:
          code.insert(code.end(), {0x09, 1, 0xd5});
          break;
        case Recipe::kCopy:
          code.insert(code.end(), {0x45, reg, source});
          break;
        case Recipe::kLoad:
          code.insert(code.end(), {0x60, reg, 0xd5});
          break;
        case Recipe::kStore:
          code.insert(code.end(), {0x61, reg, 0xd5});
          break;
        case Recipe::kBoolean:
          code.insert(code.end(), {0x23, 0xd5, 0xd5});
          break;
        case Recipe::kCount:
          Check(false);
      }
      for (const auto opcode : {0x4f, 0x4d}) {
        const auto delta =
            static_cast<std::int32_t>(random() % kBlocks * kBlockBytes) -
            static_cast<std::int32_t>(code.size());
        code.insert(code.end(), {static_cast<std::uint8_t>(opcode),
                                 static_cast<std::uint8_t>(delta)});
      }
    }
    const auto body = Body(code);
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    IdfQuota quota{1000000};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, graph, kRegisters, quota));
    std::vector<EntryValueFact> entries;
    std::vector<ValueKinds> expected(ssa.Values().size());
    for (std::uint32_t i = 0; i < ssa.Values().size(); ++i) {
      if (!std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
        continue;
      const auto kind = categories[random() % categories.size()];
      entries.push_back({{i}, kind});
      expected[i] = kind;
    }
    const auto actual = std::get<RegisterValueFlow>(
        RegisterValueFlow::Build(body, ssa, kRegisters, entries, quota));
    std::size_t passes = 0;
    bool changed = true;
    // Deliberately slow small-graph oracle: full Phi scans and full sweeps,
    // independently describing these test opcodes without production rules.
    while (changed) {
      Check(++passes <= kValueKindCount * expected.size() + 1);
      changed = false;
      for (std::uint32_t i = 0; i < expected.size(); ++i) {
        ValueKinds next;
        const auto& origin = ssa.Values()[i].origin;
        if (std::holds_alternative<MethodEntry>(origin)) continue;
        if (std::holds_alternative<PhiValue>(origin)) {
          for (const auto& input : ssa.Inputs({i}))
            next = next.Join(expected[input.value.value]);
        } else {
          const auto id = std::get<InstructionValue>(origin).instruction;
          switch (body.instructions[id.value].decoded.opcode) {
            case abc::Opcode::kLDHOLE:
              next = ValueKinds::Hole();
              break;
            case abc::Opcode::kLDUNDEFINED:
              next = ValueKinds::Language();
              break;
            case abc::Opcode::kNEWLEXENV_IMM8:
              next = ValueKinds::Environment();
              break;
            case abc::Opcode::kMOV_V8_V8:
            case abc::Opcode::kLDA_V8:
            case abc::Opcode::kSTA_V8:
              next = expected[ssa.Reads(id)[0].value.value];
              break;
            case abc::Opcode::kISTRUE: {
              const auto input = expected[ssa.Reads(id)[0].value.value];
              next = input.IsEmpty()      ? ValueKinds{}
                     : input.IsLanguage() ? ValueKinds::Language()
                                          : ValueKinds::Unknown();
              break;
            }
            default:
              Check(false);
          }
        }
        next = expected[i].Join(next);
        changed |= next != expected[i];
        expected[i] = next;
      }
    }
    Check(std::ranges::equal(actual.Facts(), expected));
    Check(actual.work.changes <= kValueKindCount * expected.size());
    Check(actual.work.edge_visits <=
          kValueKindCount * actual.work.dependencies);
  }
}
}  // namespace abcd::test
