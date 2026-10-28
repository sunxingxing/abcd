#ifndef ABCD_SOURCE_FUNCTION_CONTEXT_H_
#define ABCD_SOURCE_FUNCTION_CONTEXT_H_

#include "creation_tree_context.h"

namespace abcd::source {
// A record's module entry (func_main_0) as a phased family root. It has no
// creation site, captures nothing, is strict and returns its completion to the
// module loader. Only entries whose exception regions are not all
// environment-cleanup handlers take this owner; the rest keep the record route.
class EntryTreeContext {
 public:
  EntryTreeContext(RouteInputIdentity identity, abc::MethodPosition position,
                   abc::EntityOffset target, abc::FrameLayout frame)
      : identity_(identity), position_(position), target_(target), frame_(frame) {}
  const RouteInputIdentity& Identity() const { return identity_; }
  abc::MethodPosition Position() const { return position_; }
  abc::EntityOffset Target() const { return target_; }
  const abc::FrameLayout& Frame() const { return frame_; }
  std::u16string_view Name() const { return {}; }
  CallableKind Callable() const { return CallableKind::kFunction; }
  Strictness Mode() const { return Strictness::kStrict; }
  std::size_t AllocatedBytes() const { return sizeof(*this); }
  std::uint64_t Work() const { return 0; }
 private:
  RouteInputIdentity identity_;
  abc::MethodPosition position_;
  abc::EntityOffset target_;
  abc::FrameLayout frame_;
};
// Only the creation/ABI boundary differs. Every owner uses the same body,
// defaults, CFG, cells, AST correspondence and immutable publication pipeline.
class FunctionContext {
 public:
  explicit FunctionContext(ClassTreeContext value) : context_(std::move(value)) {}
  explicit FunctionContext(CreationTreeContext value) : context_(std::move(value)) {}
  explicit FunctionContext(EntryTreeContext value) : context_(std::move(value)) {}
  const EntryTreeContext* EntryContext() const { return std::get_if<EntryTreeContext>(&context_); }
  const ClassTreeContext* ClassContext() const { return std::get_if<ClassTreeContext>(&context_); }
  const analysis::ClassMethodSite* ClassSite() const {
    const auto* context = std::get_if<ClassTreeContext>(&context_);
    return context && !context->Constructor() && !context->DefinedMember() ? &context->Site() : nullptr;
  }
  const analysis::FunctionCreationSite* CreationSite() const {
    if (const auto* owner = ClassContext(); owner && owner->DefinedMember()) return &owner->DefinedMember()->function;
    const auto* context = std::get_if<CreationTreeContext>(&context_);
    return context ? &context->Site() : nullptr;
  }
  const ClassConstructorSite* ConstructorSite() const {
    const auto* context = ClassContext(); return context ? context->Constructor() : nullptr;
  }
  abc::EntityOffset Target() const {
    if (const auto* context = ClassContext()) return context->Target();
    if (const auto* context = EntryContext()) return context->Target();
    return std::get<CreationTreeContext>(context_).Site().target;
  }
  std::uint32_t Length() const {
    if (const auto* context = ClassContext()) return context->Length();
    if (EntryContext()) return 0;
    return CreationSite()->creation.length.value;
  }
  const RouteInputIdentity& Identity() const {
    return std::visit([](const auto& context) -> const RouteInputIdentity& { return context.Identity(); }, context_);
  }
  abc::MethodPosition Position() const {
    return std::visit([](const auto& context) { return context.Position(); }, context_);
  }
  const abc::FrameLayout& Frame() const {
    return std::visit([](const auto& context) -> const abc::FrameLayout& { return context.Frame(); }, context_);
  }
  std::u16string_view Name() const {
    return std::visit([](const auto& context) { return context.Name(); }, context_);
  }
  analysis::ModuleDomain ModuleDomain() const {
    if (const auto* site = ConstructorSite()) return site->creation.kind == abc::ClassKind::kSendable ? analysis::ModuleDomain::kSendable : analysis::ModuleDomain::kOrdinary;
    if (const auto* site = ClassSite()) return site->creation.kind == abc::ClassKind::kSendable ? analysis::ModuleDomain::kSendable : analysis::ModuleDomain::kOrdinary;
    return analysis::ModuleDomain::kOrdinary;
  }
  const ConcurrentMetadata* Concurrent() const { const auto* context=std::get_if<CreationTreeContext>(&context_);return context?context->Concurrent():nullptr; }
  CallableKind Callable() const { return std::visit([](const auto& c) { return c.Callable(); }, context_); }
  Strictness Mode() const { return std::visit([](const auto& c) { return c.Mode(); }, context_); }
  std::size_t AllocatedBytes() const { return std::visit([](const auto& c) { return c.AllocatedBytes(); }, context_); }
  std::uint64_t Work() const { return std::visit([](const auto& c) { return c.Work(); }, context_); }
 private:
  std::variant<ClassTreeContext, CreationTreeContext, EntryTreeContext> context_;
};
}  // namespace abcd::source
#endif
