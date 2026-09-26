#include "abcd/source/verified_function.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/source/value_stage.h"
#include "class_tree_context.h"
#include "function_tree.h"
#include "parameter_names.h"
#include "tree_constants.h"

namespace abcd::source {
struct VerifiedFunction::Storage {
  ClassTreeContext context;
  FunctionTree tree;
  MethodRouteReceipt receipt;
};
VerifiedFunction::VerifiedFunction(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
VerifiedFunction::VerifiedFunction(VerifiedFunction&&) noexcept = default;
VerifiedFunction& VerifiedFunction::operator=(VerifiedFunction&&) noexcept = default;
VerifiedFunction::~VerifiedFunction() = default;
const FunctionTree& VerifiedFunction::Tree() const { return storage_->tree; }
std::u16string_view VerifiedFunction::Name() const { return storage_->context.Name(); }
const analysis::ClassMethodSite& VerifiedFunction::Creation() const { return storage_->context.Site(); }
const MethodRouteReceipt& VerifiedFunction::Receipt() const { return storage_->receipt; }

namespace {
using Error = FunctionPrepareError;
class PreparationBudget {
 public:
  explicit PreparationBudget(analysis::SemanticBudget budget) : remaining_(budget) {}
  void Admit(std::size_t count, std::size_t width) {
    if (count > remaining_.bytes / width) throw Error::kResourceLimit;
    remaining_.bytes -= count * width;
  }
  void Step(std::uint64_t count) {
    if (count > remaining_.work) throw Error::kWorkLimit;
    remaining_.work -= count;
  }
  template <class T> void Arena(const std::vector<T>& values) { Admit(values.capacity(), sizeof(T)); }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    if (count <= values.capacity()) return;
    // Keep the replaced allocation charged: reserve can hold both at once.
    Admit(count, sizeof(T)); values.reserve(count); Admit(values.capacity() - count, sizeof(T));
  }
  [[nodiscard]] analysis::SemanticBudget Remaining() const { return remaining_; }
 private:
  analysis::SemanticBudget remaining_;
};
void AdmitGraph(const analysis::ValueProgram& graph, PreparationBudget& budget) {
  budget.Admit(1, sizeof(graph));
  budget.Arena(graph.blocks); budget.Arena(graph.edges); budget.Arena(graph.ops);
  budget.Arena(graph.values); budget.Arena(graph.uses); budget.Arena(graph.origins);
  budget.Arena(graph.exceptional_exits); budget.Arena(graph.input.sites); budget.Arena(graph.input.values);
}
void Names(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MethodInfo& method, const abc::MethodBody& body,
    const abc::FrameLayout& frame, FunctionSyntax& syntax, PreparationBudget& budget) {
  budget.Reserve(syntax.parameters, frame.parameters);
  syntax.parameters.resize(frame.parameters);
  budget.Step(frame.parameters);
  if (frame.parameters == 0) return;
  const auto recovered = RecoverParameterNames(file, header, method, frame, body, budget.Remaining().bytes);
  const auto* parameters = std::get_if<RecoveredParameters>(&recovered);
  if (!parameters) {
    if (const auto* error = std::get_if<ParameterNameError>(&recovered)) {
      if (*error == ParameterNameError::kUnproven) return;
      throw Error::kResourceLimit;
    }
    const auto error = std::get<abc::MetadataError>(recovered);
    if (error == abc::MetadataError::kResourceLimit) throw Error::kResourceLimit;
    if (error == abc::MetadataError::kFileChanged) throw Error::kFileChanged;
    throw Error::kParameters;
  }
  if (parameters->names.size() != frame.parameters) throw Error::kParameters;
  budget.Arena(parameters->names);
  for (const auto& name : parameters->names) { budget.Admit(name.capacity(), 1); budget.Admit(1, 1); }
  if (syntax.names.size() > std::numeric_limits<std::uint32_t>::max() - frame.parameters)
    throw Error::kResourceLimit;
  budget.Reserve(syntax.names, syntax.names.size() + frame.parameters);
  for (std::size_t i = 0; i < parameters->names.size(); ++i) {
    const auto& name = parameters->names[i];
    budget.Step(name.size());
    budget.Admit(name.size(), sizeof(char16_t)); budget.Admit(1, sizeof(char16_t));
    const NameId id{static_cast<std::uint32_t>(syntax.names.size())};
    auto& owned = syntax.names.emplace_back(name.begin(), name.end());
    budget.Admit(owned.capacity() - name.size(), sizeof(char16_t));
    syntax.parameters[i] = {id, NameEvidence::kDebugRange};
  }
}
std::size_t SyntaxStorage(const FunctionSyntax& syntax) {
  // All components have already passed bounded admission in this invocation.
  // Recredit only storage subsequently moved into the syntax builder.
  std::size_t bytes = syntax.parameters.capacity() * sizeof(ParameterSyntax) +
      (syntax.names.capacity() + syntax.texts.capacity()) * sizeof(std::u16string);
  for (const auto& name : syntax.names) bytes += (name.capacity() + 1) * sizeof(char16_t);
  for (const auto& text : syntax.texts) bytes += (text.capacity() + 1) * sizeof(char16_t);
  return bytes;
}
}  // namespace

std::variant<VerifiedFunction, FunctionPrepareFailure> VerifiedFunction::PrepareClass(
    const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const abc::MethodLookup& lookup,
    const analysis::MethodRoleIndex& roles, const analysis::ClassMethodSites& sites,
    analysis::ClassSiteId site, RouteInputIdentity identity,
    analysis::SemanticBudget allowance) {
  std::optional<MethodRouteReceipt> receipt;
  try {
    PreparationBudget budget(allowance);
    budget.Admit(1, sizeof(Storage));
    auto resolved = ClassTreeContext::Resolve(file, header, index, lookup, roles, sites, site, identity, budget.Remaining());
    auto* context = std::get_if<ClassTreeContext>(&resolved);
    if (!context) {
      const auto& failure = std::get<ClassContextFailure>(resolved);
      if (const auto* error = std::get_if<ClassContextError>(&failure.error)) {
        if (*error == ClassContextError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ClassContextError::kWorkLimit) throw Error::kWorkLimit;
      }
      if (const auto* error = std::get_if<abc::MetadataError>(&failure.error)) {
        if (*error == abc::MetadataError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == abc::MetadataError::kFileChanged) throw Error::kFileChanged;
      }
      throw Error::kContext;
    }
    budget.Admit(context->AllocatedBytes(), 1);
    budget.Step(context->Work());
    const auto& method = index.methods[context->Position().value];
    auto decoded = abc::DecodeMethodBody(file, header.profile, *method.code, {budget.Remaining().bytes});
    const auto* body = std::get_if<abc::MethodBody>(&decoded);
    if (!body) {
      const auto& failure = std::get<abc::BodyFailure>(decoded);
      const auto kind = failure.kind == abc::BodyError::kResourceLimit
          ? Error::kResourceLimit
          : failure.file_error == corpus::FileError::kChanged
              ? Error::kFileChanged : Error::kBody;
      return FunctionPrepareFailure{kind, {}, failure.offset};
    }
    // No caller-supplied graph/certificate can bypass this fresh input proof.
    // BuildValueStage currently bounds its constituent work passes separately;
    // whole-method work accounting remains distinct from this shared byte cap.
    auto values = BuildValueStage(identity, method.offset, method.owner_offset, *body, context->Frame(), budget.Remaining());
    receipt = values.receipt;
    if (!values.graph || values.receipt.stage != SourceStage::kValuesVerified || values.receipt.failure)
      throw Error::kValues;
    budget.Arena(body->instructions); budget.Arena(body->tries); budget.Arena(body->handlers);
    AdmitGraph(*values.graph, budget);
    auto regions = BuildControlPlan(*values.graph, budget.Remaining());
    const auto* control = std::get_if<ControlPlan>(&regions);
    if (!control) {
      const auto error = std::get<ControlError>(regions);
      if (error == ControlError::kResourceLimit) throw Error::kResourceLimit;
      if (error == ControlError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kControl;
    }
    budget.Admit(control->allocated_bytes, 1); budget.Step(control->work);
    receipt->stage = SourceStage::kControlFrozen;
    auto evaluated = BuildEvaluationPlan(*values.graph, budget.Remaining());
    const auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!evaluation) {
      const auto error = std::get<EvaluationError>(evaluated);
      if (error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
      if (error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kEvaluation;
    }
    budget.Admit(evaluation->allocated_bytes, 1); budget.Step(evaluation->work);
    auto constants_result = ResolveTreeConstants(file, header, index, method.offset, *values.graph, budget.Remaining());
    auto* constants = std::get_if<TreeConstants>(&constants_result);
    if (!constants) {
      if (const auto* error = std::get_if<TreeConstantError>(&constants_result)) {
        if (*error == TreeConstantError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == TreeConstantError::kWorkLimit) throw Error::kWorkLimit;
      }
      if (const auto* error = std::get_if<abc::MetadataError>(&constants_result)) {
        if (*error == abc::MetadataError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == abc::MetadataError::kFileChanged) throw Error::kFileChanged;
      }
      throw Error::kConstants;
    }
    budget.Admit(constants->allocated_bytes, 1); budget.Step(constants->work);
    auto constant_view = constants->View();
    FunctionSyntax syntax{identity, method.offset, {context->Position().value},
                          context->Callable(), context->Mode(), {},
                          std::move(constants->names), std::move(constants->texts)};
    Names(file, header, method, *body, context->Frame(), syntax, budget);
    // Pool NameIds keep their prefix positions; recovered formal names append
    // to that same arena. The view must describe the final syntax namespace.
    constant_view.name_count = syntax.names.size();
    const auto transferred = SyntaxStorage(syntax);
    auto tree_budget = budget.Remaining();
    if (transferred > allowance.bytes - tree_budget.bytes) throw Error::kResourceLimit;
    tree_budget.bytes += transferred;
    auto constructed = BuildFunctionTree(*values.graph, *control, *evaluation, std::move(syntax), constant_view, tree_budget);
    auto* function = std::get_if<ConstructedFunction>(&constructed);
    if (!function) {
      const auto error = std::get<FunctionTreeError>(constructed);
      if (error == FunctionTreeError::kResourceLimit) throw Error::kResourceLimit;
      if (error == FunctionTreeError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kTree;
    }
    if (function->tree.parameters.size() != context->Frame().parameters ||
        function->tree.callable != context->Callable() || function->tree.strictness != context->Mode() ||
        function->tree.method != context->Site().target || function->tree.identity != context->Identity()) throw Error::kTree;
    if (file.CheckUnchanged()) throw Error::kFileChanged;
    receipt->stage = SourceStage::kTreeVerified;
    receipt->coverage = Coverage::kIncomplete;  // No printer/publication ran.
    return VerifiedFunction(std::make_unique<const Storage>(Storage{
        std::move(*context), std::move(function->tree), *receipt}));
  } catch (Error error) { return FunctionPrepareFailure{error, receipt, {}};
  } catch (const std::bad_alloc&) { return FunctionPrepareFailure{Error::kResourceLimit, receipt, {}};
  } catch (const std::length_error&) { return FunctionPrepareFailure{Error::kResourceLimit, receipt, {}}; }
}
}  // namespace abcd::source
