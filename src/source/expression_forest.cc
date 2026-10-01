#include "staged_literal_names.h"
#include "expression_forest.h"
#include "expression_copies.h"
#include "class_tree.h"

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
                std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity)
      : graph_(graph), evaluation_(evaluation), owner_(owner), constants_(constants),
        budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules), recovery_(recovery), names_(names), logicals_(logicals), polarity_(polarity) {
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
      Reserve(logical_joins, graph_.ops.size()); logical_joins.resize(graph_.ops.size());
      Reserve(predicates, graph_.ops.size()); predicates.resize(graph_.ops.size());
      Reserve(selected_edges, graph_.edges.size()); selected_edges.resize(graph_.edges.size());
      for (const auto& region : logicals_) {
        Step();
        Require(evaluation_.flow == EvaluationFlow::kNormal &&
            region.boolean.value < graph_.ops.size() && region.branch.value < graph_.ops.size() &&
            region.boolean != region.branch && region.left.value < graph_.values.size() &&
            region.merged.value < graph_.values.size() &&
            (region.operation == TreeLogical::kAnd || region.operation == TreeLogical::kOr), Error::kOwnership);
        const auto* right = std::get_if<analysis::ValueId>(&region.right);
        const auto* child = std::get_if<SelectionChild>(&region.right);
        Require(((right && right->value < graph_.values.size() &&
            std::holds_alternative<analysis::OpResult>(graph_.values[right->value].origin)) ||
            (child && child->branch.value < graph_.ops.size())) &&
            !predicates[region.boolean.value] && !predicates[region.branch.value], Error::kOwnership);
        for (const auto incoming : {region.keep_input, region.evaluate_input}) {
          if (!incoming) continue;
          Require(incoming->value < selected_edges.size() && !selected_edges[incoming->value], Error::kOwnership);
          selected_edges[incoming->value] = 1;
        }
        if (evaluation_.operations[region.branch.value].kind == EvaluationKind::kLogicalValue) {
          const auto join = graph_.blocks[region.join.value].first_op.value;
          Require(join < graph_.ops.size() && !logical_joins[join], Error::kOwnership);
          logical_joins[join] = static_cast<std::size_t>(&region - logicals_.data());
        }
        selection_indices_[region.branch.value] = static_cast<std::size_t>(&region - logicals_.data());
        if (!region.consequent) predicates[region.boolean.value] = 1;
        predicates[region.branch.value] = 1;
      }
    }
    Require(constants_.operations.size() == graph_.ops.size(), Error::kConstant);
    Require(evaluation_.constructor_guards.size() == constants_.constructor_guards.size() &&
        std::equal(evaluation_.constructor_guards.begin(), evaluation_.constructor_guards.end(), constants_.constructor_guards.begin()), Error::kOwnership);
    Admit(1, sizeof(ExpressionForest));
    auto calls = forest_.bindings.flow == EvaluationFlow::kNormal
        ? BuildCallPlan(graph_, evaluation_, {bytes_, work_})
        : BuildPhasedCallPlan(graph_, evaluation_, {bytes_, work_});
    auto* call_plan = std::get_if<CallPlan>(&calls);
    if (!call_plan) {
      const auto error = std::get<CallPlanError>(calls);
      if (error == CallPlanError::kResourceLimit) throw Error::kResourceLimit;
      if (error == CallPlanError::kWorkLimit) throw Error::kWorkLimit;
      throw Error::kUnsupportedOperation;
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
    const auto expression_nodes = operation_nodes + copies * kNodesPerCopy;
    Require(buffered_properties <= kMaxId - expression_nodes, Error::kResourceLimit);
    Reserve(forest_.expressions, expression_nodes + buffered_properties);
    Reserve(forest_.arguments, graph_.uses.size());
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
    Reserve(forest_.module_initializations, graph_.ops.size());
    forest_.module_initializations.resize(graph_.ops.size());
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
    // Read/call dependencies point backward; lower them in value order
    // before assembling forward-owned nested aggregate/property expressions.
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step(); if (evaluation_.operations[i].kind != EvaluationKind::kNestedLiteral) continue;
      const auto& op = graph_.ops[i];
      if (std::holds_alternative<analysis::ValueArrayAllocation>(op.operation) ||
          std::holds_alternative<analysis::ValueObjectAllocation>(op.operation)) continue;
      const TreeOriginId origin{op.origin.value};
      if (!std::holds_alternative<NoCall>(forest_.calls.operations[i]))
        forest_.operations[i] = Call(op, forest_.calls.operations[i], origin);
      else if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) Module(i, op, origin);
      else {
        std::array<ExprId, kFixedExpressionOperands> operands{};
        Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
        for (std::uint32_t j = 0; j < op.use_count; ++j) {
          Step(); operands[j] = Use(analysis::UseId{op.first_use.value + j}, origin);
        }
        forest_.operations[i] = Lower({static_cast<std::uint32_t>(i)}, op, operands, origin, constants_.operations[i]);
      }
    }
    // Nested allocation edges point forward in the original operation order.
    // Reverse construction is therefore iterative postorder, without recursion
    // or changing the evaluation order of the resulting native literal.
    for (std::size_t i = graph_.ops.size(); i-- > 0;) {
      Step(); if (evaluation_.operations[i].kind != EvaluationKind::kNestedLiteral) continue;
      const auto& op = graph_.ops[i]; const TreeOriginId origin{op.origin.value};
      if (forest_.operations[i]) continue;
      if (objects_[i]) forest_.operations[i] = Object(evaluation_.literals->objects[*objects_[i]], origin);
      else forest_.operations[i] = Lower({static_cast<std::uint32_t>(i)}, op, {}, origin, constants_.operations[i]);
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
        // Primitive phi leaves may be built before their storage-array position
        // when a selected join needs them. They remain in the original arm.
        if (forest_.operations[i] && evaluation_.operations[i].kind == EvaluationKind::kPhiInput &&
            IsSelectionConstant(graph_, {static_cast<std::uint32_t>(i)})) continue;
        if (op.life != analysis::ValueLife::kLive || (!predicates.empty() && predicates[i])) continue;
        Require(op.origin.value < graph_.origins.size() &&
                    graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained,
                Error::kOrigin);
        const TreeOriginId origin{op.origin.value};
        if (std::binary_search(constants_.constructor_guards.begin(), constants_.constructor_guards.end(),
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
          const auto base = std::get<analysis::ValueId>(graph_.uses[op.first_use.value].operand);
          const auto allocation = std::get<analysis::OpResult>(graph_.values[base.value].origin).op;
          Require(objects_[allocation.value].has_value(), Error::kOwnership);
          if (evaluation_.logical_objects[allocation.value] != analysis::OpId{static_cast<std::uint32_t>(i)}) continue;
          forest_.operations[allocation.value] = Object(evaluation_.literals->objects[*objects_[allocation.value]],
              TreeOriginId{graph_.ops[allocation.value].origin.value});
          continue;
        }
        if (evaluation_.operations[i].kind == EvaluationKind::kInitializer ||
            evaluation_.operations[i].kind == EvaluationKind::kNestedLiteral) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kEnvironment ||
            evaluation_.operations[i].kind == EvaluationKind::kClassProjection) continue;
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
          const auto value = Operand(graph_.uses[op.first_use.value].operand, origin);
          if (binding->initialize) {
            forest_.module_initializations[i] = symbol;
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
          Require(op.use_count == call->arguments && op.result, Error::kOwnership);
          const ArgumentRange arguments{static_cast<std::uint32_t>(forest_.arguments.size()), op.use_count};
          for (std::uint32_t j = 0; j < op.use_count; ++j) {
            Step();
            Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
            forest_.arguments.push_back(Operand(graph_.uses[op.first_use.value + j].operand, origin));
          }
          forest_.operations[i] = Add(SuperCallExpr{arguments}, origin);
          continue;
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
        if (const auto* creation = std::get_if<analysis::ValueClassCreation>(&op.operation)) {
          const auto* definition = std::get_if<std::shared_ptr<const ClassTree>>(&constants_.operations[i]);
          if (!definition || !*definition) throw Error::kUnsupportedOperation;
          Require(op.use_count == 1 && evaluation_.flow == EvaluationFlow::kNormal, Error::kOwnership);
          const auto* heritage = std::get_if<analysis::ValueId>(&graph_.uses[op.first_use.value].operand);
          const bool absent = heritage && graph_.values[heritage->value].domain == analysis::ValueKinds::Hole();
          const auto& family = (*definition)->constructor.Family();
          Require(family.members[family.root.value].tree.callable ==
              (absent ? CallableKind::kBaseConstructor : CallableKind::kDerivedConstructor), Error::kOwnership);
          const auto& site = (*definition)->creation;
          Require(site.creation.constructor == creation->constructor && site.creation.literal == creation->literal &&
              site.creation.length == creation->length && site.creation.cache_slot == creation->cache_slot &&
              site.pc == graph_.origins[op.origin.value].pc,
              Error::kConstant);
          forest_.operations[i] = Add(ClassExpr{*definition, absent ? std::nullopt :
              std::optional{Operand(graph_.uses[op.first_use.value].operand, origin)}}, origin);
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
    Require(next_function_ == evaluation_.functions.size(), Error::kOwnership);
    Require(next_module_ == evaluation_.modules.size(), Error::kOwnership);
    for (const auto& region : logicals_) {
      Step(); if (evaluation_.operations[region.branch.value].kind != EvaluationKind::kLogicalValue) LogicalTree(region);
    }
    const auto copied = AppendCopyExpressions(graph_, evaluation_, forest_, owner_, work_, selected_edges);
    if (const auto* error = std::get_if<Error>(&copied)) throw *error;
    Step(std::get<std::uint64_t>(copied));
    forest_.allocated_bytes = budget_.bytes - bytes_;
    forest_.work = budget_.work - work_;
    return std::move(forest_);
  }

 private:
  void LogicalTree(const SelectionRegion& root) {
    if (forest_.operations[root.branch.value]) return;
    selection_tasks_.push_back(static_cast<std::size_t>(&root - logicals_.data()));
    while (!selection_tasks_.empty()) {
      Step(); const auto index = selection_tasks_.back(); const auto& region = logicals_[index];
      std::optional<std::size_t> pending;
      const auto child = [&](const SelectionOperand& input) {
        const auto* nested = std::get_if<SelectionChild>(&input);
        if (!nested) return;
        Require(nested->branch.value < selection_indices_.size() && selection_indices_[nested->branch.value], Error::kOwnership);
        const auto dependency = *selection_indices_[nested->branch.value];
        Require(dependency < index && logicals_[dependency].parent == region.branch, Error::kOwnership);
        if (!forest_.operations[nested->branch.value]) pending = dependency;
      };
      child(region.right); if (region.consequent) child(*region.consequent);
      if (pending) { selection_tasks_.push_back(*pending); continue; }
      Logical(region); selection_tasks_.pop_back();
    }
  }
  void Logical(const SelectionRegion& region) {
    Step();
    const TreeOriginId boolean_origin{graph_.ops[region.boolean.value].origin.value};
    const TreeOriginId branch_origin{graph_.ops[region.branch.value].origin.value};
    const auto left = Operand(region.left, boolean_origin);
    const auto operand = [&](const SelectionOperand& input) {
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
    const auto right = operand(region.right);
    const auto expression = region.consequent
        ? Add(ConditionalExpr{left, operand(*region.consequent), right}, branch_origin)
        : Combine(Add(LogicalExpr{region.operation, left, right}, boolean_origin), branch_origin);
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
    const auto expression =
        Combine(Add(SymbolExpr{symbol}, origin), {graph_.ops[module.pair.guard.value].origin.value});
    forest_.operations[i] = expression;
    forest_.operations[module.pair.guard.value] = expression;
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
      Require(name && name->value < constants_.name_count, Error::kConstant);
      std::size_t destination = writes.size();
      if (array) {
        const auto* indexed = std::get_if<analysis::ValueIndexedWrite>(&graph_.ops[id.value].operation);
        Require(indexed && indexed->index >= next && indexed->index <= writes.size(), Error::kConstant);
        destination = indexed->index;
      } else if (!order.empty()) {
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
      const auto name = std::get<NameId>(constants_.operations[id.value]);
      const TreeOriginId field_origin{op.origin.value};
      const auto value = Operand(graph_.uses[op.first_use.value + 1].operand, field_origin);
      Require(forest_.properties.size() < forest_.properties.capacity(), Error::kResourceLimit);
      const PropertyId property{static_cast<std::uint32_t>(forest_.properties.size())};
      forest_.properties.push_back({name, value, field_origin});
      forest_.initializers[id.value] = property; forest_.operations[id.value] = value;
    }
    return Add(ObjectExpr{range, kind, prefix}, origin);
  }
  ExprId Call(const analysis::ValueOp& op, const CallBinding& binding, TreeOriginId origin) {
    const bool member = std::holds_alternative<MemberCall>(binding);
    const auto callee = Operand(graph_.uses[op.first_use.value].operand, origin);
    // The proved member reference owns its base. Do not create an orphan syntax
    // node for the raw receiver occurrence or evaluate that base a second time.
    constexpr std::uint32_t kValuePrefix = 1;
    constexpr std::uint32_t kMemberPrefix = 2;
    const auto prefix = member ? kMemberPrefix : kValuePrefix;
    const ArgumentRange arguments{static_cast<std::uint32_t>(forest_.arguments.size()), op.use_count - prefix};
    for (std::uint32_t ordinal = prefix; ordinal < op.use_count; ++ordinal) {
      Step();
      Require(forest_.arguments.size() < forest_.arguments.capacity(), Error::kResourceLimit);
      forest_.arguments.push_back(Operand(graph_.uses[op.first_use.value + ordinal].operand, origin));
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
        if (std::holds_alternative<ReceiverEntryBinding>(binding))
          return Add(ThisExpr{owner_}, origin);
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
    const auto& operand = graph_.uses[use.value].operand;
    const auto* value = std::get_if<analysis::ValueId>(&operand);
    const auto* source = value ? std::get_if<analysis::OpResult>(&graph_.values[value->value].origin) : nullptr;
    if (source && evaluation_.operations[source->op.value].kind == EvaluationKind::kPredicateSnapshot &&
        evaluation_.operations[source->op.value].consumer == use) {
      const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[value->value]);
      Require(symbol && forest_.operations[source->op.value], Error::kBinding);
      return Add(AssignmentExpr{SymbolTarget{*symbol}, *forest_.operations[source->op.value]},
          TreeOriginId{graph_.ops[source->op.value].origin.value});
    }
    return Operand(operand, origin);
  }
  ExprId Unary(analysis::UnaryOperator operation, ExprId input,
               const analysis::ValueOperand& original, TreeOriginId origin) {
    using U = analysis::UnaryOperator;
    using B = analysis::BinaryOperator;
    const auto unary = [&](TreeUnary kind, ExprId operand) {
      return Add(UnaryExpr{kind, operand}, origin);
    };
    switch (operation) {
      case U::kNegate: return unary(TreeUnary::kNegate, input);
      case U::kBitNot: return unary(TreeUnary::kBitNot, input);
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
      // ++/-- require a Number-or-BigInt preserving lowering, not x +/- 1.
      case U::kToNumeric: case U::kIncrement: case U::kDecrement:
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
  std::optional<ExprId> Lower(analysis::OpId id, const analysis::ValueOp& op,
      const std::array<ExprId, kFixedExpressionOperands>& operands, TreeOriginId origin,
      const ExpressionConstant& constant) {
    return std::visit([&](const auto& operation) -> std::optional<ExprId> {
      using T = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<T, analysis::ValueCopy>)
        return Combine(operands[0], origin);
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
      else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>)
        return Add(AssignmentExpr{SymbolTarget{CellSymbol(id)}, operands[0]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueUnary>)
        return Unary(operation.op, operands[0], graph_.uses[op.first_use.value].operand, origin);
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
      else if constexpr (std::is_same_v<T, analysis::ValueBinary>)
        return Add(BinaryExpr{HoleSourceOperator(evaluation_.holes ? &*evaluation_.holes : nullptr, id, operation.op), operands[0], operands[1]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueHole>) {
        Require(evaluation_.holes && forest_.bindings.hole, Error::kBinding);
        return Add(SymbolExpr{*forest_.bindings.hole}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueNamedRead> ||
                         std::is_same_v<T, analysis::ValueNamedWrite>) {
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        if constexpr (std::is_same_v<T, analysis::ValueNamedRead>)
          return Add(MemberExpr{operands[0], *name}, origin);
        else
          return Add(AssignmentExpr{MemberTarget{operands[0], *name}, operands[1]}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValuePropertyRead>)
        return Add(MemberExpr{operands[0], ComputedKey{operands[1]}}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValuePropertyWrite>)
        return Add(AssignmentExpr{MemberTarget{operands[0], ComputedKey{operands[1]}}, operands[2]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueString>) {
        const auto* text = std::get_if<TextId>(&constant);
        Require(text && text->value < constants_.text_count, Error::kConstant);
        return Literal(*text, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueGlobalRead>) {
        Require(operation.lookup == analysis::GlobalLookup::kName, Error::kUnsupportedOperation);
        const auto* name = std::get_if<NameId>(&constant);
        Require(name && name->value < constants_.name_count, Error::kConstant);
        return Add(GlobalNameExpr{*name}, origin);
      } else if constexpr (std::is_same_v<T, analysis::ValueReturn> ||
                           std::is_same_v<T, analysis::ValueThrow>)
        return operands[0];
      else if constexpr (std::is_same_v<T, analysis::ValueJump> ||
                         std::is_same_v<T, analysis::ValueDebugger>)
        return {};
      else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation>) {
        if (operation.literal) {
          const auto properties = BufferedProperties(constant, origin);
          return Add(ObjectExpr{properties, AggregateKind::kArray, properties.count}, origin);
        }
        return Add(EmptyArrayExpr{}, origin);
      }
      else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>)
        return Add(ThisExpr{owner_}, origin);
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
        Require(next_function_ < evaluation_.functions.size(), Error::kOwnership);
        const auto& function = evaluation_.functions[next_function_++];
        Require(function.creation == id && function.child != owner_, Error::kOwnership);
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
        static_assert(std::is_same_v<T, analysis::ValueClassCreation> ||
                      std::is_same_v<T, analysis::ValueArgumentSnapshot> ||
                      std::is_same_v<T, analysis::ValueGuard> || std::is_same_v<T, analysis::ValueHole> ||
                      std::is_same_v<T, analysis::ValueModuleRead> ||
                      std::is_same_v<T, analysis::ValueModuleWrite> ||
                      std::is_same_v<T, analysis::ValueIndexedWrite> ||
                      std::is_same_v<T, analysis::ValueDefineAccessor> ||
                      std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                      std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                      std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueSuperCall> || std::is_same_v<T, analysis::ValueSuperSpread> || std::is_same_v<T, analysis::ValueSuperCheck> || std::is_same_v<T, analysis::ValueConstruct> ||
                      std::is_same_v<T, analysis::ValueNop>);
        throw Error::kUnsupportedOperation;
      }
    }, op.operation);
  }

  const analysis::ValueProgram& graph_;
  const EvaluationPlan& evaluation_;
  FunctionOwnerId owner_;
  ExpressionConstants constants_;
  analysis::SemanticBudget budget_;
  std::size_t bytes_;
  std::uint64_t work_;
  ExpressionForest forest_;
  std::vector<std::optional<std::size_t>> objects_;
  std::size_t next_function_ = 0;
  std::span<const ModuleSymbol> modules_;
  std::vector<std::size_t> module_order_;
  std::size_t next_module_ = 0;
  RecoveryMode recovery_;
  std::span<const std::u16string> names_;
  std::vector<std::optional<std::size_t>> selection_indices_;
  std::vector<std::size_t> selection_tasks_;
  std::span<const SelectionRegion> logicals_;
  std::span<const BranchPolarity> polarity_;
};
std::variant<ExpressionForest, ExpressionError> BuildForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner,
    ExpressionConstants constants, analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules,
    EvaluationFlow flow, RecoveryMode recovery, std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity) {
  auto bindings = flow == EvaluationFlow::kNormal ? BuildBindingPlan(graph, evaluation, parameter_count, budget)
      : BuildPhasedBindingPlan(graph, evaluation, parameter_count, budget);
  if (const auto* error = std::get_if<BindingError>(&bindings)) {
    if (*error == BindingError::kResourceLimit) return Error::kResourceLimit;
    if (*error == BindingError::kWorkLimit) return Error::kWorkLimit;
    return Error::kBinding;
  }
  try {
    return ForestBuilder(graph, evaluation, owner, constants, budget,
                         std::get<BindingPlan>(std::move(bindings)), modules, recovery, names, logicals, polarity).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<ExpressionForest, ExpressionError> BuildExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery,
    std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kNormal, recovery, names, logicals, polarity);
}
std::variant<ExpressionForest, ExpressionError> BuildPhasedExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery,
    std::span<const std::u16string> names, std::span<const SelectionRegion> logicals, std::span<const BranchPolarity> polarity) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kPhased, recovery, names, logicals, polarity);
}
}  // namespace abcd::source
