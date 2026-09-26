#include "creation_tree_context.h"

#include <new>
#include <stdexcept>

#include "runtime_name.h"

namespace abcd::source {
namespace {
using Error = CreationContextError;
void Require(bool condition, Error error) { if (!condition) throw CreationContextFailure{error}; }
}
std::variant<CreationTreeContext, CreationContextFailure> CreationTreeContext::Resolve(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    analysis::FunctionSiteId site, RouteInputIdentity identity, analysis::SemanticBudget budget) {
  try {
    Require(header.profile == abc::ProfileId::kDynamic1202 || header.profile == abc::ProfileId::kDynamic1206 ||
        header.profile == abc::ProfileId::kDynamic1301, Error::kProfile);
    Require(identity.profile == header.profile && identity.abc_version == header.version &&
        identity.file_size == header.file_size, Error::kProfile);
    if (file.Size() != header.file_size || file.CheckUnchanged())
      return CreationContextFailure{abc::MetadataError::kFileChanged};
    const auto* occurrence = sites.At(site);
    Require(occurrence != nullptr, Error::kSite);
    const auto position = lookup.Find(occurrence->target);
    const auto caller = lookup.Find(occurrence->creator);
    Require(position && caller && position->value < index.methods.size() && caller->value < index.methods.size() &&
        index.methods[position->value].offset == occurrence->target &&
        occurrence->resolved.position == *position, Error::kSite);
    const auto& method = index.methods[position->value];
    Require(method.code.has_value() && method.owner_offset == index.methods[caller->value].owner_offset, Error::kSite);
    // Shared MethodLiteral kinds can be refined by class materialization.
    // Require an unambiguous non-class role for both normal and arrow targets.
    Require(roles.At(*position) == analysis::MethodRole::kUnseen, Error::kRole);
    const auto& creation = occurrence->creation;
    Require(creation.kind == analysis::CreationKind::kFunction &&
        creation.home_object == analysis::HomeObjectInput::kCurrentFunction &&
        !occurrence->resolved.attributes.sendable, Error::kKind);
    CallableKind callable;
    switch (occurrence->resolved.attributes.kind) {
      case abc::FunctionKind::kBaseConstructor: callable = CallableKind::kFunction; break;
      case abc::FunctionKind::kArrow: callable = CallableKind::kArrow; break;
      case abc::FunctionKind::kNone:
      case abc::FunctionKind::kGenerator:
      case abc::FunctionKind::kAsync:
      case abc::FunctionKind::kAsyncGenerator:
      case abc::FunctionKind::kAsyncArrow:
      case abc::FunctionKind::kConcurrent:
      default: throw CreationContextFailure{Error::kKind};
    }
    const auto name = RuntimeName(method);
    Require(name.has_value(), Error::kName);
    Require(method.name.size() < budget.work, Error::kWorkLimit);
    Require(sizeof(CreationTreeContext) <= budget.bytes, Error::kResourceLimit);
    auto bytes = budget.bytes - sizeof(CreationTreeContext);
    Require(name->size() < bytes / sizeof(char16_t), Error::kResourceLimit);
    CreationTreeContext result;
    result.name_.assign(name->begin(), name->end());
    Require(result.name_.capacity() < bytes / sizeof(char16_t), Error::kResourceLimit);
    bytes -= (result.name_.capacity() + 1) * sizeof(char16_t);
    const auto call = abc::ReadCallType(file, header, index, method.annotations, {bytes});
    if (const auto* error = std::get_if<abc::MetadataError>(&call)) return CreationContextFailure{*error};
    const auto frame = abc::BuildFrameLayout(method.code->registers, method.code->arguments,
        abc::DecodeCallFlags(std::get<abc::CallType>(call).value));
    if (const auto* error = std::get_if<abc::FrameError>(&frame)) return CreationContextFailure{*error};
    result.frame_ = std::get<abc::FrameLayout>(frame);
    Require(result.frame_.parameters >= creation.length.value, Error::kSignature);
    if (file.CheckUnchanged()) return CreationContextFailure{abc::MetadataError::kFileChanged};
    result.identity_ = identity; result.site_ = *occurrence;
    result.callable_ = callable; result.work_ = method.name.size() + 1;
    return result;
  } catch (const CreationContextFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return CreationContextFailure{Error::kResourceLimit};
  } catch (const std::length_error&) { return CreationContextFailure{Error::kResourceLimit}; }
}
}  // namespace abcd::source
