#include "assignment_values.h"
#include "for_in_emission.h"
#include "integer_nan.h"
#include "staged_literal_names.h"
#include "expression_forest.h"
#include "recovery_scratch.h"
#include "for_of_emission.h"
#include "expression_copies.h"
#include "class_tree.h"
#include "logical_evaluation.h"
#include "literal_field_forest.h"

#include <array>
#include <algorithm>
#include <bit>
#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace abcd::source {
namespace {
using Error = ExpressionError;
// Largest fixed non-call shape: base, computed key, stored value. Calls keep
// their separately budgeted variable-arity argument storage.
constexpr std::size_t kFixedExpressionOperands = 3;
void Require(bool condition, Error error) { if (!condition) throw error; }
YieldForm TreeYieldForm(analysis::YieldForm form) {
  switch (form) {
    case analysis::YieldForm::kDirect: return YieldForm::kDirect;
    case analysis::YieldForm::kDelegate: return YieldForm::kDelegate;
  }
  throw Error::kUnsupportedOperation;
}

class ForestBuilder {
 public:
  ForestBuilder(const analysis::ValueProgram& graph,
                const EvaluationPlan& evaluation, FunctionOwnerId owner,
                ExpressionConstants constants, analysis::SemanticBudget budget,
                BindingPlan bindings, std::span<const ModuleSymbol> modules, RecoveryMode recovery,
                std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity,
                CallableKind callable, analysis::ArithmeticSemantics arithmetic, ConstructorSemantics constructors, const ForInEmissionView* enumeration, const ForOfEmissionView* iteration_view, const ForOfEvaluationInput* iteration_input)
      : iteration_input_(iteration_input), iteration_view_(iteration_view), enumeration_(enumeration), graph_(graph), evaluation_(evaluation), owner_(owner), constants_(constants),
        budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules), recovery_(recovery), names_(names), logicals_(logicals), polarity_(polarity), callable_(callable), arithmetic_(arithmetic), constructors_(constructors) {
    Require(polarity.empty() || polarity.size() == graph.ops.size(), Error::kOwnership);
    Require(bindings.allocated_bytes <= bytes_, Error::kResourceLimit);
    bytes_ -= bindings.allocated_bytes;
    Step(bindings.work);
    forest_.bindings = std::move(bindings);
  }

