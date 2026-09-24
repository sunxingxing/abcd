#include "semantic_fields.h"

#include <algorithm>
#include <new>
#include <stdexcept>

#include "object_buffer.h"

namespace abcd::source {
namespace {
struct FieldUse {
  const SourceOwnStore* store;
  abc::CodeOffset pc;
};
SourceFailure Fail(SourceError error, abc::CodeOffset pc = {}) {
  return {error, pc};
}
}  // namespace

std::optional<SourceFailure> VerifyOwnFields(
    const SourceAst& ast, const MethodConstants& constants,
    ObjectBufferBudget budget) {
  try {
    std::size_t count = 0;
    for (const auto& statement : ast.statements)
      count += std::holds_alternative<SourceOwnStore>(statement.operation);
    if (!count) return {};
    if (count > budget.bytes / sizeof(FieldUse))
      return Fail(SourceError::kResourceLimit);
    std::vector<FieldUse> fields;
    fields.reserve(count);
    if (fields.capacity() > budget.bytes / sizeof(FieldUse))
      return Fail(SourceError::kResourceLimit);
    budget.bytes -= fields.capacity() * sizeof(FieldUse);
    for (const auto& statement : ast.statements)
      if (const auto* store = std::get_if<SourceOwnStore>(&statement.operation))
        fields.push_back({store, statement.pc});
    std::sort(fields.begin(), fields.end(), [](const FieldUse& a, const FieldUse& b) {
      return a.store->origin.literal.value < b.store->origin.literal.value;
    });
    // One checked buffer parse per pool slot; keys are binary-searched through
    // the parser's sorted index. Never scan all keys again for each field.
    for (std::size_t begin = 0; begin < fields.size();) {
      const auto parsed = ReadObjectBuffer(fields[begin].store->origin,
                                           fields[begin].pc, constants, budget);
      if (const auto* failure = std::get_if<BodyEmitFailure>(&parsed))
        return Fail(failure->kind == BodyEmitError::kResourceLimit
                        ? SourceError::kResourceLimit : SourceError::kConstant,
                    failure->offset);
      const auto& buffer = std::get<ObjectBuffer>(parsed);
      const auto slot = fields[begin].store->origin.literal.value;
      std::size_t end = begin;
      while (end < fields.size() && fields[end].store->origin.literal.value == slot) {
        const auto& field = fields[end];
        const auto reference = abc::ResolvePoolReference(
            constants.file, constants.header, constants.index, constants.method,
            field.store->write.name, abc::PoolEntityKind::kString);
        const auto* pool = std::get_if<abc::PoolReference>(&reference);
        if (!pool)
          return Fail(std::get<abc::MetadataError>(reference) ==
                              abc::MetadataError::kResourceLimit
                          ? SourceError::kResourceLimit : SourceError::kConstant,
                      field.pc);
        const auto read = abc::ReadStringEntity(
            constants.file, constants.header, pool->offset,
            {std::min(constants.string_budget.bytes,
                      budget.bytes - buffer.allocated_bytes)});
        const auto* name = std::get_if<std::u16string>(&read);
        if (!name)
          return Fail(std::get<abc::MetadataError>(read) ==
                              abc::MetadataError::kResourceLimit
                          ? SourceError::kResourceLimit : SourceError::kConstant,
                      field.pc);
        const auto found = std::lower_bound(
            buffer.order.begin(), buffer.order.end(), *name,
            [&](std::size_t index, const std::u16string& key) {
              return buffer.keys[index] < key;
            });
        if (found == buffer.order.end() || buffer.keys[*found] != *name)
          return Fail(SourceError::kUnsupportedOperation, field.pc);
        ++end;
      }
      begin = end;
    }
    return {};
  } catch (const std::bad_alloc&) {
    return Fail(SourceError::kResourceLimit);
  } catch (const std::length_error&) {
    return Fail(SourceError::kResourceLimit);
  }
}
}  // namespace abcd::source
