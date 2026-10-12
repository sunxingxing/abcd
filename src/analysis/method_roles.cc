#include "abcd/analysis/method_roles.h"

#include <algorithm>
#include <new>
#include <set>
#include <type_traits>
#include <vector>

#include "abcd/corpus/abc_file.h"

namespace abcd::analysis {
namespace {
template <typename T, typename... V>
T Take(std::variant<V...> value) {
  if (!std::holds_alternative<T>(value))
    std::visit(
        [](const auto& item) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(item)>, T>)
            throw MethodRoleFailure{item};
        },
        value);
  return std::get<T>(std::move(value));
}
void Admit(std::size_t count, std::size_t width, std::size_t& bytes) {
  if (count > bytes / width)
    throw MethodRoleFailure{MethodRoleError::kResourceLimit};
  bytes -= count * width;
}
template <typename Use>
void Scan(const corpus::AbcFile& file, const abc::Header& header,
          const abc::MetadataIndex& index, std::size_t bytes, Use use) {
  for (const auto& method : index.methods) {
    if (!method.code) continue;
    const auto body = Take<abc::MethodBody>(
        abc::DecodeMethodBody(file, header.profile, *method.code, {bytes}));
    for (const auto& instruction : body.instructions) {
      bool constructor = false;
      switch (instruction.decoded.opcode) {
        case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM8_ID16_ID16_IMM16_V8:
        case abc::Opcode::kDEFINECLASSWITHBUFFER_IMM16_ID16_ID16_IMM16_V8:
        case abc::Opcode::
            kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8:
          constructor = true;
          break;
        case abc::Opcode::kDEPRECATED_CREATEARRAYWITHBUFFER_PREF_IMM16:
        case abc::Opcode::kDEPRECATED_CREATEOBJECTWITHBUFFER_PREF_IMM16:
        case abc::Opcode::
            kDEPRECATED_DEFINECLASSWITHBUFFER_PREF_ID16_IMM16_IMM16_V8_V8:
          throw MethodRoleFailure{MethodRoleError::kLegacyLiteral};
        default:
          break;
      }
      for (const auto& operand : std::span(instruction.decoded.operands)
                                     .first(instruction.decoded.operand_count))
        if (operand.kind == abc::OperandKind::kLiteralArray ||
            (constructor && operand.kind == abc::OperandKind::kMethod))
          use(method.offset,
              abc::PoolIndex{static_cast<std::uint32_t>(operand.raw_bits)},
              operand.kind == abc::OperandKind::kLiteralArray
                  ? abc::PoolEntityKind::kLiteralArray
                  : abc::PoolEntityKind::kMethod);
    }
  }
}
std::optional<MethodRole> Role(abc::LiteralTag tag) {
  switch (tag) {
    case abc::LiteralTag::kMethod:
      return MethodRole::kOrdinary;
    case abc::LiteralTag::kGeneratorMethod:
      return MethodRole::kGenerator;
    case abc::LiteralTag::kAsyncGeneratorMethod:
    case abc::LiteralTag::kGetter:
    case abc::LiteralTag::kSetter:
      return MethodRole::kOther;
    // Nested arrays are traversed by the caller (offset form); the header
    // buffer-index form needs the header literal index and stays refused.
    case abc::LiteralTag::kLiteralArray:
      return {};
    case abc::LiteralTag::kLiteralBufferIndex:
      throw MethodRoleFailure{MethodRoleError::kNestedLiteral};
    case abc::LiteralTag::kBoolean:
    case abc::LiteralTag::kInteger:
    case abc::LiteralTag::kFloat:
    case abc::LiteralTag::kDouble:
    case abc::LiteralTag::kString:
    case abc::LiteralTag::kAccessor:
    case abc::LiteralTag::kMethodAffiliate:
    case abc::LiteralTag::kBuiltinTypeIndex:
    case abc::LiteralTag::kImplements:
    case abc::LiteralTag::kNull:
      return {};
  }
  throw MethodRoleFailure{abc::MetadataError::kUnknownTag};
}
}  // namespace
std::variant<MethodRoleIndex, MethodRoleFailure> MethodRoleIndex::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    MethodRoleBudget budget) {
  try {
    if (header.kind != abc::AbcKind::kDynamic ||
        !std::holds_alternative<abc::DynamicLayout>(header.layout))
      return MethodRoleFailure{abc::MetadataError::kUnsupportedProfile};
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return MethodRoleFailure{abc::MetadataError::kFileChanged};
    MethodRoleIndex result;
    result.count_ = index.methods.size();
    Admit(result.count_, sizeof(MethodRole), budget.bytes);
    result.roles_ = std::make_unique<MethodRole[]>(result.count_);
    // Count literal and constructor references before allocating; no hash table
    // or geometric resize peak. Constructor slots need no retained offset.
    std::size_t count = 0;
    Scan(file, header, index, budget.bytes,
         [&](abc::EntityOffset, abc::PoolIndex, abc::PoolEntityKind) {
           if (count >= budget.bytes / sizeof(abc::EntityOffset))
             throw MethodRoleFailure{MethodRoleError::kResourceLimit};
           ++count;
         });
    Admit(count, sizeof(abc::EntityOffset), budget.bytes);
    auto literals = std::make_unique<abc::EntityOffset[]>(count);
    const auto mark = [&](abc::EntityOffset target, MethodRole role) {
      const auto position = lookup.Find(target);
      if (!position)
        throw MethodRoleFailure{abc::MethodTargetError::kTargetMissing};
      if (position->value >= result.count_ ||
          index.methods[position->value].offset != target)
        throw MethodRoleFailure{abc::MethodTargetError::kIndexMismatch};
      auto& recorded = result.roles_[position->value];
      if (recorded == MethodRole::kUnseen)
        recorded = role;
      else if (recorded != role)
        recorded = MethodRole::kConflict;
    };
    std::size_t next = 0;
    Scan(file, header, index, budget.bytes,
         [&](abc::EntityOffset method, abc::PoolIndex slot,
             abc::PoolEntityKind kind) {
           if (next >= count)
             throw MethodRoleFailure{abc::MetadataError::kFileChanged};
           const auto target = Take<abc::PoolReference>(
                                   abc::ResolvePoolReference(
                                       file, header, index, method, slot, kind))
                                   .offset;
           if (kind == abc::PoolEntityKind::kMethod) {
             // JitCompilationEnv can copy CLASS_CONSTRUCTOR back to
             // MethodLiteral. Never certify a constructor target as an
             // exclusively ordinary method.
             mark(target, MethodRole::kOther);
           } else
             literals[next++] = target;
         });
    // Each literal array is read once, including arrays nested through
    // kLiteralArray entries (24.0 emits them). Ordered visited set and a
    // worklist: O(A log A) for A distinct arrays, bounded by the byte budget.
    std::set<abc::EntityOffset> visited;
    std::vector<abc::EntityOffset> pending(literals.get(), literals.get() + next);
    while (!pending.empty()) {
      const auto current = pending.back();
      pending.pop_back();
      if (!visited.insert(current).second) continue;
      Admit(1, sizeof(abc::EntityOffset) * 4, budget.bytes);
      const auto literal = Take<abc::LiteralArray>(
          abc::ReadLiteralArray(file, header, current, {budget.bytes}));
      for (const auto& entry : literal.entries) {
        if (entry.tag == abc::LiteralTag::kLiteralArray) {
          pending.push_back(std::get<abc::EntityOffset>(entry.value));
          continue;
        }
        const auto role = Role(entry.tag);
        if (!role) continue;
        const auto target = std::get<abc::EntityOffset>(entry.value);
        mark(target, *role);
      }
    }
    if (file.CheckUnchanged())
      return MethodRoleFailure{abc::MetadataError::kFileChanged};
    return result;
  } catch (const MethodRoleFailure& failure) {
    return failure;
  } catch (const std::bad_alloc&) {
    return MethodRoleFailure{MethodRoleError::kResourceLimit};
  }
}
}  // namespace abcd::analysis
