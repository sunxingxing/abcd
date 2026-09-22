#include <algorithm>
#include <random>

#include "analysis/lexical_value_fixture.h"

namespace abcd::test {
void VerifyLexicalValueOracle() {
  using namespace analysis;
  constexpr std::uint32_t kBlocks = 8, kBlockBytes = 7, kPrefixBytes = 3;
  constexpr std::uint8_t kSlots = 4, kRegisters = 4;
  enum class Recipe {
    kHole,
    kLanguage,
    kLoad,
    kSave,
    kCellLoad,
    kCellStore,
    kBoolean,
    kCount
  };
  std::mt19937 random{985206};
  const std::array categories{ValueKinds::Language(), ValueKinds::Hole(),
                              ValueKinds::Environment(), ValueKinds::Unknown()};
  for (std::size_t trial = 0; trial < 500; ++trial) {
    std::vector<std::uint8_t> code{0x09, kSlots, 0};
    for (std::size_t block = 0; block < kBlocks; ++block) {
      const auto reg = static_cast<std::uint8_t>(random() % kRegisters);
      const auto slot = static_cast<std::uint8_t>((random() % kSlots) << 4);
      const auto recipe = static_cast<Recipe>(
          random() % static_cast<std::uint32_t>(Recipe::kCount));
      switch (recipe) {
        case Recipe::kHole:
          code.insert(code.end(), {0x70, 0xd5, 0xd5});
          break;
        case Recipe::kLanguage:
          code.insert(code.end(), {0, 0xd5, 0xd5});
          break;
        case Recipe::kLoad:
          code.insert(code.end(), {0x60, reg, 0xd5});
          break;
        case Recipe::kSave:
          code.insert(code.end(), {0x61, reg, 0xd5});
          break;
        case Recipe::kCellLoad:
          code.insert(code.end(), {0x3c, slot, 0xd5});
          break;
        case Recipe::kCellStore:
          code.insert(code.end(), {0x3d, slot, 0xd5});
          break;
        case Recipe::kBoolean:
          code.insert(code.end(), {0x23, 0xd5, 0xd5});
          break;
        case Recipe::kCount:
          Check(false);
      }
      for (const auto opcode : {0x4f, 0x4d}) {
        const auto delta =
            static_cast<std::int32_t>(kPrefixBytes +
                                      random() % kBlocks * kBlockBytes) -
            static_cast<std::int32_t>(code.size());
        code.insert(code.end(), {static_cast<std::uint8_t>(opcode),
                                 static_cast<std::uint8_t>(delta)});
      }
    }
    LexicalValueFixture fixture{Body(code), kRegisters};
    Check(fixture.escape.MayEscape({0}) == false);
    const auto& ssa = fixture.ssa;
    std::vector<EntryValueFact> entries;
    std::vector<ValueKinds> expected(ssa.Values().size());
    std::array<ValueKinds, kSlots> slots;
    slots.fill(ValueKinds::Hole());
    for (std::uint32_t i = 0; i < expected.size(); ++i) {
      if (!std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
        continue;
      expected[i] = categories[random() % categories.size()];
      entries.push_back({{i}, expected[i]});
    }
    const auto actual = fixture.Values(entries);
    bool changed = true;
    std::size_t passes = 0;
    // Independent full-sweep oracle: direct slot indexing and opcode rules,
    // not the production sparse cell index, dependency edges or queue.
    while (changed) {
      Check(++passes <= kValueKindCount * (expected.size() + kSlots) + 1);
      changed = false;
      const auto join = [&](ValueKinds& target, ValueKinds input) {
        const auto next = target.Join(input);
        changed |= next != target;
        target = next;
      };
      for (std::uint32_t i = 0; i < expected.size(); ++i) {
        const auto& origin = ssa.Values()[i].origin;
        if (std::holds_alternative<MethodEntry>(origin)) continue;
        ValueKinds next;
        if (std::holds_alternative<PhiValue>(origin)) {
          for (const auto& input : ssa.Inputs({i}))
            next = next.Join(expected[input.value.value]);
        } else {
          const auto id = std::get<InstructionValue>(origin).instruction;
          const auto& instruction = fixture.body.instructions[id.value].decoded;
          switch (instruction.opcode) {
            case abc::Opcode::kNEWLEXENV_IMM8:
              next = ValueKinds::Environment();
              break;
            case abc::Opcode::kLDUNDEFINED:
              next = ValueKinds::Language();
              break;
            case abc::Opcode::kLDHOLE:
              next = ValueKinds::Hole();
              break;
            case abc::Opcode::kLDA_V8:
            case abc::Opcode::kSTA_V8:
              next = expected[ssa.Reads(id)[0].value.value];
              break;
            case abc::Opcode::kLDLEXVAR_IMM4_IMM4:
              next = slots[instruction.operands[1].raw_bits];
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
        join(expected[i], next);
      }
      for (std::uint32_t i = 0; i < fixture.body.instructions.size(); ++i) {
        const auto& instruction = fixture.body.instructions[i].decoded;
        if (instruction.opcode != abc::Opcode::kSTLEXVAR_IMM4_IMM4 ||
            !fixture.environments.before[i])
          continue;
        join(slots[instruction.operands[1].raw_bits],
             expected[ssa.Reads({i})[0].value.value]);
      }
    }
    Check(std::ranges::equal(actual.Facts(), expected));
    Check(actual.work.edge_visits <=
          kValueKindCount * actual.work.dependencies);
  }
}
}  // namespace abcd::test
