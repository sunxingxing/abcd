#include "abcd/analysis/linear_lexicals.h"

#include <algorithm>
#include <stdexcept>

#include "abcd/analysis/function_creation.h"
#include "abcd/analysis/hole_guard.h"
#include "idf_scratch.h"
#include "register_value_rule.h"

namespace abcd::analysis {
namespace {
bool Less(const LocalCell& a, const LocalCell& b) {
  return a.environment != b.environment ? a.environment < b.environment
                                        : a.slot.value < b.slot.value;
}
}  // namespace
std::variant<LinearLexicals, LinearLexicalFailure> LinearLexicals::Build(
    const abc::MethodBody& body, const EnvironmentFlow& environments,
    std::uint64_t frame_size, IdfQuota& quota, IdfBudget budget) {
  using Error = LinearLexicalError;
  abc::CodeOffset offset{};
  const auto fail = [&](Error kind) {
    return LinearLexicalFailure{kind, offset};
  };
  if (!body.tries.empty() || !body.handlers.empty())
    return fail(Error::kExceptionRegions);
  if (body.instructions.empty()) return fail(Error::kTerminator);
  if (environments.before.size() != body.instructions.size())
    return fail(Error::kEnvironment);
  try {
    const auto consumed = quota.Consumed();
    const auto charge = [&](std::uint64_t count = 1) {
      if (!quota.Take(count)) throw detail::IdfWorkLimit{};
    };
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      charge();
      offset = body.instructions[i].offset;
      const auto transfer =
          abc::GetControlTransfer(body.instructions[i].decoded.opcode);
      if (!transfer || (*transfer != abc::ControlTransfer::kFallthrough &&
                        *transfer != abc::ControlTransfer::kReturn &&
                        *transfer != abc::ControlTransfer::kThrow))
        return fail(Error::kControlFlow);
      const bool terminal = *transfer != abc::ControlTransfer::kFallthrough;
      if (terminal != (i + 1 == body.instructions.size()))
        return fail(Error::kTerminator);
      if (!environments.before[i]) return fail(Error::kEnvironment);
    }
    detail::IdfScratch memory{budget.bytes};
    std::pmr::vector<LocalCell> cells{&memory};
    for (const auto& site : environments.bindings) {
      charge();
      if (site.instruction >= body.instructions.size())
        return fail(Error::kEnvironment);
      offset = body.instructions[site.instruction].offset;
      const auto* local = std::get_if<EnvironmentId>(&site.binding.environment);
      if (!local) return fail(Error::kCaptured);
      cells.push_back({*local, site.binding.slot});
    }
    std::sort(cells.begin(), cells.end(), [&](const auto& a, const auto& b) {
      charge();
      return Less(a, b);
    });
    cells.erase(std::unique(cells.begin(), cells.end(),
                            [&](const auto& a, const auto& b) {
                              charge();
                              return !Less(a, b) && !Less(b, a);
                            }),
                cells.end());
    if (cells.size() > UINT32_MAX || frame_size > SIZE_MAX)
      return fail(Error::kResourceLimit);
    charge(frame_size);
    charge(body.instructions.size());
    charge(cells.size());
    std::pmr::vector<ValueKinds> registers(frame_size, ValueKinds::Language(),
                                           &memory);
    std::pmr::vector<std::uint8_t> initialized(cells.size(), 0, &memory);
    std::pmr::vector<LinearStatement> statements(body.instructions.size(), {},
                                                 &memory);
    std::optional<std::uint32_t> previous;
    for (const auto& site : environments.bindings) {
      charge();
      offset = body.instructions[site.instruction].offset;
      if (previous && *previous >= site.instruction)
        return fail(Error::kEnvironment);
      previous = site.instruction;
      const LocalCell key{std::get<EnvironmentId>(site.binding.environment),
                          site.binding.slot};
      const auto found = std::lower_bound(cells.begin(), cells.end(), key,
                                          [&](const auto& a, const auto& b) {
                                            charge();
                                            return Less(a, b);
                                          });
      if (found == cells.end() || Less(key, *found))
        return fail(Error::kEnvironment);
      const LocalCellId id{static_cast<std::uint32_t>(found - cells.begin())};
      const auto operation = LiftLexicalInstruction(
          body.instructions[site.instruction].decoded, frame_size);
      if (std::holds_alternative<LexicalLoad>(operation))
        statements[site.instruction] = LocalLoad{id};
      else if (const auto* store = std::get_if<LexicalStore>(&operation))
        statements[site.instruction] = LocalStore{id, store->value};
      else
        return fail(Error::kEnvironment);
    }
    ValueKinds accumulator = ValueKinds::Hole();
    const auto state = [&](ScalarSlot slot) -> ValueKinds& {
      if (const auto* reg = std::get_if<abc::RegisterId>(&slot)) {
        if (reg->value >= registers.size()) throw fail(Error::kInstruction);
        return registers[reg->value];
      }
      return accumulator;
    };
    for (std::size_t i = 0; i < body.instructions.size(); ++i) {
      charge();
      offset = body.instructions[i].offset;
      auto& statement = statements[i];
      const auto& instruction = body.instructions[i].decoded;
      const auto lexical = LiftLexicalInstruction(instruction, frame_size);
      if (const auto* push = std::get_if<PushEnvironment>(&lexical)) {
        if (push->scope) return fail(Error::kInstruction);
        accumulator = ValueKinds::Environment();
        statement = OmitInstruction{};
        continue;
      }
      if (std::holds_alternative<PopEnvironment>(lexical) ||
          std::holds_alternative<CurrentEnvironment>(lexical)) {
        if (!std::holds_alternative<EnvironmentId>(*environments.before[i]))
          return fail(Error::kCaptured);
        if (std::holds_alternative<CurrentEnvironment>(lexical))
          accumulator = ValueKinds::Environment();
        statement = OmitInstruction{};
        continue;
      }
      if (const auto* load = std::get_if<LocalLoad>(&statement)) {
        if (!initialized[load->cell.value]) return fail(Error::kUninitialized);
        accumulator = ValueKinds::Language();
        continue;
      }
      if (const auto* store = std::get_if<LocalStore>(&statement)) {
        if (!state(store->value).IsLanguage())
          return fail(Error::kInternalValue);
        initialized[store->cell.value] = 1;
        continue;
      }
      if (!std::holds_alternative<LexicalError>(lexical) ||
          std::get<LexicalError>(lexical) != LexicalError::kUnsupportedOpcode)
        return fail(Error::kEnvironment);
      const auto hole = LiftHoleInstruction(instruction, frame_size);
      if (std::holds_alternative<LoadHole>(hole)) {
        accumulator = ValueKinds::Hole();
        statement = OmitInstruction{};
        continue;
      }
      if (const auto* guard = std::get_if<HoleGuard>(&hole)) {
        // The successful path does not read diagnostic name registers/pool.
        if (!state(guard->value).IsLanguage())
          return fail(Error::kMayThrowGuard);
        statement = OmitInstruction{};
        continue;
      }
      if (std::get<HoleError>(hole) != HoleError::kUnsupportedOpcode)
        return fail(Error::kInstruction);
      // Creation captures implicit current env even without a register read.
      if (std::holds_alternative<FunctionCreation>(
              LiftFunctionCreation(instruction)))
        return fail(Error::kInstruction);
      const auto scalar = LiftScalarInstruction(instruction, frame_size);
      if (const auto* assignment = std::get_if<ScalarAssignment>(&scalar)) {
        if (const auto* value =
                std::get_if<ScalarValue>(&assignment->expression)) {
          std::optional<ScalarSlot> source;
          if (std::holds_alternative<Accumulator>(*value))
            source = Accumulator{};
          if (const auto* reg = std::get_if<abc::RegisterId>(value))
            source = *reg;
          if (source) {
            state(assignment->destination) = state(*source);
            if (!state(*source).IsLanguage()) statement = OmitInstruction{};
            continue;
          }
        }
      }
      const auto effect = LiftRegisterEffect(instruction, frame_size);
      if (std::holds_alternative<ScalarError>(effect))
        return fail(Error::kInstruction);
      const auto& access = std::get<RegisterEffect>(effect);
      for (std::size_t read = 0; read < access.read_count; ++read) {
        charge();
        if (!state(access.reads[read]).IsLanguage())
          return fail(Error::kInternalValue);
      }
      if (access.range) {
        for (std::uint32_t arg = 0; arg < access.range->count; ++arg) {
          charge();
          if (!state(abc::RegisterId{access.range->first.value + arg})
                   .IsLanguage())
            return fail(Error::kInternalValue);
        }
      }
      if (access.write) {
        const auto described =
            detail::DescribeRegisterValue(instruction, frame_size);
        if (std::holds_alternative<ScalarError>(described))
          return fail(Error::kInstruction);
        const auto rule = std::get<detail::ValueRule>(described);
        state(*access.write) =
            rule.kind == detail::ValueRuleKind::kLanguageOperation
                ? ValueKinds::Language()
                : rule.seed;
      }
    }
    LinearLexicals result;
    const auto copy = [&]<class T>(const std::pmr::vector<T>& from,
                                   std::unique_ptr<T[]>& to) {
      charge(from.size());
      if (from.size() > memory.Available() / sizeof(T)) throw std::bad_alloc{};
      if (!from.empty()) {
        to = std::make_unique<T[]>(from.size());
        memory.Retain(from.size() * sizeof(T));
        std::copy(from.begin(), from.end(), to.get());
      }
    };
    copy(cells, result.cells_);
    copy(statements, result.statements_);
    result.cell_count_ = cells.size();
    result.statement_count_ = statements.size();
    result.work = {body.instructions.size(), quota.Consumed() - consumed,
                   memory.Peak()};
    return result;
  } catch (const LinearLexicalFailure& failure) {
    return failure;
  } catch (const detail::IdfWorkLimit&) {
    return fail(Error::kWorkLimit);
  } catch (const std::bad_alloc&) {
    return fail(Error::kResourceLimit);
  } catch (const std::length_error&) {
    return fail(Error::kResourceLimit);
  }
}
}  // namespace abcd::analysis
