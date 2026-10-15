#include "abcd/analysis/class_method_sites.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>

#include "abcd/analysis/class_creation.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/abc/string_entity.h"

namespace abcd::analysis {
namespace {
struct ClassReference {
  abc::EntityOffset creator;
  abc::CodeOffset pc;
  abc::EntityOffset literal;
  ClassCreation creation;
};
template <class T, class... Alternatives>
T Take(std::variant<Alternatives...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::visit([](const auto& error) {
    if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, T>) throw ClassSiteFailure{error};
  }, result);
  throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
}
class SiteBudget {
 public:
  explicit SiteBudget(SemanticBudget budget) : remaining_(budget) {}
  void Step(std::uint64_t count = 1) {
    if (count > remaining_.work) throw ClassSiteFailure{ClassSiteError::kWorkLimit};
    remaining_.work -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    remaining_.bytes -= count * width;
  }
  [[nodiscard]] std::size_t Bytes() const { return remaining_.bytes; }
  [[nodiscard]] std::uint64_t Work() const { return remaining_.work; }
 private:
  SemanticBudget remaining_;
};
// The allocator charges the actual shared-control-block allocation before new.
// Deallocation never touches the construction-only budget pointer.
template<class T> struct IndexAllocator {
  using value_type=T;
  SiteBudget* budget;
  std::size_t* allocated=nullptr;
  explicit IndexAllocator(SiteBudget& b,std::size_t* a):budget(&b),allocated(a){}
  template<class U> IndexAllocator(const IndexAllocator<U>& o):budget(o.budget),allocated(o.allocated){}
  T* allocate(std::size_t n){budget->Admit(n,sizeof(T));*allocated+=n*sizeof(T);return std::allocator<T>{}.allocate(n);}
  void deallocate(T* p,std::size_t n){std::allocator<T>{}.deallocate(p,n);}
  template<class U> bool operator==(const IndexAllocator<U>& o)const{return budget==o.budget;}
};
template <class Visit>
void ScanClasses(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, SiteBudget& budget, Visit visit) {
  for (const auto& method : index.methods) {
    budget.Step();
    if (!method.code) continue;
    budget.Step(method.code->instruction_bytes);
    const auto body = Take<abc::MethodBody>(abc::DecodeMethodBody(file, header.profile, *method.code, {budget.Bytes()}));
    for (const auto& instruction : body.instructions) {
      budget.Step();
      const auto lifted = LiftClassCreation(instruction.decoded);
      const auto* creation = std::get_if<ClassCreation>(&lifted);
      if (!creation) {
        if (std::get<CreationError>(lifted) != CreationError::kUnsupportedOpcode)
          throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
        continue;
      }
      const auto registers = static_cast<std::uint64_t>(method.code->registers) + method.code->arguments;
      if (creation->parent.value >= registers)
        throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
      visit(method.offset, instruction.offset, *creation);
    }
  }
}
template <class Visit>
void ScanMembers(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    std::span<const ClassReference> references, SiteBudget& budget, Visit visit) {
  for (std::size_t first = 0; first < references.size();) {
    budget.Step();
    auto end = first + 1;
    while (end < references.size() && references[end].literal == references[first].literal && references[end].creation.kind == references[first].creation.kind) { budget.Step(); ++end; }
    const auto literal = Take<abc::LiteralArray>(abc::ReadLiteralArray(file, header, references[first].literal,
                                                                    {budget.Bytes()}));
    budget.Step(literal.entries.size());
    if (literal.entries.capacity() > budget.Bytes() / sizeof(abc::LiteralEntry))
      throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    const auto bytes = budget.Bytes() - literal.entries.capacity() * sizeof(abc::LiteralEntry);
    const auto members = Take<abc::ClassBuffer>(abc::DecodeClassLiteral(literal, references[first].creation.kind, {bytes}));
    if (members.properties.size() > std::numeric_limits<std::uint32_t>::max())
      throw ClassSiteFailure{ClassSiteError::kResourceLimit};
    for (std::size_t p = 0; p < members.properties.size(); ++p) {
      budget.Step();
      const auto& property = members.properties[p];
      const auto* method = std::get_if<abc::ClassMethod>(&property.value);
      if (!method) continue;
      const auto target = lookup.Find(method->target);
      if (!target || target->value >= index.methods.size() || index.methods[target->value].offset != method->target)
        throw ClassSiteFailure{ClassSiteError::kTargetMissing};
      visit(references.subspan(first, end - first), static_cast<std::uint32_t>(p), property, *method);
    }
    first = end;
  }
}
}  // namespace

