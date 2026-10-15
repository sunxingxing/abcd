#include "sendable_metadata.h"
#include "abcd/abc/pool_reference.h"
#include "abcd/abc/string_entity.h"
#include <algorithm>
#include <new>
#include <stdexcept>
namespace abcd::source {
std::variant<SendableMetadata, SendableFailure> ReadSendableMetadata(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset record, abc::EntityOffset method, abc::CodeOffset pc,
    analysis::SemanticBudget budget) {
  using Error = SendableError;
  const auto initial = budget;
  SendableFailure at;
  at.record = record; at.method = method;
  try {
    const auto require = [&](bool ok, Error kind) { if (!ok) { at.kind = kind; throw at; } };
    const auto step = [&](std::uint64_t n = 1) { require(n <= budget.work, Error::kWorkLimit); budget.work -= n; };
    const auto admit = [&](std::size_t n, std::size_t width = 1) { require(n <= budget.bytes / width, Error::kResourceLimit); budget.bytes -= n * width; };
    const auto take = [&]<class T, class... V>(std::variant<T, V...> value, Error kind) -> T {
      if (auto* ready = std::get_if<T>(&value)) return std::move(*ready);
      std::visit([&](const auto& e) {
        if constexpr (!std::is_same_v<std::decay_t<decltype(e)>, T>) {
          at.kind = kind;
          if constexpr (std::is_enum_v<std::decay_t<decltype(e)>>) at.cause = e;
          else at.cause = e.kind;
          if constexpr (std::is_same_v<std::decay_t<decltype(e)>, abc::SendableFieldFailure>) at.field = e.field;
          if constexpr (std::is_same_v<std::decay_t<decltype(e)>, abc::LiteralFailure>)
            if (e.kind == abc::LiteralError::kResourceLimit) at.kind = Error::kResourceLimit;
          if constexpr (std::is_same_v<std::decay_t<decltype(e)>, abc::BodyFailure>)
            if (e.kind == abc::BodyError::kResourceLimit) at.kind = Error::kResourceLimit;
          if constexpr (std::is_same_v<std::decay_t<decltype(e)>, abc::SendableFieldFailure>) {
            if (e.kind == abc::SendableFieldError::kResourceLimit) at.kind = Error::kResourceLimit;
            if (e.kind == abc::SendableFieldError::kWorkLimit) at.kind = Error::kWorkLimit;
          }
          using E = std::decay_t<decltype(e)>;
          if constexpr (std::is_enum_v<E>)
            if constexpr (requires { E::kResourceLimit; })
              if (e == E::kResourceLimit) at.kind = Error::kResourceLimit;
          throw at;
        }
      }, value);
      throw at;
    };
    admit(sizeof(SendableMetadata)); step();
    if (file.Size() != header.file_size || file.CheckUnchanged()) {
      at.kind = Error::kOwner; at.cause = abc::MetadataError::kFileChanged; throw at;
    }
    const auto position = lookup.Find(method);
    require(position && position->value < index.methods.size(), Error::kOwner);
    const auto& owner = index.methods[position->value];
    require(owner.offset == method && owner.owner_offset == record && owner.code, Error::kOwner);
    // Charge decoding dimensions before the nested reader. Its budget is an
    // exclusive loan; no parent allocation overlaps it before actual transfer.
    step(owner.code->instruction_bytes);
    admit(owner.code->instruction_bytes);
    auto body = take(abc::DecodeMethodBody(file, header.profile, *owner.code, {budget.bytes}), Error::kCreation);
    admit(body.instructions.capacity(), sizeof(abc::BodyInstruction));
    admit(body.tries.capacity(), sizeof(abc::TryRegion));
    admit(body.handlers.capacity(), sizeof(abc::CatchHandler));
    const auto site = std::lower_bound(body.instructions.begin(), body.instructions.end(), pc,
        [](const auto& a, abc::CodeOffset b) { return a.offset < b; });
    require(site != body.instructions.end() && site->offset == pc, Error::kCreation);
    require(site->decoded.opcode == abc::Opcode::kCALLRUNTIME_DEFINESENDABLECLASS_PREF_IMM16_ID16_ID16_IMM16_V8, Error::kCreation);
    at.pc = pc;
    const auto creation = take(analysis::LiftClassCreation(site->decoded), Error::kCreation);
    require(creation.kind == abc::ClassKind::kSendable, Error::kCreation);
    at.pc = pc; at.pool = creation.constructor;
    const auto own_method = [&](abc::EntityOffset target) {
      step(); const auto p = lookup.Find(target);
      require(p && p->value < index.methods.size() && index.methods[p->value].offset == target &&
          index.methods[p->value].owner_offset == record, Error::kOwner);
    };
    const auto ctor = take(abc::ResolvePoolReference(file, header, index, method,
        creation.constructor, abc::PoolEntityKind::kMethod), Error::kPool);
    at.constructor = ctor.offset; own_method(ctor.offset);
    at.pool = creation.literal;
    const auto literal = take(abc::ResolvePoolReference(file, header, index, method,
        creation.literal, abc::PoolEntityKind::kLiteralArray), Error::kPool);
    at.literal = literal.offset;
    auto members_input = take(abc::ReadLiteralArray(file, header, literal.offset, {budget.bytes}), Error::kClassLiteral);
    step(members_input.entries.size()); admit(members_input.entries.capacity(), sizeof(abc::LiteralEntry));
    auto members = take(abc::DecodeClassLiteral(members_input, creation.kind, {budget.bytes}), Error::kClassLiteral);
    admit(members.properties.capacity(), sizeof(abc::ClassProperty));
    require(members.field_types.has_value() && members.properties.size() <= UINT32_MAX, Error::kClassLiteral);
    at.field_types = members.field_types; at.member_count = static_cast<std::uint32_t>(members.properties.size());
    for (const auto& member : members.properties) {
      step(); if (const auto* m = std::get_if<abc::ClassMethod>(&member.value)) own_method(m->target);
    }
    auto field_input = take(abc::ReadLiteralArray(file, header, *members.field_types, {budget.bytes}), Error::kFieldTypes);
    step(field_input.entries.size()); admit(field_input.entries.capacity(), sizeof(abc::LiteralEntry));
    auto decoded_fields = abc::DecodeSendableFields(field_input, {budget.bytes, budget.work});
    if (const auto* error = std::get_if<abc::SendableFieldFailure>(&decoded_fields); error && error->field) {
      const auto mask_index = static_cast<std::size_t>(*error->field) * 2 + 1;
      if (mask_index < field_input.entries.size())
        if (const auto* raw = std::get_if<abc::LiteralBits>(&field_input.entries[mask_index].value); raw && raw->value <= UINT32_MAX)
          at.mask = static_cast<std::uint32_t>(raw->value);
    }
    auto fields = take(std::move(decoded_fields), Error::kFieldTypes);
    admit(fields.allocated_bytes); step(fields.work);
    at.field_count = static_cast<std::uint32_t>(fields.fields.size()); at.instance_count = fields.instance_count;
    // Validate each distinct key once. Aliased long strings must not turn field
    // validation into quadratic work; declaration order remains in fields.
    require(members.properties.size() <= (budget.bytes / sizeof(abc::EntityOffset)), Error::kResourceLimit);
    require(members.properties.size() <= SIZE_MAX - fields.fields.size(), Error::kResourceLimit);
    const auto key_count = fields.fields.size() + members.properties.size();
    admit(key_count, sizeof(abc::EntityOffset));
    std::vector<abc::EntityOffset> keys; keys.reserve(key_count);
    admit(keys.capacity() - key_count, sizeof(abc::EntityOffset));
    for (const auto& f : fields.fields) { step(); keys.push_back(std::get<abc::EntityOffset>(f.key.value)); }
    for (const auto& m : members.properties) {
      step(); if (m.key.tag == abc::LiteralTag::kString) keys.push_back(std::get<abc::EntityOffset>(m.key.value));
    }
    std::sort(keys.begin(), keys.end(), [&](auto a, auto b) { step(); return a < b; });
    keys.erase(std::unique(keys.begin(), keys.end(), [&](auto a, auto b) { step(); return a == b; }), keys.end());
    for (auto key : keys) {
      step(); auto text = take(abc::ReadStringEntity(file, header, key, {budget.bytes}), Error::kFieldTypes);
      step(text.size()); admit(text.capacity() + 1, sizeof(char16_t));
    }
    if (file.CheckUnchanged()) { at.kind = Error::kOwner; at.cause = abc::MetadataError::kFileChanged; throw at; }
    SendableMetadata result{creation, std::move(members), std::move(fields), at};
    result.allocated_bytes = initial.bytes - budget.bytes;
    result.work = initial.work - budget.work;
    return result;
  } catch (const SendableFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { at.kind = Error::kResourceLimit; return at;
  } catch (const std::length_error&) { at.kind = Error::kResourceLimit; return at; }
}
namespace {
bool SameEntry(const abc::LiteralEntry& a, const abc::LiteralEntry& b) {
  if (a.tag != b.tag || a.value.index() != b.value.index()) return false;
  if (const auto* id = std::get_if<abc::EntityOffset>(&a.value)) return *id == std::get<abc::EntityOffset>(b.value);
  return std::get<abc::LiteralBits>(a.value).value == std::get<abc::LiteralBits>(b.value).value;
}
}
std::optional<SendableFailure> VerifySendableMetadata(const SendableMetadata& evidence,
    const corpus::AbcFile& file, const abc::Header& header, const abc::MetadataIndex& index,
    const abc::MethodLookup& lookup, abc::EntityOffset record, abc::EntityOffset method,
    abc::CodeOffset pc, analysis::SemanticBudget budget) {
  // Caller retains and charges evidence. This reader gets only additional
  // scratch, and recomputes all identities from the original occurrence.
  auto read = ReadSendableMetadata(file, header, index, lookup, record, method, pc, budget);
  const auto* original = std::get_if<SendableMetadata>(&read);
  if (!original) return std::get<SendableFailure>(std::move(read));
  auto failure = original->location; failure.kind = SendableError::kCorrespondence;
  const auto extra = original->fields.fields.size() + original->members.properties.size();
  if (original->work > budget.work || extra > budget.work - original->work) { failure.kind = SendableError::kWorkLimit; return failure; }
  if (original->location != evidence.location || original->creation != evidence.creation ||
      original->members.kind != evidence.members.kind || original->members.field_types != evidence.members.field_types ||
      original->members.properties.size() != evidence.members.properties.size() ||
      original->fields.instance_count != evidence.fields.instance_count || original->fields.fields.size() != evidence.fields.fields.size()) return failure;
  for (std::size_t i = 0; i < original->members.properties.size(); ++i) {
    const auto& a = original->members.properties[i]; const auto& b = evidence.members.properties[i];
    if (a.placement != b.placement || !SameEntry(a.key, b.key) || a.value.index() != b.value.index()) return failure;
    if (const auto* m = std::get_if<abc::ClassMethod>(&a.value)) {
      const auto& n = std::get<abc::ClassMethod>(b.value);
      if (m->target != n.target || m->kind != n.kind || m->length != n.length) return failure;
    } else if (!SameEntry(std::get<abc::LiteralEntry>(a.value), std::get<abc::LiteralEntry>(b.value))) return failure;
  }
  for (std::size_t i = 0; i < original->fields.fields.size(); ++i) {
    const auto& a = original->fields.fields[i]; const auto& b = evidence.fields.fields[i];
    if (!SameEntry(a.key, b.key) || a.placement != b.placement || a.type.bits != b.type.bits) {
      failure.field = static_cast<std::uint32_t>(i); failure.mask = b.type.bits; return failure;
    }
  }
  return {};
}
SharedModuleFailure InspectSharedModule(const RecordModuleContext& context,
    const corpus::AbcFile& file, abc::EntityOffset record, SourceContract contract) {
  SharedModuleFailure at; at.record = record;
  if (file.CheckUnchanged()) { at.kind = SharedModuleError::kOwner; at.cause = abc::MetadataError::kFileChanged; return at; }
  const auto* attributes = context.Attributes(file, record);
  const auto module = context.ForOwner(file, record);
  if (!attributes || !module) { at.kind = SharedModuleError::kOwner; return at; }
  at.shared = attributes->shared; at.module = attributes->module; at.request_phases = attributes->request_phases;
  const auto count = [&](std::size_t n, std::uint32_t& out) {
    if (n > UINT32_MAX) return false;
    out = static_cast<std::uint32_t>(n); return true;
  };
  if (!count(module->requests.size(), at.requests) || !count(module->imports.size(), at.imports) ||
      !count(module->namespace_imports.size(), at.namespace_imports) || !count(module->local_exports.size(), at.local_exports) ||
      !count(module->indirect_exports.size(), at.indirect_exports) || !count(module->star_exports.size(), at.star_exports)) {
    at.kind = SharedModuleError::kResourceLimit; return at;
  }
  at.kind = SharedModuleOutput(contract) == SharedModuleOutputPolicy::kArkDirectiveV1 ?
      SharedModuleError::kPreparationGate : SharedModuleError::kOutputPolicy;
  return at;
}
}