  ExpressionForest Build() {
    std::vector<std::uint8_t> predicates;
    std::vector<std::uint8_t> selected_edges;
    std::vector<std::optional<std::size_t>> logical_joins;
    if (!logicals_.empty()) {
      Reserve(selection_indices_, graph_.ops.size()); selection_indices_.resize(graph_.ops.size());
      Reserve(selection_tasks_, logicals_.size());
      Reserve(selection_built_, logicals_.size()); selection_built_.resize(logicals_.size());
      Reserve(logical_joins, graph_.ops.size()); logical_joins.resize(graph_.ops.size());
      Reserve(predicates, graph_.ops.size()); predicates.resize(graph_.ops.size());
      Reserve(selected_edges, graph_.edges.size()); selected_edges.resize(graph_.edges.size());
      for (const auto& region : logicals_) {
        Step();
        Require(region.boolean.value < graph_.ops.size() && region.branch.value < graph_.ops.size() &&
            region.boolean != region.branch && region.left.value < graph_.values.size() &&
            region.merged.value < graph_.values.size() &&
            (region.operation == TreeLogical::kAnd || region.operation == TreeLogical::kOr), Error::kOwnership);
        const auto* right = std::get_if<analysis::ValueId>(&region.right);
        const auto* child = std::get_if<SelectionChild>(&region.right);
        const auto* snapshot = std::get_if<SelectionSnapshot>(&region.right);
        Require(((right && right->value < graph_.values.size() &&
            std::holds_alternative<analysis::OpResult>(graph_.values[right->value].origin)) ||
            (child && child->branch.value < graph_.ops.size()) ||
            (snapshot && region.consequent && snapshot->value.value < graph_.values.size())) &&
            !predicates[region.boolean.value] && !predicates[region.branch.value], Error::kOwnership);
        for (const auto incoming : {region.keep_input, region.evaluate_input}) {
          if (!incoming) continue;
          Require(incoming->value < selected_edges.size() && !selected_edges[incoming->value], Error::kOwnership);
          selected_edges[incoming->value] = 1;
        }
        if (evaluation_.operations[region.branch.value].kind == EvaluationKind::kLogicalValue ||
            LiteralFieldPrefix(evaluation_,region.branch)) {
          const auto join = graph_.blocks[region.join.value].first_op.value;
          Require(join < graph_.ops.size() && !logical_joins[join], Error::kOwnership);
          logical_joins[join] = static_cast<std::size_t>(&region - logicals_.data());
        }
        selection_indices_[region.branch.value] = static_cast<std::size_t>(&region - logicals_.data());
        if (!region.consequent) predicates[region.boolean.value] = 1;
        if (region.conditions.empty()) predicates[region.branch.value] = 1;
        if (region.logical) {
          for (const auto truth : region.logical->truths) { Step(); predicates[truth.value] = 1; }
          for (const auto& condition : region.conditions) { Step(); predicates[condition.branch.value] = 1; }
          if (region.logical->shared_right) {
            const auto defer = [&](const SelectionOperand& input) {
              if (const auto* value = std::get_if<analysis::ValueId>(&input)) {
                const auto producer = std::get<analysis::OpResult>(graph_.values[value->value].origin).op;
                predicates[producer.value] = 1;
              }
            };
            defer(region.right); if (region.consequent) defer(*region.consequent);
          }
        }
      }
    }
    Require(constants_.operations.size() == graph_.ops.size(), Error::kConstant);
    Require(evaluation_.receiver_guards.size() == constants_.receiver_guards.size() &&
        std::equal(evaluation_.receiver_guards.begin(), evaluation_.receiver_guards.end(), constants_.receiver_guards.begin()), Error::kOwnership);
    Admit(1, sizeof(ExpressionForest));
    auto calls = forest_.bindings.flow == EvaluationFlow::kNormal
        ? BuildCallPlan(graph_, evaluation_, {bytes_, work_},iteration_input_)
        : BuildPhasedCallPlan(graph_, evaluation_, {bytes_, work_},iteration_input_);
    auto* call_plan = std::get_if<CallPlan>(&calls);
    if (!call_plan) {
      const auto error = std::get<CallPlanFailure>(calls);
      if (error == CallPlanError::kResourceLimit) throw Error::kResourceLimit;
      if (error == CallPlanError::kWorkLimit) throw Error::kWorkLimit;
      throw ExpressionFailure{Error::kUnsupportedOperation, error};
    }
    Admit(call_plan->allocated_bytes, 1); Step(call_plan->work);
    forest_.calls = std::move(*call_plan);
    Require(forest_.bindings.symbols.size() <= std::numeric_limits<std::uint32_t>::max() &&
        modules_.size() <= std::numeric_limits<std::uint32_t>::max() - forest_.bindings.symbols.size(), Error::kResourceLimit);
    Reserve(module_order_, modules_.size());
    for (std::size_t i = 0; i < modules_.size(); ++i) { Step(); module_order_.push_back(i); }
    std::sort(module_order_.begin(), module_order_.end(), [&](auto a, auto b) { Step(); return modules_[a] < modules_[b]; });
    for (std::size_t i = 1; i < module_order_.size(); ++i) {
      Step(); Require(modules_[module_order_[i - 1]] != modules_[module_order_[i]], Error::kBinding);
    }
    // Largest admitted lowering is TrueOrNonzeroNumber: three leaf reads,
    // three comparisons/type tests and two logical nodes. Reserve a generous
    // fixed per-op bound; each input use additionally needs at most one leaf.
    constexpr std::size_t kNodesPerOperation = 12;
    constexpr std::size_t kOriginPartsPerOperation = 2;
    constexpr auto kMaxId = std::numeric_limits<std::uint32_t>::max();
    std::size_t buffered_properties = 0;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); const auto& op = graph_.ops[i];
      if (op.life == analysis::ValueLife::kLive &&
          std::holds_alternative<analysis::ValueIterResult>(op.operation)) {
        Require(buffered_properties <= kMaxId - IterResultConstant::kProperties, Error::kResourceLimit);
        buffered_properties += IterResultConstant::kProperties;
      }
      const auto* object = std::get_if<analysis::ValueObjectAllocation>(&op.operation);
      const auto* array = std::get_if<analysis::ValueArrayAllocation>(&op.operation);
      if (op.life != analysis::ValueLife::kLive || !((object && object->literal) || (array && array->literal))) continue;
      const auto* constant = std::get_if<AggregateConstant>(&constants_.operations[i]);
      Require(constant && constant->kind == (array ? AggregateKind::kArray : AggregateKind::kObject) &&
          constant->first <= constants_.properties.size() &&
          constant->count <= constants_.properties.size() - constant->first, Error::kConstant);
      Require(constant->count <= kMaxId - buffered_properties, Error::kResourceLimit);
      buffered_properties += constant->count;
    }
    std::size_t indexed_arguments = 0;
    std::vector<std::uint32_t> array_sizes;
    bool indexed = false;
    for (const auto& call : forest_.calls.operations) {
      Step(); const auto* construct = std::get_if<ConstructCall>(&call);
      indexed |= construct && construct->array &&
          evaluation_.operations[construct->array->value].kind == EvaluationKind::kRoot;
    }
    if (indexed) {
      Reserve(array_sizes, graph_.ops.size()); array_sizes.resize(graph_.ops.size()); Step(graph_.ops.size());
      for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
        Step(); const auto* array = std::get_if<analysis::ValueArrayAllocation>(&graph_.ops[i].operation);
        if (!array || !array->literal || graph_.ops[i].life != analysis::ValueLife::kLive) continue;
        const auto* constant = std::get_if<AggregateConstant>(&constants_.operations[i]);
        Require(constant && constant->kind == AggregateKind::kArray, Error::kConstant);
        array_sizes[i] = constant->count;
      }
      if (evaluation_.literals) for (const auto& object : evaluation_.literals->objects) {
        Step(); if (!std::holds_alternative<analysis::ValueArrayAllocation>(graph_.ops[object.allocation.value].operation)) continue;
        Require(object.count <= kMaxId - array_sizes[object.allocation.value], Error::kResourceLimit);
        array_sizes[object.allocation.value] += object.count;
      }
      for (const auto& call : forest_.calls.operations) {
        Step(); const auto* construct = std::get_if<ConstructCall>(&call);
        if (!construct || !construct->array || evaluation_.operations[construct->array->value].kind != EvaluationKind::kRoot) continue;
        Require(array_sizes[construct->array->value] <= kMaxId - indexed_arguments, Error::kResourceLimit);
        indexed_arguments += array_sizes[construct->array->value];
      }
    }
    std::size_t captures = 0;
    for (const auto& function : evaluation_.functions) {
      Step(); Require(function.captures.size() <= kMaxId - captures, Error::kResourceLimit);
      captures += function.captures.size();
    }
    Reserve(forest_.closure_captures, captures);
    std::size_t copies = 0, normal_copies = 0, staged_copies = 0, staged_bundles = 0;
    const auto bundles = evaluation_.phis ? evaluation_.phis->bundles.size() : 0;
    if (evaluation_.phis) {
      for (std::size_t b = 0; b < bundles; ++b) {
        Step();
        const auto& bundle = evaluation_.phis->bundles[b];
        const bool staged = evaluation_.exceptions &&
            std::holds_alternative<ThrowCopies>(evaluation_.exceptions->placements[b]);
        const auto count = staged ? bundle.count : bundle.schedule.steps.size();
        Require(count <= kMaxId - copies, Error::kResourceLimit);
        copies += count;
        if (staged) { staged_copies += count; ++staged_bundles; }
        else normal_copies += count;
      }
    }
    const auto catch_copies = evaluation_.exceptions ? evaluation_.exceptions->slots.size() : 0;
    const auto commits = evaluation_.exceptions ? evaluation_.exceptions->commits.size() : 0;
    Require(catch_copies <= kMaxId - copies, Error::kResourceLimit);
    copies += catch_copies;
    constexpr std::size_t kNodesPerCopy = 2;  // Owned RHS leaf and assignment.
    Require(graph_.ops.size() <= kMaxId / kNodesPerOperation &&
            graph_.uses.size() <= kMaxId - graph_.ops.size() * kNodesPerOperation &&
            graph_.origins.size() <= kMaxId - graph_.ops.size(), Error::kResourceLimit);
    const auto operation_nodes = graph_.uses.size() + graph_.ops.size() * kNodesPerOperation;
    Require(copies <= (kMaxId - operation_nodes) / kNodesPerCopy &&
            bundles <= kMaxId - graph_.origins.size() - graph_.ops.size(), Error::kResourceLimit);
    Require(commits <= kMaxId - graph_.origins.size() - graph_.ops.size() - bundles, Error::kResourceLimit);
    std::size_t prefixes = 0;
    for (const auto& call : evaluation_.phase_selections) {
      Step(); Require(call.prefix.size() <= kMaxId - prefixes, Error::kResourceLimit);
      prefixes += call.prefix.size();
    }
    for (const auto& region : logicals_) if (region.original_prefix) {
      Step(); Require(region.original_prefix->size() <= kMaxId - prefixes, Error::kResourceLimit);
      prefixes += region.original_prefix->size();
    }
    Require(evaluation_.literal_fields.prefixes.size() <= kMaxId-prefixes, Error::kResourceLimit);
    prefixes += evaluation_.literal_fields.prefixes.size();
    constexpr std::size_t kNodesPerPrefix = 2; // Optional snapshot assignment, then sequence.
    Require(prefixes <= (kMaxId - operation_nodes - copies * kNodesPerCopy) / kNodesPerPrefix, Error::kResourceLimit);
    const auto close_nodes=iteration_view_?iteration_view_->closes.size():0;
    Require(close_nodes<=kMaxId-operation_nodes-copies*kNodesPerCopy-prefixes*kNodesPerPrefix,Error::kResourceLimit);
    const auto expression_nodes = operation_nodes + copies * kNodesPerCopy + prefixes * kNodesPerPrefix + close_nodes;
    Require(buffered_properties <= (kMaxId - expression_nodes) / 2, Error::kResourceLimit);
    constexpr std::size_t kNodesPerIndexedArgument = 3; // Snapshot leaf, numeric key, member read.
    Require(indexed_arguments <= (kMaxId - expression_nodes - buffered_properties * 2) / kNodesPerIndexedArgument &&
        indexed_arguments <= kMaxId - graph_.uses.size() &&
        buffered_properties <= kMaxId - graph_.uses.size() - indexed_arguments, Error::kResourceLimit);
    Reserve(forest_.expressions, expression_nodes + buffered_properties * 2 + indexed_arguments * kNodesPerIndexedArgument);
    Reserve(forest_.arguments, graph_.uses.size() + indexed_arguments + buffered_properties);
    Reserve(forest_.origins, graph_.origins.size() + graph_.ops.size() + bundles + commits);
    Reserve(forest_.edge_assignments, normal_copies);
    Reserve(forest_.edges, bundles - staged_bundles);
    Reserve(forest_.throw_assignments, staged_copies);
    Reserve(forest_.throws, staged_bundles);
    Reserve(forest_.catch_assignments, catch_copies);
    Reserve(forest_.catches, commits);
    Reserve(forest_.origin_parts, graph_.ops.size() * kOriginPartsPerOperation);
    Reserve(forest_.operations, graph_.ops.size());
    forest_.operations.resize(graph_.ops.size());
    Reserve(forest_.initializations, graph_.ops.size());
    forest_.initializations.resize(graph_.ops.size());
    Reserve(forest_.initializers, graph_.ops.size());
    Reserve(objects_, graph_.ops.size());
    Step(graph_.ops.size()); Step(graph_.ops.size());
    forest_.initializers.resize(graph_.ops.size()); objects_.resize(graph_.ops.size());
    const auto defined_properties = evaluation_.literals ? evaluation_.literals->definitions.size() : 0;
    Require(defined_properties <= kMaxId - buffered_properties, Error::kResourceLimit);
    Reserve(forest_.properties, defined_properties + buffered_properties);
    if (evaluation_.literals) {
      for (std::size_t i = 0; i < evaluation_.literals->objects.size(); ++i) {
        Step(); if (evaluation_.literals->objects[i].form == LiteralForm::kExpression ||
            (!evaluation_.logical_objects.empty() && evaluation_.logical_objects[evaluation_.literals->objects[i].allocation.value]))
          objects_[evaluation_.literals->objects[i].allocation.value] = i;
      }
    }
    if (evaluation_.literals) Require(CheckStagedLiteralNames(*evaluation_.literals, constants_, names_,
        [&](std::uint64_t count) { Step(count); }, [&](std::size_t count, std::size_t width) { Admit(count, width); }), Error::kConstant);
    for (const auto& origin : graph_.origins) {
      Step();
      Require(origin.disposition != analysis::OriginDisposition::kCombined, Error::kOrigin);
      if (origin.disposition == analysis::OriginDisposition::kElidedPure)
        forest_.origins.emplace_back(ElidedOrigin{origin.site, origin.pc, origin.original});
      else
        forest_.origins.emplace_back(OriginalSite{origin.site, origin.pc, origin.original});
    }
    NestedTrees();
    // Dependency-free expressions may precede their consumers in execution
    // while following them in the storage array. Build the verified owned
    // leaves once from their original constants/origins, without replaying an
    // effect, context read or allocation. Their physical visit is then inert.
    for (std::size_t i = 0; i < evaluation_.closed_expressions.size(); ++i) {
      Step(); if (!evaluation_.closed_expressions[i]) continue;
      const auto& op = graph_.ops[i];
      Require(IsClosedPureValue(graph_, {static_cast<std::uint32_t>(i)}) &&
          op.origin.value < graph_.origins.size() &&
          graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained, Error::kOwnership);
      forest_.operations[i] = Lower({static_cast<std::uint32_t>(i)}, op, {}, {op.origin.value}, constants_.operations[i]);
      Require(forest_.operations[i].has_value(), Error::kUnsupportedOperation);
    }
    const bool partial = CanRetainPrefix();
    struct Boundary {
      std::size_t operation = 0, expressions = 0, arguments = 0, origins = 0, parts = 0;
    } boundary{0, 0, 0, forest_.origins.size(), 0};
    std::optional<std::size_t> last_consumer;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
      if (!logical_joins.empty() && logical_joins[i]) LogicalTree(logicals_[*logical_joins[i]]);
      if (partial && (!last_consumer || *last_consumer < i))
        boundary = {i, forest_.expressions.size(), forest_.arguments.size(),
                    forest_.origins.size(), forest_.origin_parts.size()};
      if (partial && evaluation_.operations[i].kind == EvaluationKind::kInline) {
        const auto consumer = evaluation_.operations[i].consumer;
        Require(consumer && consumer->value < graph_.uses.size(), Error::kOwnership);
        const auto* owner = std::get_if<analysis::OpId>(&graph_.uses[consumer->value].owner);
        Require(owner != nullptr && owner->value > i && owner->value < graph_.ops.size(), Error::kOwnership);
        const auto target = owner->value;
        last_consumer = std::max(last_consumer.value_or(i), static_cast<std::size_t>(target));
      }
      try {
        const auto& op = graph_.ops[i];
        if (evaluation_.operations[i].kind == EvaluationKind::kForOfEvent) {
          const auto member = evaluation_.for_of->operations[i];
          Require(member.has_value(),Error::kOwnership);
          if(member->role == ForOfRole::kAcquisition)
            forest_.operations[i]=Use(op.first_use,{op.origin.value});
          continue;
        }
        if (enumeration_ && enumeration_->forwarded_operations[i]) continue;
        if (enumeration_ && evaluation_.operations[i].kind == EvaluationKind::kForInEvent) {
          const auto member = *evaluation_.for_in->operations[i];
          if (evaluation_.for_in->loops[member.loop].acquisition == analysis::OpId{static_cast<std::uint32_t>(i)})
            forest_.operations[i] = Use(op.first_use, {op.origin.value});
          continue;
        }
        if (!evaluation_.closed_expressions.empty() && evaluation_.closed_expressions[i]) continue;
        // Primitive phi leaves may be built before their storage-array position
        // when a selected join needs them. They remain in the original arm.
        if (forest_.operations[i] && evaluation_.operations[i].kind == EvaluationKind::kPhiInput &&
            IsSelectionConstant(graph_, {static_cast<std::uint32_t>(i)})) continue;
        if (op.life != analysis::ValueLife::kLive || (!predicates.empty() && predicates[i])) continue;
        Require(op.origin.value < graph_.origins.size() &&
                    graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained,
                Error::kOrigin);
        const TreeOriginId origin{op.origin.value};
        if (std::binary_search(constants_.receiver_guards.begin(), constants_.receiver_guards.end(),
            analysis::OpId{static_cast<std::uint32_t>(i)})) {
          Require(std::holds_alternative<analysis::ValueSuperCheck>(op.operation) && !op.result, Error::kOwnership);
          continue;
        }
        if (IsAbsentHeritageTransport(graph_, evaluation_, op)) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
            evaluation_.operations[i].kind == EvaluationKind::kAlias)
          continue;
        if (!evaluation_.logical_objects.empty() && evaluation_.logical_objects[i]) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kInitializer &&
            IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, {static_cast<std::uint32_t>(i)})) {
          const auto base = std::get<analysis::ValueId>(
              ResolveEvaluationOperand(evaluation_, graph_.uses[op.first_use.value].operand));
          const auto allocation = std::get<analysis::OpResult>(graph_.values[base.value].origin).op;
          Require(objects_[allocation.value].has_value(), Error::kOwnership);
          if (evaluation_.logical_objects[allocation.value] != analysis::OpId{static_cast<std::uint32_t>(i)}) continue;
          forest_.operations[allocation.value] = Object(evaluation_.literals->objects[*objects_[allocation.value]],
              TreeOriginId{graph_.ops[allocation.value].origin.value});
          continue;
        }
        if(MemberUpdateIntrinsic(evaluation_,{static_cast<std::uint32_t>(i)}))continue;
        if(const auto* update=MemberUpdateAt(evaluation_,{static_cast<std::uint32_t>(i)})) {
          const TreeOriginId get_origin{graph_.ops[update->read.value].origin.value};
          const auto base=Use(update->base,get_origin);
          MemberKey key;
          if(update->key)key=ComputedKey{Use(*update->key,get_origin)};
          else {const auto* name=std::get_if<NameId>(&constants_.operations[i]);Require(name,Error::kConstant);key=*name;}
          const auto rhs=update->rhs?std::optional{Use(*update->rhs,{graph_.ops[update->calculation.value].origin.value})}:std::nullopt;
          MemberMutationExpr mutation{{base,key},MemberIncrement{}};
          if (const auto* increment=std::get_if<MemberIncrement>(&update->operation)) mutation.operation=*increment;
          else { Require(rhs.has_value(),Error::kOwnership); mutation.operation=MemberCompound{std::get<analysis::BinaryOperator>(update->operation),*rhs}; }
          const auto node=Add(mutation,origin);
          for(const auto& port:update->ports){Step();forest_.operations[port.operation.value]=node;if(port.operation.value!=i)Combine(node,{graph_.ops[port.operation.value].origin.value});}
          continue;
        }
        if (evaluation_.operations[i].kind == EvaluationKind::kInitializer ||
            evaluation_.operations[i].kind == EvaluationKind::kNestedLiteral) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kConcurrentNotification || evaluation_.operations[i].kind == EvaluationKind::kEnvironment ||
            evaluation_.operations[i].kind == EvaluationKind::kTemplateConstruction || evaluation_.operations[i].kind == EvaluationKind::kClassProjection ||
            evaluation_.operations[i].kind == EvaluationKind::kArrayCursor) continue;
        if(const auto* site=TemplateSite(evaluation_,{static_cast<std::uint32_t>(i)})) {
          TreeCheckBudget tpl_budget({bytes_,work_});auto quasis=std::allocate_shared<std::vector<TemplateQuasi>>(RecoveryAllocator<std::vector<TemplateQuasi>>{tpl_budget});tpl_budget.Array(*quasis,site->quasis.size());
          for(std::size_t q=0;q<quasis->size();++q){tpl_budget.Step();const auto& source=site->quasis[q];const auto* raw=std::get_if<TextId>(&constants_.operations[source.raw.value]);Require(raw,Error::kConstant);std::optional<TextId> cooked;if(source.cooked){const auto* text=std::get_if<TextId>(&constants_.operations[source.cooked->value]);Require(text,Error::kConstant);cooked=*text;}(*quasis)[q]={*raw,cooked};}
          Admit(tpl_budget.Bytes(),1);Step(tpl_budget.Work());forest_.operations[i]=Add(TemplateObjectExpr{std::move(quasis)},origin);continue;
        }
        if (const auto* array = ArrayLiteralFor(evaluation_, {static_cast<std::uint32_t>(i)})) {
          forest_.operations[i] = Array(*array, origin); continue;
        }
        if (GuardedInput(evaluation_, {static_cast<std::uint32_t>(i)})) {
          const auto peer = *GuardedPeer(evaluation_, {static_cast<std::uint32_t>(i)});
          const auto expression = Combine(Add(SymbolExpr{CellSymbol({static_cast<std::uint32_t>(i)})}, origin),
              {graph_.ops[peer.value].origin.value});
          forest_.operations[i] = forest_.operations[peer.value] = expression;
          continue;
        }
        if (evaluation_.operations[i].kind == EvaluationKind::kGuardedCellRead) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kModuleGuard ||
            evaluation_.operations[i].kind == EvaluationKind::kCellGuard) continue;
        if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) {
          Module(i, op, origin);
          continue;
        }
        if (const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation)) {
          const auto* binding = std::get_if<ModuleStoreConstant>(&constants_.operations[i]);
          Require(binding && binding->slot == store->slot && op.use_count == 1, Error::kConstant);
          const auto found = std::lower_bound(module_order_.begin(), module_order_.end(), binding->target,
              [&](auto index, auto target) { Step(); return modules_[index] < target; });
          Require(found != module_order_.end() && modules_[*found] == binding->target, Error::kBinding);
          const SymbolId symbol{static_cast<std::uint32_t>(forest_.bindings.symbols.size() + *found)};
          const auto value = Use(op.first_use, origin);
          if (binding->initialize) {
            forest_.initializations[i] = symbol;
            forest_.operations[i] = value;
          } else forest_.operations[i] = Add(AssignmentExpr{SymbolTarget{symbol}, value}, origin);
          continue;
        }
        if (objects_[i]) {
          forest_.operations[i] = Object(evaluation_.literals->objects[*objects_[i]], origin);
          continue;
        }
        if (!std::holds_alternative<NoCall>(forest_.calls.operations[i])) {
          forest_.operations[i] = Call(op, forest_.calls.operations[i], origin);
          continue;
        }
        if (const auto* call = std::get_if<analysis::ValueSuperCall>(&op.operation)) {
          Require(call->callee == analysis::SuperCalleeSource::kInvocationFunction, Error::kUnsupportedOperation);
          Require(op.use_count == call->arguments + (call->source == analysis::SuperArgumentSource::kInvocationFrame ? 1U : 0U) && op.result, Error::kOwnership);
          if (call->source == analysis::SuperArgumentSource::kInvocationFrame &&
              forest_.bindings.parameter_form == ParameterForm::kInvocationRest) {
            Require(constants_.derived_constructor && constants_.forwarding_owner &&
                callable_ == CallableKind::kDerivedConstructor && !forest_.bindings.symbols.empty() &&
                std::holds_alternative<InvocationRestBinding>(forest_.bindings.symbols.front()), Error::kBinding);
            // This leaf is owned by the super event. It is a source invocation
            // binding, not a fabricated ValueRestArguments instruction or use.
            const auto rest = Add(SymbolExpr{SymbolId{0}}, origin);
            forest_.operations[i] = Add(SuperCallExpr{SpreadArguments{rest}, call->source}, origin);
            continue;
          }
          const ArgumentRange arguments{static_cast<std::uint32_t>(forest_.arguments.size()), call->arguments};
          for (std::uint32_t j = 0; j < call->arguments; ++j) {
            Step();
            Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
            forest_.arguments.push_back(Use(analysis::UseId{op.first_use.value + j}, origin));
          }
          forest_.operations[i] = Add(SuperCallExpr{arguments, call->source}, origin);
          continue;
        }
        if(std::holds_alternative<analysis::ValueObjectRest>(op.operation)) {
          forest_.operations[i]=Rest(op,origin);continue;
        }
        if (std::holds_alternative<analysis::ValueArgumentSnapshot>(op.operation)) {
          const ArgumentRange slots{static_cast<std::uint32_t>(forest_.arguments.size()), op.use_count};
          for (std::uint32_t j = 0; j < op.use_count; ++j) {
            Step();
            Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
            forest_.arguments.push_back(Operand(graph_.uses[op.first_use.value + j].operand, origin));
          }
          forest_.operations[i] = Add(ArgumentSnapshotExpr{owner_, slots}, origin);
          continue;
        }
        if (std::holds_alternative<analysis::ValueClassCreation>(op.operation)) {
          Class(i, op, origin); continue;
        }
        if (std::holds_alternative<analysis::ValueSuperSpread>(op.operation)) {
          // Explicit language equivalence selects native parent and iterator
          // behavior. It never supplies a lifetime brand proof under pinned mode.
          if (constructors_ != ConstructorSemantics::kSourceLanguage)
            throw Error::kUnsupportedOperation;
          Require(callable_ == CallableKind::kDerivedConstructor && op.result && op.use_count == 2,
              Error::kOwnership);
          const auto iterable = Use(analysis::UseId{op.first_use.value + 1}, origin);
          forest_.operations[i] = Add(SuperCallExpr{SpreadArguments{iterable}}, origin);
          continue;
        }
        std::array<ExprId, kFixedExpressionOperands> operands{};
        Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
        for (std::uint32_t j = 0; j < op.use_count; ++j) {
          Step();
          operands[j] = Use(analysis::UseId{op.first_use.value + j}, origin);
        }
        forest_.operations[i] =
            Lower({static_cast<std::uint32_t>(i)}, op, operands, origin, constants_.operations[i]);
      } catch (Error error) {
        if (!partial || error != Error::kUnsupportedOperation) throw;
        // Roll back the entire unfinished inline expression, not just its last
        // opcode. Retain only roots whose operands have all been consumed.
        forest_.expressions.resize(boundary.expressions);
        forest_.arguments.resize(boundary.arguments);
        forest_.origins.resize(boundary.origins);
        forest_.origin_parts.resize(boundary.parts);
        for (std::size_t omitted = boundary.operation; omitted < forest_.operations.size(); ++omitted) {
          Step(); forest_.operations[omitted].reset();
        }
        forest_.recovery_stop = analysis::OpId{static_cast<std::uint32_t>(boundary.operation)};
        break;
      }
    }
    Require(built_functions_ == evaluation_.functions.size(), Error::kOwnership);
    Require(next_module_ == evaluation_.modules.size(), Error::kOwnership);
    for (const auto& region : logicals_) {
      Step(); if (evaluation_.operations[region.branch.value].kind != EvaluationKind::kLogicalValue) LogicalTree(region);
    }
    const auto copied = AppendCopyExpressions(graph_, evaluation_, forest_, owner_, work_, selected_edges);
    if (const auto* error = std::get_if<ExpressionFailure>(&copied)) throw error->kind;
    Step(std::get<std::uint64_t>(copied));
    if(iteration_view_) {
      Reserve(forest_.for_of_exits,iteration_view_->closes.size());forest_.for_of_exits.resize(iteration_view_->closes.size());
      for(std::size_t n=0;n<iteration_view_->closes.size();++n){Step();const auto& close=iteration_view_->closes[n];
        if(!close || !close->payload || (close->pending.kind!=analysis::PendingCompletionKind::kReturn && close->pending.kind!=analysis::PendingCompletionKind::kThrow))continue;
        const auto& edge=graph_.edges[close->edge.value];
        if(edge.kind==analysis::FlowEdgeKind::kException || iteration_view_->close_blocks[edge.source.value])continue;
        const auto b=edge.source;
        Require(graph_.phases[b.value].operation.has_value(),Error::kOrigin);
        const auto origin=graph_.ops[graph_.phases[b.value].operation->value].origin;
        const analysis::ValueOperand payload=std::holds_alternative<ForOfUndefined>(*close->payload)?analysis::ValueOperand{analysis::SpecialValue::kUndefined}:analysis::ValueOperand{std::get<analysis::ValueId>(*close->payload)};
        forest_.for_of_exits[n]=Operand(payload,{origin.value});
      }
    }
    forest_.allocated_bytes = budget_.bytes - bytes_;
    forest_.work = budget_.work - work_;
    return std::move(forest_);
  }

 private:
  void LogicalTree(const SelectionRegion& root) {
    const auto root_index = static_cast<std::size_t>(&root - logicals_.data());
    if (selection_built_[root_index]) return;
    selection_tasks_.push_back(root_index);
    while (!selection_tasks_.empty()) {
      Step(); const auto index = selection_tasks_.back(); const auto& region = logicals_[index];
      std::optional<std::size_t> pending;
      const auto child = [&](const SelectionOperand& input) {
        const auto* nested = std::get_if<SelectionChild>(&input);
        if (!nested) return;
        Require(nested->branch.value < selection_indices_.size() && selection_indices_[nested->branch.value], Error::kOwnership);
        const auto dependency = *selection_indices_[nested->branch.value];
        Require(dependency < index && logicals_[dependency].parent == region.branch, Error::kOwnership);
        if (!selection_built_[dependency]) pending = dependency;
      };
      if (!region.logical || !region.logical->shared_right) {
        child(region.right); if (region.consequent) child(*region.consequent);
      }
      if (pending) { selection_tasks_.push_back(*pending); continue; }
      Logical(region); selection_built_[index] = 1; selection_tasks_.pop_back();
    }
  }
  void Logical(const SelectionRegion& region) {
    Step();
    const TreeOriginId boolean_origin{graph_.ops[region.boolean.value].origin.value};
    const TreeOriginId branch_origin{graph_.ops[region.branch.value].origin.value};
    ExprId left;
    if (region.logical) left = Use(region.logical->input, branch_origin);
    else if (region.conditions.empty()) {
      // Logical LHS uses the original predicate input; a conditional retains
      // the predicate's result. Preserve the actual occurrence in either form.
      const auto use = graph_.ops[region.consequent ? region.branch.value : region.boolean.value].first_use;
      left = Use(use, boolean_origin);
    }
    if (!region.logical && !region.conditions.empty()) {
      Require(region.consequent && region.conditions.front().branch == region.branch, Error::kOwnership);
      const auto last = region.conditions.back().branch;
      Require(forest_.operations[last.value].has_value(), Error::kOwnership);
      left = *forest_.operations[last.value];
      for (auto i = region.conditions.size() - 1; i != 0; --i) {
        Step(); const auto branch = region.conditions[i - 1].branch;
        Require(forest_.operations[branch.value].has_value(), Error::kOwnership);
        left = Add(LogicalExpr{TreeLogical::kOr, *forest_.operations[branch.value], left},
            TreeOriginId{graph_.ops[branch.value].origin.value});
      }
    }
    const auto operand = [&](const SelectionOperand& input) {
      if (const auto* snapshot = std::get_if<SelectionSnapshot>(&input)) return Operand(snapshot->value, branch_origin);
      analysis::OpId producer;
      if (const auto* child = std::get_if<SelectionChild>(&input)) producer = child->branch;
      else {
        const auto value = std::get<analysis::ValueId>(input);
        producer = std::get<analysis::OpResult>(graph_.values[value.value].origin).op;
      }
      Require(producer.value < forest_.operations.size(), Error::kOwnership);
      if (!forest_.operations[producer.value]) {
        Require(evaluation_.operations[producer.value].kind == EvaluationKind::kPhiInput && IsSelectionConstant(graph_, producer), Error::kOwnership);
        const auto& op = graph_.ops[producer.value];
        forest_.operations[producer.value] = Operand(graph_.uses[op.first_use.value].operand, TreeOriginId{op.origin.value});
      }
      return *forest_.operations[producer.value];
    };
    const auto* call = FindPhaseSelectionOperand(graph_, evaluation_, region.branch);
    const auto* prefixes = region.original_prefix ? &*region.original_prefix : call ? &call->prefix : nullptr;
    if (prefixes) {
      for (auto i = prefixes->size(); i != 0;) {
        Step(); const auto id = (*prefixes)[--i]; const auto& op = graph_.ops[id.value];
        Require(forest_.operations[id.value].has_value(), Error::kOwnership);
        auto prefix = *forest_.operations[id.value];
        const TreeOriginId origin{op.origin.value};
        if (evaluation_.operations[id.value].temporary) {
          Require(op.result.has_value(), Error::kOwnership);
          const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[op.result->value]);
          Require(symbol != nullptr, Error::kBinding);
          prefix = Add(AssignmentExpr{SymbolTarget{*symbol}, prefix}, origin);
        }
        left = Add(SequenceExpr{prefix, left}, branch_origin);
      }
    }
    const auto right = operand(region.logical && region.logical->shared_right
        ? *region.logical->shared_right
        : region.logical && !region.logical->keep_consequent ? *region.consequent : region.right);
    const auto expression = region.logical
        ? Add(LogicalExpr{region.logical->operation, left, right}, branch_origin)
        : region.consequent ? Add(ConditionalExpr{left, operand(*region.consequent), right}, branch_origin)
        : Combine(Add(LogicalExpr{region.operation, left, right}, boolean_origin), branch_origin);
    if (region.logical) {
      for (const auto truth : region.logical->truths) {
        Step(); Combine(expression, {graph_.ops[truth.value].origin.value}); forest_.operations[truth.value] = expression;
      }
      for (const auto& condition : region.conditions) {
        Step();
        if (condition.branch != region.branch) Combine(expression, {graph_.ops[condition.branch.value].origin.value});
        forest_.operations[condition.branch.value] = expression;
      }
    }
    if (region.logical && region.logical->shared_right) {
      const auto bind = [&](const SelectionRegion& part) {
        const auto leaf = [&](const SelectionOperand& input) {
          if (const auto* value = std::get_if<analysis::ValueId>(&input)) {
            const auto op = std::get<analysis::OpResult>(graph_.values[value->value].origin).op;
            const auto* representative = std::get_if<analysis::ValueId>(&*region.logical->shared_right);
            if (!representative || *representative != *value)
              Combine(right, {graph_.ops[op.value].origin.value});
            forest_.operations[op.value] = right;
          }
        };
        leaf(part.right); if (part.consequent) leaf(*part.consequent);
        if (part.branch != region.branch) for (const auto& test : part.tests) {
          Step(); Combine(expression, {graph_.ops[test.operation.value].origin.value});
          forest_.operations[test.operation.value] = expression;
        }
        forest_.operations[part.branch.value] = expression;
      };
      bind(region);
      for (const auto branch : region.logical->shared_branches) {
        Step(); const auto index = *selection_indices_[branch.value];
        bind(logicals_[index]); selection_built_[index] = 1;
      }
    }
    if (!region.consequent) forest_.operations[region.boolean.value] = expression;
    if (region.parent || evaluation_.operations[region.branch.value].kind == EvaluationKind::kLogicalValue) {
      forest_.operations[region.branch.value] = expression;
      return;
    }
    const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[region.merged.value]);
    Require(symbol != nullptr, Error::kBinding);
    forest_.operations[region.branch.value] = Add(AssignmentExpr{SymbolTarget{*symbol}, expression}, branch_origin);
  }
  void Module(std::size_t i, const analysis::ValueOp&, TreeOriginId origin) {
    const auto found_module = std::lower_bound(evaluation_.modules.begin(), evaluation_.modules.end(), i,
        [&](const auto& entry, auto index) { Step(); return entry.pair.read.value < index; });
    Require(found_module != evaluation_.modules.end() && found_module->pair.read.value == i, Error::kBinding);
    ++next_module_;
    const auto& module = *found_module;
    const auto found = std::lower_bound(module_order_.begin(), module_order_.end(), module.target,
                      [&](auto index, auto target) {
                        Step();
                        return modules_[index] < target;
                      });
    Require(found != module_order_.end() && modules_[*found] == module.target, Error::kBinding);
    // FunctionTree appends module aliases in this supplied order after the
    // shared IR bindings. This is a source SymbolId, never an ABC slot.
    const SymbolId symbol{static_cast<std::uint32_t>(forest_.bindings.symbols.size() + *found)};
    const auto read = Add(SymbolExpr{symbol, module.read.loading, module.read.domain}, origin);
    const auto expression = module.pair.guard
        ? Combine(read, {graph_.ops[module.pair.guard->value].origin.value}) : read;
    forest_.operations[i] = expression;
    if (module.pair.guard) forest_.operations[module.pair.guard->value] = expression;
  }
  bool CanRetainPrefix() {
    // Initial partial construction handles one normal straight-line region.
    // Other topologies still use the full builder until their region ownership
    // can be retained. Never linearize a branch, exception, cell or phi region.
    if (recovery_ != RecoveryMode::kAllowPartial || evaluation_.flow != EvaluationFlow::kNormal ||
        graph_.blocks.size() != 1 || !graph_.edges.empty() ||
        graph_.blocks[0].first_op.value != 0 || graph_.blocks[0].op_count != graph_.ops.size() ||
        !evaluation_.functions.empty() || !evaluation_.modules.empty() || !modules_.empty() ||
        (evaluation_.phis && !evaluation_.phis->bundles.empty()) ||
        (evaluation_.cells && !evaluation_.cells->cells.cells.empty()) || evaluation_.exceptions ||
        evaluation_.literals || !forest_.bindings.cells.empty()) return false;
    for (const auto& op : graph_.ops) {
      Step();
      if (std::holds_alternative<analysis::ValueEnvironmentPush>(op.operation) ||
          std::holds_alternative<analysis::ValueEnvironmentPop>(op.operation) ||
          std::holds_alternative<analysis::ValueObjectAllocation>(op.operation)) return false;
    }
    return true;
  }
  // Native class construction is identical in a literal-owned dependency and
  // an ordinary root; the original creation occurrence and heritage prove it.
  void Class(std::size_t i, const analysis::ValueOp& op, TreeOriginId origin) {
    const auto& creation = std::get<analysis::ValueClassCreation>(op.operation);
    const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[i]);
    if (!definition || !*definition) throw Error::kUnsupportedOperation;
    Require(op.use_count == 1, Error::kOwnership);
    if (evaluation_.flow == EvaluationFlow::kPhased) {
      const auto checked = CheckClassParentScope(**definition, work_);
      if (std::holds_alternative<ClassTreeError>(checked)) throw Error::kWorkLimit;
      const auto scope = std::get<ClassParentScope>(checked); Step(scope.work);
      Require(scope.independent, Error::kOwnership);
    }
    const auto* heritage = std::get_if<analysis::ValueId>(&graph_.uses[op.first_use.value].operand);
    const bool absent = heritage && graph_.values[heritage->value].domain == analysis::ValueKinds::Hole();
    Require((*definition)->callable ==
        (absent ? CallableKind::kBaseConstructor : CallableKind::kDerivedConstructor), Error::kOwnership);
    const auto& site = (*definition)->creation;
    Require(site.creation.constructor == creation.constructor && site.creation.literal == creation.literal &&
        site.creation.length == creation.length && site.creation.cache_slot == creation.cache_slot &&
        site.pc == graph_.origins[op.origin.value].pc,
        Error::kConstant);
    forest_.operations[i] = Add(ClassExpr{*definition, absent ? std::nullopt :
        std::optional{Operand(graph_.uses[op.first_use.value].operand, origin)}}, origin);
  }
  void NestedTrees() {
    if (!evaluation_.literals || (evaluation_.literals->nested.empty() && !ArrayInitializations(evaluation_))) return;
    // Each checked nested value has one syntax consumer. Redirect own-write
    // consumers to their allocation, then use Kahn's worklist over these
    // ownership edges. Read/call edges and aggregate/property edges may point
    // in opposite IR directions; two scans cannot order their mixed trees.
    // O(operations + nested edges), bounded storage, no recursive lowering or
    // repeated dependency scans. This schedules construction, not execution.
    std::vector<std::optional<analysis::OpId>> parents;
    std::vector<std::size_t> pending;
    std::vector<analysis::OpId> ready;
    Reserve(parents, graph_.ops.size()); parents.resize(graph_.ops.size());
    Reserve(pending, graph_.ops.size()); pending.resize(graph_.ops.size());
    Step(graph_.ops.size()); Step(graph_.ops.size());
    Reserve(ready, graph_.ops.size());
    std::size_t count = 0;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); if (evaluation_.operations[i].kind != EvaluationKind::kNestedLiteral) continue;
      ++count;
      const auto use = evaluation_.operations[i].consumer;
      Require(use && use->value < graph_.uses.size(), Error::kOwnership);
      const auto* parent = std::get_if<analysis::OpId>(&graph_.uses[use->value].owner);
      Require(parent && parent->value < graph_.ops.size(), Error::kOwnership);
      auto target = *parent;
      if (evaluation_.operations[target.value].kind == EvaluationKind::kInitializer) {
        if (const auto* arrays = ArrayInitializations(evaluation_); arrays && arrays->owners[target.value]) {
          target = *arrays->owners[target.value];
        } else {
          const auto& definition = graph_.ops[target.value];
          Require(definition.use_count == (std::holds_alternative<analysis::ValueComputedOwnWrite>(definition.operation) ? 3 : 2), Error::kOwnership);
          const auto operand = ResolveEvaluationOperand(evaluation_, graph_.uses[definition.first_use.value].operand);
          const auto* base = std::get_if<analysis::ValueId>(&operand);
          const auto* allocation = base ? std::get_if<analysis::OpResult>(&graph_.values[base->value].origin) : nullptr;
          Require(allocation && objects_[allocation->op.value], Error::kOwnership);
          target = allocation->op;
        }
      }
      if (evaluation_.operations[target.value].kind != EvaluationKind::kNestedLiteral) continue;
      parents[i] = target; ++pending[target.value];
    }
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); if (evaluation_.operations[i].kind == EvaluationKind::kNestedLiteral && !pending[i])
        ready.push_back({static_cast<std::uint32_t>(i)});
    }
    for (std::size_t at = 0; at < ready.size(); ++at) {
      Step(); const auto id = ready[at]; const auto i = id.value;
      const auto& op = graph_.ops[i]; const TreeOriginId origin{op.origin.value};
      Require(!forest_.operations[i], Error::kOwnership);
      if (objects_[i]) forest_.operations[i] = Object(evaluation_.literals->objects[*objects_[i]], origin);
      else if (!std::holds_alternative<NoCall>(forest_.calls.operations[i]))
        forest_.operations[i] = Call(op, forest_.calls.operations[i], origin);
      else if (std::holds_alternative<analysis::ValueObjectRest>(op.operation)) forest_.operations[i]=Rest(op,origin);
      else if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) Module(i, op, origin);
      else if (std::holds_alternative<analysis::ValueClassCreation>(op.operation)) Class(i, op, origin);
      else {
        std::array<ExprId, kFixedExpressionOperands> operands{};
        Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
        for (std::uint32_t j = 0; j < op.use_count; ++j) {
          Step(); operands[j] = Use(analysis::UseId{op.first_use.value + j}, origin);
        }
        forest_.operations[i] = Lower(id, op, operands, origin, constants_.operations[i]);
      }
      if (parents[i]) {
        const auto parent = *parents[i];
        Require(pending[parent.value] != 0, Error::kOwnership);
        if (--pending[parent.value] == 0) ready.push_back(parent);
      }
    }
    Require(ready.size() == count, Error::kOwnership);
  }
  PropertyRange BufferedProperties(const ExpressionConstant& constant, TreeOriginId origin) {
    const auto* object = std::get_if<AggregateConstant>(&constant);
    Require(object != nullptr, Error::kConstant);
    const PropertyRange properties{static_cast<std::uint32_t>(forest_.properties.size()), object->count};
    for (std::uint32_t i = 0; i < object->count; ++i) {
      Step(); const auto& property = constants_.properties[object->first + i];
      Require(property.key.value < constants_.name_count, Error::kConstant);
      const auto value = Add(property.value, origin);
      forest_.properties.push_back({property.key, value, origin});
    }
    return properties;
  }
  ExprId Object(const ObjectInit& object, TreeOriginId origin) {
    const auto* allocation = std::get_if<analysis::ValueObjectAllocation>(&graph_.ops[object.allocation.value].operation);
    const auto kind = allocation ? AggregateKind::kObject : AggregateKind::kArray;
    const AggregateConstant* buffer = nullptr;
    const auto* array = std::get_if<analysis::ValueArrayAllocation>(&graph_.ops[object.allocation.value].operation);
    if ((allocation && allocation->literal) || (array && array->literal)) {
      buffer = std::get_if<AggregateConstant>(&constants_.operations[object.allocation.value]);
      Require(buffer && buffer->kind == kind, Error::kConstant);
    }
    const auto prefix = buffer ? buffer->count : 0;
    std::vector<std::optional<analysis::OpId>> writes;
    std::vector<std::uint32_t> order;
    Reserve(writes, static_cast<std::size_t>(prefix) + object.count);
    writes.resize(prefix);
    Reserve(order, prefix);
    const auto key = [&](std::uint32_t index) -> std::u16string_view {
      const auto name = constants_.properties[buffer->first + index].key;
      Require(name.value < names_.size(), Error::kConstant);
      return names_[name.value];
    };
    if (!names_.empty()) {
      for (std::uint32_t i = 0; i < prefix; ++i) { Step(); order.push_back(i); }
      std::sort(order.begin(), order.end(), [&](auto a, auto b) {
        const auto x = key(a), y = key(b); Step(); Step(x.size()); Step(y.size());
        return x == y ? a < b : x < y;
      });
    }
    // LiteralPlan proves no observation before initialization finishes. Buffer
    // values are primitive, so an overwritten buffer value has no effect. Keep
    // its original key position, but never reorder two own-definition RHSs.
    // Last duplicate buffer occurrence is essential: a later primitive entry
    // must not overwrite a relocated RHS. O((P+D) log(P+1)), no per-write scan.
    std::size_t next = 0;
    for (std::uint32_t i = 0; i < object.count; ++i) {
      Step(); const auto id = evaluation_.literals->definitions[object.first + i];
      const auto* name = std::get_if<NameId>(&constants_.operations[id.value]);
      const bool spread=std::holds_alternative<analysis::ValueCopyProperties>(graph_.ops[id.value].operation);
      const bool computed=std::holds_alternative<analysis::ValueComputedOwnWrite>(graph_.ops[id.value].operation);
      Require(spread || computed || (name && name->value < constants_.name_count), Error::kConstant);
      std::size_t destination = writes.size();
      if (array) {
        const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&graph_.ops[id.value].operation);
        Require(indexed && indexed->index >= next && indexed->index <= writes.size(), Error::kConstant);
        destination = indexed->index;
      } else if (!spread && !computed && !order.empty()) {
        Require(name->value < names_.size(), Error::kConstant);
        const std::u16string_view sought = names_[name->value];
        const auto found = std::upper_bound(order.begin(), order.end(), sought,
            [&](auto text, auto index) { const auto other = key(index); Step(); Step(text.size()); Step(other.size()); return text < other; });
        if (found != order.begin()) {
          const auto index = *(found - 1); const auto text = key(index);
          Step(sought.size()); Step(text.size());
          if (text == sought && index >= next) destination = index;
        }
      }
      if (destination == writes.size()) writes.push_back(id);
      else writes[destination] = id;
      next = destination + 1;
    }
    const PropertyRange range{static_cast<std::uint32_t>(forest_.properties.size()),
                              static_cast<std::uint32_t>(writes.size())};
    for (std::size_t i = 0; i < writes.size(); ++i) {
      Step();
      if (!writes[i]) {
        const auto& property = constants_.properties[buffer->first + i];
        Require(property.key.value < constants_.name_count, Error::kConstant);
        const auto value = Add(property.value, origin);
        forest_.properties.push_back({property.key, value, origin});
        continue;
      }
      const auto id = *writes[i]; const auto& op = graph_.ops[id.value];
      const bool spread=std::holds_alternative<analysis::ValueCopyProperties>(op.operation);
      const bool computed=std::holds_alternative<analysis::ValueComputedOwnWrite>(op.operation);
      const auto name = (spread || computed)?std::optional<NameId>{}:std::optional{std::get<NameId>(constants_.operations[id.value])};
      const TreeOriginId field_origin{op.origin.value};
      const auto computed_key = computed ? std::optional{Operand(graph_.uses[op.first_use.value+1].operand, field_origin)} : std::nullopt;
      auto value = Operand(graph_.uses[op.first_use.value + (computed ? 2 : 1)].operand, field_origin);
      if(id.value<evaluation_.literal_fields.fields.size()) {
        const auto range=evaluation_.literal_fields.fields[id.value];
        for(std::size_t p=range.count;p;--p){
          Step();const auto root=evaluation_.literal_fields.prefixes[range.first+p-1];
          Require(forest_.operations[root.value].has_value(),Error::kOwnership);
          auto prefix=*forest_.operations[root.value];const auto& operation=graph_.ops[root.value];
          if(evaluation_.operations[root.value].temporary){
            Require(operation.result.has_value(),Error::kBinding);
            const auto* symbol=std::get_if<SymbolId>(&forest_.bindings.values[operation.result->value]);Require(symbol,Error::kBinding);
            prefix=Add(AssignmentExpr{SymbolTarget{*symbol},prefix},TreeOriginId{operation.origin.value});
          }
          value=Add(SequenceExpr{prefix,value},field_origin);
        }
      }
      Require(forest_.properties.size() < forest_.properties.capacity(), Error::kResourceLimit);
      const PropertyId property{static_cast<std::uint32_t>(forest_.properties.size())};
      const auto* own = std::get_if<analysis::ValueOwnWrite>(&op.operation);
      forest_.properties.push_back({name, value, field_origin,
          own ? own->name_setting : computed ? std::get<analysis::ValueComputedOwnWrite>(op.operation).name_setting : analysis::PropertyNameSetting::kNone,
          std::get_if<analysis::ValueCopyProperties>(&op.operation) &&
          std::get<analysis::ValueCopyProperties>(op.operation).kind == analysis::ObjectMutationKind::kSetPrototype ?
          ObjectPropertyEffect::kSetPrototype : ObjectPropertyEffect::kDefine, computed_key});
      forest_.initializers[id.value] = property; forest_.operations[id.value] = value;
    }
    return Add(ObjectExpr{range, kind, prefix}, origin);
  }
  ExprId Array(const ArrayInitialization& array, TreeOriginId origin) {
    const auto* proof = ArrayInitializations(evaluation_);
    Require(proof != nullptr, Error::kOwnership);
    std::vector<ExprId> elements;
    Reserve(elements, static_cast<std::size_t>(array.buffered) + array.count);
    if (array.buffered) {
      const auto* buffer = std::get_if<AggregateConstant>(&constants_.operations[array.allocation.value]);
      Require(buffer && buffer->kind == AggregateKind::kArray && buffer->count == array.buffered,
          Error::kConstant);
      for (std::uint32_t i = 0; i < array.buffered; ++i) {
        Step(); const auto value = Literal(constants_.properties[buffer->first + i].value, origin);
        elements.push_back(Add(ArrayElement{ArrayValueElement{value}}, origin));
      }
    }
    for (std::uint32_t i = 0; i < array.count; ++i) {
      Step(); const auto& step = proof->steps[array.first + i];
      if (!step.value) continue;
      const TreeOriginId site{graph_.ops[step.operation.value].origin.value};
      const auto value = Use(*step.value, site);
      const auto element = step.kind == ArrayStepKind::kSpread
          ? Add(ArrayElement{ArraySpreadElement{value}}, site)
          : Add(ArrayElement{ArrayValueElement{value}}, site);
      forest_.operations[step.operation.value] = element; elements.push_back(element);
    }
    const ArgumentRange range{static_cast<std::uint32_t>(forest_.arguments.size()),
        static_cast<std::uint32_t>(elements.size())};
    Require(elements.size() <= forest_.arguments.capacity() - forest_.arguments.size(), Error::kResourceLimit);
    forest_.arguments.insert(forest_.arguments.end(), elements.begin(), elements.end());
    return Add(ArrayExpr{range, array.buffered}, origin);
  }
  ExprId Call(const analysis::ValueOp& op, const CallBinding& binding, TreeOriginId origin) {
    const bool member = std::holds_alternative<MemberCall>(binding);
    const auto callee = Use(op.first_use, origin);
    const auto spread = member ? std::get<MemberCall>(binding).array :
        (std::holds_alternative<ValueCall>(binding) ? std::get<ValueCall>(binding).array : std::nullopt);
    if (spread) {
      const auto array = Use(analysis::UseId{op.first_use.value + 2}, origin);
      return Add(CallExpr{callee, {}, member ? CallReference::kMember : CallReference::kValue, array}, origin);
    }
    if (const auto* construct = std::get_if<ConstructCall>(&binding); construct && construct->array) {
      if (evaluation_.operations[construct->array->value].kind == EvaluationKind::kRoot) {
        Require(forest_.operations[construct->array->value].has_value(), Error::kOwnership);
        const auto& original = forest_.expressions[forest_.operations[construct->array->value]->value].expression;
        const auto* array = std::get_if<ObjectExpr>(&original);
        Require((array && array->kind == AggregateKind::kArray) || std::holds_alternative<EmptyArrayExpr>(original), Error::kOwnership);
        const auto count = array ? array->properties.count : 0;
        const ArgumentRange arguments{static_cast<std::uint32_t>(forest_.arguments.size()), count};
        for (std::uint32_t i = 0; i < count; ++i) {
          Step(); const auto base = Operand(graph_.uses[op.first_use.value + 1].operand, origin);
          const auto key = Literal(NumberLiteral{std::bit_cast<std::uint64_t>(static_cast<double>(i))}, origin);
          const auto member = Add(MemberExpr{base, ComputedKey{key}}, origin);
          Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
          forest_.arguments.push_back(member);
        }
        return Add(ConstructExpr{callee, arguments}, origin);
      }
      const auto array = Use(analysis::UseId{op.first_use.value + 1}, origin);
      return Add(ConstructExpr{callee, ArrayArguments{array}}, origin);
    }
    // The proved member reference owns its base. Do not create an orphan syntax
    // node for the raw receiver occurrence or evaluate that base a second time.
    constexpr std::uint32_t kValuePrefix = 1;
    constexpr std::uint32_t kMemberPrefix = 2;
    const auto prefix = member ? kMemberPrefix : kValuePrefix;
    const ArgumentRange arguments{static_cast<std::uint32_t>(forest_.arguments.size()), op.use_count - prefix};
    for (std::uint32_t ordinal = prefix; ordinal < op.use_count; ++ordinal) {
      Step();
      Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
      forest_.arguments.push_back(Use(analysis::UseId{op.first_use.value + ordinal}, origin));
    }
    if (std::holds_alternative<ConstructCall>(binding))
      return Add(ConstructExpr{callee, arguments}, origin);
    return Add(CallExpr{callee, arguments, member ? CallReference::kMember : CallReference::kValue}, origin);
  }
  void Step(std::uint64_t count = 1) {
    Require(count <= work_, Error::kWorkLimit);
    work_ -= count;
  }
  void Admit(std::size_t count, std::size_t width) {
    Require(count <= bytes_ / width, Error::kResourceLimit);
    bytes_ -= count * width;
  }
  template <class T> void Reserve(std::vector<T>& values, std::size_t count) {
    Admit(count, sizeof(T));
    values.reserve(count);
    Admit(values.capacity() - count, sizeof(T));
  }
  ExprId Add(TreeExpression expression, TreeOriginId origin) {
    Step();
    Require(forest_.expressions.size() < forest_.expressions.capacity(), Error::kResourceLimit);
    const ExprId id{static_cast<std::uint32_t>(forest_.expressions.size())};
    forest_.expressions.push_back({std::move(expression), origin});
    return id;
  }
  ExprId Literal(TreeLiteral value, TreeOriginId origin) {
    return Add(std::move(value), origin);
  }
  ExprId Combine(ExprId expression, TreeOriginId origin) {
    Require(forest_.origins.size() < forest_.origins.capacity() &&
            forest_.origin_parts.capacity() - forest_.origin_parts.size() >= 2,
            Error::kResourceLimit);
    const auto first = static_cast<std::uint32_t>(forest_.origin_parts.size());
    forest_.origin_parts.push_back(forest_.expressions[expression.value].origin);
    forest_.origin_parts.push_back(origin);
    const TreeOriginId combined{static_cast<std::uint32_t>(forest_.origins.size())};
    forest_.origins.emplace_back(CombinedOrigin{first, 2});
    forest_.expressions[expression.value].origin = combined;
    return expression;
  }
  ExprId RepeatLeaf(ExprId expression) {
    // Predicate expansion may repeat only already evaluated bindings/literals.
    // Clone leaves, not IDs: final executable syntax must remain an owned tree.
    const auto node = forest_.expressions[expression.value];
    Require(std::holds_alternative<TreeLiteral>(node.expression) ||
            std::holds_alternative<SymbolExpr>(node.expression) ||
            std::holds_alternative<ThisExpr>(node.expression), Error::kOwnership);
    return Add(node.expression, node.origin);
  }
  ExprId Operand(const analysis::ValueOperand& operand, TreeOriginId origin) {
    return std::visit([&](const auto& value) -> ExprId {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, analysis::IntegerLiteral>)
        return Literal(IntegerLiteral{value.value}, origin);
      else if constexpr (std::is_same_v<T, analysis::Float64Bits>)
        return Literal(NumberLiteral{value.value}, origin);
      else if constexpr (std::is_same_v<T, analysis::SpecialValue>) {
        using S = analysis::SpecialValue;
        switch (value) {
          case S::kUndefined: return Literal(UndefinedLiteral{}, origin);
          case S::kNull: return Literal(NullLiteral{}, origin);
          case S::kTrue: return Literal(BooleanLiteral{true}, origin);
          case S::kFalse: return Literal(BooleanLiteral{false}, origin);
          case S::kNaN: return Literal(NumberLiteral{std::bit_cast<std::uint64_t>(
              std::numeric_limits<double>::quiet_NaN())}, origin);
          case S::kInfinity: return Literal(NumberLiteral{std::bit_cast<std::uint64_t>(
              std::numeric_limits<double>::infinity())}, origin);
        }
        throw Error::kUnsupportedOperation;
      } else {
        static_assert(std::is_same_v<T, analysis::ValueId>);
        Require(value.value < forest_.bindings.values.size(), Error::kBinding);
        if (!evaluation_.logical_values.empty() && evaluation_.logical_values[value.value]) {
          const auto branch = *evaluation_.logical_values[value.value];
          Require(forest_.operations[branch.value].has_value(), Error::kUnavailableValue);
          return *forest_.operations[branch.value];
        }
        const auto& binding = forest_.bindings.values[value.value];
        if (const auto* symbol = std::get_if<SymbolId>(&binding))
          return Add(SymbolExpr{*symbol}, origin);
        if (std::holds_alternative<NewTargetEntryBinding>(binding)) return Add(NewTargetExpr{owner_}, origin);
        if (std::holds_alternative<NativeReceiverBinding>(binding)) return Add(ThisExpr{owner_}, origin);
        if (std::holds_alternative<ReceiverEntryBinding>(binding)) {
          // The VM derived entry slot contains undefined until super assigns
          // a new SSA result. Reading it is not native JS's throwing `this`.
          // Tree operands recheck this leaf against the actual callable owner.
          if (callable_ == CallableKind::kDerivedConstructor) return Literal(UndefinedLiteral{}, origin);
          return Add(ThisExpr{owner_}, origin);
        }
        const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value.value].origin);
        Require(result && result->op.value < forest_.operations.size() &&
                (evaluation_.operations[result->op.value].kind == EvaluationKind::kInline ||
                 evaluation_.operations[result->op.value].kind == EvaluationKind::kLogicalLeft ||
                 evaluation_.operations[result->op.value].kind == EvaluationKind::kNestedLiteral) &&
                forest_.operations[result->op.value].has_value(), Error::kUnavailableValue);
        return *forest_.operations[result->op.value];
      }
    }, ResolveEvaluationOperand(evaluation_, operand));
  }
  ExprId Use(analysis::UseId use, TreeOriginId origin) {
    if (const auto assignment = AssignmentProducer(evaluation_, use)) {
      Require(forest_.operations[assignment->value].has_value(), Error::kUnavailableValue);
      return *forest_.operations[assignment->value];
    }
    const auto& operand = graph_.uses[use.value].operand;
    const auto resolved = ResolveEvaluationOperand(evaluation_, operand);
    const auto* value = std::get_if<analysis::ValueId>(&resolved);
    const auto* source = value ? std::get_if<analysis::OpResult>(&graph_.values[value->value].origin) : nullptr;
    if (source && evaluation_.operations[source->op.value].kind == EvaluationKind::kPredicateSnapshot &&
        evaluation_.operations[source->op.value].consumer == use) {
      if (std::holds_alternative<NativeReceiverBinding>(forest_.bindings.values[value->value])) {
        Require(forest_.operations[source->op.value].has_value(), Error::kBinding);
        return *forest_.operations[source->op.value];
      }
      const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[value->value]);
      Require(symbol && forest_.operations[source->op.value], Error::kBinding);
      if (IsInvocationBinding(forest_.bindings.symbols[symbol->value]))
        return *forest_.operations[source->op.value];
      return Add(AssignmentExpr{SymbolTarget{*symbol}, *forest_.operations[source->op.value]},
          TreeOriginId{graph_.ops[source->op.value].origin.value});
    }
    return Operand(operand, origin);
  }
  ExprId IntegerOperand(ExprId expression, const analysis::ValueOperand& original) {
    if (!ErasableNaN(original)) return expression;
    auto* literal = std::get_if<TreeLiteral>(&forest_.expressions[expression.value].expression);
    auto* number = literal ? std::get_if<NumberLiteral>(literal) : nullptr;
    Require(number && number->bits == std::get<analysis::Float64Bits>(original).value, Error::kOwnership);
    number->bits = CanonicalNaNBits();
    return expression;
  }
  ExprId Unary(analysis::UnaryOperator operation, ExprId input,
               const analysis::ValueOperand& original, TreeOriginId origin) {
    using U = analysis::UnaryOperator;
    using B = analysis::BinaryOperator;
    const auto unary = [&](TreeUnary kind, ExprId operand) {
      return Add(UnaryExpr{kind, operand}, origin);
    };
    switch (operation) {
      case U::kTemplateObject: throw Error::kUnsupportedOperation;
      case U::kLegacyNegate:
        Require(LegacyNegationEvaluation(graph_, original, arithmetic_), Error::kUnsupportedOperation);
        return unary(TreeUnary::kNegate, input);
      case U::kNegate: return unary(TreeUnary::kNegate, input);
      case U::kBitNot: return unary(TreeUnary::kBitNot, IntegerOperand(input, original));
      case U::kLogicalNot: return unary(TreeUnary::kNot, input);
      case U::kTypeOf: return unary(TreeUnary::kTypeof, input);
      case U::kToNumber: return unary(TreeUnary::kPlus, input);
      case U::kToBoolean:
        // Typed boolean producers already perform this conversion. Preserve
        // their single evaluation and combine origins; do not infer from text.
        return BooleanEvaluation(graph_, original) ? Combine(input, origin)
            : unary(TreeUnary::kNot, unary(TreeUnary::kNot, input));
      case U::kFalseOrZero:
      case U::kTrueOrNonzeroNumber: {
        if (BooleanEvaluation(graph_, original))
          return operation == U::kFalseOrZero ? unary(TreeUnary::kNot, input) : Combine(input, origin);
        const auto repeated = RepeatLeaf(input);
        const auto boolean = Literal(BooleanLiteral{operation == U::kTrueOrNonzeroNumber}, origin);
        const auto left = Add(BinaryExpr{B::kStrictEqual, input, boolean}, origin);
        const auto zero = Literal(IntegerLiteral{0}, origin);
        if (operation == U::kFalseOrZero) {
          const auto right = Add(BinaryExpr{B::kStrictEqual, repeated, zero}, origin);
          return Add(LogicalExpr{TreeLogical::kOr, left, right}, origin);
        }
        Require(constants_.number_type_text && constants_.number_type_text->value < constants_.text_count,
                Error::kConstant);
        const auto third = RepeatLeaf(input);
        const auto type = unary(TreeUnary::kTypeof, repeated);
        const auto number = Literal(*constants_.number_type_text, origin);
        const auto is_number = Add(BinaryExpr{B::kStrictEqual, type, number}, origin);
        const auto nonzero = Add(BinaryExpr{B::kStrictNotEqual, third, zero}, origin);
        const auto right = Add(LogicalExpr{TreeLogical::kAnd, is_number, nonzero}, origin);
        return Add(LogicalExpr{TreeLogical::kOr, left, right}, origin);
      }
      case U::kToNumeric:
        if (NumberEvaluation(graph_, original)) return Combine(input, origin);
        // General conversion is handled by private postfix storage above.
        // Identity must still follow an input fact, never the later consumer.
        throw Error::kUnsupportedOperation;
      case U::kIncrement: case U::kDecrement:
        throw Error::kUnsupportedOperation;
    }
    throw Error::kUnsupportedOperation;
  }
  SymbolId CellSymbol(analysis::OpId op) {
    Require(evaluation_.cells.has_value() && op.value < evaluation_.cells->cells.operations.size(), Error::kBinding);
    const auto cell = evaluation_.cells->cells.operations[op.value];
    Require(cell && cell->value < forest_.bindings.cells.size(), Error::kBinding);
    return forest_.bindings.cells[cell->value];
  }
  ExprId Rest(const analysis::ValueOp& op,TreeOriginId origin) {
    const auto& rest=std::get<analysis::ValueObjectRest>(op.operation);
    Require(rest.keys<std::numeric_limits<std::uint32_t>::max()&&op.use_count==rest.keys+1,Error::kOwnership);
    const auto source=Use(op.first_use,origin);
    // Operand subtrees may own their own argument ranges. Collect child IDs
    // before appending this window; never retain a range across nested growth.
    std::vector<ExprId> keys;Reserve(keys,rest.keys);
    for(std::uint32_t k=0;k<rest.keys;++k){Step();keys.push_back(Use({op.first_use.value+k+1},origin));}
    Require(keys.size()<=forest_.arguments.capacity()-forest_.arguments.size(),Error::kResourceLimit);
    const ArgumentRange range{static_cast<std::uint32_t>(forest_.arguments.size()),rest.keys};
    for(auto key:keys){Step();forest_.arguments.push_back(key);}
    return Add(ObjectRestExpr{source,range},origin);
  }
  std::optional<ExprId> Lower(analysis::OpId id, const analysis::ValueOp& op,
      const std::array<ExprId, kFixedExpressionOperands>& operands, TreeOriginId origin,
      const ExpressionConstant& constant) {
    return std::visit([&](const auto& operation) -> std::optional<ExprId> {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, analysis::ValueCopy>) {
        if (id.value < evaluation_.phase_transports.size() && evaluation_.phase_transports[id.value]) {
          // A logical projection owns its pure truth/branch sites at one root.
          // A conditional retains its separate scalar predicate at the branch
          // site; only its selected value root receives the transport origin.
          if (std::holds_alternative<LogicalExpr>(forest_.expressions[operands[0].value].expression))
            return Combine(operands[0], origin);
          Require(std::holds_alternative<ConditionalExpr>(forest_.expressions[operands[0].value].expression), Error::kOwnership);
          forest_.expressions[operands[0].value].origin = origin;
          return operands[0];
        }
        const bool normalize = !evaluation_.integer_nans.empty() && evaluation_.integer_nans[id.value] == NaNProjection::kIntegerCoercion;
        return Combine(normalize ? IntegerOperand(operands[0], graph_.uses[op.first_use.value].operand) : operands[0], origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueFrameFunction>) {
        Require(forest_.bindings.self.has_value(), Error::kBinding);
        return Add(SymbolExpr{*forest_.bindings.self}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueRestArguments>) {
        Require(operation.first_argument < forest_.bindings.parameter_count && op.use_count == 0 && op.result, Error::kBinding);
        return Add(SymbolExpr{SymbolId{operation.first_argument}}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueLexicalLoad>)
        return Add(SymbolExpr{CellSymbol(id)}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>) {
        // A deferred first store is the binding's declaration, not a write.
        const auto& cells = evaluation_.cells;
        const auto cell = cells && id.value < cells->cells.operations.size() ? cells->cells.operations[id.value] : std::nullopt;
        if (cell && cell->value < cells->deferred.size() && cells->deferred[cell->value] == id) {
          forest_.initializations[id.value] = CellSymbol(id);
          return operands[0];
        }
        return Add(AssignmentExpr{SymbolTarget{CellSymbol(id)}, operands[0]}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueUnary>) {
        using U = analysis::UnaryOperator;
        if (NeedsNumericScratch(graph_, op)) {
          Require(id.value < forest_.bindings.updates.size() && forest_.bindings.updates[id.value], Error::kBinding);
          const auto symbol = *forest_.bindings.updates[id.value];
          // Ark publishes acc only on success and leaves the input register
          // unchanged. The comma owns one input evaluation and a native update
          // on this operation's private storage (its own result snapshot or
          // exclusive scratch); no helper call, user store or x +/- 1.
          const auto save = Add(AssignmentExpr{SymbolTarget{symbol}, operands[0]}, origin);
          // ToNumeric returns the postfix old value; only compiler scratch
          // receives the extra arithmetic result. Publication gates resources.
          const auto update = Add(UpdateExpr{symbol, operation.op == U::kDecrement
              ? UpdateDirection::kDecrement : UpdateDirection::kIncrement,
              operation.op == U::kToNumeric ? UpdateResult::kPostfix : UpdateResult::kPrefix}, origin);
          return Add(SequenceExpr{save, update}, origin);
        }
        return Unary(operation.op, operands[0], graph_.uses[op.first_use.value].operand, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueAwait>)
        return Add(AwaitExpr{operands[0]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueYield>)
        return Add(YieldExpr{operands[0], TreeYieldForm(operation.origin.form)}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueBranch>) {
        const auto& input = graph_.uses[op.first_use.value].operand;
        const bool reverse = !polarity_.empty() && polarity_[id.value] == BranchPolarity::kFallthrough;
        using U = analysis::UnaryOperator;
        if (reverse && BooleanEvaluation(graph_, input)) {
          Require(operation.predicate == U::kFalseOrZero || operation.predicate == U::kTrueOrNonzeroNumber, Error::kOwnership);
          return Unary(operation.predicate == U::kFalseOrZero ? U::kTrueOrNonzeroNumber : U::kFalseOrZero, operands[0], input, origin);
        }
        const auto predicate = Unary(operation.predicate, operands[0], input, origin);
        // VM zero/true predicates are not complements over arbitrary JS values.
        // Negate the proven original test when the input is not boolean.
        return reverse ? Add(UnaryExpr{TreeUnary::kNot, predicate}, origin) : predicate;
      }
      else if constexpr (std::is_same_v<T, analysis::ValueBinary>) {
        const bool integer = IntegerCoercion(op.operation);
        const auto left = integer ? IntegerOperand(operands[0], graph_.uses[op.first_use.value].operand) : operands[0];
        const auto right = integer ? IntegerOperand(operands[1], graph_.uses[op.first_use.value + 1].operand) : operands[1];
        return Add(BinaryExpr{HoleSourceOperator(evaluation_.holes ? &*evaluation_.holes : nullptr, id, operation.op), left, right}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueHole>) {
        Require(evaluation_.holes && forest_.bindings.hole, Error::kBinding);
        return Add(SymbolExpr{*forest_.bindings.hole}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueNamedRead> ||
                         std::is_same_v<T, analysis::ValueNamedWrite>) {
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        if constexpr (std::is_same_v<T, analysis::ValueNamedRead>)
          return Add(MemberExpr{operands[0], *name, operation.lookup}, origin);
        else
          return Add(AssignmentExpr{MemberTarget{operands[0], *name}, operands[1], operation.lookup}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>)
        return Add(MemberExpr{operands[0], ComputedKey{operands[1]}, operation.lookup}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValuePropertyDelete>)
        return Add(DeleteExpr{operands[0], ComputedKey{operands[1]}}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValuePropertyWrite>)
        return Add(AssignmentExpr{MemberTarget{operands[0], ComputedKey{operands[1]}}, operands[2], operation.lookup}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueString>) {
        const auto* text = std::get_if<TextId>(&constant);
        Require(text && text->value < constants_.text_count, Error::kConstant);
        if(operation.kind==analysis::PoolTextKind::kRegExp)return Add(RegExpExpr{*text,operation.flags},origin);
        return operation.kind == analysis::PoolTextKind::kBigInt ? Add(BigIntExpr{*text}, origin) : Literal(*text, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueGlobalRead>) {
        Require(operation.lookup == analysis::GlobalLookup::kName ||
            GlobalScripts(evaluation_.call_spread_contract), Error::kUnsupportedOperation);
        // Actual global scope is separately checked by the script certificate.
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        return Add(GlobalNameExpr{*name, {}, operation.lookup == analysis::GlobalLookup::kName ? analysis::GlobalOp::kReadName : analysis::GlobalOp::kReadVar}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueGlobalWrite>) {
        Require(GlobalScripts(evaluation_.call_spread_contract), Error::kUnsupportedOperation);
        // Scheduling a global commit does not establish its script scope.
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        return Add(GlobalNameExpr{*name, operands[0], operation.operation}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueThrow>) {
        return operation.kind==analysis::ThrowKind::kPatternNonCoercible ? Add(PatternFailureExpr{},origin) : operands[0];
      } else if constexpr (std::is_same_v<T, analysis::ValueObjectRest>) {
        throw Error::kOwnership; // Variadic Rest owns a separately admitted window.
      } else if constexpr (std::is_same_v<T, analysis::ValueReturn>)return operands[0];
      else if constexpr (std::is_same_v<T, analysis::ValueJump> ||
                         std::is_same_v<T, analysis::ValueConcurrentNotification> || std::is_same_v<T, analysis::ValueDebugger>)
        return {};
      else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation>) {
        if (operation.literal) {
          const auto properties = BufferedProperties(constant, origin);
          return Add(ObjectExpr{properties, AggregateKind::kArray, properties.count}, origin);
        }
        return Add(EmptyArrayExpr{}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueThisProperty>) {
        const auto base=Add(ThisExpr{owner_},origin);
        MemberKey key;
        if(operation.name){const auto* name=std::get_if<NameId>(&constant);Require(name,Error::kConstant);key=*name;}
        else key=ComputedKey{operands[0]};
        if(analysis::WritesThisProperty(operation.kind))return Add(AssignmentExpr{MemberTarget{base,key},operands[operation.name?0:1]},origin);
        return Add(MemberExpr{base,key},origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueDynamicImport>) return Add(DynamicImportExpr{operands[0]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueNewTarget>) return Add(NewTargetExpr{owner_, NewTargetForm::kInstruction}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>)
        return Add(ThisExpr{owner_}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueIterResult>) {
        const auto* keys = std::get_if<IterResultConstant>(&constant);
        Require(keys && keys->value.value < constants_.name_count &&
            keys->done.value < constants_.name_count && keys->value != keys->done, Error::kConstant);
        const auto done = BooleanEvaluation(graph_, ResolveEvaluationOperand(evaluation_, graph_.uses[op.first_use.value + 1].operand))
            ? operands[1] : Add(BinaryExpr{analysis::BinaryOperator::kStrictEqual, operands[1],
                Literal(BooleanLiteral{true}, origin)}, origin);
        const PropertyRange properties{static_cast<std::uint32_t>(forest_.properties.size()), IterResultConstant::kProperties};
        forest_.properties.push_back({keys->value, operands[0], origin});
        forest_.properties.push_back({keys->done, done, origin});
        return Add(ObjectExpr{properties}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueGlobalObject>)
        return Add(RealmObjectExpr{}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueSymbolConstructor>)
        return Add(SymbolConstructorExpr{}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueObjectAllocation>) {
        if (operation.literal) {
          const auto properties = BufferedProperties(constant, origin);
          return Add(ObjectExpr{properties, AggregateKind::kObject, properties.count}, origin);
        }
        return Add(EmptyObjectExpr{}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueFunctionCreation>) {
        // Literal dependencies are constructed before ordinary roots. Binding
        // identity follows the original creation, never that AST build order.
        // Evaluation verification established the sorted, unique creation map.
        const auto found = std::lower_bound(evaluation_.functions.begin(), evaluation_.functions.end(), id,
            [&](const FunctionBinding& binding, analysis::OpId creation) {
              Step(); return binding.creation < creation;
            });
        Require(found != evaluation_.functions.end() && found->creation == id && found->child != owner_, Error::kOwnership);
        ++built_functions_;
        const auto& function = *found;
        const CaptureRange captures{static_cast<std::uint32_t>(forest_.closure_captures.size()),
            static_cast<std::uint32_t>(function.captures.size())};
        for (const auto cell : function.captures) {
          Step(); Require(cell.value < forest_.bindings.cells.size(), Error::kBinding);
          forest_.closure_captures.push_back(forest_.bindings.cells[cell.value]);
        }
        return Add(FunctionExpr{function.child, captures}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueOwnWrite>) {
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        if (IsStagedLiteral(evaluation_.literals ? &*evaluation_.literals : nullptr, id))
          return Add(AssignmentExpr{MemberTarget{operands[0], *name}, operands[1]}, origin);
        Require(IsReceiverField(graph_, op), Error::kUnsupportedOperation);
        return Add(FieldDefinitionExpr{operands[0], *name, operands[1]}, origin);
      }
      else {
        static_assert(std::is_same_v<T, analysis::ValueCopyProperties> || std::is_same_v<T, analysis::ValueIteratorAcquire> || std::is_same_v<T, analysis::ValueObjectCheck> || std::is_same_v<T, analysis::ValueEnumeration> || std::is_same_v<T, analysis::ValueArraySpread> || std::is_same_v<T, analysis::ValueComputedOwnWrite> || std::is_same_v<T, analysis::ValueClassCreation> ||
                      std::is_same_v<T, analysis::ValueArgumentSnapshot> ||
                      std::is_same_v<T, analysis::ValueGuard> || std::is_same_v<T, analysis::ValueHole> ||
                      std::is_same_v<T, analysis::ValueModuleRead> ||
                      std::is_same_v<T, analysis::ValueModuleWrite> ||
                      std::is_same_v<T, analysis::ValueIndexedWrite> ||
                      std::is_same_v<T, analysis::ValueDefineAccessor> ||
                      std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                      std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                      std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueSuperCall> || std::is_same_v<T, analysis::ValueSuperSpread> || std::is_same_v<T, analysis::ValueSuperCheck> || std::is_same_v<T, analysis::ValueConstruct> || std::is_same_v<T, analysis::ValueConstructApply> ||
                      std::is_same_v<T, analysis::ValueNop>);
        throw Error::kUnsupportedOperation;
      }
    }, op.operation);
  }

  const ForOfEvaluationInput* iteration_input_;
  const ForOfEmissionView* iteration_view_;
  const ForInEmissionView* enumeration_;
  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  FunctionOwnerId owner_;
  ExpressionConstants constants_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  ExpressionForest forest_;
  std::vector<std::optional<std::size_t>> objects_;
  std::size_t built_functions_ = 0;
  std::span<const ModuleSymbol> modules_;
  std::vector<std::size_t> module_order_;
  std::size_t next_module_ = 0;
  RecoveryMode recovery_;
  std::span<const std::u16string> names_;
  std::vector<std::optional<std::size_t>> selection_indices_;
  std::vector<std::size_t> selection_tasks_;
  std::span<const SelectionRegion> logicals_;
  std::vector<std::uint8_t> selection_built_;
  std::span<const BranchPolarity> polarity_;
  CallableKind callable_;
  analysis::ArithmeticSemantics arithmetic_;
  ConstructorSemantics constructors_;
};
std::variant<ExpressionForest, ExpressionFailure> BuildForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner,
    ExpressionConstants constants, analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules,
    EvaluationFlow flow, RecoveryMode recovery, std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity,
    CallableKind callable, analysis::ArithmeticSemantics arithmetic, ConstructorSemantics constructors, ParameterForm parameters, const ForInEmissionView* enumeration, const ForOfEvaluationInput* iteration, const ForOfEmissionView* iteration_view) {
  auto bindings = flow == EvaluationFlow::kNormal ? BuildBindingPlan(graph, evaluation, parameter_count, budget, parameters, enumeration, iteration)
      : BuildPhasedBindingPlan(graph, evaluation, parameter_count, budget, parameters, enumeration, iteration);
  if (const auto* error = std::get_if<BindingError>(&bindings)) {
    if (*error == BindingError::kResourceLimit) return Error::kResourceLimit;
    if (*error == BindingError::kWorkLimit) return Error::kWorkLimit;
    return Error::kBinding;
  }
  try {
    return ForestBuilder(graph, evaluation, owner, constants, budget,
                         std::get<BindingPlan>(std::move(bindings)), modules, recovery, names, logicals, polarity, callable, arithmetic, constructors, enumeration, iteration_view, iteration).Build();
  } catch (Error error) { return error;
  } catch (const ExpressionFailure& failure) { return failure;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<ExpressionForest, ExpressionFailure> BuildExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery,
    std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity, CallableKind callable, analysis::ArithmeticSemantics arithmetic, ConstructorSemantics constructors, ParameterForm parameters, const ForInEmissionView* enumeration, const ForOfEvaluationInput* iteration, const ForOfEmissionView* iteration_view) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kNormal, recovery, names, logicals, polarity, callable, arithmetic, constructors, parameters, enumeration, iteration, iteration_view);
}
std::variant<ExpressionForest, ExpressionFailure> BuildPhasedExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery,
    std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity, CallableKind callable, analysis::ArithmeticSemantics arithmetic, ConstructorSemantics constructors, ParameterForm parameters, const ForInEmissionView* enumeration, const ForOfEvaluationInput* iteration, const ForOfEmissionView* iteration_view) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kPhased, recovery, names, logicals, polarity, callable, arithmetic, constructors, parameters, enumeration, iteration, iteration_view);
}
}  // namespace abcd::source
