#ifndef ABCD_SOURCE_FUNCTION_CONTEXT_H_
#define ABCD_SOURCE_FUNCTION_CONTEXT_H_

#include "creation_tree_context.h"

namespace abcd::source {
// Only the creation/ABI boundary differs. Every owner uses the same body,
// defaults, CFG, cells, AST correspondence and immutable publication pipeline.
class FunctionContext {
 public:
  explicit FunctionContext(ClassTreeContext value) : context_(std::move(value)) {}
  explicit FunctionContext(CreationTreeContext value) : context_(std::move(value)) {}
  const ClassTreeContext* ClassContext() const { return std::get_if<ClassTreeContext>(&context_); }
  const analysis::ClassMethodSite* ClassSite() const {
    const auto* context = std::get_if<ClassTreeContext>(&context_);
    return context && !context->Constructor() ? &context->Site() : nullptr;
  }
  const analysis::FunctionCreationSite* CreationSite() const {
    const auto* context = std::get_if<CreationTreeContext>(&context_);
    return context ? &context->Site() : nullptr;
  }
  const ClassConstructorSite* ConstructorSite() const {
    const auto* context = ClassContext(); return context ? context->Constructor() : nullptr;
  }
  abc::EntityOffset Target() const {
    if (const auto* context = ClassContext()) return context->Target();
    return std::visit([](const auto& context) { return context.Site().target; }, context_);
  }
  std::uint32_t Length() const {
    if (const auto* context = ClassContext()) return context->Length();
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
  CallableKind Callable() const { return std::visit([](const auto& c) { return c.Callable(); }, context_); }
  Strictness Mode() const { return std::visit([](const auto& c) { return c.Mode(); }, context_); }
  std::size_t AllocatedBytes() const { return std::visit([](const auto& c) { return c.AllocatedBytes(); }, context_); }
  std::uint64_t Work() const { return std::visit([](const auto& c) { return c.Work(); }, context_); }
 private:
  std::variant<ClassTreeContext, CreationTreeContext> context_;
};
}  // namespace abcd::source
#endif
