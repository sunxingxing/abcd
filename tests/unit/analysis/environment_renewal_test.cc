#include "abcd/analysis/environment_renewal.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <source_location>

#include "abcd/abc/call_type.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/async_completion.h"

using namespace abcd;
using namespace abcd::analysis;
namespace {
void Check(bool ok,
           std::source_location site = std::source_location::current()) {
  if (!ok) {
    std::cerr << site.file_name() << ":" << site.line()
              << " assertion failed\n";
    std::abort();
  }
}
template <class T, class... U>
T Take(std::variant<U...> v) {
  if (auto* p = std::get_if<T>(&v)) return std::move(*p);
  std::cerr << "unexpected alternative " << v.index() << '\n';
  std::abort();
}
}  // namespace
int main(int argc, char** argv) {
  const auto path = argc > 1 ? argv[1] : ABCD_ENVIRONMENT_RENEWAL_FIXTURE;
  auto file =
      Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  auto hash = Take<corpus::FileFingerprint>(file->Fingerprint());
  auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(hash.prefix).first(hash.prefix_size), hash.size));
  auto index = Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  unsigned admitted = 0, refused = 0;
  for (const auto& m : index.methods)
    if (m.code && (argc < 3 || m.offset.value == std::stoul(argv[2]))) {
      const auto call = Take<abc::CallType>(
          abc::ReadCallType(*file, header, index, m.annotations));
      auto frame = Take<abc::FrameLayout>(
          abc::BuildFrameLayout(m.code->registers, m.code->arguments,
                                abc::DecodeCallFlags(call.value)));
      auto body = Take<abc::MethodBody>(
          abc::DecodeMethodBody(*file, header.profile, *m.code));
      std::vector<abc::CodeOffset> origins;
      std::vector<AwaitSite> awaits;
      auto normalized = source::ProveAsyncCompletion(
          body, frame, 64 * 1024 * 1024, &origins, &awaits, header.profile,
          source::ResourceFailureScope::kExcluded);
      if (auto* b = std::get_if<abc::MethodBody>(&normalized))
        body = std::move(*b);
      else
        continue;
      auto flow = Take<ExceptionFlow>(BuildExceptionFlow(
          body, header.profile, frame.registers, {}, awaits, &frame));
      IdfQuota quota{kDefaultSemanticWork};
      auto ssa =
          Take<ExceptionSsa>(ExceptionSsa::Build(flow, frame.registers, quota));
      auto legacy = AnalyzeExceptionEnvironments(flow);
      if (std::holds_alternative<EnvironmentFlow>(legacy)) continue;
      Check(std::get<EnvironmentFlowFailure>(legacy).kind ==
            EnvironmentFlowError::kAmbiguousJoin);
      auto proof = ProveEnvironmentRenewal(
          flow, ssa, EnvironmentRenewalResources::kExcluded);
      if (auto* error = std::get_if<EnvironmentRenewalFailure>(&proof)) {
        std::cout << "method=" << m.offset.value
                  << " refusal=" << unsigned(error->kind)
                  << " pc=" << error->pc.value << '\n';
        ++refused;
        continue;
      }
      ++admitted;
      auto& p = std::get<EnvironmentRenewal>(proof);
      std::cout << "method=" << m.offset.value << " events=" << p.events.size()
                << " transfers=" << p.transfers.size()
                << " resets=" << p.resets.size() << " work=" << p.work << '\n';
      Check(std::holds_alternative<std::uint64_t>(
          VerifyEnvironmentRenewal(flow, ssa, p)));
      Check(std::holds_alternative<EnvironmentFlow>(
          ProjectRenewedEnvironments(flow, ssa, p)));
      Check(std::holds_alternative<EnvironmentRenewalFailure>(
          ProveEnvironmentRenewal(flow, ssa,
                                  EnvironmentRenewalResources::kObservable)));
      const auto rejects = [&](const EnvironmentRenewal& wrong,
                               EnvironmentRenewalError expected) {
        const auto checked = VerifyEnvironmentRenewal(flow, ssa, wrong);
        Check(std::holds_alternative<EnvironmentRenewalFailure>(checked));
        Check(std::get<EnvironmentRenewalFailure>(checked).kind == expected);
      };
      auto wrong = p;
      wrong.coverage.clear();
      rejects(wrong, EnvironmentRenewalError::kCoverage);
      wrong = p;
      ++wrong.transfers[0].new_slot;
      rejects(wrong, EnvironmentRenewalError::kSlotMap);
      wrong = p;
      ++wrong.transfers[0].saved.value;
      rejects(wrong, EnvironmentRenewalError::kCopyValue);
      wrong = p;
      ++wrong.inputs[0].edge.source.value;
      rejects(wrong, EnvironmentRenewalError::kActivation);
      wrong = p;
      wrong.definitions[0].parent = CapturedDepth{1};
      rejects(wrong, EnvironmentRenewalError::kParent);
      wrong = p;
      ++wrong.events[0].old_token.value;
      rejects(wrong, EnvironmentRenewalError::kActivation);
      wrong = p;
      ++wrong.resets[0].initializer.value;
      rejects(wrong, EnvironmentRenewalError::kInitialization);
      wrong = p;
      for (std::uint32_t n = 0; n < flow.nodes.size(); ++n)
        if (flow.nodes[n].phase == ExceptionPhase::kBefore &&
            std::holds_alternative<FunctionCreation>(
                flow.instructions[flow.nodes[n].instruction.value].operation) &&
            wrong.current[n]) {
          ++wrong.current[n]->value;
          break;
        }
      rejects(wrong, EnvironmentRenewalError::kActivation);
      wrong = p;
      ++wrong.transfers[0].store.value;
      rejects(wrong, EnvironmentRenewalError::kActivation);
      const auto prove = [&](const ExceptionFlow& changed,
                             std::uint64_t registers = 0) {
        IdfQuota work{kDefaultSemanticWork};
        auto other = Take<ExceptionSsa>(ExceptionSsa::Build(
            changed, registers ? registers : frame.registers, work));
        return ProveEnvironmentRenewal(changed, other,
                                       EnvironmentRenewalResources::kExcluded);
      };
      // Mutate owned semantic bodies; no fixture/input ABC is ever modified.
      if (p.definitions[p.events[0].new_environment.value].allocation.slots >
          1) {
        auto changed = flow;
        auto& op = std::get<LexicalStore>(
            changed
                .instructions[changed.nodes[p.transfers[0].store.value]
                                  .instruction.value]
                .operation);
        op.address.slot.value = static_cast<std::uint16_t>(
            (op.address.slot.value + 1) %
            p.definitions[p.events[0].new_environment.value].allocation.slots);
        auto broken = prove(changed);
        Check(std::holds_alternative<EnvironmentRenewalFailure>(broken));
        Check(std::get<EnvironmentRenewalFailure>(broken).kind ==
              EnvironmentRenewalError::kSlotMap);
        changed = flow;
        for (auto& instruction : changed.instructions) {
          LexicalAddress* address = nullptr;
          if (auto* load = std::get_if<LexicalLoad>(&instruction.operation))
            address = &load->address;
          if (auto* store = std::get_if<LexicalStore>(&instruction.operation))
            address = &store->address;
          if (address && address->depth.value == 0 && address->slot.value < 2)
            address->slot.value = 1 - address->slot.value;
        }
        Check(std::holds_alternative<EnvironmentRenewal>(prove(changed)));
      }
      const auto insert = [&](SemanticOperation operation,
                              SemanticEffects effects) {
        auto changed = flow;
        const auto pop = p.events[0].pop.value;
        Check(changed.nodes[pop].successor_count == 1);
        const auto target = changed.nodes[pop].successors[0].target;
        const auto instruction = InstructionId{
            static_cast<std::uint32_t>(changed.instructions.size())};
        auto item = changed.instructions.back();
        item.pc.value += 1;
        item.operation = std::move(operation);
        item.effects = effects;
        changed.instructions.push_back(std::move(item));
        const auto before =
            ExceptionNodeId{static_cast<std::uint32_t>(changed.nodes.size())};
        auto before_node = changed.nodes[pop];
        before_node.instruction = instruction;
        before_node.phase = ExceptionPhase::kBefore;
        before_node.exception.reset();
        before_node.successor_count = 1;
        before_node.successors[0] = {ExceptionNodeId{before.value + 1},
                                     ExceptionArcKind::kNormal};
        auto commit_node = before_node;
        commit_node.phase = ExceptionPhase::kCommit;
        commit_node.successors[0] = {target, ExceptionArcKind::kNormal};
        changed.nodes.push_back(before_node);
        changed.nodes.push_back(commit_node);
        changed.nodes[pop].successors[0].target = before;
        return changed;
      };
      Check(std::holds_alternative<EnvironmentRenewal>(
          prove(insert(NoOperation{}, {}))));
      auto repeated = insert(NoOperation{}, {});
      const auto branch = static_cast<std::uint32_t>(repeated.nodes.size() - 1);
      auto initializer = p.resets[0].initializer;
      const auto instruction = flow.nodes[initializer.value].instruction;
      for (std::uint32_t n = 0; n < flow.nodes.size(); ++n)
        if (flow.nodes[n].instruction == instruction &&
            flow.nodes[n].phase == ExceptionPhase::kBefore) {
          initializer = {n};
          break;
        }
      // Put the branch BEFORE departure: both successors retain the same
      // physical environment, and one revisits initialization without reset.
      const auto pop = p.events[0].pop;
      const auto pop_instruction = flow.nodes[pop.value].instruction;
      ExceptionNodeId pop_before{};
      for (std::uint32_t n = 0; n < flow.nodes.size(); ++n)
        if (flow.nodes[n].instruction == pop_instruction &&
            flow.nodes[n].phase == ExceptionPhase::kBefore) {
          pop_before = {n};
          break;
        }
      const auto appended = ExceptionNodeId{branch - 1};
      repeated.nodes[pop.value].successors[0] =
          repeated.nodes[branch].successors[0];
      repeated.nodes[pop_before.value].successors[0].target = appended;
      repeated.nodes[branch].successors[0] = {pop,
                                              ExceptionArcKind::kFallthrough};
      repeated.nodes[branch].successor_count = 2;
      repeated.nodes[branch].successors[1] = {initializer,
                                              ExceptionArcKind::kTaken};
      auto repeated_result = prove(repeated);
      Check(std::holds_alternative<EnvironmentRenewalFailure>(repeated_result));
      Check(std::get<EnvironmentRenewalFailure>(repeated_result).kind ==
            EnvironmentRenewalError::kSourceScope);

      const auto transfer_store =
          flow.nodes[p.transfers[0].store.value].instruction.value;
      const auto supplied = ssa.Reads(p.transfers[0].store)[0].value;
      const auto producer =
          std::get<InstructionValue>(ssa.Values()[supplied.value].origin)
              .instruction.value;
      const auto& expression = std::get<ScalarValue>(
          std::get<ScalarAssignment>(
              flow.instructions[flow.nodes[producer].instruction.value]
                  .operation)
              .expression);
      const auto saved = std::visit(
          [](const auto& value) -> ScalarSlot {
            using T = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<T, abc::RegisterId> ||
                          std::is_same_v<T, Accumulator>)
              return value;
            else {
              std::abort();
            }
          },
          expression);
      auto moved = insert(
          ScalarAssignment{
              abc::RegisterId{static_cast<std::uint32_t>(frame.registers)},
              std::visit([](auto slot) -> ScalarValue { return slot; }, saved)},
          {});
      auto& move = moved.instructions.back();
      move.effects.normal_registers.reads[0] = saved;
      move.effects.normal_registers.read_count = 1;
      move.effects.normal_registers.write =
          abc::RegisterId{static_cast<std::uint32_t>(frame.registers)};
      std::get<LexicalStore>(moved.instructions[transfer_store].operation)
          .value = abc::RegisterId{static_cast<std::uint32_t>(frame.registers)};
      moved.instructions[transfer_store].effects.normal_registers.reads[0] =
          abc::RegisterId{static_cast<std::uint32_t>(frame.registers)};
      Check(std::holds_alternative<EnvironmentRenewal>(
          prove(moved, frame.registers + 1)));
      auto overwritten =
          insert(ScalarAssignment{saved, ScalarValue{IntegerLiteral{123}}}, {});
      overwritten.instructions.back().effects.normal_registers.write = saved;
      auto refused_value = prove(overwritten);
      Check(std::holds_alternative<EnvironmentRenewalFailure>(refused_value));
      Check(std::get<EnvironmentRenewalFailure>(refused_value).kind ==
            EnvironmentRenewalError::kSlotMap);
      SemanticEffects interference;
      interference.normal =
          LanguageEffect::kLexicalWrite | LanguageEffect::kEnvironmentRead;
      interference.normal_registers.reads[0] = Accumulator{};
      interference.normal_registers.read_count = 1;
      auto write =
          insert(LexicalStore{{{0}, {0}}, Accumulator{}}, interference);
      auto refused_write = prove(write);
      Check(std::holds_alternative<EnvironmentRenewalFailure>(refused_write));
      Check(std::get<EnvironmentRenewalFailure>(refused_write).kind ==
            EnvironmentRenewalError::kInterference);
      Check(std::get<EnvironmentRenewalFailure>(
                ProveEnvironmentRenewal(flow, ssa,
                                        EnvironmentRenewalResources::kExcluded,
                                        {0, kDefaultSemanticWork}))
                .kind == EnvironmentRenewalError::kResourceLimit);
      Check(std::get<EnvironmentRenewalFailure>(
                ProveEnvironmentRenewal(flow, ssa,
                                        EnvironmentRenewalResources::kExcluded,
                                        {64 * 1024 * 1024, 0}))
                .kind == EnvironmentRenewalError::kWorkLimit);
    }
  if (argc == 1) Check(admitted >= 1 && refused == 0);
}
