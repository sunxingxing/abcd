#include "expression_forest.h"
#include "expression_copies.h"

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

class ForestBuilder {
 public:
  ForestBuilder(const analysis::ValueProgram& graph,
                const EvaluationPlan& evaluation, FunctionOwnerId owner,
                ExpressionConstants constants, analysis::SemanticBudget budget,
                BindingPlan bindings, std::span<const ModuleSymbol> modules, RecoveryMode recovery)
      : graph_(graph), evaluation_(evaluation), owner_(owner), constants_(constants),
        budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules), recovery_(recovery) {
    Require(bindings.allocated_bytes <= bytes_, Error::kResourceLimit);
    bytes_ -= bindings.allocated_bytes;
    Step(bindings.work);
    forest_.bindings = std::move(bindings);
  }

  ExpressionForest Build() {
    Require(constants_.operations.size() == graph_.ops.size(), Error::kConstant);
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
      if (op.life != analysis::ValueLife::kLive || !object || !object->literal) continue;
      const auto* constant = std::get_if<ObjectConstant>(&constants_.operations[i]);
      Require(constant && constant->first <= constants_.properties.size() &&
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
    Reserve(forest_.initializers, graph_.ops.size());
    Reserve(objects_, graph_.ops.size());
    Step(graph_.ops.size()); Step(graph_.ops.size());
    forest_.initializers.resize(graph_.ops.size()); objects_.resize(graph_.ops.size());
    const auto defined_properties = evaluation_.literals ? evaluation_.literals->definitions.size() : 0;
    Require(defined_properties <= kMaxId - buffered_properties, Error::kResourceLimit);
    Reserve(forest_.properties, defined_properties + buffered_properties);
    if (evaluation_.literals) {
      for (std::size_t i = 0; i < evaluation_.literals->objects.size(); ++i) {
        Step(); objects_[evaluation_.literals->objects[i].allocation.value] = i;
      }
    }
    for (const auto& origin : graph_.origins) {
      Step();
      Require(origin.disposition != analysis::OriginDisposition::kCombined, Error::kOrigin);
      if (origin.disposition == analysis::OriginDisposition::kElidedPure)
        forest_.origins.emplace_back(ElidedOrigin{origin.site, origin.pc, origin.original});
      else
        forest_.origins.emplace_back(OriginalSite{origin.site, origin.pc, origin.original});
    }
    const bool partial = CanRetainPrefix();
    struct Boundary {
      std::size_t operation = 0, expressions = 0, arguments = 0, origins = 0, parts = 0;
    } boundary{0, 0, 0, forest_.origins.size(), 0};
    std::optional<std::size_t> last_consumer;
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
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
        if (op.life != analysis::ValueLife::kLive) continue;
        Require(op.origin.value < graph_.origins.size() &&
                    graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained,
                Error::kOrigin);
        const TreeOriginId origin{op.origin.value};
        if (evaluation_.operations[i].kind == EvaluationKind::kElidedPure ||
            evaluation_.operations[i].kind == EvaluationKind::kAlias)
          continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kInitializer) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kEnvironment) continue;
        if (evaluation_.operations[i].kind == EvaluationKind::kModuleGuard) continue;
        if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) {
          Require(next_module_ < evaluation_.modules.size(), Error::kBinding);
          const auto& module = evaluation_.modules[next_module_++];
          Require(module.pair.read.value == i, Error::kBinding);
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
        std::array<ExprId, kFixedExpressionOperands> operands{};
        Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
        for (std::uint32_t j = 0; j < op.use_count; ++j) {
          Step();
          operands[j] = Operand(graph_.uses[op.first_use.value + j].operand, origin);
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
    const auto copied = AppendCopyExpressions(graph_, evaluation_, forest_, owner_, work_);
    if (const auto* error = std::get_if<Error>(&copied)) throw *error;
    Step(std::get<std::uint64_t>(copied));
    forest_.allocated_bytes = budget_.bytes - bytes_;
    forest_.work = budget_.work - work_;
    return std::move(forest_);
  }

 private:
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
    const auto* object = std::get_if<ObjectConstant>(&constant);
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
    PropertyRange range{static_cast<std::uint32_t>(forest_.properties.size()), object.count};
    const auto& allocation = std::get<analysis::ValueObjectAllocation>(graph_.ops[object.allocation.value].operation);
    if (allocation.literal) {
      const auto prefix = BufferedProperties(constants_.operations[object.allocation.value], origin);
      range.count += prefix.count; // Combined capacity was admitted before construction.
    }
    for (std::uint32_t i = 0; i < object.count; ++i) {
      Step();
      const auto id = evaluation_.literals->definitions[object.first + i];
      const auto& op = graph_.ops[id.value];
      const auto* name = std::get_if<NameId>(&constants_.operations[id.value]);
      Require(name && name->value < constants_.name_count, Error::kConstant);
      const TreeOriginId field_origin{op.origin.value};
      const auto value = Operand(graph_.uses[op.first_use.value + 1].operand, field_origin);
      Require(forest_.properties.size() < forest_.properties.capacity(), Error::kResourceLimit);
      const PropertyId property{static_cast<std::uint32_t>(forest_.properties.size())};
      forest_.properties.push_back({*name, value, field_origin});
      forest_.initializers[id.value] = property; forest_.operations[id.value] = value;
    }
    return Add(ObjectExpr{range}, origin);
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
        const auto& binding = forest_.bindings.values[value.value];
        if (const auto* symbol = std::get_if<SymbolId>(&binding))
          return Add(SymbolExpr{*symbol}, origin);
        if (std::holds_alternative<ReceiverEntryBinding>(binding))
          return Add(ThisExpr{owner_}, origin);
        const auto* result = std::get_if<analysis::OpResult>(&graph_.values[value.value].origin);
        Require(result && result->op.value < forest_.operations.size() &&
                evaluation_.operations[result->op.value].kind == EvaluationKind::kInline &&
                forest_.operations[result->op.value].has_value(), Error::kUnavailableValue);
        return *forest_.operations[result->op.value];
      }
    }, ResolveEvaluationOperand(evaluation_, operand));
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
      case U::kToBoolean: return unary(TreeUnary::kNot, unary(TreeUnary::kNot, input));
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
      else if constexpr (std::is_same_v<T, analysis::ValueLexicalLoad>)
        return Add(SymbolExpr{CellSymbol(id)}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueLexicalStore>)
        return Add(AssignmentExpr{SymbolTarget{CellSymbol(id)}, operands[0]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueUnary>)
        return Unary(operation.op, operands[0], graph_.uses[op.first_use.value].operand, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueAwait>)
        return Add(AwaitExpr{operands[0]}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueBranch>)
        return Unary(operation.predicate, operands[0], graph_.uses[op.first_use.value].operand, origin);
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
      else if constexpr (std::is_same_v<T, analysis::ValueArrayAllocation>)
        return Add(EmptyArrayExpr{}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueFrameThis>)
        return Add(ThisExpr{owner_}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueGlobalObject>)
        return Add(RealmObjectExpr{}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueSymbolConstructor>)
        return Add(SymbolConstructorExpr{}, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueObjectAllocation>) {
        if (operation.literal) {
          return Add(ObjectExpr{BufferedProperties(constant, origin)}, origin);
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
      else {
        static_assert(std::is_same_v<T, analysis::ValueArgumentSnapshot> ||
                      std::is_same_v<T, analysis::ValueGuard> || std::is_same_v<T, analysis::ValueHole> ||
                      std::is_same_v<T, analysis::ValueModuleRead> ||
                      std::is_same_v<T, analysis::ValueModuleWrite> ||
                      std::is_same_v<T, analysis::ValueOwnWrite> ||
                      std::is_same_v<T, analysis::ValueEnvironmentPush> || std::is_same_v<T, analysis::ValueEnvironmentPop> ||
                      std::is_same_v<T, analysis::ValueCurrentEnvironment> ||
                      std::is_same_v<T, analysis::ValueInvoke> || std::is_same_v<T, analysis::ValueConstruct> ||
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
};
std::variant<ExpressionForest, ExpressionError> BuildForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner,
    ExpressionConstants constants, analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules,
    EvaluationFlow flow, RecoveryMode recovery) {
  auto bindings = flow == EvaluationFlow::kNormal ? BuildBindingPlan(graph, evaluation, parameter_count, budget)
      : BuildPhasedBindingPlan(graph, evaluation, parameter_count, budget);
  if (const auto* error = std::get_if<BindingError>(&bindings)) {
    if (*error == BindingError::kResourceLimit) return Error::kResourceLimit;
    if (*error == BindingError::kWorkLimit) return Error::kWorkLimit;
    return Error::kBinding;
  }
  try {
    return ForestBuilder(graph, evaluation, owner, constants, budget,
                         std::get<BindingPlan>(std::move(bindings)), modules, recovery).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace
std::variant<ExpressionForest, ExpressionError> BuildExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kNormal, recovery);
}
std::variant<ExpressionForest, ExpressionError> BuildPhasedExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner, ExpressionConstants constants,
    analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules, RecoveryMode recovery) {
  return BuildForest(graph, evaluation, parameter_count, owner, constants, budget, modules, EvaluationFlow::kPhased, recovery);
}
}  // namespace abcd::source
