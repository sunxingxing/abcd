#include <algorithm>

#include "abcd/analysis/semantic_ir.h"
#include "register_ssa_builder.h"

namespace abcd::analysis {
void RegisterSsa::Builder::Rename(const RegisterUses& uses,
                                  const DominatorTree& tree) {
  struct Undo {
    RegisterBindingId binding;
    SsaValueId previous;
  };
  struct Frame {
    std::uint32_t end;
    std::size_t undo_begin;
  };
  std::pmr::vector<Undo> undo{&memory};
  std::pmr::vector<Frame> frames{&memory};
  std::pmr::vector<SsaValueId> current{&memory};
  std::pmr::vector<BlockId> preorder(tree.nodes.size(), {}, &memory);
  std::pmr::vector<std::size_t> phi_begin(tree.nodes.size() + 1, 0, &memory);
  std::size_t reachable = 0;
  for (std::size_t i = 0; i < tree.nodes.size(); ++i) {
    Charge();
    if (tree.nodes[i].begin != 0) {
      preorder[tree.nodes[i].begin - 1] = {static_cast<std::uint32_t>(i)};
      ++reachable;
    }
  }
  for (const auto& binding : uses.Bindings())
    current.push_back(Define(binding.slot, MethodEntry{}));
  for (auto& phi : phis) {
    const auto slot = uses.Bindings()[phi.binding.value].slot;
    phi.value = Define(slot, PhiValue{phi.block});
    ++phi_begin[std::size_t(phi.block.value) + 1];
    // Entry can be a loop header. Its initial invocation is a real Phi input,
    // independent of incoming backedges and their current versions.
    if (phi.block.value == 0) {
      Charge();
      inputs.push_back({phi.value, MethodEntry{}, current[phi.binding.value]});
    }
  }
  for (std::size_t i = 1; i < phi_begin.size(); ++i) {
    Charge();
    phi_begin[i] += phi_begin[i - 1];
  }
  const auto assign = [&](RegisterBindingId binding, SsaValueId value) {
    Charge();
    undo.push_back({binding, current[binding.value]});
    current[binding.value] = value;
  };
  // Dominator subtree intervals replace recursive DFS. Undo only actual writes;
  // copying the full register map at every child would be quadratic.
  for (std::size_t order = 0; order < reachable; ++order) {
    Charge();
    const auto id = preorder[order];
    const auto& node = tree.nodes[id.value];
    while (!frames.empty() && node.begin >= frames.back().end) {
      Charge();
      while (undo.size() > frames.back().undo_begin) {
        Charge();
        const auto restored = undo.back();
        current[restored.binding.value] = restored.previous;
        undo.pop_back();
      }
      frames.pop_back();
    }
    frames.push_back({node.end, undo.size()});
    for (auto i = phi_begin[id.value]; i < phi_begin[std::size_t(id.value) + 1];
         ++i)
      assign(phis[i].binding, phis[i].value);
    const auto& block = graph.blocks[id.value];
    for (std::size_t i = block.first_instruction;
         i < std::size_t(block.first_instruction) + block.instruction_count;
         ++i) {
      Charge();
      ++instructions;
      const InstructionId instruction{static_cast<std::uint32_t>(i)};
      // Def/use indexing and renaming consume the identical immutable effects.
      // Never re-lift bytecode when the owning semantic path supplied its IR.
      const auto effect = !body ? sites[i].effect : semantics.empty()
          ? std::get<RegisterEffect>(
                LiftRegisterEffect(body->instructions[i].decoded, frame_size))
          : semantics[i].effects.normal_registers;
      std::uint16_t ordinal = 0;
      const auto read = [&](ScalarSlot slot) {
        Charge();
        const auto binding = *uses.Find(slot);
        reads.push_back({instruction, ordinal++, current[binding.value]});
      };
      for (std::size_t r = 0; r < effect.read_count; ++r) read(effect.reads[r]);
      if (effect.range)
        for (std::uint32_t r = 0; r < effect.range->count; ++r)
          read(abc::RegisterId{effect.range->first.value + r});
      if (effect.write) {
        const auto value = Define(*effect.write, InstructionValue{instruction});
        assign(*uses.Find(*effect.write), value);
        writes.push_back({instruction, value});
      }
    }
    for (std::size_t edge = 0; edge < block.successor_count; ++edge) {
      Charge();
      const auto target = block.successors[edge].target.value;
      for (auto i = phi_begin[target]; i < phi_begin[std::size_t(target) + 1];
           ++i) {
        Charge();
        inputs.push_back({phis[i].value,
                          SsaEdge{id, static_cast<std::uint8_t>(edge)},
                          current[phis[i].binding.value]});
      }
    }
  }
  std::sort(
      reads.begin(), reads.end(), [&](const SsaRead& a, const SsaRead& b) {
        Charge();
        return a.instruction != b.instruction ? a.instruction < b.instruction
                                              : a.ordinal < b.ordinal;
      });
  std::sort(writes.begin(), writes.end(),
            [&](const SsaWrite& a, const SsaWrite& b) {
              Charge();
              return a.instruction < b.instruction;
            });
  std::sort(inputs.begin(), inputs.end(),
            [&](const PhiInput& a, const PhiInput& b) {
              Charge();
              if (a.phi != b.phi) return a.phi < b.phi;
              const auto* left = std::get_if<SsaEdge>(&a.edge);
              const auto* right = std::get_if<SsaEdge>(&b.edge);
              if (!left || !right) return !left && right;
              return left->source != right->source
                         ? left->source < right->source
                         : left->successor < right->successor;
            });
}
}  // namespace abcd::analysis
