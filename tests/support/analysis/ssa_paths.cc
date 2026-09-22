#include <random>

#include "analysis/ssa_fixture.h"

namespace abcd::test {
namespace {
using namespace analysis;
enum class TokenKind { kEntry, kDefinition };
struct Token {
  TokenKind kind;
  std::uint32_t identity;
  std::uint32_t generation = 0;
  auto operator<=>(const Token&) const = default;
};
std::uint32_t Slot(ScalarSlot slot) {
  const auto* reg = std::get_if<abc::RegisterId>(&slot);
  return reg ? reg->value + 1 : 0;
}
}  // namespace

void VerifySsaPaths() {
  constexpr std::uint32_t kRegisters = 4, kBlocks = 8, kBlockBytes = 9;
  std::mt19937 random{37921};
  for (std::size_t trial = 0; trial < 200; ++trial) {
    std::vector<std::uint8_t> code;
    for (std::uint32_t block = 0; block < kBlocks; ++block) {
      const auto dst = static_cast<std::uint8_t>(random() % kRegisters);
      const auto src = static_cast<std::uint8_t>(random() % kRegisters);
      const auto input = static_cast<std::uint8_t>(random() % kRegisters);
      code.insert(code.end(), {0x45, dst, src, 0x60, input});
      for (const auto opcode : {0x4f, 0x4d}) {
        const auto target =
            static_cast<std::int32_t>(random() % kBlocks * kBlockBytes);
        const auto delta = target - static_cast<std::int32_t>(code.size());
        Check(delta >= INT8_MIN && delta <= INT8_MAX);
        code.insert(code.end(), {static_cast<std::uint8_t>(opcode),
                                 static_cast<std::uint8_t>(delta)});
      }
    }
    const auto body = Body(code);
    const auto graph = std::get<NormalFlow>(BuildNormalFlow(body));
    IdfQuota quota{1000000};
    const auto ssa = std::get<RegisterSsa>(
        RegisterSsa::Build(body, graph, kRegisters, quota));
    for (std::size_t path = 0; path < 8; ++path) {
      std::array<Token, kRegisters + 1> current{};
      for (std::uint32_t slot = 0; slot < current.size(); ++slot)
        current[slot] = {TokenKind::kEntry, slot};
      std::vector<std::optional<Token>> versions(ssa.Values().size());
      for (std::size_t i = 0; i < ssa.Values().size(); ++i)
        if (std::holds_alternative<MethodEntry>(ssa.Values()[i].origin))
          versions[i] = current[Slot(ssa.Values()[i].slot)];
      BlockId block{};
      std::optional<SsaEdge> incoming;
      std::uint32_t generation = 0;
      for (std::size_t step = 0; step < 100; ++step) {
        std::vector<std::pair<SsaValueId, Token>> selected;
        for (std::uint32_t i = 0; i < ssa.Values().size(); ++i) {
          const auto* phi = std::get_if<PhiValue>(&ssa.Values()[i].origin);
          if (!phi || phi->block != block) continue;
          std::size_t matched = 0;
          for (const auto& input : ssa.Inputs({i})) {
            const auto* edge = std::get_if<SsaEdge>(&input.edge);
            const bool match = incoming
                                   ? edge && edge->source == incoming->source &&
                                         edge->successor == incoming->successor
                                   : !edge;
            if (!match) continue;
            Check(versions[input.value.value].has_value());
            selected.push_back({{i}, *versions[input.value.value]});
            ++matched;
          }
          Check(matched == 1);
        }
        // All Phi inputs see predecessor state, including self/backedge Phi
        // dependencies. A sequential overwrite here would invalidate the
        // oracle.
        for (const auto& [value, token] : selected)
          versions[value.value] = token;
        const auto& node = graph.blocks[block.value];
        for (std::uint32_t i = node.first_instruction;
             i < node.first_instruction + node.instruction_count; ++i) {
          const auto& instruction = body.instructions[i].decoded;
          std::optional<std::uint32_t> read, write;
          switch (instruction.opcode) {
            case abc::Opcode::kMOV_V8_V8:
              read =
                  static_cast<std::uint32_t>(instruction.operands[1].raw_bits) +
                  1;
              write =
                  static_cast<std::uint32_t>(instruction.operands[0].raw_bits) +
                  1;
              break;
            case abc::Opcode::kLDA_V8:
              read =
                  static_cast<std::uint32_t>(instruction.operands[0].raw_bits) +
                  1;
              write = 0;
              break;
            case abc::Opcode::kJEQZ_IMM8:
              read = 0;
              break;
            case abc::Opcode::kJMP_IMM8:
              break;
            default:
              Check(false);
          }
          const auto reads = ssa.Reads({i});
          Check(reads.size() == (read ? 1 : 0));
          if (read) {
            Check(reads[0].ordinal == 0);
            Check(Slot(ssa.Values()[reads[0].value.value].slot) == *read);
            Check(versions[reads[0].value.value] == current[*read]);
          }
          const auto written = ssa.Write({i});
          Check(written.has_value() == write.has_value());
          if (write) {
            Check(
                std::get<InstructionValue>(ssa.Values()[written->value].origin)
                    .instruction.value == i);
            current[*write] = {TokenKind::kDefinition, i, ++generation};
            versions[written->value] = current[*write];
          }
        }
        Check(node.successor_count != 0);
        const auto successor =
            static_cast<std::uint8_t>(random() % node.successor_count);
        incoming = SsaEdge{block, successor};
        block = node.successors[successor].target;
      }
    }
  }
}
}  // namespace abcd::test
