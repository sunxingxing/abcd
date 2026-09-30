#include "value_heritage.h"
#include <new>
#include <stdexcept>
namespace abcd::analysis {
namespace {
using Error = ValueHeritageError;
void Require(bool value, Error error = Error::kInput) { if (!value) throw error; }
enum class Visit : std::uint8_t { kUnseen, kActive, kDone };
}
std::variant<ValueHeritages, ValueHeritageError> AnalyzeValueHeritage(
    const ValueProgram& graph, SemanticBudget budget) {
  const auto initial = budget;
  try {
    const auto step = [&] {
      Require(budget.work != 0, Error::kWorkLimit); --budget.work;
    };
    const auto array = [&]<class T>(std::vector<T>& values, std::size_t count) {
      Require(count <= budget.bytes / sizeof(T), Error::kResourceLimit);
      budget.bytes -= count * sizeof(T); values.resize(count);
      Require(values.capacity() - count <= budget.bytes / sizeof(T), Error::kResourceLimit);
      budget.bytes -= (values.capacity() - count) * sizeof(T);
    };
    std::size_t count = 0;
    for (const auto& op : graph.ops) {
      step(); count += op.life == ValueLife::kLive && std::holds_alternative<ValueClassCreation>(op.operation);
    }
    ValueHeritages result{graph.revision, {}};
    array(result.classes, count);
    if (count == 0) { result.work = initial.work - budget.work; return result; }
    Require(graph.values.size() <= UINT32_MAX && graph.ops.size() <= UINT32_MAX, Error::kResourceLimit);
    std::vector<HeritageKind> facts;
    std::vector<Visit> visited;
    std::vector<ValueId> path;
    array(facts, graph.values.size()); array(visited, graph.values.size()); array(path, graph.values.size());
    const auto literal = [](const ValueOperand& operand) {
      const auto* value = std::get_if<SpecialValue>(&operand);
      return value && *value == SpecialValue::kNull ? HeritageKind::kNull : HeritageKind::kUnknown;
    };
    const auto resolve = [&](ValueId start) {
      std::size_t length = 0;
      auto current = start;
      HeritageKind kind = HeritageKind::kUnknown;
      while (true) {
        step(); Require(current.value < graph.values.size());
        const auto& value = graph.values[current.value];
        Require(value.life == ValueLife::kLive);
        if (visited[current.value] == Visit::kDone) { kind = facts[current.value]; break; }
        Require(visited[current.value] != Visit::kActive);
        visited[current.value] = Visit::kActive; path[length++] = current;
        if (value.domain == ValueKinds::Hole()) { kind = HeritageKind::kAbsent; break; }
        Require(value.domain.IsLanguage(), Error::kDomain);
        const auto* origin = std::get_if<OpResult>(&value.origin);
        if (!origin) break;
        Require(origin->op.value < graph.ops.size());
        const auto& producer = graph.ops[origin->op.value];
        if (!std::holds_alternative<ValueCopy>(producer.operation)) break;
        Require(producer.life == ValueLife::kLive && producer.result == current && producer.use_count == 1 &&
            producer.first_use.value < graph.uses.size() && producer.normal == LanguageEffect::kNone &&
            producer.before_throw == LanguageEffect::kNone && producer.exceptional == ExceptionalCompletion::kImpossible);
        const auto& operand = graph.uses[producer.first_use.value].operand;
        if (const auto* next = std::get_if<ValueId>(&operand)) { current = *next; continue; }
        kind = literal(operand); break;
      }
      while (length) {
        step(); const auto id = path[--length]; facts[id.value] = kind; visited[id.value] = Visit::kDone;
      }
      return kind;
    };
    std::size_t next = 0;
    for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (op.life != ValueLife::kLive || !std::holds_alternative<ValueClassCreation>(op.operation)) continue;
      Require(op.result && op.use_count == 1 && op.first_use.value < graph.uses.size());
      const auto& use = graph.uses[op.first_use.value];
      Require(use.life == ValueLife::kLive && use.owner == decltype(use.owner){OpId{i}} &&
          use.role == OperandRole::kHeritage && use.purpose == ReadPurpose::kProtocol);
      const auto* value = std::get_if<ValueId>(&use.operand);
      result.classes[next++] = {{i}, op.first_use, value ? resolve(*value) : literal(use.operand)};
    }
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (Error error) { return error; }
    catch (const std::bad_alloc&) { return Error::kResourceLimit; }
    catch (const std::length_error&) { return Error::kResourceLimit; }
}
}
