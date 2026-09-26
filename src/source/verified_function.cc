#include "abcd/source/verified_function.h"

#include <limits>
#include <new>
#include <stdexcept>

#include "abcd/source/value_stage.h"
#include "class_tree_context.h"
#include "cell_plan.h"
#include "cell_lifetime.h"
#include "default_signature.h"
#include "function_tree.h"
#include "function_abi.h"
#include "parameter_names.h"
#include "tree_constants.h"
#include "tree_semantics.h"
#include "tree_globals.h"

namespace abcd::source {
struct VerifiedFunction::Storage {
  ClassTreeContext context;
  FunctionTree tree;
  MethodRouteReceipt receipt;
  FunctionAbi abi;
  std::size_t allocated_bytes = 0;
};
VerifiedFunction::VerifiedFunction(std::unique_ptr<const Storage> storage) : storage_(std::move(storage)) {}
VerifiedFunction::VerifiedFunction(VerifiedFunction&&) noexcept = default;
VerifiedFunction& VerifiedFunction::operator=(VerifiedFunction&&) noexcept = default;
VerifiedFunction::~VerifiedFunction() = default;
const FunctionTree& VerifiedFunction::Tree() const { return storage_->tree; }
std::u16string_view VerifiedFunction::Name() const { return storage_->context.Name(); }
const analysis::ClassMethodSite& VerifiedFunction::Creation() const { return storage_->context.Site(); }
const MethodRouteReceipt& VerifiedFunction::Receipt() const { return storage_->receipt; }
const abc::FrameLayout& VerifiedFunction::Frame() const { return storage_->context.Frame(); }
const FunctionAbi& VerifiedFunction::Abi() const { return storage_->abi; }
std::size_t VerifiedFunction::AllocatedBytes() const { return storage_->allocated_bytes; }

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
DefaultBody DefaultParameters(const analysis::ValueProgram& graph,
    std::uint32_t count, std::uint32_t length, PreparationBudget& budget) {
  auto planned = BuildParameterPlan(graph, count, budget.Remaining());
  const auto* parameters = std::get_if<ParameterPlan>(&planned);
  if (!parameters) {
    const auto error = std::get<ParameterPlanError>(planned);
    if (error == ParameterPlanError::kResourceLimit) throw Error::kResourceLimit;
    if (error == ParameterPlanError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kParameters;
  }
  budget.Admit(parameters->allocated_bytes, 1); budget.Step(parameters->work);
  auto found = FindDefaultCandidates(graph, *parameters, length, budget.Remaining());
  const auto* candidates = std::get_if<DefaultCandidates>(&found);
  if (!candidates) {
    const auto error = std::get<DefaultCandidateError>(found);
    if (error == DefaultCandidateError::kResourceLimit) throw Error::kResourceLimit;
    if (error == DefaultCandidateError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kParameters;
  }
  budget.Admit(candidates->allocated_bytes, 1); budget.Step(candidates->work);
  auto projected = DefaultBody::Build(graph, *parameters, *candidates, length, budget.Remaining());
  auto* result = std::get_if<DefaultBody>(&projected);
  if (!result) {
    const auto error = std::get<DefaultBodyError>(projected);
    if (error == DefaultBodyError::kResourceLimit) throw Error::kResourceLimit;
    if (error == DefaultBodyError::kWorkLimit) throw Error::kWorkLimit;
    throw Error::kParameters;
  }
  budget.Admit(result->AllocatedBytes(), 1); budget.Step(result->Work());
  return std::move(*result);
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
    analysis::SemanticBudget allowance, const GlobalScope* scope) {
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
    if (values.environments) budget.Admit(values.environments->allocated_bytes, 1);
    std::optional<DefaultBody> defaults;
    if (context->Frame().parameters != context->Site().length)
      defaults.emplace(DefaultParameters(*values.graph, context->Frame().parameters,
                                         context->Site().length, budget));
    const auto& graph = defaults ? defaults->Graph() : *values.graph;
    receipt->work_revision = graph.revision.value;
    std::optional<CellPlan> cells;
    if (values.environments) {
      // A projected default body has a new identity. Never reuse a topology or
      // cell plan across that boundary merely because the PCs look unchanged.
      if (values.environments->revision != graph.revision || values.environments->cfg_revision != graph.cfg_revision) {
        auto rebuilt = analysis::BuildValueEnvironments(graph, budget.Remaining());
        auto* environments = std::get_if<analysis::ValueEnvironments>(&rebuilt);
        if (!environments) {
          const auto error = std::get<analysis::ValueEnvironmentError>(rebuilt);
          if (error == analysis::ValueEnvironmentError::kResourceLimit) throw Error::kResourceLimit;
          if (error == analysis::ValueEnvironmentError::kWorkLimit) throw Error::kWorkLimit;
          throw Error::kValues;
        }
        budget.Admit(environments->allocated_bytes, 1); budget.Step(environments->work);
        values.environments = std::move(*environments);
      }
      auto planned = BuildCellPlan(graph, *values.environments, budget.Remaining());
      auto* plan = std::get_if<CellPlan>(&planned);
      if (!plan) {
        const auto error = std::get<CellError>(planned);
        if (error == CellError::kResourceLimit) throw Error::kResourceLimit;
        if (error == CellError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kValues;
      }
      budget.Admit(plan->allocated_bytes, 1); budget.Step(plan->work);
      cells = std::move(*plan);
      // Binding topology alone does not authorize source cell reads/stores.
      // Evaluation remains gated until lifetime and AST correspondence close.
    }
    auto regions = BuildControlPlan(graph, budget.Remaining());
    const auto* control = std::get_if<ControlPlan>(&regions);
    if (!control) {
      const auto error = std::get<ControlError>(regions);
      if (error == ControlError::kResourceLimit) throw Error::kResourceLimit;
      if (error == ControlError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kControl;
    }
    budget.Admit(control->allocated_bytes, 1); budget.Step(control->work);
    std::optional<CellLifetimes> lifetimes;
    if (cells) {
      auto placed = BuildCellLifetimes(graph, *values.environments, *cells, *control, budget.Remaining());
      auto* scopes = std::get_if<CellLifetimes>(&placed);
      if (!scopes) {
        const auto error = std::get<CellLifetimeError>(placed);
        if (error == CellLifetimeError::kResourceLimit) throw Error::kResourceLimit;
        if (error == CellLifetimeError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kControl;
      }
      budget.Admit(scopes->allocated_bytes, 1); budget.Step(scopes->work);
      lifetimes = std::move(*scopes);
    }
    receipt->stage = SourceStage::kControlFrozen;
    auto evaluated = BuildEvaluationPlan(graph, budget.Remaining());
    const auto* evaluation = std::get_if<EvaluationPlan>(&evaluated);
    if (!evaluation) {
      const auto error = std::get<EvaluationError>(evaluated);
      if (error == EvaluationError::kResourceLimit) throw Error::kResourceLimit;
      if (error == EvaluationError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kEvaluation;
    }
    budget.Admit(evaluation->allocated_bytes, 1); budget.Step(evaluation->work);
    auto constants_result = ResolveTreeConstants(file, header, index, method.offset, graph, budget.Remaining());
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
    auto constructed = BuildFunctionTree(graph, *control, *evaluation, std::move(syntax), constant_view, tree_budget);
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
    if (function->allocated_bytes < transferred) throw Error::kTree;
    budget.Admit(function->allocated_bytes - transferred, 1);
    budget.Step(function->work);
    if (defaults) {
      // Keep original and residual IR alive until BOTH signature migration and
      // residual body correspondence succeed. No caller-supplied certificate
      // or legacy writer can fill a gap in this publication boundary.
      const auto signature = BuildDefaultSignature(*defaults, function->tree, budget.Remaining());
      const auto* proof = std::get_if<DefaultSignatureProof>(&signature);
      if (!proof) {
        const auto error = std::get<DefaultSignatureError>(signature);
        if (error == DefaultSignatureError::kResourceLimit) throw Error::kResourceLimit;
        if (error == DefaultSignatureError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kParameters;
      }
      budget.Admit(proof->allocated_bytes, 1); budget.Step(proof->work);
      const auto semantics = VerifyTreeSemantics(graph, *control, *evaluation,
          function->tree, constant_view, budget.Remaining());
      const auto* verified = std::get_if<TreeSemantics>(&semantics);
      if (!verified) {
        const auto error = std::get<TreeSemanticError>(semantics);
        if (error == TreeSemanticError::kResourceLimit) throw Error::kResourceLimit;
        if (error == TreeSemanticError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kTree;
      }
      budget.Admit(verified->allocated_bytes, 1); budget.Step(verified->work);
    }
    const auto globals = VerifyTreeGlobals(function->tree, scope, budget.Remaining());
    const auto* checked_globals = std::get_if<TreeGlobals>(&globals);
    if (!checked_globals) {
      const auto error = std::get<TreeGlobalError>(globals);
      if (error == TreeGlobalError::kResourceLimit) throw Error::kResourceLimit;
      if (error == TreeGlobalError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kContext;
    }
    budget.Step(checked_globals->work);
    auto census = BuildFunctionAbi(graph, function->tree, context->Frame(),
                                  context->Site().length, budget.Remaining());
    auto* abi = std::get_if<ConstructedAbi>(&census);
    if (!abi) {
      const auto error = std::get<FunctionAbiError>(census);
      if (error == FunctionAbiError::kResourceLimit) throw Error::kResourceLimit;
      if (error == FunctionAbiError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kAbi;
    }
    budget.Admit(abi->allocated_bytes, 1);
    budget.Step(abi->work);
    const auto measured = MeasureTreeStorage(function->tree, {allowance.bytes, budget.Remaining().work});
    const auto* storage = std::get_if<TreeStorage>(&measured);
    if (!storage) throw std::get<TreeStorageError>(measured) == TreeStorageError::kWorkLimit
        ? Error::kWorkLimit : Error::kResourceLimit;
    // Persistent capacity estimate, not the peak preparation scratch charge.
    // Inline subobjects are conservatively counted again; allocator/RSS limits
    // still belong to process admission, not this ownership measurement.
    PreparationBudget retained(allowance);
    retained.Admit(1, sizeof(Storage));
    retained.Admit(context->AllocatedBytes(), 1);
    retained.Admit(storage->bytes, 1);
    retained.Arena(abi->abi.parameters);
    const auto retained_bytes = allowance.bytes - retained.Remaining().bytes;
    if (file.CheckUnchanged()) throw Error::kFileChanged;
    receipt->stage = SourceStage::kTreeVerified;
    receipt->coverage = Coverage::kIncomplete;  // No printer/publication ran.
    return VerifiedFunction(std::make_unique<const Storage>(Storage{
        std::move(*context), std::move(function->tree), *receipt, std::move(abi->abi), retained_bytes}));
  } catch (Error error) { return FunctionPrepareFailure{error, receipt, {}};
  } catch (const std::bad_alloc&) { return FunctionPrepareFailure{Error::kResourceLimit, receipt, {}};
  } catch (const std::length_error&) { return FunctionPrepareFailure{Error::kResourceLimit, receipt, {}}; }
}
}  // namespace abcd::source
