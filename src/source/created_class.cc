#include "abcd/source/created_class.h"

#include <new>
#include <ostream>
#include <type_traits>

#include "abcd/abc/call_type.h"
#include "abcd/source/base_constructor.h"
#include "capture_stream.h"
#include "runtime_name.h"

namespace abcd::source {
namespace {
struct ClassFailure {
  CreatedClassResult result;
};
template <typename T, typename... V>
T Take(std::variant<V...> value) {
  if (!std::holds_alternative<T>(value))
    std::visit(
        [](const auto& item) {
          if constexpr (!std::is_same_v<std::decay_t<decltype(item)>, T>)
            throw ClassFailure{item};
        },
        value);
  return std::get<T>(std::move(value));
}
void Admit(std::size_t count, std::size_t width, std::size_t& bytes) {
  if (count > bytes / width)
    throw ClassFailure{CreatedClassError::kResourceLimit};
  bytes -= count * width;
}
void CheckMembers(const corpus::AbcFile& file, const abc::Header& header,
                  const abc::MetadataIndex& index, abc::EntityOffset caller,
                  abc::PoolIndex literal, std::size_t bytes) {
  const auto reference = Take<abc::PoolReference>(
      abc::ResolvePoolReference(file, header, index, caller, literal,
                                abc::PoolEntityKind::kLiteralArray));
  const auto raw = Take<abc::LiteralArray>(
      abc::ReadLiteralArray(file, header, reference.offset, {bytes}));
  Admit(raw.entries.capacity(), sizeof(abc::LiteralEntry), bytes);
  const auto members =
      Take<abc::ClassBuffer>(abc::DecodeClassBuffer(raw, {bytes}));
  if (!members.properties.empty())
    throw ClassFailure{CreatedClassError::kMembers};
}
}  // namespace
CreatedClassResult WriteCreatedClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    abc::EntityOffset caller, const abc::BodyInstruction& instruction,
    analysis::ClassHeritage heritage, std::ostream& output,
    LocalLexicalBudget budget, abc::StringBudget strings) {
  try {
    if (heritage.pc != instruction.offset ||
        heritage.kind != analysis::HeritageKind::kAbsent)
      return CreatedClassError::kHeritage;
    const auto creation = Take<analysis::ClassCreation>(
        analysis::LiftClassCreation(instruction.decoded));
    const auto target = Take<abc::MethodTarget>(abc::ResolveMethodTarget(
        file, header, index, lookup, caller, creation.constructor));
    if (target.attributes.sendable) return FunctionEmitError::kSendable;
    // Empty literal buffers still encode the reserved native class properties.
    // Validate the whole buffer; do not silently omit any member or
    // initializer.
    CheckMembers(file, header, index, caller, creation.literal, budget.bytes);
    const auto& method = index.methods[target.position.value];
    if (!method.code) return CreatedClassError::kMissingCode;
    const auto name = RuntimeName(method);
    if (!name) return CreatedClassError::kName;
    const auto call = Take<abc::CallType>(abc::ReadCallType(
        file, header, index, method.annotations, {budget.bytes}));
    const auto frame = Take<abc::FrameLayout>(
        abc::BuildFrameLayout(method.code->registers, method.code->arguments,
                              abc::DecodeCallFlags(call.value)));
    const auto body = Take<abc::MethodBody>(abc::DecodeMethodBody(
        file, header.profile, *method.code, {budget.bytes}));
    Admit(body.instructions.capacity(), sizeof(abc::BodyInstruction),
          budget.bytes);
    Admit(body.tries.capacity(), sizeof(abc::TryRegion), budget.bytes);
    Admit(body.handlers.capacity(), sizeof(abc::CatchHandler), budget.bytes);
    const MethodConstants constants{file, header, index, method.offset,
                                    strings};
    const BaseConstructor constructor{creation, target.attributes, frame,
                                      *name};
    constexpr std::string_view kPrefix = "(class ", kOpen = " {\n",
                               kClose = "\n})";
    auto available = budget.output.bytes;
    for (const auto part : {kPrefix, *name, kOpen, kClose}) {
      if (part.size() > available) return EmitError::kOutputLimit;
      available -= part.size();
    }
    budget.output.bytes = available;
    CaptureDiscard discard;
    std::ostream dry(&discard);
    const auto size = Take<std::size_t>(
        WriteBaseConstructor(constructor, body, dry, budget, &constants));
    output << kPrefix << *name << kOpen;
    if (!output) return EmitError::kWrite;
    Take<std::size_t>(
        WriteBaseConstructor(constructor, body, output, budget, &constants));
    output << kClose;
    if (!output) return EmitError::kWrite;
    return kPrefix.size() + name->size() + kOpen.size() + size + kClose.size();
  } catch (const ClassFailure& failure) {
    return failure.result;
  } catch (const std::bad_alloc&) {
    return CreatedClassError::kResourceLimit;
  } catch (const std::ios_base::failure&) {
    return EmitError::kWrite;
  }
}
}  // namespace abcd::source
