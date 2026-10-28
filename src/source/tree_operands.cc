#include "iterator_continuation.h"
#include <algorithm>
#include <bit>
#include <limits>
#include <type_traits>

#include "assignment_values.h"
#include "class_tree.h"
#include "derived_projection.h"
#include "derived_value_states.h"
#include "environment_renewal_binding.h"
#include "function_object.h"
#include "guarded_catch_verification.h"
#include "implicit_constructor.h"
#include "integer_nan.h"
#include "logical_evaluation.h"
#include "logical_regions.h"
#include "native_arguments.h"
#include "original_flow.h"
#include "phase_evaluation.h"
#include "phased_parameter_defaults.h"
#include "sendable_class.h"
#include "staged_literal_names.h"
#include "super_store.h"
#include "template_tree.h"
#include "tree_correspondence.h"

namespace abcd::source {
namespace {
using Error = TreeSemanticError;
class OperandCheck {
 public:
  OperandCheck(const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
      const FunctionTree& tree, const TreeFlow& flow,
      ExpressionConstants constants, TreeCheckBudget& budget, OriginalFlow& original)
      : graph_(graph), evaluation_(evaluation), tree_(tree), flow_(flow),
        constants_(constants), budget_(budget), original_(original) {}
  void Run() {
    VerifyRenewalInput(graph_, budget_);
    RequireMeaning(original_.environment_renewal == graph_.environment_renewal,
                   Error::kOperand);
    VerifyRenewalBindings(tree_, original_, budget_);
    VerifyGuardedCatchInput(graph_.guarded_catches, budget_);
    RequireMeaning(original_.guarded_catches == graph_.guarded_catches, Error::kOperand);
    RequireMeaning(constants_.operations.size() == graph_.ops.size() &&
                   constants_.name_count <= tree_.names.size() &&
                   constants_.text_count <= tree_.texts.size(), Error::kConstant);
    if (const auto* arrays = ArrayInitializations(evaluation_)) {
      const auto checked = VerifyArrayInitializations(graph_, *arrays, tree_.identity.contract,
          constants_, tree_.names, budget_.Remaining(), &evaluation_);
      if (const auto* error = std::get_if<ArrayInitializationError>(&checked)) {
        if (*error == ArrayInitializationError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == ArrayInitializationError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kOperand;
      }
      budget_.Step(std::get<std::uint64_t>(checked));
    }
    const auto super_rhs = SuperStoreRhsSafety(tree_, budget_);
    bool native_snapshot=false;
    for(const auto& node:tree_.expressions){budget_.Step();if(const auto* snapshot=std::get_if<ArgumentSnapshotExpr>(&node.expression))native_snapshot|=snapshot->form==ArgumentSnapshotForm::kNativeEntry;}
    if(native_snapshot) {
      RequireMeaning(NativeArguments(tree_.identity.contract)&&tree_.strictness==Strictness::kStrict,Error::kOperand);
      const auto proof=ProveNativeArguments(graph_,tree_.callable,budget_);
      RequireMeaning(std::holds_alternative<NativeArgumentsProof>(proof),Error::kOperand);
      RequireMeaning(VerifyNativeArguments(graph_,tree_.callable,std::get<NativeArgumentsProof>(proof),budget_),Error::kOperand);
      VerifyNativeArgumentsNames(tree_,budget_);
    }
    const auto indexed = IndexCallArgumentUses(graph_, evaluation_, budget_.Remaining());
    if (const auto* error = std::get_if<CallPlanError>(&indexed)) {
      if (*error == CallPlanError::kResourceLimit) throw Error::kResourceLimit;
      if (*error == CallPlanError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kOperand;
    }
    const auto& uses = std::get<CallArgumentUses>(indexed);
    budget_.Admit(uses.allocated_bytes, 1); budget_.Step(uses.work);
    if (evaluation_.literals) RequireMeaning(CheckStagedLiteralNames(*evaluation_.literals, constants_, tree_.names,
        [&](std::uint64_t count) { budget_.Step(count); },
        [&](std::size_t count, std::size_t width) { budget_.Admit(count, width); }), Error::kConstant);
    if (constants_.number_type_text)
      RequireMeaning(constants_.number_type_text->value < constants_.text_count &&
                     tree_.texts[constants_.number_type_text->value] == u"number", Error::kConstant);
    for (const auto& site : evaluation_.class_prototype_reads) {
      budget_.Step();
      RequireMeaning(HasClassPrototypeKey(site, constants_, tree_.names, tree_.texts), Error::kConstant);
      RequireMeaning(site.creation.value < constants_.operations.size(), Error::kConstant);
      const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[site.creation.value]);
      RequireMeaning(definition && *definition && (*definition)->operation == site.creation, Error::kConstant);
    }
    for (const auto read : evaluation_.class_projection.prototypes) {
      budget_.Step();
      RequireMeaning(read.value < constants_.operations.size(), Error::kConstant);
      const auto site = std::lower_bound(evaluation_.class_prototype_reads.begin(), evaluation_.class_prototype_reads.end(), read,
          [&](const auto& entry, auto id) { budget_.Step(); return entry.read < id; });
      if (site != evaluation_.class_prototype_reads.end() && site->read == read) continue;
      const auto* name = std::get_if<NameId>(&constants_.operations[read.value]);
      RequireMeaning(name && name->value < tree_.names.size() && tree_.names[name->value] == u"prototype", Error::kConstant);
    }
    for (const auto& call : evaluation_.class_initializers) {
      budget_.Step();
      RequireMeaning(call.creation.value < constants_.operations.size(), Error::kConstant);
      const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[call.creation.value]);
      RequireMeaning(definition && *definition && (*definition)->initializer_site &&
          *(*definition)->initializer_site == call, Error::kConstant);
      // A layout carries the original call, never a fabricated initializer
      // body. Final record verification repeats this check after preparation.
      if (!(*definition)->constructor) {
        RequireMeaning(!(*definition)->initializer, Error::kConstant);
        continue;
      }
      RequireMeaning((*definition)->initializer && (*definition)->initializer->call == call, Error::kConstant);
      const auto& fields = *(*definition)->initializer;
      const auto* site = fields.context.CreationSite();
      const auto* method = std::get_if<analysis::ValueFunctionCreation>(&graph_.ops[call.method.value].operation);
      RequireMeaning(site && method && fields.tree.identity == tree_.identity &&
          fields.tree.method == site->target && site->creator == tree_.method &&
          site->pc == graph_.origins[graph_.ops[call.method.value].origin.value].pc &&
          site->creation.method == method->method, Error::kConstant);
    }
    for (const auto& definition_site : evaluation_.class_definitions) {
      budget_.Step();
      RequireMeaning(definition_site.creation.value < constants_.operations.size(), Error::kConstant);
      const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[definition_site.creation.value]);
      RequireMeaning(definition && *definition, Error::kConstant);
      const auto& members = (*definition)->definitions;
      const auto found = std::lower_bound(members.begin(), members.end(), definition_site.order,
          [&](const auto& member, auto position) { budget_.Step(); return member.site.order < position; });
      RequireMeaning(found != members.end() && found->site == definition_site, Error::kConstant);
      const auto check = [&](const ClassDefinedMember* site, const RouteInputIdentity& identity,
          analysis::OpId method_id, abc::ClassMethodKind kind) {
        budget_.Step();
        const auto* method = std::get_if<analysis::ValueFunctionCreation>(&graph_.ops[method_id.value].operation);
        RequireMeaning(site && method && identity == tree_.identity &&
            site->function.creator == tree_.method && site->definition == definition_site.definition && site->kind == kind &&
            site->function.pc == graph_.origins[graph_.ops[method_id.value].origin.value].pc &&
            site->function.creation.method == method->method, Error::kConstant);
      };
      const auto methods = definition_site.Methods();
      for (std::size_t i = 0; i < methods.size(); ++i) {
        const auto& omitted = found->omissions[i];
        RequireMeaning(!(found->bodies[i] && omitted), Error::kConstant);
        RequireMeaning((*definition)->constructor ?
            (found->bodies[i].has_value() || omitted.has_value()) == methods[i].operation.has_value() :
            !found->bodies[i] && !omitted, Error::kConstant);
        if (found->bodies[i]) check(found->bodies[i]->DefinedMemberCreation(), found->bodies[i]->Identity(),
            *methods[i].operation, methods[i].kind);
        if (omitted) {
          // Recheck the original definition occurrence even for a marked gap.
          // Its context grants no internal body/closure/ABI proof.
          RequireMeaning(omitted->InputGap(), Error::kConstant);
          check(omitted->context.DefinedMember(), omitted->context.Identity(), *methods[i].operation, methods[i].kind);
        }
      }
    }
    std::size_t next_function = 0;
    std::size_t next_module = 0;
    std::size_t next_capture = evaluation_.modules.size();
    if(!evaluation_.modules.empty() || !evaluation_.guarded_reads.empty()) {
      budget_.Array(original_.action_regions,evaluation_.modules.size() + evaluation_.guarded_reads.size());
      budget_.Array(original_.action_region_members,graph_.ops.size());
    }
    std::vector<std::optional<std::size_t>> module_aliases;
    if (!tree_.modules.empty()) {
      budget_.Array(module_aliases, tree_.symbols.size());
      for (std::size_t i = 0; i < tree_.modules.size(); ++i) {
        budget_.Step();
        const auto symbol = tree_.modules[i].symbol;
        RequireMeaning(symbol.value < module_aliases.size() && !module_aliases[symbol.value], Error::kOperand);
        module_aliases[symbol.value] = i;
      }
    }
    RequireMeaning(tree_.ImplicitConstructor() == (constants_.implicit_constructor != nullptr), Error::kOperand);
    if (constants_.implicit_constructor) {
      RequireMeaning(tree_.callable == CallableKind::kDerivedConstructor &&
          &constants_.implicit_constructor->values.graph == &graph_ &&
          tree_.identity == constants_.implicit_constructor->input.Identity() &&
          tree_.method == constants_.implicit_constructor->input.Method(), Error::kOperand);
      const auto proof = ProveImplicitConstructor(*constants_.implicit_constructor, budget_.Remaining());
      RequireMeaning(std::holds_alternative<ImplicitConstructorProof>(proof), Error::kOperand);
      budget_.Step(std::get<ImplicitConstructorProof>(proof).work);
    }
    if (tree_.parameter_form == ParameterForm::kInvocationRest) {
      RequireMeaning(constants_.derived_constructor && constants_.forwarding_owner &&
          &constants_.derived_constructor->values.graph == &graph_ &&
          tree_.identity == constants_.derived_constructor->input.Identity() &&
          tree_.method == constants_.derived_constructor->input.Method(), Error::kOperand);
      // Recompute from the immutable original frame and current working graph;
      // a synthetic parameter or a cached projection is never proof authority.
      const auto checked = ProjectDerivedConstructor(*constants_.derived_constructor,
          *constants_.forwarding_owner, budget_.Remaining(), constants_.forwarding_captures);
      if (const auto* error = std::get_if<DerivedProjectionError>(&checked)) {
        if (*error == DerivedProjectionError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == DerivedProjectionError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kOperand;
      }
      const auto& proof = std::get<DerivedProjection>(checked);
      budget_.Admit(proof.allocated_bytes, 1); budget_.Step(proof.work);
      RequireMeaning(proof.form == DerivedConstructorForm::kInvocationRest &&
          proof.guards == tree_.receiver_guards, Error::kOperand);
    }
    if (tree_.derived_protocol) {
      RequireMeaning(
          constants_.derived_protocol &&
              *tree_.derived_protocol == *constants_.derived_protocol &&
              constants_.derived_constructor && constants_.forwarding_owner,
          Error::kOperand);
      auto checked = ProjectDerivedConstructor(
          *constants_.derived_constructor, *constants_.forwarding_owner,
          budget_.Remaining(), constants_.forwarding_captures);
      if (const auto* error = std::get_if<DerivedProjectionError>(&checked)) {
        if (*error == DerivedProjectionError::kResourceLimit)
          throw Error::kResourceLimit;
        if (*error == DerivedProjectionError::kWorkLimit)
          throw Error::kWorkLimit;
        throw Error::kOperand;
      }
      const auto& proof = std::get<DerivedProjection>(checked);
      budget_.Admit(proof.allocated_bytes, 1);
      budget_.Step(proof.work);
      RequireMeaning(proof.native == tree_.derived_protocol &&
                         proof.guards == tree_.receiver_guards,
                     Error::kOperand);
    }
    std::optional<DerivedValueStates> derived_states;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      if (graph_.ops[i].life != analysis::ValueLife::kLive ||
          evaluation_.operations[i].kind == EvaluationKind::kClassProjection ||
          !std::holds_alternative<analysis::ValueSuperSpread>(graph_.ops[i].operation)) continue;
      RequireMeaning(constants_.derived_constructor &&
          &constants_.derived_constructor->values.graph == &graph_ &&
          tree_.identity == constants_.derived_constructor->input.Identity() &&
          tree_.method == constants_.derived_constructor->input.Method(), Error::kOperand);
      auto states = BuildDerivedValueStates(*constants_.derived_constructor,
          analysis::OpId{static_cast<std::uint32_t>(i)}, {}, budget_.Remaining());
      if (const auto* error = std::get_if<DerivedProjectionError>(&states)) {
        if (*error == DerivedProjectionError::kResourceLimit) throw Error::kResourceLimit;
        if (*error == DerivedProjectionError::kWorkLimit) throw Error::kWorkLimit;
        throw Error::kOperand;
      }
      derived_states.emplace(std::get<DerivedValueStates>(std::move(states)));
      budget_.Step(derived_states->work);
      budget_.Admit(derived_states->allocated_bytes, 1);
      break; // One shared identity index for every original super occurrence.
    }
    if(original_.iterations)for(std::uint32_t l=0;l<original_.iterations->loops.size();++l) {
      budget_.Step();const auto& native=*original_.iterations;
      current_=native.loops[l].acquisition;origin_={graph_.ops[current_.value].origin.value};
      const auto& syntax=std::get<ForOfStmt>(tree_.statements[native.statements[l].value].statement);
      InputAt(0,syntax.iterable);
    }
    for (const auto& native : original_.enumerations) {
      budget_.Step(); current_ = native.original.acquisition.operation;
      origin_ = {graph_.ops[current_.value].origin.value};
      const auto& syntax = std::get<ForInStmt>(tree_.statements[native.statement.value].statement);
      RequireMeaning(syntax.object == native.object && syntax.key == native.key, Error::kOperand);
      InputAt(0, syntax.object);
    }
    RequireMeaning(tree_.receiver_guards == evaluation_.receiver_guards, Error::kOperand);
    RequireMeaning(tree_.receiver_guards.size() == constants_.receiver_guards.size() &&
        std::equal(tree_.receiver_guards.begin(), tree_.receiver_guards.end(), constants_.receiver_guards.begin()), Error::kOperand);
    if (tree_.derived_protocol) {
      for (const auto call : tree_.derived_protocol->calls) {
        budget_.Step();
        RequireMeaning(flow_.expressions[call.value].has_value(),
                       Error::kOperand);
        original_.operations[call.value].derived_construction =
            *flow_.expressions[call.value];
      }
      budget_.Array(original_.derived_bindings,
                    tree_.derived_protocol->bindings.size());
      for (std::uint32_t b = 0; b < tree_.derived_protocol->bindings.size();
           ++b) {
        const auto binding = tree_.derived_protocol->bindings[b];
        budget_.Step();
        RequireMeaning(
            flow_.expressions[binding.construction.value].has_value(),
            Error::kOperand);
        original_.derived_bindings[b] =
            *flow_.expressions[binding.construction.value];
        auto& range = original_.operations[binding.guard.value].derived_binding;
        if (!range) range = OriginalDerivedBindingRange{b, 0};
        ++range->count;
      }
    }
    VerifyMemberUpdates(graph_, evaluation_, budget_);
    RetainTemplateInputs(graph_,evaluation_,tree_,constants_,original_,budget_);
    budget_.Admit(MemberUpdates(evaluation_).updates.size(), sizeof(OriginalMemberUpdate));
    original_.member_updates.reserve(MemberUpdates(evaluation_).updates.size());
    budget_.Admit(original_.member_updates.capacity()-MemberUpdates(evaluation_).updates.size(), sizeof(OriginalMemberUpdate));
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      budget_.Step();
      const auto& op = graph_.ops[i];
      if (MemberUpdateIntrinsic(evaluation_, {static_cast<std::uint32_t>(i)})) continue;
      if (original_.iterations && original_.iterations->operations[i]) continue;
      if ((!original_.enumeration_operations.empty() && original_.enumeration_operations[i]) ||
          (!original_.forwarded_operations.empty() && original_.forwarded_operations[i])) continue;
      if (evaluation_.operations[i].kind == EvaluationKind::kTemplateConstruction || evaluation_.operations[i].kind == EvaluationKind::kArrayCursor || evaluation_.operations[i].kind == EvaluationKind::kClassProjection || IsAbsentHeritageTransport(graph_, evaluation_, op) || op.life != analysis::ValueLife::kLive ||
          evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
          evaluation_.operations[i].kind == EvaluationKind::kAlias) continue;
      current_ = {static_cast<std::uint32_t>(i)};
      origin_ = {op.origin.value};
      if (std::holds_alternative<analysis::ValueConcurrentNotification>(op.operation)) {
        RequireMeaning(tree_.concurrent.has_value() && evaluation_.concurrent &&
            evaluation_.concurrent->members[i] && !flow_.expressions[i],Error::kOperand);
        continue; // Payload identity is independently coupled to the return port.
      }
      if (std::holds_alternative<analysis::ValueJump>(op.operation) || IsEnvironmentState(op.operation)) continue;
      if (std::holds_alternative<analysis::ValueDebugger>(op.operation)) {
        RequireMeaning(!op.result && op.use_count == 0 && !flow_.expressions[i], Error::kOperand);
        continue;
      }
      if (evaluation_.operations[i].kind == EvaluationKind::kCellGuard) {
        RequireMeaning(evaluation_.cells && IsInitializedCellGuard(graph_, *evaluation_.cells, current_) &&
            !flow_.expressions[i], Error::kOperand);
        const auto value=*InitializedCellGuardInput(graph_,*evaluation_.cells,current_);
        const auto load=std::get<analysis::OpResult>(graph_.values[value.value].origin).op;
        original_.operations[i].initialized_guard=OriginalInitializedGuard{load,value};
        continue;
      }
      if (IsReceiverGuard(tree_, current_)) {
        RequireMeaning(std::holds_alternative<analysis::ValueSuperCheck>(op.operation) &&
            (tree_.callable == CallableKind::kDerivedConstructor ||
            std::get<analysis::ValueSuperCheck>(op.operation).kind == analysis::SuperCheckKind::kRequireInitialized) &&
            !op.result &&
            !flow_.expressions[i], Error::kOperand);
        continue;
      }
      RequireMeaning(flow_.expressions[i].has_value(), Error::kOperand);
      const auto predicate = !flow_.branch_predicates.empty() ? flow_.branch_predicates[i] : std::nullopt;
      const auto expression = predicate.value_or(*flow_.expressions[i]);
      if (const auto* proof=MemberUpdateAt(evaluation_, current_)) {
        const auto& native=Node<MemberMutationExpr>(expression);
        RequireMeaning(tree_.strictness==Strictness::kStrict && ((std::get_if<MemberIncrement>(&native.operation) &&
              std::get_if<MemberIncrement>(&proof->operation) &&
              std::get<MemberIncrement>(native.operation)==std::get<MemberIncrement>(proof->operation)) ||
             (std::get_if<MemberCompound>(&native.operation) &&
              std::get_if<analysis::BinaryOperator>(&proof->operation) &&
              std::get<MemberCompound>(native.operation).operation==std::get<analysis::BinaryOperator>(proof->operation))) &&
            std::holds_alternative<MemberCompound>(native.operation)==bool(proof->rhs), Error::kOperator);
        current_=proof->read; origin_={graph_.ops[current_.value].origin.value};
        InputAt(0,native.target.base);
        if (proof->key) {
          const auto* key=std::get_if<ComputedKey>(&native.target.key);
          RequireMeaning(key,Error::kOperand);InputAt(1,key->expression);
        } else {
          Named(native.target.key,constants_.operations[proof->read.value]);
          Named(native.target.key,constants_.operations[proof->write.value]);
        }
        if (proof->rhs) {
          current_=proof->calculation;origin_={graph_.ops[current_.value].origin.value};
          InputAt(1,std::get<MemberCompound>(native.operation).rhs);
        }
        for(const auto& port:proof->ports) {
          budget_.Step();RequireMeaning(flow_.expressions[port.operation.value]==expression,Error::kOperand);
          original_.operations[port.operation.value].completion=OriginalCompletionBoundary::kIntrinsicAction;
          if(graph_.ops[port.operation.value].result==proof->value)
            original_.operations[port.operation.value].result_expression=expression;
        }
        budget_.Admit(proof->inputs.size(),sizeof(MemberUpdateInput));
        budget_.Admit(proof->ports.size(),sizeof(MemberUpdatePort));
        original_.member_updates.push_back({expression,*proof,native});
        continue;
      }
      if (!flow_.selection_truths.empty() && flow_.selection_truths[i]) {
        const auto& truth = *flow_.selection_truths[i];
        const auto& logical = Node<LogicalExpr>(expression);
        RequireMeaning(flow_.logical_operations[i] && logical.operation == *flow_.logical_operations[i], Error::kOperand);
        // Fresh flow proved the original pure truth equations and polarity,
        // not merely an operator shape. Check the retained input identity at
        // its original saving use; phi-edge inputs are verified below, and
        // ordered prefixes were consumed by flow. Those semantic obligations,
        // not an additional producer-kind list, authorize this projection.
        origin_ = {graph_.ops[truth.input_owner.value].origin.value};
        Input(truth.value, truth.input, truth.occurrence);
        original_.operations[i].control=OriginalControlInput{truth.input,
            logical.operation==TreeLogical::kNullish?OriginalControlKind::kNullishSelection:OriginalControlKind::kTruthSelection,truth.test};
        continue;
      }
      if (!predicate && !flow_.conditional_operations.empty() && flow_.conditional_operations[i]) {
        const auto& conditional = Node<ConditionalExpr>(expression);
        RequireMeaning(std::holds_alternative<analysis::ValueBranch>(op.operation) && op.use_count == 1, Error::kOperand);
        Input(graph_.uses[op.first_use.value].operand, conditional.condition, op.first_use);
        // The verified conditional uses this exact Boolean input. Retain the
        // pinned predicate polarity through the common truth adapter; an input
        // occurrence alone cannot certify its raw taken/fallthrough phases.
        const auto truth=PureBranchTruth(graph_,current_);
        RequireMeaning(truth.has_value(),Error::kOperator);
        original_.operations[i].control=OriginalControlInput{conditional.condition,OriginalControlKind::kPredicate,truth};
        continue;
      }
      if (IsArrayInitialization(evaluation_, current_)) {
        const auto& element = Node<ArrayElement>(expression);
        const bool spread = std::holds_alternative<analysis::ValueArraySpread>(op.operation);
        const auto value = spread ? std::get<ArraySpreadElement>(element.element).iterable :
            std::get<ArrayValueElement>(element.element).value;
        const auto role = spread ? analysis::OperandRole::kSource : analysis::OperandRole::kStoredValue;
        bool found = false;
        for (std::uint32_t u = 0; u < op.use_count; ++u) {
          budget_.Step(); const analysis::UseId use{op.first_use.value + u};
          if (graph_.uses[use.value].role == role) {
            RequireMeaning(!found, Error::kOperand); found = true;
            Input(graph_.uses[use.value].operand, value, use);
          }
        }
        RequireMeaning(found, Error::kOperand);
        original_.operations[i].intrinsic_expression = expression;
        original_.operations[i].completion = OriginalCompletionBoundary::kIntrinsicAction;
        continue;
      }
      if (const auto* array = ArrayLiteralFor(evaluation_, current_)) {
        const auto& syntax = Node<ArrayExpr>(expression);
        RequireMeaning(syntax.buffered == array->buffered, Error::kConstant);
        if (array->buffered) {
          const auto* buffer = std::get_if<AggregateConstant>(&constants_.operations[i]);
          RequireMeaning(buffer && buffer->count == array->buffered, Error::kConstant);
          for (std::uint32_t p = 0; p < array->buffered; ++p) {
            budget_.Step(); const auto& element = Node<ArrayElement>(tree_.arguments[syntax.elements.first + p]);
            BufferedLiteral(constants_.properties[buffer->first + p].value,
                std::get<ArrayValueElement>(element.element).value);
          }
        }
        original_.operations[i].intrinsic_expression = expression;
        original_.operations[i].result_expression = expression;
        original_.operations[i].completion = OriginalCompletionBoundary::kIntrinsicAction;
        continue;
      }
      const auto input = [&](std::uint32_t ordinal) -> const analysis::ValueOperand& {
        return graph_.uses[op.first_use.value + ordinal].operand;
      };
      std::visit([&](const auto& operation) {
        using T = std::decay_t<decltype(operation)>;
        // Verified input reads own native evaluation boundaries. The common
        // projection withdraws them when replaced; ABI/host identity premises
        // and provenance remain independent. Rest allocation instead belongs
        // to invocation binding, never to a later SymbolExpr read.
        if constexpr (std::is_same_v<T, analysis::ValueFrameFunction>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage() && tree_.self &&
              Node<SymbolExpr>(expression).symbol == tree_.self, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueGlobalObject>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          (void)Node<RealmObjectExpr>(expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueSymbolConstructor>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          (void)Node<SymbolConstructorExpr>(expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueRestArguments>) {
          original_.operations[i].invocation_parameter=ParameterId{operation.first_argument};
          original_.operations[i].completion=OriginalCompletionBoundary::kInvocationBinding;
          RequireMeaning(op.use_count == 0 && op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage() &&
              operation.first_argument < tree_.parameters.size() &&
              tree_.parameters[operation.first_argument].rest &&
              Node<SymbolExpr>(expression).symbol == tree_.parameters[operation.first_argument].symbol, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueArgumentSnapshot>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& snapshot = Node<ArgumentSnapshotExpr>(expression);
          RequireMeaning(graph_.arguments && evaluation_.arguments == graph_.arguments &&
              snapshot.owner == tree_.owner && operation.slots == graph_.arguments->frame.parameters,
              Error::kOperand);
          Arguments(op, snapshot.slots, 0, operation.slots);
        } else if constexpr (std::is_same_v<T, analysis::ValueThisProperty>) {
          RequireMeaning(!IsArrowCallable(tree_.callable)&&tree_.callable!=CallableKind::kDerivedConstructor,Error::kThisPropertyScope);
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(operation.kind <= analysis::ThisPropertyKind::kWriteValue, Error::kOperator);
          const bool write=analysis::WritesThisProperty(operation.kind);
          RequireMeaning(operation.name.has_value()==analysis::NamedThisProperty(operation.kind),Error::kOperator);
          ExprId base;const MemberKey* key;
          if(write){const auto& assigned=Node<AssignmentExpr>(expression);const auto* target=std::get_if<MemberTarget>(&assigned.target);RequireMeaning(target,Error::kOperator);base=target->base;key=&target->key;RequireMeaning(tree_.strictness==Strictness::kStrict,Error::kUnsupportedOperation);InputAt(operation.name?0:1,assigned.value);}
          else {const auto& member=Node<MemberExpr>(expression);RequireMeaning(member.lookup==analysis::PropertyLookup::kObject,Error::kOperator);base=member.base;key=&member.key;}
          RequireMeaning(Node<ThisExpr>(base).owner==tree_.owner,Error::kSuperReceiver);
          if(operation.name)Named(*key,constants_.operations[i]);
          else {
            const auto* computed=std::get_if<ComputedKey>(key);
            RequireMeaning(computed,Error::kOperator);
            InputAt(0,computed->expression);
            // RuntimeLd/StObjByValue (runtime_stubs-inl.h:640,664) checks
            // receiver coercibility before ToPropertyKey, at Get/PutValue.
            // Native MemberTarget retains the same reference through RHS;
            // common recovery owns these separate key/Get/Set events.
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueDynamicImport>) {
          RequireMeaning(DynamicImports(tree_.identity.contract), Error::kUnsupportedOperation);
          InputAt(0, Node<DynamicImportExpr>(expression).specifier);
        } else if constexpr (std::is_same_v<T, analysis::ValueArrayPattern>) {
          const auto& pattern=Node<ArrayPatternExpr>(expression);
          if(original_.iterations && original_.iterations->view.completion_ports) {
            // The pattern owner certifies one native acquire/step/close action.
            // Retain its own exception port inside the enclosing iterator;
            // the internal record result never substitutes for that action.
            original_.operations[i].intrinsic_expression=expression;
            original_.operations[i].completion=OriginalCompletionBoundary::kIntrinsicAction;
          }
          RequireMeaning(ArrayPatternAcquisition(tree_.identity.contract) && pattern.action==operation.action, Error::kUnsupportedOperation);
          InputAt(0, pattern.operand);
        } else if constexpr (std::is_same_v<T, analysis::ValueNewTarget>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(NativeNewTarget(tree_.identity.contract), Error::kNewTargetPremise);
          RequireMeaning(!IsArrowCallable(tree_.callable) && Node<NewTargetExpr>(expression).owner == tree_.owner &&
              Node<NewTargetExpr>(expression).form == NewTargetForm::kInstruction, Error::kNewTargetOwner);
        } else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(op.result && op.result->value < graph_.values.size() &&
              graph_.values[op.result->value].domain.IsLanguage(), Error::kOperand);
          RequireMeaning(Node<ThisExpr>(expression).owner == tree_.owner, Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueCopy>) {
          if (!evaluation_.integer_nans.empty() && evaluation_.integer_nans[current_.value] == NaNProjection::kIntegerCoercion)
            IntegerInput(input(0), expression, op.first_use);
          else InputAt(0, expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueObjectRest>) {
          RequireMeaning(ObjectRests(tree_.identity.contract),Error::kUnsupportedOperation);
          const auto& rest=Node<ObjectRestExpr>(expression);
          RequireMeaning(rest.keys.count==operation.keys,Error::kOperand);
          original_.operations[i].intrinsic_expression=expression;
          InputAt(0,rest.source);
          for(std::uint32_t k=0;k<operation.keys;++k){budget_.Step();InputAt(k+1,tree_.arguments[rest.keys.first+k]);}
        } else if constexpr (std::is_same_v<T, analysis::ValueThrow>) {
          if(analysis::ThrowsIntrinsicTypeError(operation.kind)){Node<PatternFailureExpr>(expression);original_.operations[i].intrinsic_expression=expression;}
          else InputAt(0,expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueSuperCheck>) {
          RequireMeaning(
              tree_.derived_protocol &&
                  operation.kind ==
                      analysis::SuperCheckKind::kRequireInitialized &&
                  std::binary_search(tree_.derived_protocol->reads.begin(),
                                     tree_.derived_protocol->reads.end(),
                                     current_) &&
                  Node<ThisExpr>(expression).owner == tree_.owner,
              Error::kOperand);
          original_.operations[i].intrinsic_expression = expression;
        } else if constexpr (std::is_same_v<T, analysis::ValueReturn>) {
          InputAt(0, expression);
          if (tree_.derived_protocol) {
            RequireMeaning(std::binary_search(
                               tree_.derived_protocol->returns.begin(),
                               tree_.derived_protocol->returns.end(), current_),
                           Error::kOperand);
            original_.operations[i].derived_return = true;
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueUnary>) {
          if(operation.op==analysis::UnaryOperator::kTemplateObject){
            const auto* site=TemplateSite(evaluation_,current_);RequireMeaning(site,Error::kOperand);const auto& object=Node<TemplateObjectExpr>(expression);RequireMeaning(object.quasis&&object.quasis->size()==site->quasis.size(),Error::kConstant);
            for(std::size_t q=0;q<site->quasis.size();++q){budget_.Step();const auto& expected=site->quasis[q];const auto* raw=std::get_if<TextId>(&constants_.operations[expected.raw.value]);RequireMeaning(raw&&(*object.quasis)[q].raw==*raw,Error::kConstant);const auto* cooked=expected.cooked?std::get_if<TextId>(&constants_.operations[expected.cooked->value]):nullptr;RequireMeaning((*object.quasis)[q].cooked==(cooked?std::optional{*cooked}:std::nullopt),Error::kConstant);}
            original_.operations[i].intrinsic_expression=expression;
          } else original_.operations[i].intrinsic_expression=Unary(operation.op, input(0), expression);
        } else if constexpr (std::is_same_v<T, analysis::ValueAwait>) {
          InputAt(0, Node<AwaitExpr>(expression).operand);
          original_.operations[i].intrinsic_expression=expression;
        } else if constexpr (std::is_same_v<T, analysis::ValueYield>) {
          const auto& yielded = Node<YieldExpr>(expression);
          RequireMeaning((yielded.form == YieldForm::kDirect && operation.origin.form == analysis::YieldForm::kDirect) ||
              (yielded.form == YieldForm::kDelegate && operation.origin.form == analysis::YieldForm::kDelegate), Error::kOperator);
          InputAt(0, yielded.operand);
          original_.operations[i].intrinsic_expression=expression;
        } else if constexpr (std::is_same_v<T, analysis::ValueBranch>) {
          if (flow_.branch_polarity[current_.value] == BranchPolarity::kFallthrough) {
            using U = analysis::UnaryOperator;
            if (BooleanEvaluation(graph_, input(0))) {
              RequireMeaning(operation.predicate == U::kFalseOrZero || operation.predicate == U::kTrueOrNonzeroNumber, Error::kOperator);
              Unary(operation.predicate == U::kFalseOrZero ? U::kTrueOrNonzeroNumber : U::kFalseOrZero, input(0), expression);
            } else Unary(operation.predicate, input(0), UnaryInput(expression, TreeUnary::kNot));
          } else Unary(operation.predicate, input(0), expression);
          original_.operations[i].control=OriginalControlInput{expression,OriginalControlKind::kPredicate,
              PureTruthTest{TruthDomains::All(),flow_.branch_polarity[current_.value]==BranchPolarity::kTaken?
                  TruthDomains::Truthy():TruthDomains::Truthy().Complement(),PureTruthOutcome::kDecision}};
        } else if constexpr (std::is_same_v<T, analysis::ValueBinary>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& binary = Node<BinaryExpr>(expression);
          RequireMeaning(binary.operation == HoleSourceOperator(evaluation_.holes ? &*evaluation_.holes : nullptr,
              current_, operation.op), Error::kOperator);
          if (IntegerCoercion(op.operation)) {
            IntegerInput(input(0), binary.left, analysis::UseId{op.first_use.value});
            IntegerInput(input(1), binary.right, analysis::UseId{op.first_use.value + 1});
          } else {
            Input(input(0), binary.left, analysis::UseId{op.first_use.value});
            Input(input(1), binary.right, analysis::UseId{op.first_use.value + 1});
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueHole>) {
          RequireMeaning(evaluation_.holes && tree_.hole && Node<SymbolExpr>(expression).symbol == *tree_.hole,
              Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueModuleRead>) {
          RequireMeaning(next_module < evaluation_.modules.size(), Error::kOperand);
          const OriginalActionRegionId region{static_cast<std::uint32_t>(next_module)};
          const auto& module = evaluation_.modules[next_module++];
          const auto& reference = Node<SymbolExpr>(expression);
          RequireMeaning(reference.loading == operation.loading && reference.domain == operation.domain, Error::kOperand);
          const auto symbol = reference.symbol;
          RequireMeaning(module.pair.read == current_ && module.read == operation &&
              symbol.value < module_aliases.size() && module_aliases[symbol.value] &&
              tree_.modules[*module_aliases[symbol.value]].target == module.target, Error::kOperand);
          // ModuleGuards and TreeFlow already proved this composition, including
          // order, no bypass and exception ownership. Export the whole native
          // GetValue, never a no-throw cap for its raw pre-guard load.
          const auto exit=module.pair.guard.value_or(current_);
          RequireMeaning(exit.value<graph_.ops.size()&&flow_.expressions[exit.value]==expression,Error::kOperand);
          original_.action_regions[region.value]={expression,current_,exit,current_,symbol,{}};
          for(const auto member:{current_,exit}) {
            auto& owner=original_.action_region_members[member.value];
            RequireMeaning(!owner||owner==region,Error::kOperand);owner=region;
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueModuleWrite>) {
          const auto* binding = std::get_if<ModuleStoreConstant>(&constants_.operations[i]);
          RequireMeaning(binding && binding->slot == operation.slot, Error::kConstant);
          SymbolId symbol;
          if (binding->initialize) {
            RequireMeaning(flow_.statements[i].has_value(), Error::kOperand);
            const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[flow_.statements[i]->value].statement);
            RequireMeaning(declaration && declaration->initializer == expression, Error::kOperand);
            symbol = declaration->symbol;
            InputAt(0, expression);
            Store(*flow_.statements[i],symbol,expression,input(0));
          } else {
            const auto& assignment = Node<AssignmentExpr>(expression);
            const auto* target = std::get_if<SymbolTarget>(&assignment.target);
            RequireMeaning(target != nullptr, Error::kOperand);
            symbol = target->symbol;
            InputAt(0, assignment.value);
            Store(expression,symbol,assignment.value,input(0));
          }
          RequireMeaning(symbol.value < module_aliases.size() && module_aliases[symbol.value], Error::kOperand);
          const auto& alias = tree_.modules[*module_aliases[symbol.value]];
          RequireMeaning(alias.target == binding->target && alias.access == ModuleAccess::kMutable &&
              (!binding->initialize || alias.local), Error::kOperand);
        } else if constexpr (std::is_same_v<T, analysis::ValueGuard>) {
          // Flow independently proved this placement is the checked read's
          // same expression; combined-origin checking covers both original PCs.
          RequireMeaning((evaluation_.operations[i].kind == EvaluationKind::kModuleGuard ||
              evaluation_.operations[i].kind == EvaluationKind::kGuardedCellRead), Error::kUnsupportedOperation);
        } else if constexpr (std::is_same_v<T, analysis::ValueLexicalLoad>) {
          const auto symbol = CellSymbol(expression,CellAccessKind::kLoad);
          RequireMeaning(Node<SymbolExpr>(expression).symbol == symbol, Error::kOperand);
          if (GuardedInput(evaluation_, current_)) {
            const auto exit = *GuardedPeer(evaluation_, current_);
            RequireMeaning(next_capture < original_.action_regions.size() && flow_.expressions[exit.value] == expression, Error::kOperand);
            const OriginalActionRegionId region{static_cast<std::uint32_t>(next_capture++)};
            original_.action_regions[region.value] = {expression,current_,exit,current_,symbol,{},OriginalReadKind::kCapture};
            original_.action_region_members[i] = original_.action_region_members[exit.value] = region;
          } else original_.operations[i].intrinsic_expression=expression;
        } else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>) {
          if (const auto cell = DeferredCell()) {
            // Deferred TDZ initialization: the cell's own declaration statement
            // owns the stored value, exactly as a module initialization does.
            RequireMeaning(flow_.statements[i].has_value() && cell->value < flow_.cells.size() &&
                flow_.cells[cell->value] && original_.cells && cell->value < original_.cells->bindings.size(), Error::kOperand);
            const auto* declaration = std::get_if<DeclarationStmt>(&tree_.statements[flow_.statements[i]->value].statement);
            RequireMeaning(declaration && declaration->initializer == expression &&
                declaration->symbol == *flow_.cells[cell->value] &&
                original_.cells->bindings[cell->value].binding == declaration->symbol &&
                original_.cells->bindings[cell->value].declaration == flow_.statements[i], Error::kOperand);
            InputAt(0, expression);
            Store(*flow_.statements[i], declaration->symbol, expression, input(0));
            return;
          }
          const auto& assignment = Node<AssignmentExpr>(expression);
          const auto* target = std::get_if<SymbolTarget>(&assignment.target);
          RequireMeaning(target && target->symbol == CellSymbol(expression,CellAccessKind::kStore), Error::kOperand);
          InputAt(0, assignment.value);
          Store(expression,target->symbol,assignment.value,input(0));
        } else if constexpr (std::is_same_v<T, analysis::ValueNamedRead>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& member = Node<MemberExpr>(expression);
          Named(member.key, constants_.operations[i]);
          RequireMeaning(member.lookup == operation.lookup, Error::kOperator);
          if (operation.lookup == analysis::PropertyLookup::kSuper) {
            RequireMeaning(tree_.home_object == HomeObjectBinding::kClass, Error::kSuperHomeObject);
            const auto receiver = ResolveEvaluationOperand(evaluation_, input(0));
            RequireMeaning(IsReceiverOperand(graph_, receiver) &&
                std::holds_alternative<ThisExpr>(tree_.expressions[member.base.value].expression) &&
                std::get<ThisExpr>(tree_.expressions[member.base.value].expression).owner == tree_.owner,
                Error::kSuperReceiver);
          } else RequireMeaning(operation.lookup == analysis::PropertyLookup::kObject, Error::kOperator);
          InputAt(0, member.base);
        } else if constexpr (std::is_same_v<T, analysis::ValueNamedWrite>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& assigned = Node<AssignmentExpr>(expression);
          const auto* member = std::get_if<MemberTarget>(&assigned.target);
          RequireMeaning(member != nullptr, Error::kOperator);
          Named(member->key, constants_.operations[i]);
          RequireMeaning(assigned.lookup == operation.lookup, Error::kOperator);
          if (operation.lookup == analysis::PropertyLookup::kSuper)
            CheckSuperStore(*member, assigned.value, input(0), super_rhs);
          InputAt(0, member->base); InputAt(1, assigned.value);
        } else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& member = Node<MemberExpr>(expression);
          RequireMeaning(member.lookup == operation.lookup, Error::kOperator);
          if (operation.lookup == analysis::PropertyLookup::kSuper) {
            RequireMeaning(tree_.home_object == HomeObjectBinding::kClass, Error::kSuperHomeObject);
            // Pinned RuntimeLdSuperByValue rejects a null super base before
            // ToPropertyKey; native GetValue rejects it after key conversion.
            // Primitive keys make that ordering unobservable. Unknown object
            // coercion needs an independent non-null-base proof, not admission.
            const auto key_operand = ResolveEvaluationOperand(evaluation_, input(1));
            const bool primitive = std::visit([&](const auto& value) {
              if constexpr (std::is_same_v<std::decay_t<decltype(value)>, analysis::ValueId>) {
                const auto kinds = graph_.values[value.value].domain;
                return kinds.IsNumber() || kinds.IsString() || kinds.IsZeroOrOnePrimitive();
              } else return true;
            }, key_operand);
            RequireMeaning(primitive, Error::kSuperKeyDomain);
            const auto receiver = ResolveEvaluationOperand(evaluation_, input(0));
            RequireMeaning(IsReceiverOperand(graph_, receiver) &&
                std::holds_alternative<ThisExpr>(tree_.expressions[member.base.value].expression) &&
                std::get<ThisExpr>(tree_.expressions[member.base.value].expression).owner == tree_.owner,
                Error::kSuperReceiver);
          }
          const auto* key = std::get_if<ComputedKey>(&member.key);
          RequireMeaning(key != nullptr, Error::kOperator);
          InputAt(0, member.base); InputAt(1, key->expression);
        } else if constexpr (std::is_same_v<T, analysis::ValuePropertyDelete>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(tree_.strictness==Strictness::kStrict,Error::kDeleteStrictness);
          const auto& node=Node<DeleteExpr>(expression);
          const auto* key=std::get_if<ComputedKey>(&node.key);
          RequireMeaning(key!=nullptr,Error::kOperator);
          InputAt(0,node.base);InputAt(1,key->expression);
        } else if constexpr (std::is_same_v<T, analysis::ValuePropertyWrite>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(tree_.strictness == Strictness::kStrict, Error::kUnsupportedOperation);
          const auto& assignment = Node<AssignmentExpr>(expression);
          const auto* member = std::get_if<MemberTarget>(&assignment.target);
          RequireMeaning(member != nullptr, Error::kOperator);
          const auto* key = std::get_if<ComputedKey>(&member->key);
          RequireMeaning(key != nullptr, Error::kOperator);
          RequireMeaning(assignment.lookup == operation.lookup, Error::kOperator);
          if (operation.lookup == analysis::PropertyLookup::kSuper) {
            CheckSuperStore(*member, assignment.value, input(0), super_rhs);
            const auto operand = ResolveEvaluationOperand(evaluation_, input(1));
            const bool primitive = std::visit([&](const auto& v) {
              if constexpr(std::is_same_v<std::decay_t<decltype(v)>,analysis::ValueId>) {
                const auto kinds=graph_.values[v.value].domain;
                return kinds.IsString() || kinds.IsNumber() || kinds.IsZeroOrOnePrimitive();
              } else return true;
            },operand);
            RequireMeaning(primitive,Error::kSuperKeyDomain);
          }
          InputAt(0, member->base); InputAt(1, key->expression);
          InputAt(2, assignment.value);
        } else if constexpr (std::is_same_v<T, analysis::ValueIndexedWrite>) {
          RequireMeaning(operation.kind==analysis::IndexedDefinition::kOwnStore || operation.kind==analysis::IndexedDefinition::kDefineField,Error::kOperator);
          RequireMeaning(flow_.initializers[i].has_value(), Error::kOperand);
          const auto& property = tree_.properties[flow_.initializers[i]->value];
          original_.operations[i].intrinsic_property=flow_.initializers[i];
          const auto expected = std::to_string(operation.index);
          RequireMeaning(property.key && property.key->value < tree_.names.size() &&
              tree_.names[property.key->value] == std::u16string(expected.begin(), expected.end()), Error::kConstant);
          const auto previous = edge_input_;
          edge_input_ = !IsStagedLiteral(
              evaluation_.literals ? &*evaluation_.literals : nullptr,
              current_);
          InputAt(1, property.value); edge_input_ = previous;
        } else if constexpr (std::is_same_v<T, analysis::ValueCopyProperties>) {
          RequireMeaning((operation.kind == analysis::ObjectMutationKind::kSetPrototype ||
              ObjectSpreads(tree_.identity.contract)) && flow_.initializers[i],Error::kUnsupportedOperation);
          const auto& property=tree_.properties[flow_.initializers[i]->value];
          RequireMeaning(!property.key && property.name_setting==analysis::PropertyNameSetting::kNone &&
              property.effect == (operation.kind == analysis::ObjectMutationKind::kSetPrototype ?
                  ObjectPropertyEffect::kSetPrototype : ObjectPropertyEffect::kDefine),Error::kOperator);
          const auto target=ResolveEvaluationOperand(evaluation_,input(0));
          const auto result=op.result?ResolveEvaluationOperand(evaluation_,analysis::ValueOperand{*op.result}):analysis::ValueOperand{analysis::SpecialValue::kUndefined};
          const auto* target_id=std::get_if<analysis::ValueId>(&target);
          const auto* result_id=std::get_if<analysis::ValueId>(&result);
          RequireMeaning(target_id && result_id && *target_id==*result_id,Error::kOperand);
          original_.operations[i].intrinsic_property=flow_.initializers[i];
          const auto previous=edge_input_;edge_input_=true;InputAt(1,property.value);edge_input_=previous;
        } else if constexpr (std::is_same_v<T,
                                            analysis::ValueComputedOwnWrite>) {
          RequireMeaning(ComputedFunctionNames(tree_.identity.contract) && flow_.initializers[i], Error::kUnsupportedOperation);
          const auto property_id = *flow_.initializers[i];
          const auto& property = tree_.properties[property_id.value];
          RequireMeaning(!property.key && property.computed_key && property.effect == ObjectPropertyEffect::kDefine &&
              property.name_setting == operation.name_setting && operation.kind == analysis::OwnWriteKind::kStore, Error::kOperator);
          original_.operations[i].intrinsic_property = property_id;
          const auto previous = edge_input_; edge_input_ = true;
          InputAt(1, *property.computed_key); InputAt(2, property.value);
          edge_input_ = previous;
        } else if constexpr (std::is_same_v<T, analysis::ValueOwnWrite>) {
          if (IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, current_) &&
              evaluation_.operations[i].kind != EvaluationKind::kInitializer) {
            const auto& assignment = Node<AssignmentExpr>(expression);
            original_.operations[i].intrinsic_expression=expression;
            const auto* member = std::get_if<MemberTarget>(&assignment.target);
            RequireMeaning(member != nullptr, Error::kOperand);
            Named(member->key, constants_.operations[i]);
            InputAt(0, member->base); InputAt(1, assignment.value);
            return;
          }
          if (IsReceiverField(graph_, op)) {
            const auto& field = Node<FieldDefinitionExpr>(expression);
            original_.operations[i].intrinsic_expression=expression;
            const auto* name = std::get_if<NameId>(&constants_.operations[i]);
            RequireMeaning(name && field.name == *name, Error::kConstant);
            InputAt(0, field.receiver); InputAt(1, field.value);
            return;
          }
          RequireMeaning(flow_.initializers[i].has_value(), Error::kOperand);
          const auto& property = tree_.properties[flow_.initializers[i]->value];
          original_.operations[i].intrinsic_property=flow_.initializers[i];
          RequireMeaning(property.name_setting == operation.name_setting, Error::kOperator);
          const auto* name = std::get_if<NameId>(&constants_.operations[i]);
          RequireMeaning(name && name->value < constants_.name_count && property.key == *name, Error::kConstant);
          // LiteralPlan proved the receiver's allocation; only its stored value
          // appears in syntax, always as a pre-allocation snapshot or leaf.
          const auto previous = edge_input_;
          edge_input_ = !IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, current_);
          auto value=property.value;
          if(i<evaluation_.literal_fields.fields.size()) {
            const auto range=evaluation_.literal_fields.fields[i];
            for(std::size_t p=0;p<range.count;++p){
              budget_.Step();const auto& sequence=Node<SequenceExpr>(value);value=sequence.right;
            }
          }
          InputAt(1, value);
          edge_input_ = previous;
        } else if constexpr (std::is_same_v<T, analysis::ValueString>) {
          const auto* text = std::get_if<TextId>(&constants_.operations[i]);
          RequireMeaning(text && text->value < constants_.text_count, Error::kConstant);
          if(operation.kind==analysis::PoolTextKind::kRegExp){const auto& literal=Node<RegExpExpr>(expression);RequireMeaning(literal.text==*text&&literal.flags==operation.flags,Error::kConstant);}
          else if (operation.kind == analysis::PoolTextKind::kBigInt)
            RequireMeaning(Node<BigIntExpr>(expression).text == *text, Error::kConstant);
          else RequireMeaning(Literal<TextId>(expression) == *text, Error::kConstant);
        } else if constexpr (std::is_same_v<T, analysis::ValueGlobalRead>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(operation.lookup == analysis::GlobalLookup::kName || GlobalScripts(tree_.identity.contract), Error::kUnsupportedOperation);
          const auto* name = std::get_if<NameId>(&constants_.operations[i]);
          RequireMeaning(name && name->value < constants_.name_count &&
              Node<GlobalNameExpr>(expression).name == *name && !Node<GlobalNameExpr>(expression).value &&
              Node<GlobalNameExpr>(expression).operation == (operation.lookup == analysis::GlobalLookup::kName ? analysis::GlobalOp::kReadName : analysis::GlobalOp::kReadVar), Error::kConstant);
        } else if constexpr (std::is_same_v<T, analysis::ValueGlobalWrite>) {
          RequireMeaning(GlobalScripts(tree_.identity.contract), Error::kUnsupportedOperation);
          original_.operations[i].intrinsic_expression=expression;
          const auto& store=Node<GlobalNameExpr>(expression);
          const auto* name=std::get_if<NameId>(&constants_.operations[i]);
          RequireMeaning(name && *name==store.name && store.operation==operation.operation && store.value, Error::kConstant);
          InputAt(0,*store.value);
        } else if constexpr (std::is_same_v<T, analysis::ValueInvoke>) {
          RequireMeaning(operation.range_semantics == analysis::RangeCallSemantics::kCommon ||
              WideRangeCalls(tree_.identity.contract), Error::kUnsupportedOperation);
          original_.operations[i].intrinsic_expression=expression;
          const auto& call = Node<CallExpr>(expression);
          InputAt(0, call.callee);
          constexpr std::uint32_t kValuePrefix = 1;
          constexpr std::uint32_t kMemberPrefix = 2;
          if (operation.form == analysis::CallArgumentForm::kApplyArray) {
            RequireMeaning(CallSpread(tree_.identity.contract) == CallSpreadSemantics::kPrivateArgumentsV1 &&
                op.use_count == 3 && operation.arguments == 1 && call.array_arguments && call.arguments.count == 0,
                Error::kOperand);
            if (call.reference == CallReference::kMember) Receiver(op, call, input(0), input(1));
            else {
              const auto receiver = ResolveEvaluationOperand(evaluation_, input(1));
              const auto* constant = std::get_if<analysis::SpecialValue>(&receiver);
              RequireMeaning(constant && *constant == analysis::SpecialValue::kUndefined, Error::kOperand);
            }
            const auto source = ResolveEvaluationOperand(evaluation_, input(2));
            const auto* value = std::get_if<analysis::ValueId>(&source);
            RequireMeaning(value && value->value < graph_.values.size() && value->value < uses.values.size() &&
                uses.values[value->value] == 1, Error::kOperand);
            const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
            RequireMeaning(result && result->op.value < graph_.ops.size() &&
                std::holds_alternative<analysis::ValueArrayAllocation>(graph_.ops[result->op.value].operation), Error::kOperand);
            const auto& placement = evaluation_.operations[result->op.value];
            RequireMeaning((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) &&
                placement.consumer == analysis::UseId{op.first_use.value + 2}, Error::kOperand);
            InputAt(2, *call.array_arguments);
            return;
          }
          RequireMeaning(!call.array_arguments, Error::kOperand);
          if (operation.receiver == analysis::InvokeReceiver::kExplicit) {
            RequireMeaning(call.reference == CallReference::kMember, Error::kOperand);
            Receiver(op, call, input(0), input(1));
            Arguments(op, call.arguments, kMemberPrefix, operation.arguments);
          } else {
            RequireMeaning(operation.receiver == analysis::InvokeReceiver::kUndefined &&
                call.reference == CallReference::kValue, Error::kOperand);
            Arguments(op, call.arguments, kValuePrefix, operation.arguments);
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueSuperCall>) {
          RequireMeaning(operation.callee == analysis::SuperCalleeSource::kInvocationFunction,
              Error::kUnsupportedOperation);
          RequireMeaning(tree_.callable == CallableKind::kDerivedConstructor, Error::kOperand);
          const auto& call = Node<SuperCallExpr>(expression);
          RequireMeaning(call.source == operation.source, Error::kOperand);
          if (operation.source == analysis::SuperArgumentSource::kInvocationFrame) {
            RequireMeaning(op.use_count == 1 && !operation.arguments, Error::kOperand);
            if (tree_.parameter_form == ParameterForm::kInvocationRest) {
              const auto* spread = std::get_if<SpreadArguments>(&call.arguments);
              RequireMeaning(spread && tree_.parameters.size() == 1 &&
                  Node<SymbolExpr>(spread->iterable).symbol == tree_.parameters.front().symbol, Error::kOperand);
            } else RequireMeaning(tree_.ImplicitConstructor() && std::holds_alternative<ArgumentRange>(call.arguments) &&
                !std::get<ArgumentRange>(call.arguments).count, Error::kOperand);
          } else {
            const auto* range = std::get_if<ArgumentRange>(&call.arguments);
            RequireMeaning(range, Error::kOperand);
            Arguments(op, *range, 0, operation.arguments);
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueSuperSpread>) {
          RequireMeaning(tree_.callable == CallableKind::kDerivedConstructor &&
              op.use_count == 2 && op.result, Error::kOperand);
          const auto& call = Node<SuperCallExpr>(expression);
          RequireMeaning(call.source == analysis::SuperArgumentSource::kExplicit, Error::kOperand);
          const auto* spread = std::get_if<SpreadArguments>(&call.arguments);
          RequireMeaning(spread, Error::kOperand);
          const auto* constructor = std::get_if<analysis::ValueId>(&input(0));
          RequireMeaning(derived_states && constructor &&
              constructor->value < derived_states->values.size() &&
              derived_states->values[constructor->value] == DerivedValueState::kConstructor, Error::kOperand);
          InputAt(1, spread->iterable);
        } else if constexpr (std::is_same_v<T, analysis::ValueConstruct>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& construct = Node<ConstructExpr>(expression);
          InputAt(0, construct.constructor);
          constexpr std::uint32_t kConstructorPrefix = 1;
          const auto* arguments = std::get_if<ArgumentRange>(&construct.arguments);
          RequireMeaning(arguments, Error::kOperand);
          Arguments(op, *arguments, kConstructorPrefix, operation.arguments);
        } else if constexpr (std::is_same_v<T, analysis::ValueConstructApply>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& construct = Node<ConstructExpr>(expression);
          RequireMeaning(op.use_count == 2, Error::kOperand);
          const auto callee = ResolveEvaluationOperand(evaluation_, input(0));
          const auto* callee_value = std::get_if<analysis::ValueId>(&callee);
          RequireMeaning(callee_value && callee_value->value < graph_.values.size(), Error::kOperand);
          const auto* constructor = std::get_if<analysis::OpResult>(&graph_.values[callee_value->value].origin);
          RequireMeaning(constructor && constructor->op.value < graph_.ops.size() &&
              DirectFunctionObject(graph_.ops[constructor->op.value].operation).has_value(), Error::kOperand);
          const auto source = ResolveEvaluationOperand(evaluation_, input(1));
          const auto* value = std::get_if<analysis::ValueId>(&source);
          RequireMeaning(value && value->value < graph_.values.size() &&
              value->value < uses.values.size() && uses.values[value->value] == 1, Error::kOperand);
          const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
          RequireMeaning(result && result->op.value < graph_.ops.size() &&
              std::holds_alternative<analysis::ValueArrayAllocation>(graph_.ops[result->op.value].operation), Error::kOperand);
          const auto& placement = evaluation_.operations[result->op.value];
          InputAt(0, construct.constructor);
          if (const auto* arguments = std::get_if<ArgumentRange>(&construct.arguments)) {
            RequireMeaning(placement.kind == EvaluationKind::kRoot && placement.temporary &&
                flow_.expressions[result->op.value].has_value(), Error::kOperand);
            const auto& allocation = tree_.expressions[flow_.expressions[result->op.value]->value].expression;
            const auto* array = std::get_if<ObjectExpr>(&allocation);
            RequireMeaning((array && array->kind == AggregateKind::kArray) ||
                std::holds_alternative<EmptyArrayExpr>(allocation), Error::kOperand);
            RequireMeaning(arguments->count == (array ? array->properties.count : 0), Error::kOperand);
            for (std::uint32_t a = 0; a < arguments->count; ++a) {
              budget_.Step(); const auto& member = Node<MemberExpr>(tree_.arguments[arguments->first + a]);
              const auto* key = std::get_if<ComputedKey>(&member.key);
              RequireMeaning(key && Literal<NumberLiteral>(key->expression).bits ==
                  std::bit_cast<std::uint64_t>(static_cast<double>(a)), Error::kOperand);
              InputAt(1, member.base);
            }
            return;
          }
          const auto& arguments = std::get<ArrayArguments>(construct.arguments);
          RequireMeaning((placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) &&
              placement.consumer == analysis::UseId{op.first_use.value + 1}, Error::kOperand);
          InputAt(1, arguments.array);
        } else if constexpr (std::is_same_v<T, analysis::ValueIterResult>) {
          const auto& object = Node<ObjectExpr>(expression);
          const auto* keys = std::get_if<IterResultConstant>(&constants_.operations[i]);
          RequireMeaning(keys && keys->value.value < constants_.name_count &&
              keys->done.value < constants_.name_count &&
              tree_.names[keys->value.value] == u"value" && tree_.names[keys->done.value] == u"done" &&
              object.kind == AggregateKind::kObject && object.buffered == 0 &&
              object.properties.count == IterResultConstant::kProperties && op.use_count == IterResultConstant::kProperties, Error::kConstant);
          const auto& value = tree_.properties[object.properties.first];
          const auto& done = tree_.properties[object.properties.first + 1];
          RequireMeaning(value.key == keys->value && done.key == keys->done, Error::kConstant);
          InputAt(0, value.value);
          if (BooleanEvaluation(graph_, ResolveEvaluationOperand(evaluation_, input(1)))) InputAt(1, done.value);
          else {
            const auto& test = Node<BinaryExpr>(done.value);
            RequireMeaning(test.operation == analysis::BinaryOperator::kStrictEqual, Error::kOperator);
            InputAt(1, test.left);
            RequireMeaning(Literal<BooleanLiteral>(test.right).value, Error::kOperand);
          }
        } else if constexpr (std::is_same_v<T,
                                            analysis::ValueArrayAllocation> ||
                             std::is_same_v<T,
                                            analysis::ValueObjectAllocation>) {
          constexpr bool array = std::is_same_v<T, analysis::ValueArrayAllocation>;
          constexpr auto kind = array ? AggregateKind::kArray : AggregateKind::kObject;
          // Literal/TreeFlow verification already proved no observation until
          // initialization completes. Buffered materialization has no user
          // callbacks; folded overwrites keep their own property actions. This
          // correspondence owns fresh identity, not an intermediate heap image.
          original_.operations[i].intrinsic_expression=expression;
          original_.operations[i].completion=OriginalCompletionBoundary::kIntrinsicAction;
          if (operation.literal) {
            const auto& object = Node<ObjectExpr>(expression);
            RequireMeaning(object.kind == kind, Error::kOperator);
            const auto* expected = std::get_if<AggregateConstant>(&constants_.operations[i]);
            RequireMeaning(expected && expected->kind == kind && expected->first <= constants_.properties.size() &&
                expected->count <= constants_.properties.size() - expected->first &&
                i < flow_.buffered_properties.size() &&
                flow_.buffered_properties[i] == expected->count &&
                object.properties.count >= expected->count, Error::kConstant);
            for (std::uint32_t p = 0; p < expected->count; ++p) {
              budget_.Step();
              const auto& source = constants_.properties[expected->first + p];
              const auto& actual = tree_.properties[object.properties.first + p];
              RequireMeaning(source.key.value < constants_.name_count, Error::kConstant);
              const auto& source_name = tree_.names[source.key.value];
              RequireMeaning(actual.key && actual.key->value < tree_.names.size(),Error::kConstant);
              const auto& actual_name = tree_.names[actual.key->value];
              budget_.Step(source_name.size()); budget_.Step(actual_name.size());
              RequireMeaning(actual_name == source_name, Error::kConstant);
              RequireMeaning(actual.method == source.method, Error::kConstant);
              if (source.method) {
                // The next verified binding is this allocation's key ordinal.
                RequireMeaning(!flow_.defined_properties[object.properties.first + p] &&
                    next_function < evaluation_.functions.size(), Error::kOperand);
                const auto& function = evaluation_.functions[next_function++];
                const auto& created = Node<FunctionExpr>(actual.value);
                RequireMeaning(function.creation == current_ && function.property == p &&
                    function.child != tree_.owner && created.child == function.child &&
                    created.captures.count == function.captures.size(), Error::kOperand);
                for (std::size_t c = 0; c < function.captures.size(); ++c) {
                  budget_.Step(); const auto cell = function.captures[c];
                  RequireMeaning(cell.value < flow_.cells.size() && flow_.cells[cell.value] &&
                      tree_.closure_captures[created.captures.first + c] == *flow_.cells[cell.value], Error::kOperand);
                }
              } else if (!flow_.defined_properties[object.properties.first + p])
                BufferedLiteral(source.value, actual.value);
            }
            for (auto p = expected->count; p < object.properties.count; ++p) {
              budget_.Step(); RequireMeaning(!tree_.properties[object.properties.first + p].method, Error::kConstant);
            }
            if constexpr (array) return;
            // A folded own definition may replace only the last buffered
            // occurrence of its key. Otherwise a following buffer value would
            // overwrite it. Check actual keys, not NameId equality or a builder
            // supplied lookup; distinct pool entries can spell the same name.
            std::vector<std::uint32_t> order;
            budget_.Array(order, expected->count);
            for (std::uint32_t p = 0; p < expected->count; ++p) order[p] = p;
            const auto key = [&](auto p) -> std::u16string_view {
              return tree_.names[constants_.properties[expected->first + p].key.value];
            };
            std::sort(order.begin(), order.end(), [&](auto a, auto b) {
              const auto x = key(a), y = key(b); budget_.Step(); budget_.Step(x.size()); budget_.Step(y.size());
              return x == y ? a < b : x < y;
            });
            for (std::size_t p = 1; p < order.size(); ++p) {
              const auto x = key(order[p - 1]), y = key(order[p]);
              budget_.Step(); budget_.Step(x.size()); budget_.Step(y.size());
              if (x == y) RequireMeaning(!flow_.defined_properties[object.properties.first + order[p - 1]], Error::kConstant);
            }
            return;
          }
          const auto* aggregate = std::get_if<ObjectExpr>(&tree_.expressions[expression.value].expression);
          RequireMeaning(aggregate ? aggregate->kind == kind :
              (array ? std::holds_alternative<EmptyArrayExpr>(tree_.expressions[expression.value].expression) :
                       std::holds_alternative<EmptyObjectExpr>(tree_.expressions[expression.value].expression)), Error::kOperator);
        } else if constexpr (std::is_same_v<T,
                                            analysis::ValueFunctionCreation>) {
          original_.operations[i].intrinsic_expression=expression;
          RequireMeaning(next_function < evaluation_.functions.size(), Error::kOperand);
          const auto& function = evaluation_.functions[next_function++];
          const auto& created = Node<FunctionExpr>(expression);
          RequireMeaning(function.creation == current_ && function.property == analysis::kNoLiteralProperty &&
              function.child != tree_.owner &&
              created.child == function.child && created.captures.count == function.captures.size(), Error::kOperand);
          for (std::size_t c = 0; c < function.captures.size(); ++c) {
            budget_.Step(); const auto cell = function.captures[c];
            RequireMeaning(cell.value < flow_.cells.size() && flow_.cells[cell.value] &&
                tree_.closure_captures[created.captures.first + c] == *flow_.cells[cell.value], Error::kOperand);
          }
        } else if constexpr (std::is_same_v<T, analysis::ValueClassCreation>) {
          original_.operations[i].intrinsic_expression=expression;
          const auto& created = Node<ClassExpr>(expression);
          const auto* bound = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[i]);
          RequireMeaning(bound && *bound && created.definition == *bound, Error::kConstant);
          const auto& definition = *created.definition;
          RequireMeaning(definition.identity == tree_.identity && definition.creation.creator == tree_.method &&
              definition.creation.pc == graph_.origins[op.origin.value].pc &&
              definition.creation.creation.constructor == operation.constructor &&
              definition.creation.creation.literal == operation.literal &&
              definition.creation.creation.length == operation.length &&
              definition.creation.creation.cache_slot == operation.cache_slot &&
              definition.creation.creation.kind == operation.kind && op.use_count == 1, Error::kOperand);
          std::uint64_t sendable_work=0;
          if(const auto failure=VerifySendableClass(definition,definition.DraftPrepared(),budget_.Remaining(),&sendable_work)) {
            if(failure->kind==SendableError::kWorkLimit)throw Error::kWorkLimit;
            throw Error::kOperand;
          }
          budget_.Step(sendable_work);
          const auto* heritage = std::get_if<analysis::ValueId>(&input(0));
          const bool absent = heritage && graph_.values[heritage->value].domain == analysis::ValueKinds::Hole();
          if (evaluation_.flow == EvaluationFlow::kPhased) {
            budget_.Step();
            RequireMeaning(definition.phase_binding &&
                               definition.phase_binding->Matches(
                                   constants_.class_creator, current_),
                           Error::kOperand);
          }
          RequireMeaning(created.heritage.has_value() != absent &&
              definition.callable ==
                  (absent ? CallableKind::kBaseConstructor : CallableKind::kDerivedConstructor), Error::kOperand);
          if (definition.constructor) {
            const auto& family = definition.constructor->Family();
            RequireMeaning(family.members[family.root.value].tree.callable == definition.callable, Error::kOperand);
          }
          if (created.heritage) InputAt(0, *created.heritage);
        } else {
          static_assert(
              std::is_same_v<T, analysis::ValueIteratorAcquire> ||
              std::is_same_v<T, analysis::ValueObjectCheck> ||
              std::is_same_v<T, analysis::ValueEnumeration> ||
              std::is_same_v<T, analysis::ValueArraySpread> ||
              std::is_same_v<T, analysis::ValueSuperCall> ||
              std::is_same_v<T, analysis::ValueDefineAccessor> ||
              std::is_same_v<T, analysis::ValueHole> ||
              std::is_same_v<T, analysis::ValueEnvironmentPush> ||
              std::is_same_v<T, analysis::ValueEnvironmentPop> ||
              std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
              std::is_same_v<T, analysis::ValueNop> ||
              std::is_same_v<T, analysis::ValueConcurrentNotification> ||
              std::is_same_v<T, analysis::ValueDebugger> ||
              std::is_same_v<T, analysis::ValueJump>);
          throw Error::kUnsupportedOperation;
        }
      }, op.operation);
      if(op.result && !std::holds_alternative<analysis::ValueCopyProperties>(op.operation) &&
          (original_.action_region_members.empty()||!original_.action_region_members[i]))
        original_.operations[i].result_expression=expression;
    }
    RequireMeaning(next_function == evaluation_.functions.size(), Error::kOperand);
    RequireMeaning(next_module == evaluation_.modules.size(), Error::kOperand);
    // One indexed scatter retains successful region exits independently of
    // block numbering, storage order, or individual raw result Commit sites.
    if(!original_.action_regions.empty())for(std::size_t i=0;i<graph_.phases.size();++i) {
      budget_.Step();const auto& phase=graph_.phases[i];
      if(!phase.operation||phase.phase!=analysis::ExceptionPhase::kCommit)continue;
      const auto owner=original_.action_region_members[phase.operation->value];
      if(owner&&original_.action_regions[owner->value].exit==*phase.operation)
        original_.action_regions[owner->value].commit=analysis::WorkBlockId{static_cast<std::uint32_t>(i)};
    }
    CopyInputs();
  }

 private:
  void CheckSuperStore(const MemberTarget& member, ExprId rhs,
      analysis::ValueOperand receiver, std::span<const std::uint8_t> safety) {
    RequireMeaning(tree_.strictness==Strictness::kStrict &&
        tree_.home_object==HomeObjectBinding::kClass, Error::kSuperHomeObject);
    RequireMeaning(IsReceiverOperand(graph_,ResolveEvaluationOperand(evaluation_,receiver)) &&
        member.base.value<tree_.expressions.size() &&
        std::holds_alternative<ThisExpr>(tree_.expressions[member.base.value].expression) &&
        std::get<ThisExpr>(tree_.expressions[member.base.value].expression).owner==tree_.owner,
        Error::kSuperReceiver);
    RequireMeaning(rhs.value<safety.size() && safety[rhs.value],Error::kSuperStoreOrder);
  }

  // This store's cell when it is that cell's deferred TDZ initialization.
  std::optional<CellId> DeferredCell() const {
    if (!evaluation_.cells || current_.value >= evaluation_.cells->cells.operations.size()) return {};
    const auto cell = evaluation_.cells->cells.operations[current_.value];
    const auto& deferred = evaluation_.cells->deferred;
    if (!cell || cell->value >= deferred.size() || deferred[cell->value] != current_) return {};
    return cell;
  }
  SymbolId CellSymbol(ExprId expression,CellAccessKind kind) {
    RequireMeaning(evaluation_.cells.has_value(), Error::kOperand);
    const auto cell = evaluation_.cells->cells.operations[current_.value];
    RequireMeaning(cell && cell->value < flow_.cells.size() && flow_.cells[cell->value], Error::kOperand);
    RequireMeaning(original_.cells&&cell->value<original_.cells->bindings.size()&&
        original_.cells->bindings[cell->value].binding==*flow_.cells[cell->value]&&
        !original_.cells->references[current_.value],Error::kOperand);
    original_.cells->references[current_.value]=OriginalCellReference{*cell,kind,expression};
    return *flow_.cells[cell->value];
  }
  void CopyInputs() {
    if (!evaluation_.phis) return;
    RequireMeaning(flow_.copies.size() == evaluation_.phis->bundles.size(), Error::kOperand);
    edge_input_ = true;
    const auto& emission = evaluation_.exception_emission;
    RequireMeaning(flow_.shared_stages.size() == emission.assignments.size(), Error::kOperand);
    budget_.Array(original_.exception_stages, emission.coverage.size());
    std::size_t covered = 0;
    for (const auto& cover : emission.coverage) {
      budget_.Step();
      const auto root = flow_.shared_stages[cover.assignment];
      origin_ = tree_.expressions[root.value].origin;
      Input(evaluation_.phis->copies[cover.copy].source, Node<AssignmentExpr>(root).value);
      const auto& group = emission.groups[cover.group];
      RequireMeaning(flow_.expressions[group.root.value].has_value(), Error::kOperand);
      original_.exception_stages[covered++] = {root, *flow_.expressions[group.root.value],
          group.entry, cover.operation, cover.edge, group.handler};
    }
    for (std::size_t i = 0; i < flow_.copies.size(); ++i) {
      budget_.Step();
      const auto& copies = flow_.copies[i];
      if (evaluation_.exceptions &&
          std::holds_alternative<ThrowCopies>(evaluation_.exceptions->placements[i])) {
        const auto& bundle = evaluation_.phis->bundles[i];
        if (!emission.bundles.empty() && emission.bundles[i]) {
          RequireMeaning(copies.stage_group == emission.bundles[i] && copies.expressions.empty() && copies.temporaries.empty(), Error::kOperand);
          continue;
        }
        RequireMeaning(!copies.stage_group, Error::kOperand);
        bool ordinary=false;
        for(std::uint32_t j=0;j<bundle.count;++j) {
          budget_.Step();ordinary|=evaluation_.exceptions->projection!=ExceptionCopyProjection::kForOfClose || evaluation_.phis->projection[bundle.first+j]!=PhiCopyUse::kForOfProtocol;
        }
        RequireMeaning(copies.expressions.size() == (ordinary?bundle.count:0) && copies.temporaries.empty(), Error::kOperand);
        for (std::uint32_t j = 0; j < bundle.count; ++j) {
          if(evaluation_.exceptions->projection==ExceptionCopyProjection::kForOfClose && evaluation_.phis->projection[bundle.first+j]==PhiCopyUse::kForOfProtocol)continue;
          budget_.Step();
          const auto root = copies.expressions[j];
          origin_ = tree_.expressions[root.value].origin;
          // Snapshot the pre-throw operand, not a normal result or a cycle
          // temporary from the abstract exceptional parallel-copy schedule.
          Input(evaluation_.phis->copies[bundle.first + j].source, Node<AssignmentExpr>(root).value);
        }
        continue;
      }
      const auto& schedule = evaluation_.phis->bundles[i].schedule;
      if (copies.selected) {
        RequireMeaning(copies.expressions.empty() && copies.temporaries.empty() && schedule.steps.size() == 1, Error::kOperand);
        origin_ = tree_.expressions[copies.selected->value].origin;
        const auto& operand = std::get<analysis::ValueOperand>(schedule.steps[0].source);
        std::optional<analysis::UseId> forwarded;
        if (const auto* value = std::get_if<analysis::ValueId>(&operand);
            value && !evaluation_.logical_values.empty() && evaluation_.logical_values[value->value]) {
          forwarded = evaluation_.operations[evaluation_.logical_values[value->value]->value].consumer;
          RequireMeaning(forwarded && forwarded->value < graph_.uses.size(), Error::kOperand);
          const auto& use = graph_.uses[forwarded->value];
          const auto* destination = std::get_if<analysis::ValueId>(&schedule.steps[0].destination);
          const auto* edge = use.incoming ? std::get_if<analysis::EdgeId>(&*use.incoming) : nullptr;
          const auto* incoming = std::get_if<analysis::EdgeId>(&evaluation_.phis->bundles[i].incoming);
          const auto* predicate = std::get_if<analysis::OpId>(&use.owner);
          if (predicate && !flow_.logical_operations.empty() && flow_.logical_operations[predicate->value]) {
            // A fused selected LHS is evaluated once by native &&/||. Its
            // predicate and keep-edge occurrence share that same left subtree.
            RequireMeaning(flow_.expressions[predicate->value].has_value() &&
                Node<LogicalExpr>(*flow_.expressions[predicate->value]).left == *copies.selected, Error::kOperand);
          } else RequireMeaning(destination && use.owner == decltype(use.owner){*destination} &&
              edge && incoming && *edge == *incoming && use.role == analysis::OperandRole::kPhiInput, Error::kOperand);
        }
        // A shared native LHS may save its value at the predicate occurrence.
        // The keep edge retains that evaluated value, never executes its save
        // again. Bind the copy to the same freshly checked logical-left node.
        bool retained_left = false;
        if (const auto* value = std::get_if<analysis::ValueId>(&operand)) {
          const auto* source = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
          if (source) {
            const auto& placement = evaluation_.operations[source->op.value];
            if (placement.kind == EvaluationKind::kPredicateSnapshot && placement.consumer) {
              const auto* owner = std::get_if<analysis::OpId>(&graph_.uses[placement.consumer->value].owner);
              if (owner && !flow_.logical_operations.empty() && flow_.logical_operations[owner->value] &&
                  flow_.expressions[owner->value] &&
                  ((!flow_.selection_truths.empty() && flow_.selection_truths[owner->value] &&
                    flow_.selection_truths[owner->value]->input == *copies.selected) ||
                   Node<LogicalExpr>(*flow_.expressions[owner->value]).left == *copies.selected)) {
                const auto previous = edge_input_; edge_input_ = false;
                Input(operand, *copies.selected, placement.consumer);
                edge_input_ = previous; retained_left = true;
              }
            }
          }
        }
        if (!retained_left) Input(operand, *copies.selected, forwarded);
        continue;
      }
      RequireMeaning(copies.expressions.size() == schedule.steps.size(), Error::kOperand);
      for (std::size_t j = 0; j < copies.expressions.size(); ++j) {
        budget_.Step();
        const auto root = copies.expressions[j];
        const auto& assignment = Node<AssignmentExpr>(root);
        origin_ = tree_.expressions[root.value].origin;
        const auto& source = schedule.steps[j].source;
        if (const auto* temporary = std::get_if<CopyTempId>(&source)) {
          RequireMeaning(temporary->value < copies.temporaries.size() &&
              copies.temporaries[temporary->value].has_value(), Error::kOperand);
          LeafOrigin(assignment.value);
          RequireMeaning(Node<SymbolExpr>(assignment.value).symbol ==
              *copies.temporaries[temporary->value], Error::kOperand);
        } else Input(std::get<analysis::ValueOperand>(source), assignment.value);
      }
    }
  }
  void Arguments(const analysis::ValueOp& op, ArgumentRange arguments,
      std::uint32_t prefix, std::uint32_t count) {
    RequireMeaning(arguments.count == count && arguments.first <= tree_.arguments.size() &&
        count <= tree_.arguments.size() - arguments.first, Error::kOperand);
    for (std::uint32_t i = 0; i < count; ++i)
      Input(graph_.uses[op.first_use.value + prefix + i].operand, tree_.arguments[arguments.first + i], analysis::UseId{op.first_use.value + prefix + i});
  }
  void Receiver(const analysis::ValueOp& op, const CallExpr& call,
      const analysis::ValueOperand& callee, const analysis::ValueOperand& receiver) {
    // Derive the actual reference from executable syntax, independently of the
    // builder's CallPlan. Its base must represent the suppressed receiver use.
    const auto resolved_callee = ResolveEvaluationOperand(evaluation_, callee);
    const auto resolved_receiver = ResolveEvaluationOperand(evaluation_, receiver);
    const auto* value = std::get_if<analysis::ValueId>(&resolved_callee);
    RequireMeaning(value != nullptr, Error::kOperand);
    const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value->value].origin);
    RequireMeaning(result != nullptr, Error::kOperand);
    const auto& property = graph_.ops[result->op.value];
    const auto& placement = evaluation_.operations[result->op.value];
    RequireMeaning((std::holds_alternative<analysis::ValueNamedRead>(property.operation) ||
        std::holds_alternative<analysis::ValuePropertyRead>(property.operation)) &&
        property.life == analysis::ValueLife::kLive &&
        (placement.kind == EvaluationKind::kInline || placement.kind == EvaluationKind::kNestedLiteral) &&
        placement.consumer == op.first_use, Error::kOperand);
    const auto& member = Node<MemberExpr>(call.callee);
    if (const auto* id = std::get_if<analysis::ValueId>(&resolved_receiver)) {
      const auto& definition = graph_.values[id->value];
      if (const auto* producer = std::get_if<analysis::OpResult>(&definition.origin)) {
        const auto& base = evaluation_.operations[producer->op.value];
        RequireMeaning((evaluation_.for_of && evaluation_.for_of->bindings[id->value]) ||
            (evaluation_.for_in && id->value < evaluation_.for_in->values.size() && evaluation_.for_in->values[id->value] &&
             evaluation_.for_in->values[id->value]->role == ForInRole::kNextResult) ||
            ((base.kind == EvaluationKind::kRoot || base.kind == EvaluationKind::kPredicateSnapshot) &&
                (flow_.snapshots[producer->op.value].has_value() || flow_.receivers[producer->op.value] != ReceiverValue::kNone)) ||
            ((base.kind == EvaluationKind::kInline || base.kind == EvaluationKind::kNestedLiteral) && base.consumer == property.first_use), Error::kOperand);
      } else if (std::holds_alternative<analysis::PhiDefinition>(definition.origin))
        // TreeFlow derives this symbol from the actual, exclusively owned edge
        // assignments. Same-block/no-copy corridor checks exclude a write to
        // the provisional phi location between reference creation and use.
        RequireMeaning(id->value < flow_.phis.size() && flow_.phis[id->value], Error::kOperand);
      else RequireMeaning(std::holds_alternative<analysis::EntryDefinition>(definition.origin), Error::kOperand);
    }
    const auto saved_origin = origin_;
    const auto saved_current = current_;
    origin_ = {property.origin.value};
    current_ = result->op;
    // Fresh evaluation owns availability/order. The native Reference evaluates
    // its base once at the property occurrence; its suppressed receiver is that
    // same identity, including a retained assignment at this exact occurrence.
    Input(receiver, member.base, property.first_use);
    // A member Reference retains this single evaluated base as its receiver.
    // Preserve the suppressed raw receiver occurrence too; it is not a second
    // source read and must not borrow the invocation's later Before point.
    RecordOriginalInput(original_, receiver, resolved_receiver, member.base,
                        analysis::UseId{op.first_use.value + 1}, budget_);
    origin_ = saved_origin;
    current_ = saved_current;
  }
  template <class T> const T& Node(ExprId id) const {
    RequireMeaning(id.value < tree_.expressions.size(), Error::kOperand);
    const auto* value = std::get_if<T>(&tree_.expressions[id.value].expression);
    RequireMeaning(value != nullptr, Error::kOperator);
    return *value;
  }
  template <class T> const T& Literal(ExprId id) const {
    const auto* value = std::get_if<T>(&Node<TreeLiteral>(id));
    RequireMeaning(value != nullptr, Error::kOperand);
    return *value;
  }
  void LeafOrigin(ExprId id) const {
    const auto origin = tree_.expressions[id.value].origin;
    const auto predicate = !flow_.branch_predicates.empty() ? flow_.branch_predicates[current_.value] : std::nullopt;
    // Identity lowering combines provenance on its checked execution root,
    // including a scalar predicate retained inside a conditional expression.
    // The origin pass independently proves every retained operation's membership.
    RequireMeaning(origin == origin_ ||
        (!edge_input_ && (flow_.expressions[current_.value] == id || predicate == id) &&
         std::holds_alternative<CombinedOrigin>(tree_.origins[origin.value])), Error::kOrigin);
  }
  void InputAt(std::uint32_t ordinal, ExprId expression) {
    // Carry the actual occurrence through scalar/argument lowering. A value
    // alone cannot distinguish the saving use from later private-slot reads.
    const analysis::UseId use{graph_.ops[current_.value].first_use.value + ordinal};
    Input(graph_.uses[use.value].operand, expression, use);
  }
  void Input(const analysis::ValueOperand& operand, ExprId expression, std::optional<analysis::UseId> occurrence = {}) {
    budget_.Step();
    if (occurrence) if (const auto assignment = AssignmentProducer(evaluation_, *occurrence)) {
      RequireMeaning(!edge_input_ && flow_.expressions[assignment->value] == expression, Error::kOperand);
      // The store's own operand check independently verifies this expression's
      // reference and RHS; fresh evaluation proof ties RHS to this SSA use.
      return;
    }
    const auto resolved=ResolveEvaluationOperand(evaluation_,operand);
    if (const auto* value = std::get_if<analysis::ValueId>(&resolved);
        value && !original_.enumeration_values.empty() && original_.enumeration_values[value->value]) {
      const auto member = *original_.enumeration_values[value->value];
      RequireMeaning(member.role == ForInRole::kNextResult && member.loop < original_.enumerations.size(), Error::kOperand);
      const auto& native = original_.enumerations[member.loop];
      RequireMeaning(Node<SymbolExpr>(expression).symbol == native.key, Error::kOperand);
      LeafOrigin(expression);
      // Raw next-result is not a language expression. This source read denotes
      // only the edge-qualified successful key definition.
      if (occurrence) {
        if (original_.enumeration_inputs.size() == original_.enumeration_inputs.capacity()) {
          const auto old = original_.enumeration_inputs.capacity();
          const auto next = old ? old * 2 : 1;
          RequireMeaning(next > old, Error::kResourceLimit);
          budget_.Admit(next, sizeof(OriginalEnumerationInput));
          original_.enumeration_inputs.reserve(next);
          budget_.Admit(original_.enumeration_inputs.capacity()-next, sizeof(OriginalEnumerationInput));
        }
        original_.enumeration_inputs.push_back({member.loop,*occurrence,expression});
      }
      return;
    }
    if(const auto* value=std::get_if<analysis::ValueId>(&resolved);value && original_.iterations && (original_.iterations->values[value->value] || original_.iterations->binding_values[value->value])) {
      auto& native=*original_.iterations;const auto member=native.binding_values[value->value]
          ? ForOfMember{*native.binding_values[value->value],ForOfRole::kValue} : *native.values[value->value];
      RequireMeaning(member.role==ForOfRole::kValue && member.protocol.value<native.statements.size(),Error::kOperand);
      const auto& syntax=std::get<ForOfStmt>(tree_.statements[native.statements[member.protocol.value].value].statement);
      RequireMeaning(Node<SymbolExpr>(expression).symbol==syntax.target,Error::kOperand);LeafOrigin(expression);
      if(occurrence) {
        if(native.inputs.size()==native.inputs.capacity()) {
          const auto old=native.inputs.capacity(),next=old?old*2:1;RequireMeaning(next>old,Error::kResourceLimit);
          budget_.Admit(next,sizeof(OriginalIterationInput));native.inputs.reserve(next);budget_.Admit(native.inputs.capacity()-next,sizeof(OriginalIterationInput));
        }
        native.inputs.push_back({member.protocol,*occurrence,expression});
      }
      return;
    }
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::ValueId>) {
        const auto& definition = graph_.values[value.value];
        if (const auto* entry = std::get_if<analysis::EntryDefinition>(&definition.origin)) {
          LeafOrigin(expression);
          if (const auto* parameter = std::get_if<analysis::ParameterEntry>(entry)) {
            RequireMeaning(parameter->index < tree_.parameters.size() &&
                Node<SymbolExpr>(expression).symbol == tree_.parameters[parameter->index].symbol, Error::kOperand);
          } else if (std::holds_alternative<analysis::ReceiverEntry>(*entry)) {
            if (tree_.callable == CallableKind::kDerivedConstructor) static_cast<void>(Literal<UndefinedLiteral>(expression));
            else RequireMeaning(Node<ThisExpr>(expression).owner == tree_.owner, Error::kOperand);
          } else if (std::holds_alternative<analysis::NewTargetEntry>(*entry)) {
            RequireMeaning(NativeNewTarget(tree_.identity.contract), Error::kNewTargetPremise);
            RequireMeaning(!IsArrowCallable(tree_.callable) && Node<NewTargetExpr>(expression).owner == tree_.owner &&
                Node<NewTargetExpr>(expression).form == NewTargetForm::kAbiEntry, Error::kNewTargetOwner);
          } else throw Error::kOperand;
          return;
        }
        if (std::holds_alternative<analysis::PhiDefinition>(definition.origin)) {
          if (!evaluation_.logical_values.empty() && evaluation_.logical_values[value.value]) {
            const auto branch = *evaluation_.logical_values[value.value];
            RequireMeaning((!edge_input_ || (occurrence && evaluation_.operations[branch.value].consumer == occurrence)) &&
                flow_.expressions[branch.value] == expression, Error::kOperand);
            return;
          }
          LeafOrigin(expression);
          RequireMeaning(value.value < flow_.phis.size() && flow_.phis[value.value] &&
              Node<SymbolExpr>(expression).symbol == *flow_.phis[value.value], Error::kOperand);
          return;
        }
        if (std::holds_alternative<analysis::CatchDefinition>(definition.origin)) {
          LeafOrigin(expression);
          RequireMeaning(value.value < flow_.catch_parameters.size() && flow_.catch_parameters[value.value] &&
              Node<SymbolExpr>(expression).symbol == *flow_.catch_parameters[value.value], Error::kOperand);
          return;
        }
        const auto* result = std::get_if<analysis::OpResult>(&definition.origin);
        RequireMeaning(result != nullptr, Error::kOperand);
        const auto& placement = evaluation_.operations[result->op.value];
        const auto receiver = flow_.receivers[result->op.value];
        if (receiver != ReceiverValue::kNone) {
          const auto shape = DescribeEvaluation(graph_.ops[result->op.value].operation);
          RequireMeaning(shape && shape->receiver == receiver && definition.domain.IsLanguage() &&
              (receiver != ReceiverValue::kInitialized || tree_.callable == CallableKind::kDerivedConstructor),
              Error::kOperand);
          if (placement.kind == EvaluationKind::kPredicateSnapshot && placement.consumer == occurrence) {
            RequireMeaning(!edge_input_ && flow_.expressions[result->op.value] == expression, Error::kOperand);
          } else {
            LeafOrigin(expression);
            RequireMeaning(Node<ThisExpr>(expression).owner == tree_.owner, Error::kOperand);
          }
          return;
        }
        if (placement.kind == EvaluationKind::kPredicateSnapshot && placement.consumer &&
            occurrence == placement.consumer) {
          // The eager consumer receives the owned expression at the exact
          // occurrence. A stable invocation binding needs no extra assignment;
          // all other uses still read that same independently checked identity.
          RequireMeaning(!edge_input_ && flow_.snapshots[result->op.value], Error::kOperand);
          if (flow_.expressions[result->op.value] == expression) {
            const auto symbol = Node<SymbolExpr>(expression).symbol;
            const auto kind = tree_.symbols[symbol.value].kind;
            RequireMeaning(symbol == *flow_.snapshots[result->op.value] &&
                (kind == BindingKind::kParameter || kind == BindingKind::kFunctionSelf), Error::kOperand);
            return;
          }
          const auto& assignment = Node<AssignmentExpr>(expression);
          const auto* target = std::get_if<SymbolTarget>(&assignment.target);
          RequireMeaning(target && target->symbol == *flow_.snapshots[result->op.value] &&
              assignment.value == flow_.expressions[result->op.value], Error::kOperand);
          return;
        }
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kNestedLiteral) {
          RequireMeaning(flow_.expressions[result->op.value] == expression, Error::kOperand);
          return;
        }
        if (evaluation_.operations[result->op.value].kind == EvaluationKind::kLogicalLeft) {
          // Fresh flow selected the predicate/keep edge as one native LHS.
          // Both original observations must name that exact owned expression.
          RequireMeaning(flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else if (evaluation_.operations[result->op.value].kind == EvaluationKind::kPhiInput) {
          RequireMeaning(edge_input_ && flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else if (evaluation_.operations[result->op.value].kind == EvaluationKind::kInline) {
          RequireMeaning(!edge_input_ && flow_.expressions[result->op.value] == expression, Error::kOperand);
        } else {
          LeafOrigin(expression);
          RequireMeaning(flow_.snapshots[result->op.value].has_value() &&
              Node<SymbolExpr>(expression).symbol == *flow_.snapshots[result->op.value], Error::kOperand);
        }
      } else if constexpr (std::is_same_v<T, analysis::IntegerLiteral>) {
        LeafOrigin(expression);
        RequireMeaning(Literal<IntegerLiteral>(expression).value == value.value, Error::kOperand);
      } else if constexpr (std::is_same_v<T, analysis::Float64Bits>) {
        LeafOrigin(expression);
        RequireMeaning(Literal<NumberLiteral>(expression).bits == value.value, Error::kOperand);
      } else {
        static_assert(std::is_same_v<T, analysis::SpecialValue>);
        LeafOrigin(expression);
        using S = analysis::SpecialValue;
        switch (value) {
          case S::kUndefined: static_cast<void>(Literal<UndefinedLiteral>(expression)); break;
          case S::kNull: static_cast<void>(Literal<NullLiteral>(expression)); break;
          case S::kTrue: RequireMeaning(Literal<BooleanLiteral>(expression).value, Error::kOperand); break;
          case S::kFalse: RequireMeaning(!Literal<BooleanLiteral>(expression).value, Error::kOperand); break;
          case S::kNaN:
            RequireMeaning(Literal<NumberLiteral>(expression).bits ==
                std::bit_cast<std::uint64_t>(std::numeric_limits<double>::quiet_NaN()), Error::kOperand); break;
          case S::kInfinity:
            RequireMeaning(Literal<NumberLiteral>(expression).bits ==
                std::bit_cast<std::uint64_t>(std::numeric_limits<double>::infinity()), Error::kOperand); break;
        }
      }
    }, resolved);
    RecordOriginalInput(original_,operand,resolved,expression,occurrence,budget_);
  }
  void BufferedLiteral(const TreeLiteral& expected, ExprId expression) {
    LeafOrigin(expression);
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      const auto& actual = Literal<T>(expression);
      if constexpr (std::is_same_v<T, NullLiteral>) return;
      else if constexpr (std::is_same_v<T, UndefinedLiteral>) throw Error::kConstant;
      else if constexpr (std::is_same_v<T, NumberLiteral>) RequireMeaning(actual.bits == value.bits, Error::kConstant);
      else if constexpr (std::is_same_v<T, TextId>)
        RequireMeaning(value.value < constants_.text_count && actual == value, Error::kConstant);
      else RequireMeaning(actual.value == value.value, Error::kConstant);
    }, expected);
  }
  void Named(const MemberKey& key, const ExpressionConstant& constant) const {
    const auto* name = std::get_if<NameId>(&key);
    const auto* expected = std::get_if<NameId>(&constant);
    RequireMeaning(name && expected && expected->value < constants_.name_count && *name == *expected, Error::kConstant);
  }
  const BinaryExpr& Compare(ExprId expression, analysis::BinaryOperator operation) const {
    const auto& binary = Node<BinaryExpr>(expression);
    RequireMeaning(binary.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return binary;
  }
  const LogicalExpr& Logical(ExprId expression, TreeLogical operation) const {
    const auto& logical = Node<LogicalExpr>(expression);
    RequireMeaning(logical.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return logical;
  }
  ExprId UnaryInput(ExprId expression, TreeUnary operation) const {
    const auto& unary = Node<UnaryExpr>(expression);
    RequireMeaning(unary.operation == operation, Error::kOperator);
    LeafOrigin(expression);
    return unary.operand;
  }
  void IntegerInput(const analysis::ValueOperand& operand, ExprId expression, std::optional<analysis::UseId> occurrence = {}) {
    if (!ErasableNaN(operand)) { Input(operand, expression, occurrence); return; }
    LeafOrigin(expression);
    RequireMeaning(Literal<NumberLiteral>(expression).bits == CanonicalNaNBits(), Error::kOperand);
    RecordOriginalInput(original_,operand,ResolveEvaluationOperand(evaluation_,operand),expression,occurrence,budget_);
  }
  std::optional<ExprId> Unary(analysis::UnaryOperator operation, const analysis::ValueOperand& operand, ExprId expression) {
    const auto occurrence = graph_.ops[current_.value].first_use;
    using U = analysis::UnaryOperator;
    switch (operation) {
      case U::kTemplateObject: throw Error::kUnsupportedOperation;
      case U::kLegacyNegate:
        RequireMeaning(LegacyNegationEvaluation(graph_, operand, kSourceArithmetic), Error::kOperator);
        Input(operand, UnaryInput(expression, TreeUnary::kNegate), occurrence); return expression;
      case U::kNegate: Input(operand, UnaryInput(expression, TreeUnary::kNegate), occurrence); return expression;
      case U::kBitNot: IntegerInput(operand, UnaryInput(expression, TreeUnary::kBitNot), occurrence); return expression;
      case U::kLogicalNot: Input(operand, UnaryInput(expression, TreeUnary::kNot), occurrence); return expression;
      case U::kTypeOf: Input(operand, UnaryInput(expression, TreeUnary::kTypeof), occurrence); return expression;
      case U::kToNumber: Input(operand, UnaryInput(expression, TreeUnary::kPlus), occurrence); return expression;
      case U::kToBoolean:
        if (BooleanEvaluation(graph_, operand)) { Input(operand, expression, occurrence); return {}; }
        Input(operand, UnaryInput(UnaryInput(expression, TreeUnary::kNot), TreeUnary::kNot), occurrence); return expression;
      case U::kFalseOrZero:
      case U::kTrueOrNonzeroNumber:
        if (BooleanEvaluation(graph_, operand)) {
          Input(operand, operation == U::kFalseOrZero ? UnaryInput(expression, TreeUnary::kNot) : expression, occurrence);
          return {};
        }
        Predicate(operation, operand, expression); return {};
      case U::kToNumeric:
        if (NumberEvaluation(graph_, operand)) {
          Input(operand, expression, occurrence);
          original_.operations[current_.value].number_identity=OriginalNumberIdentity{occurrence,expression};
          return {};
        }

        [[fallthrough]];
      case U::kIncrement: case U::kDecrement: {
        const auto& sequence = Node<SequenceExpr>(expression); LeafOrigin(expression);
        const auto& save = Node<AssignmentExpr>(sequence.left); LeafOrigin(sequence.left);
        const auto* target = std::get_if<SymbolTarget>(&save.target);
        const auto& update = Node<UpdateExpr>(sequence.right); LeafOrigin(sequence.right);
        RequireMeaning(target && target->symbol == update.symbol &&
            update.result == (operation == U::kToNumeric ? UpdateResult::kPostfix : UpdateResult::kPrefix) &&
            update.direction == (operation == U::kDecrement ? UpdateDirection::kDecrement : UpdateDirection::kIncrement), Error::kOperator);
        Input(operand, save.value, occurrence);
        // The verified save carries the captured raw input. Conversion/update
        // effects belong to the native action, not this save or its GetValue.
        Save(sequence.left,target->symbol,save.value,operand);
        return sequence.right;
      }
    }
    throw Error::kUnsupportedOperation;
  }
  template<class Site>
  void Store(Site site,SymbolId binding,ExprId input,const analysis::ValueOperand& operand) {
    Save(site,binding,input,operand);
    auto& operation=original_.operations[current_.value];
    RequireMeaning(!operation.write,Error::kOperand);operation.write=OriginalWriteSite{site};
  }
  template<class Site>
  void Save(Site site,SymbolId binding,ExprId input,const analysis::ValueOperand& operand) {
    // Operand verification owns this value-to-binding correspondence once.
    // Storage lifetime and success-only publication remain native flow facts.
    auto& definitions=[&]() -> auto& {
      if constexpr(std::is_same_v<Site,ExprId>)return original_.definitions;
      else {static_assert(std::is_same_v<Site,StmtId>);return original_.declarations;}
    }();
    budget_.Step();RequireMeaning(site.value<definitions.size()&&!definitions[site.value],Error::kOperand);
    definitions[site.value]=OriginalSourceDefinition{OriginalSavedDefinition{operand,current_},binding,input};
  }
  void Predicate(analysis::UnaryOperator operation, const analysis::ValueOperand& operand, ExprId expression) {
    using B = analysis::BinaryOperator;
    const bool positive = operation == analysis::UnaryOperator::kTrueOrNonzeroNumber;
    const auto& disjunction = Logical(expression, TreeLogical::kOr);
    const auto& boolean = Compare(disjunction.left, B::kStrictEqual);
    Input(operand, boolean.left);
    LeafOrigin(boolean.right);
    RequireMeaning(Literal<BooleanLiteral>(boolean.right).value == positive, Error::kOperand);
    if (!positive) {
      const auto& zero = Compare(disjunction.right, B::kStrictEqual);
      Input(operand, zero.left);
      LeafOrigin(zero.right);
      RequireMeaning(Literal<IntegerLiteral>(zero.right).value == 0, Error::kOperand);
      return;
    }
    const auto& conjunction = Logical(disjunction.right, TreeLogical::kAnd);
    const auto& type = Compare(conjunction.left, B::kStrictEqual);
    Input(operand, UnaryInput(type.left, TreeUnary::kTypeof));
    LeafOrigin(type.right);
    RequireMeaning(constants_.number_type_text && Literal<TextId>(type.right) == *constants_.number_type_text,
                   Error::kConstant);
    const auto& nonzero = Compare(conjunction.right, B::kStrictNotEqual);
    Input(operand, nonzero.left);
    LeafOrigin(nonzero.right);
    RequireMeaning(Literal<IntegerLiteral>(nonzero.right).value == 0, Error::kOperand);
  }

  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  const FunctionTree& tree_;
  const TreeFlow& flow_;
  ExpressionConstants constants_;
  TreeCheckBudget& budget_;
  OriginalFlow& original_;
  analysis::OpId current_;
  TreeOriginId origin_;
  bool edge_input_ = false;
};
}  // namespace

void CheckTreeOperands(const analysis::ValueProgram& graph,
    const EvaluationPlan& evaluation, const FunctionTree& tree,
    const TreeFlow& flow, ExpressionConstants constants, TreeCheckBudget& budget, OriginalFlow& original, const ForOfEvaluationInput* iteration) {
  VerifyPhasedDefaultInput(graph, tree, budget);
  if (iteration && iteration->control.continuations) {
    auto checked = VerifyIteratorContinuations(iteration->body, graph, iteration->input,
        iteration->regions, iteration->ownership, *iteration->control.continuations, budget.Remaining());
    if (const auto* f = std::get_if<ForOfPlacementFailure>(&checked))
      throw f->kind == ForOfPlacementError::kResourceLimit ? TreeStructureError::kResourceLimit :
          f->kind == ForOfPlacementError::kWorkLimit ? TreeStructureError::kWorkLimit : TreeStructureError::kOwnership;
    budget.Step(std::get<std::uint64_t>(checked));
  }
  budget.Admit(1, sizeof(OperandCheck));
  OperandCheck(graph, evaluation, tree, flow, constants, budget, original).Run();
}
}  // namespace abcd::source
