#include "iterator_keys.h"

#include "abcd/corpus/abc_file.h"
#include "../abc/metadata_cursor.h"
#include "../analysis/idf_scratch.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace abcd::source {
namespace {
IteratorKey Classify(std::string_view value) {
  if (value == "next") return IteratorKey::kNext;
  if (value == "done") return IteratorKey::kDone;
  if (value == "value") return IteratorKey::kValue;
  if (value == "return") return IteratorKey::kReturn;
  return IteratorKey::kOther;
}
}
IteratorKeysResult ResolveIteratorKeys(const IteratorKeyInput& input,
    abc::EntityOffset method, const analysis::ValueProgram& graph, analysis::SemanticBudget budget) {
  try {
    if (input.file.Size() != input.header.file_size || input.file.CheckUnchanged())
      return abc::MetadataError::kFileChanged;
    analysis::detail::IdfScratch memory(budget.bytes);
    auto remaining = budget.work;
    const auto step = [&](std::uint64_t count = 1) {
      if (count > remaining) throw IteratorKeyError::kWorkLimit;
      remaining -= count;
    };
    const auto reserve = [&]<class T>(std::vector<T>& values, std::size_t count) {
      if (count > UINT32_MAX || count > memory.Available() / sizeof(T))
        throw IteratorKeyError::kResourceLimit;
      memory.Retain(count * sizeof(T)); values.reserve(count);
      memory.Retain((values.capacity() - count) * sizeof(T));
    };
    IteratorKeys result{method, graph.revision, graph.cfg_revision};
    struct Slot { abc::PoolIndex pool; analysis::OpId op; };
    std::pmr::vector<Slot> slots(&memory);
    std::size_t count = 0;
    for (const auto& op : graph.ops) {
      step(); count += op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueNamedRead>(op.operation);
    }
    if (graph.ops.size() > UINT32_MAX) return IteratorKeyError::kInput;
    slots.reserve(count);
    for (std::uint32_t i = 0; i < graph.ops.size(); ++i) {
      step(); const auto& op = graph.ops[i];
      if (const auto* read = std::get_if<analysis::ValueNamedRead>(&op.operation);
          read && op.life == analysis::ValueLife::kLive) slots.push_back({read->name, {i}});
    }
    std::sort(slots.begin(), slots.end(), [&](const auto& a, const auto& b) {
      step(); return a.pool != b.pool ? a.pool < b.pool : a.op < b.op;
    });
    std::size_t unique = 0;
    for (std::size_t i = 0; i < slots.size(); ++i) {
      step(); unique += i == 0 || slots[i - 1].pool != slots[i].pool;
    }
    reserve(result.origins, unique); reserve(result.uses, slots.size());
    for (std::size_t i = 0; i < slots.size(); ++i) {
      step();
      if (i == 0 || slots[i - 1].pool != slots[i].pool) {
        const auto resolved = abc::ResolvePoolReference(input.file, input.header, input.index,
            method, slots[i].pool, abc::PoolEntityKind::kString);
        if (const auto* error = std::get_if<abc::MetadataError>(&resolved)) return *error;
        result.origins.push_back({slots[i].pool, std::get<abc::PoolReference>(resolved).offset, IteratorKey::kOther});
      }
      result.uses.push_back({slots[i].op, static_cast<std::uint32_t>(result.origins.size() - 1)});
    }
    std::pmr::vector<std::uint32_t> entities(result.origins.size(), &memory);
    for (std::uint32_t i = 0; i < entities.size(); ++i) { step(); entities[i] = i; }
    std::sort(entities.begin(), entities.end(), [&](auto a, auto b) {
      step(); return result.origins[a].entity < result.origins[b].entity;
    });
    for (std::size_t first = 0; first < entities.size();) {
      step(); const auto entity = result.origins[entities[first]].entity;
      abc::MetadataCursor cursor(input.file, entity.value, input.header.file_size);
      std::uint32_t units = 0;
      std::size_t spent = 0;
      // Validate the entire MUTF-8 entity but do not allocate a second UTF-16
      // string to classify four ASCII keys. Geometric growth overlaps at most
      // two raw buffers; reserve that upper bound in the caller's allowance.
      const auto raw_budget = memory.Available() / 2;
      auto text = abc::ReadMetadataString(&cursor, &units, &spent, raw_budget);
      if (const auto* error = std::get_if<abc::MetadataError>(&text)) return *error;
      memory.Observe(2 * (spent + 1));
      const auto& value = std::get<std::string>(text);
      step(value.size()); const auto key = Classify(value);
      auto end = first;
      while (end < entities.size() && result.origins[entities[end]].entity == entity) {
        step(); result.origins[entities[end++]].key = key;
      }
      first = end;
    }
    std::sort(result.uses.begin(), result.uses.end(), [&](const auto& a, const auto& b) {
      step(); return a.operation < b.operation;
    });
    result.allocated_bytes = memory.Peak(); result.work = budget.work - remaining;
    result.retained_bytes = result.origins.capacity() * sizeof(IteratorKeyOrigin) +
        result.uses.capacity() * sizeof(IteratorKeyUse);
    if (input.file.CheckUnchanged()) return abc::MetadataError::kFileChanged;
    return result;
  } catch (IteratorKeyError error) { return error;
  } catch (const std::bad_alloc&) { return IteratorKeyError::kResourceLimit;
  } catch (const std::length_error&) { return IteratorKeyError::kResourceLimit; }
}
}  // namespace abcd::source
