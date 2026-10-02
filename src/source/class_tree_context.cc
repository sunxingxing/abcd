#include "class_tree_context.h"

#include <algorithm>
#include <new>
#include <stdexcept>
#include <type_traits>

#include "abcd/abc/string_entity.h"
#include "element_name.h"
#include "runtime_name.h"

namespace abcd::source {
namespace {
using Error = ClassContextError;
void Require(bool condition, Error error) { if (!condition) throw ClassContextFailure{error}; }
template <class T, class... Alternatives>
T Take(std::variant<Alternatives...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::visit([](const auto& error) {
    if constexpr (!std::is_same_v<std::decay_t<decltype(error)>, T>) throw ClassContextFailure{error};
  }, result);
  throw ClassContextFailure{Error::kSite};
}
}  // namespace

std::variant<ClassTreeContext, ClassContextFailure> ClassTreeContext::Resolve(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& sites,
    analysis::ClassSiteId site, RouteInputIdentity identity,
    analysis::SemanticBudget budget) {
  try {
    auto work = budget.work;
    const auto step = [&](std::uint64_t count = 1) {
      Require(count <= work, Error::kWorkLimit); work -= count;
    };
    step();
    Require(header.profile == abc::ProfileId::kDynamic1202 ||
            header.profile == abc::ProfileId::kDynamic1206 ||
            header.profile == abc::ProfileId::kDynamic1301, Error::kProfile);
    Require(identity.profile == header.profile && identity.file_size == header.file_size &&
            identity.abc_version == header.version, Error::kProfile);
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return ClassContextFailure{abc::MetadataError::kFileChanged};
    const auto* occurrence = sites.At(site);
    Require(occurrence != nullptr, Error::kSite);
    const auto position = lookup.Find(occurrence->target);
    Require(position && position->value < index.methods.size() &&
            index.methods[position->value].offset == occurrence->target, Error::kSite);
    const bool generator_tag = occurrence->kind == abc::ClassMethodKind::kGenerator;
    Require(roles.At(*position) == (generator_tag ? analysis::MethodRole::kGenerator : analysis::MethodRole::kOrdinary), Error::kRole);
    const auto& method = index.methods[position->value];
    Require(method.code.has_value(), Error::kSite);
    const auto attributes = Take<abc::MethodAttributes>(abc::DecodeMethodAttributes(method.access_flags, header.profile));
    // OpenHarmony-v7.0-Release LiteralDataExtractor::DefineFunctionTemplate
    // refines NONE using the class-buffer tag. MethodRoleIndex excludes all
    // competing template/constructor roles; initial NONE alone is insufficient.
    // NONE is refined by METHOD/GENERATORMETHOD; an explicit runtime kind is
    // preserved. Require one consistent file-wide template role before using
    // that refinement. Body normalization remains shared with non-method owners.
    Require(!attributes.sendable && (generator_tag || occurrence->kind == abc::ClassMethodKind::kOrdinary), Error::kKind);
    CallableKind callable;
    switch (attributes.kind) {
      case abc::FunctionKind::kNone:
        callable = generator_tag ? CallableKind::kGeneratorMethod : CallableKind::kMethod;
        break;
      case abc::FunctionKind::kAsync:
        Require(!generator_tag, Error::kKind);
        callable = CallableKind::kAsyncMethod;
        break;
      case abc::FunctionKind::kGenerator:
        callable = CallableKind::kGeneratorMethod;
        break;
      default: throw ClassContextFailure{Error::kKind};
    }
    Require(sizeof(ClassTreeContext) <= budget.bytes, Error::kResourceLimit);
    auto bytes = budget.bytes - sizeof(ClassTreeContext);
    const auto* key = std::get_if<abc::EntityOffset>(&occurrence->key.value);
    Require(occurrence->key.tag == abc::LiteralTag::kString && key, Error::kName);
    auto name = Take<std::u16string>(abc::ReadStringEntity(file, header, *key, {bytes}));
    Require(name.capacity() <= bytes / sizeof(char16_t), Error::kResourceLimit);
    bytes -= name.capacity() * sizeof(char16_t);
    Require(sizeof(char16_t) <= bytes, Error::kResourceLimit);
    bytes -= sizeof(char16_t);
    step(name.size() + method.name.size());
    const auto runtime_name = RuntimeName(method);
    Require(runtime_name && name.size() == runtime_name->size() &&
            std::equal(name.begin(), name.end(), runtime_name->begin()) && !IsElementName(name), Error::kName);
    // Pinned class-layout special properties are not ordinary member syntax.
    Require(occurrence->placement == abc::ClassPlacement::kPrototype
        ? name != u"constructor" : name != u"name" && name != u"length" && name != u"prototype", Error::kName);
    const auto call = Take<abc::CallType>(abc::ReadCallType(file, header, index, method.annotations, {bytes}));
    const auto frame = Take<abc::FrameLayout>(abc::BuildFrameLayout(
        method.code->registers, method.code->arguments, abc::DecodeCallFlags(call.value)));
    // A shorter encoded length requires independently proved native defaults
    // in VerifiedFunction; metadata alone does not authorize their emission.
    Require(frame.parameters >= occurrence->length, Error::kSignature);
    if (file.CheckUnchanged()) return ClassContextFailure{abc::MetadataError::kFileChanged};
    ClassTreeContext result;
    result.identity_ = identity;
    result.site_ = *occurrence;
    result.occurrence_ = site;
    result.position_ = *position;
    result.frame_ = frame;
    result.callable_ = callable;
    result.name_ = std::move(name);
    result.work_ = budget.work - work;
    return result;
  } catch (const ClassContextFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return ClassContextFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return ClassContextFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