ClassSiteRange ClassMethodSites::Find(abc::EntityOffset method) const {
  const auto first = std::lower_bound(sites_.begin(), sites_.end(), method,
      [](const auto& site, auto target) { return site.target < target; });
  const auto end = std::upper_bound(first, sites_.end(), method,
      [](auto target, const auto& site) { return target < site.target; });
  return {{static_cast<std::uint32_t>(first - sites_.begin())}, static_cast<std::uint32_t>(end - first)};
}
std::optional<ClassSiteId> ClassMethodSites::FindOccurrence(abc::EntityOffset target,
    abc::EntityOffset creator, abc::CodeOffset pc, std::uint32_t property) const {
  const auto key = std::tuple{target, creator, pc, property};
  const auto found = std::lower_bound(sites_.begin(), sites_.end(), key,
      [](const auto& site, const auto& value) {
        return std::tuple{site.target, site.creator, site.pc, site.property} < value;
      });
  if (found == sites_.end() || std::tuple{found->target, found->creator, found->pc, found->property} != key)
    return {};
  return ClassSiteId{static_cast<std::uint32_t>(found - sites_.begin())};
}
std::shared_ptr<const IndexedSendableLiteral> ClassMethodSites::SendableLiteral(abc::EntityOffset literal) const {
  const auto found = std::lower_bound(sendable_.begin(),sendable_.end(),literal,
      [](const auto& entry,auto key){return entry->literal<key;});
  return found!=sendable_.end() && (*found)->literal==literal ? *found : nullptr;
}
std::variant<ClassMethodSites, ClassSiteFailure> ClassMethodSites::Build(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    SemanticBudget allowance) {
  try {
    if (header.kind != abc::AbcKind::kDynamic || !std::holds_alternative<abc::DynamicLayout>(header.layout))
      return ClassSiteFailure{abc::MetadataError::kUnsupportedProfile};
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return ClassSiteFailure{abc::MetadataError::kFileChanged};
    SiteBudget budget(allowance);
    budget.Admit(1, sizeof(ClassMethodSites) + sizeof(std::vector<ClassReference>));
    std::size_t references_count = 0;
    ScanClasses(file, header, index, budget, [&](auto, auto, auto) {
      if (references_count >= budget.Bytes() / sizeof(ClassReference))
        throw ClassSiteFailure{ClassSiteError::kResourceLimit};
      ++references_count;
    });
    budget.Admit(references_count, sizeof(ClassReference));
    std::vector<ClassReference> references;
    references.reserve(references_count);
    budget.Admit(references.capacity() - references_count, sizeof(ClassReference));
    ScanClasses(file, header, index, budget, [&](auto method, auto pc, auto creation) {
      if (references.size() == references_count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
      const auto target = Take<abc::PoolReference>(abc::ResolvePoolReference(
          file, header, index, method, creation.literal, abc::PoolEntityKind::kLiteralArray));
      references.push_back({method, pc, target.offset, creation});
    });
    if (references.size() != references_count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
    std::sort(references.begin(), references.end(), [&](const auto& a, const auto& b) {
      budget.Step(); return std::tuple{a.literal, a.creation.kind, a.creator, a.pc} < std::tuple{b.literal, b.creation.kind, b.creator, b.pc};
    });
    // Decode each unique buffer twice: first admit the exact occurrence output,
    // then populate it. Reused buffers are never decoded per member or method.
    std::size_t count = 0;
    ScanMembers(file, header, index, lookup, references, budget,
        [&](auto sites, auto, const auto&, const auto&) {
          const auto limit = budget.Bytes() / sizeof(ClassMethodSite);
          if (sites.size() > limit - count ||
              sites.size() > std::numeric_limits<std::uint32_t>::max() - count)
            throw ClassSiteFailure{ClassSiteError::kResourceLimit};
          count += sites.size();
        });
    ClassMethodSites result;
    budget.Admit(count, sizeof(ClassMethodSite));
    result.sites_.reserve(count);
    budget.Admit(result.sites_.capacity() - count, sizeof(ClassMethodSite));
    ScanMembers(file, header, index, lookup, references, budget,
        [&](auto sites, auto property_index, const auto& property, const auto& method) {
          for (const auto& site : sites) {
            budget.Step();
            if (result.sites_.size() >= count) throw ClassSiteFailure{abc::MetadataError::kFileChanged};
            result.sites_.push_back({method.target, site.creator, site.pc, site.literal,
                                    property_index, property.key, method.kind, property.placement, method.length, site.creation});
          }
        });
    if (result.sites_.size() != count || file.CheckUnchanged())
      throw ClassSiteFailure{abc::MetadataError::kFileChanged};
    std::sort(result.sites_.begin(), result.sites_.end(), [&](const auto& a, const auto& b) {
      budget.Step();
      return std::tuple{a.target, a.creator, a.pc, a.property} < std::tuple{b.target, b.creator, b.pc, b.property};
    });
    // Shared immutable original literal index. Creators already own their
    // decoded SemanticMethod, so no per-creation body decoding is necessary.
    std::size_t literals = 0;
    for (std::size_t i = 0; i < references.size(); ++i) {
      budget.Step();
      if (references[i].creation.kind == abc::ClassKind::kSendable &&
          (!i || references[i-1].literal != references[i].literal ||
           references[i-1].creation.kind != references[i].creation.kind)) ++literals;
    }
    budget.Admit(literals, sizeof(decltype(result.sendable_)::value_type)); result.sendable_.reserve(literals);
    budget.Admit(result.sendable_.capacity()-literals, sizeof(decltype(result.sendable_)::value_type));
    for (std::size_t i = 0; i < references.size(); ++i) {
      budget.Step(); const auto& reference = references[i];
      if (reference.creation.kind != abc::ClassKind::kSendable ||
          (i && references[i-1].literal == reference.literal && references[i-1].creation.kind == reference.creation.kind)) continue;
      const auto literal = Take<abc::LiteralArray>(abc::ReadLiteralArray(file, header, reference.literal, {budget.Bytes()}));
      budget.Admit(literal.entries.capacity(), sizeof(abc::LiteralEntry)); budget.Step(literal.entries.size());
      auto members = Take<abc::ClassBuffer>(abc::DecodeClassLiteral(literal, reference.creation.kind, {budget.Bytes()}));
      budget.Admit(members.properties.capacity(), sizeof(abc::ClassProperty));
      if (!members.field_types) throw ClassSiteFailure{ClassSiteError::kMalformedCreation};
      const auto field_literal = Take<abc::LiteralArray>(abc::ReadLiteralArray(file, header, *members.field_types, {budget.Bytes()}));
      budget.Admit(field_literal.entries.capacity(), sizeof(abc::LiteralEntry)); budget.Step(field_literal.entries.size());
      auto decoded=abc::DecodeSendableFields(field_literal,{budget.Bytes(),budget.Work()});
      std::optional<abc::SendableFieldFailure> field_failure;
      std::optional<std::uint32_t> field_mask;
      abc::SendableFields fields;
      if(const auto* error=std::get_if<abc::SendableFieldFailure>(&decoded)) {
        if(error->kind==abc::SendableFieldError::kResourceLimit || error->kind==abc::SendableFieldError::kWorkLimit)
          throw ClassSiteFailure{*error};
        field_failure=*error;
        if(error->field) {
          const auto mask=std::size_t(*error->field)*2+1;
          if(mask<field_literal.entries.size())if(const auto* raw=std::get_if<abc::LiteralBits>(&field_literal.entries[mask].value);raw && raw->value<=UINT32_MAX)
            field_mask=static_cast<std::uint32_t>(raw->value);
        }
        // DecodeSendableFields performs one fixed step and at most one per
        // pair; failed attempts have no retained arena. Charge that bound.
        budget.Step(field_literal.entries.size()+1);
      } else {
        fields=std::get<abc::SendableFields>(std::move(decoded));
        budget.Admit(fields.allocated_bytes,1);budget.Step(fields.work);
      }
      // Resolve each distinct key once: aliases of a long string do not
      // multiply input reads or retained text by the number of fields.
      budget.Admit(fields.fields.size(),sizeof(abc::EntityOffset));
      std::vector<abc::EntityOffset> keys;keys.reserve(fields.fields.size());
      budget.Admit(keys.capacity()-fields.fields.size(),sizeof(abc::EntityOffset));
      for(const auto& field:fields.fields){budget.Step();keys.push_back(std::get<abc::EntityOffset>(field.key.value));}
      std::sort(keys.begin(),keys.end(),[&](auto a,auto b){budget.Step();return a<b;});
      keys.erase(std::unique(keys.begin(),keys.end(),[&](auto a,auto b){budget.Step();return a==b;}),keys.end());
      budget.Admit(keys.size(),sizeof(SendableFieldName));
      std::vector<SendableFieldName> names;names.reserve(keys.size());
      budget.Admit(names.capacity()-keys.size(),sizeof(SendableFieldName));
      for(const auto key:keys) {
        budget.Step();auto text=Take<std::u16string>(abc::ReadStringEntity(file,header,key,{budget.Bytes()}));
        budget.Step(text.size());budget.Admit(text.capacity()+1,sizeof(char16_t));names.push_back({key,std::move(text)});
      }
      auto original=std::allocate_shared<IndexedSendableLiteral>(IndexAllocator<IndexedSendableLiteral>{budget,&result.allocated_bytes_},
          IndexedSendableLiteral{reference.literal,std::move(members),std::move(fields),std::move(names),field_failure,field_mask});
      result.sendable_.push_back(std::move(original));
    }
    result.allocated_bytes_ += result.sites_.capacity()*sizeof(ClassMethodSite) + result.sendable_.capacity()*sizeof(decltype(result.sendable_)::value_type);
    for (const auto& literal : result.sendable_) {
      result.allocated_bytes_ +=
          literal->members.properties.capacity()*sizeof(abc::ClassProperty) + literal->fields.fields.capacity()*sizeof(abc::SendableField) +
          literal->field_names.capacity()*sizeof(SendableFieldName);
      for(const auto& name:literal->field_names)result.allocated_bytes_+=(name.text.capacity()+1)*sizeof(char16_t);
    }
    result.work_ = allowance.work - budget.Work();
    return result;
  } catch (const ClassSiteFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return ClassSiteFailure{ClassSiteError::kResourceLimit};
  } catch (const std::length_error&) { return ClassSiteFailure{ClassSiteError::kResourceLimit}; }
}
}  // namespace abcd::analysis
