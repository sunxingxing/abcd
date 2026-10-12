#include "creation_tree_context.h"
#include "abcd/abc/profile_capabilities.h"

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
    analysis::FunctionSiteId site, RouteInputIdentity identity, analysis::SemanticBudget budget,
    CreationContextMode mode) {
  try {
    Require(abc::HasPinnedRuntimeSemantics(header.profile), Error::kProfile);
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
    // Require an unambiguous non-class role for ordinary, arrow and async targets.
    Require(roles.At(*position) == analysis::MethodRole::kUnseen, Error::kRole);
    const auto& creation = occurrence->creation;
    Require(!occurrence->resolved.attributes.sendable, Error::kKind);
    CallableKind callable;
    if (mode == CreationContextMode::kMethodBody) {
      // OH7 ObjectFactory::NewJSFunctionForDefineMethod selects AsyncFunction
      // for ASYNC_FUNCTION, the no-prototype class for NONE, and installs the
      // actual accumulator home. Keep that callable kind through common body
      // normalization; async methods must not become synchronous methods.
      // This enables common body analysis only; it does not authorize emitting
      // an ordinary function or replacing a home-object/super relationship.
      Require(creation.kind == analysis::CreationKind::kMethod &&
          creation.home_object == analysis::HomeObjectInput::kAccumulator, Error::kKind);
      switch (occurrence->resolved.attributes.kind) {
        case abc::FunctionKind::kNone: callable = CallableKind::kMethod; break;
        case abc::FunctionKind::kAsync: callable = CallableKind::kAsyncMethod; break;
        default: throw CreationContextFailure{Error::kKind};
      }
    } else {
      Require(mode == CreationContextMode::kFunction && creation.kind == analysis::CreationKind::kFunction &&
          creation.home_object == analysis::HomeObjectInput::kCurrentFunction, Error::kKind);
      switch (occurrence->resolved.attributes.kind) {
      case abc::FunctionKind::kBaseConstructor: callable = CallableKind::kFunction; break;
      case abc::FunctionKind::kArrow: callable = CallableKind::kArrow; break;
      case abc::FunctionKind::kAsync: callable = CallableKind::kAsyncFunction; break;
      // OH7 PandaGen::SetFunctionKind emits ASYNC_NC_FUNCTION for async arrows.
      // Keep lexical receiver semantics; ordinary async function syntax differs.
      case abc::FunctionKind::kAsyncArrow: callable = CallableKind::kAsyncArrow; break;
      case abc::FunctionKind::kGenerator: callable = CallableKind::kGenerator; break;
      case abc::FunctionKind::kAsyncGenerator: callable = CallableKind::kAsyncGenerator; break;
      case abc::FunctionKind::kNone:
      case abc::FunctionKind::kConcurrent:
      default: throw CreationContextFailure{Error::kKind};
      }
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
