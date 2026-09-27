#include "family_file.h"

#include <bit>
#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/corpus/abc_file.h"

namespace abcd::source {
namespace {
using Error = FamilyFileError;
struct FamilyCursor { FamilyId member; abc::MethodPosition method; std::size_t next = 0; };
class FamilyReader {
 public:
  FamilyReader(const corpus::AbcFile& file, const abc::Header& header,
      const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
      const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
      RouteInputIdentity identity, analysis::SemanticBudget budget, const RecordModuleContext* modules)
      : file_(file), header_(header), index_(index), lookup_(lookup), roles_(roles), sites_(sites),
        identity_(identity), budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules) {}

  FileFamily Read(abc::MethodPosition root) {
    Require(root.value < index_.methods.size(), Error::kMethod);
    method_ = index_.methods[root.value].offset;
    Admit(1, sizeof(FamilyReader));
    Grow(active_, index_.methods.size()); Step(index_.methods.size());
    active_.resize(index_.methods.size());
    auto function = ReadMethod(root);
    Add(std::move(function), {}, {}, root);
    while (!stack_.empty()) {
      Step();
      const auto cursor = stack_.back();
      const auto& parent = result_.members[cursor.member.value].function;
      method_ = parent.input.Method(); parent_.reset(); pc_.reset();
      if (cursor.next == parent.input.Children().size()) {
        active_[cursor.method.value] = 0; stack_.pop_back(); continue;
      }
      const auto site = parent.input.Children()[cursor.next];
      ++stack_.back().next;
      Require(site.value < parent.body.instructions.size(), Error::kCreation);
      parent_ = parent.input.Method(); pc_ = parent.body.instructions[site.value].offset;
      Require(site.value < parent.input.Input().instructions.size(), Error::kCreation);
      const auto& operation = parent.input.Input().instructions[site.value].operation;
      if (const auto* object = std::get_if<analysis::BufferedObjectCreation>(&operation)) {
        // Capture barriers are not all function edges. The shared literal
        // reader refuses methods/accessors and validates every key/value.
        // Keep the original barrier intact and retain its discharge evidence.
        const MethodConstants constants(file_, header_, index_, *parent_, {bytes_});
        auto parsed = ReadObjectBuffer(*object, *pc_, constants, {bytes_, work_});
        if (const auto* error = std::get_if<BodyEmitFailure>(&parsed)) Fail(*error);
        auto buffer = std::get<ObjectBuffer>(std::move(parsed));
        Admit(buffer.allocated_bytes, 1); Step(buffer.work);
        auto& objects = result_.members[cursor.member.value].objects;
        AppendRoom(objects);
        objects.push_back({site, *object, std::move(buffer)});
        continue;
      }
      constexpr std::size_t kMethodLookups = 2;
      Step(std::bit_width(index_.regions.size()) + kMethodLookups * std::bit_width(index_.methods.size()) + 1);
      auto resolved = analysis::BindCreation(file_, header_, index_, lookup_, parent.input.Method(),
          parent.body, parent.input.Proof().environments, site.value);
      const auto* binding = std::get_if<analysis::CreationBinding>(&resolved);
      Require(binding != nullptr, Error::kCreation);
      const auto target = binding->target.position;
      Require(target.value < index_.methods.size(), Error::kMethod);
      method_ = index_.methods[target.value].offset;
      Require(!active_[target.value], Error::kRecursiveCreation);
      Step(std::bit_width(sites_.Size()) + 1);
      const auto occurrence = sites_.FindOccurrence(*method_, *parent_, *pc_);
      Require(occurrence.has_value(), Error::kOccurrence);
      auto child = ReadMethod(target);
      auto context_result = CreationTreeContext::Resolve(file_, header_, index_, lookup_, roles_,
          sites_, *occurrence, identity_, Remaining());
      if (const auto* error = std::get_if<CreationContextFailure>(&context_result)) Fail(*error);
      auto context = std::get<CreationTreeContext>(std::move(context_result));
      Admit(context.AllocatedBytes(), 1); Step(context.Work());
      const CaptureFunction parent_work{parent.input, parent.values};
      const CaptureFunction child_work{child.input, child.values};
      auto captured = BindCaptureCells(index_, parent_work, child_work, site, *binding, Remaining());
      if (const auto* error = std::get_if<CaptureCellError>(&captured)) Fail(*error);
      auto cells = std::get<CaptureCells>(std::move(captured));
      Admit(cells.allocated_bytes, 1); Step(cells.work);
      Require(result_.members.size() < std::numeric_limits<std::uint32_t>::max(), Error::kResourceLimit);
      const FunctionOwnerId child_owner{static_cast<std::uint32_t>(result_.members.size())};
      auto evaluated = BindCaptureEvaluation(index_, parent_work, child_work, site, *binding,
          cells, {cursor.member.value}, child_owner, Remaining());
      if (const auto* error = std::get_if<CaptureEvaluationFailure>(&evaluated)) Fail(*error);
      auto evaluation = std::get<CapturedFunctionBinding>(std::move(evaluated));
      Admit(evaluation.allocated_bytes, 1); Step(evaluation.work);
      auto& children = result_.members[cursor.member.value].children;
      Require(children.empty() || children.back().creation < evaluation.binding.creation, Error::kCreation);
      children.push_back(std::move(evaluation.binding));
      // No parent reference may survive Add: member-vector growth moves its
      // FileFunction envelope. All relationship identity is stored as IDs.
      FamilyCreation incoming{cursor.member, site, *binding, std::move(cells)};
      Add(std::move(child), std::move(context), std::move(incoming), target);
    }
    if (file_.CheckUnchanged()) Fail(FunctionFileFailure{FunctionFileError::kChanged});
    result_.allocated_bytes = budget_.bytes - bytes_;
    result_.work = budget_.work - work_;
    return std::move(result_);
  }
 private:
  template <class T> [[noreturn]] void Fail(T cause) const {
    throw FamilyFileFailure{std::move(cause), method_, parent_, pc_};
  }
  void Require(bool condition, Error error) const { if (!condition) Fail(error); }
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit); work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit); bytes_ -= count * width;
  }
  template <class T> void Grow(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  template <class T> void AppendRoom(std::vector<T>& values) {
    if (values.size() < values.capacity()) return;
    constexpr std::size_t kGrowth = 2;
    Require(values.size() <= (std::numeric_limits<std::uint32_t>::max() - 1) / kGrowth, Error::kResourceLimit);
    Grow(values, values.empty() ? 1 : values.size() * kGrowth);
  }
  analysis::SemanticBudget Remaining() const { return {bytes_, work_}; }
  FileFunction ReadMethod(abc::MethodPosition position) {
    auto read = ReadFunctionValues(file_, header_, index_, position, identity_,
        InputCaptureMode::kConditional, Remaining(), modules_);
    if (const auto* error = std::get_if<FunctionFileFailure>(&read)) Fail(*error);
    auto function = std::get<FileFunction>(std::move(read));
    Admit(function.allocated_bytes, 1);
    return function;
  }
  void Add(FileFunction function, std::optional<CreationTreeContext> context,
      std::optional<FamilyCreation> incoming, abc::MethodPosition method) {
    AppendRoom(result_.members); AppendRoom(stack_);
    const FamilyId id{static_cast<std::uint32_t>(result_.members.size())};
    FileFamilyMember member{std::move(function), std::move(context), std::move(incoming)};
    Grow(member.children, member.function.input.Children().size());
    result_.members.push_back(std::move(member));
    stack_.push_back({id, method}); active_[method.value] = 1;
  }
  const corpus::AbcFile& file_;
  const abc::Header& header_;
  const abc::MetadataIndex& index_;
  const abc::MethodLookup& lookup_;
  const analysis::MethodRoleIndex& roles_;
  const analysis::FunctionCreationSites& sites_;
  RouteInputIdentity identity_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  const RecordModuleContext* modules_;
  FileFamily result_;
  std::vector<FamilyCursor> stack_;
  std::vector<std::uint8_t> active_;
  std::optional<abc::EntityOffset> method_;
  std::optional<abc::EntityOffset> parent_;
  std::optional<abc::CodeOffset> pc_;
};
}  // namespace
std::variant<FileFamily, FamilyFileFailure> ReadFunctionFamily(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::FunctionCreationSites& sites,
    abc::MethodPosition root, RouteInputIdentity identity,
    analysis::SemanticBudget budget, const RecordModuleContext* modules) {
  try { return FamilyReader(file, header, index, lookup, roles, sites, identity, budget, modules).Read(root);
  } catch (const FamilyFileFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}};
  } catch (const std::length_error&) { return FamilyFileFailure{FamilyFileError::kResourceLimit, {}, {}, {}}; }
}
}  // namespace abcd::source
