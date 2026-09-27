#include "expression_forest.h"

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
                BindingPlan bindings, std::span<const ModuleSymbol> modules)
      : graph_(graph), evaluation_(evaluation), owner_(owner), constants_(constants),
        budget_(budget), bytes_(budget.bytes), work_(budget.work), modules_(modules) {
    Require(bindings.allocated_bytes <= bytes_, Error::kResourceLimit);
    bytes_ -= bindings.allocated_bytes;
    Step(bindings.work);
    forest_.bindings = std::move(bindings);
  }

  ExpressionForest Build() {
    Require(constants_.operations.size() == graph_.ops.size(), Error::kConstant);
    Admit(1, sizeof(ExpressionForest));
    auto calls = BuildCallPlan(graph_, evaluation_, {bytes_, work_});
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
    std::size_t copies = 0;
    const auto bundles = evaluation_.phis ? evaluation_.phis->bundles.size() : 0;
    if (evaluation_.phis) {
      for (const auto& bundle : evaluation_.phis->bundles) {
        Step();
        Require(bundle.schedule.steps.size() <= kMaxId - copies, Error::kResourceLimit);
        copies += bundle.schedule.steps.size();
      }
    }
    constexpr std::size_t kNodesPerCopy = 2;  // Owned RHS leaf and assignment.
    Require(graph_.ops.size() <= kMaxId / kNodesPerOperation &&
            graph_.uses.size() <= kMaxId - graph_.ops.size() * kNodesPerOperation &&
            graph_.origins.size() <= kMaxId - graph_.ops.size(), Error::kResourceLimit);
    const auto operation_nodes = graph_.uses.size() + graph_.ops.size() * kNodesPerOperation;
    Require(copies <= (kMaxId - operation_nodes) / kNodesPerCopy &&
            bundles <= kMaxId - graph_.origins.size() - graph_.ops.size(), Error::kResourceLimit);
    const auto expression_nodes = operation_nodes + copies * kNodesPerCopy;
    Require(buffered_properties <= kMaxId - expression_nodes, Error::kResourceLimit);
    Reserve(forest_.expressions, expression_nodes + buffered_properties);
    Reserve(forest_.arguments, graph_.uses.size());
    Reserve(forest_.origins, graph_.origins.size() + graph_.ops.size() + bundles);
    Reserve(forest_.edge_assignments, copies);
    Reserve(forest_.edges, bundles);
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
    for (std::size_t i = 0; i < graph_.ops.size(); ++i) {
      Step();
      const auto& op = graph_.ops[i];
      if (op.life != analysis::ValueLife::kLive) continue;
      Require(op.origin.value < graph_.origins.size() &&
              graph_.origins[op.origin.value].disposition == analysis::OriginDisposition::kRetained,
              Error::kOrigin);
      const TreeOriginId origin{op.origin.value};
      if (evaluation_.operations[i].kind == EvaluationKind::kInitializer) continue;
      if (evaluation_.operations[i].kind == EvaluationKind::kEnvironment) continue;
      if (evaluation_.operations[i].kind == EvaluationKind::kModuleGuard) continue;
      if (std::holds_alternative<analysis::ValueModuleRead>(op.operation)) {
        Require(next_module_ < evaluation_.modules.size(), Error::kBinding);
        const auto& module = evaluation_.modules[next_module_++];
        Require(module.pair.read.value == i, Error::kBinding);
        const auto found = std::lower_bound(module_order_.begin(), module_order_.end(), module.target,
            [&](auto index, auto target) { Step(); return modules_[index] < target; });
        Require(found != module_order_.end() && modules_[*found] == module.target, Error::kBinding);
        // FunctionTree appends module aliases in this supplied order after the
        // shared IR bindings. This is a source SymbolId, never an ABC slot.
        const SymbolId symbol{static_cast<std::uint32_t>(forest_.bindings.symbols.size() + *found)};
        const auto expression = Combine(Add(SymbolExpr{symbol}, origin),
            {graph_.ops[module.pair.guard.value].origin.value});
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
      std::array<ExprId, kFixedExpressionOperands> operands{};
      Require(op.use_count <= operands.size(), Error::kUnsupportedOperation);
      for (std::uint32_t j = 0; j < op.use_count; ++j) {
        Step();
        operands[j] = Operand(graph_.uses[op.first_use.value + j].operand, origin);
      }
      forest_.operations[i] = Lower({static_cast<std::uint32_t>(i)}, op, operands, origin, constants_.operations[i]);
    }
    Require(next_function_ == evaluation_.functions.size(), Error::kOwnership);
    Require(next_module_ == evaluation_.modules.size(), Error::kOwnership);
    EdgeAssignments();
    forest_.allocated_bytes = budget_.bytes - bytes_;
    forest_.work = budget_.work - work_;
    return std::move(forest_);
  }

 private:
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
  SymbolId CycleSymbol(std::uint32_t bundle, CopyTempId temporary) {
    Require(bundle < forest_.bindings.cycle_bases.size(), Error::kBinding);
    const auto base = forest_.bindings.cycle_bases[bundle];
    Require(base && base->value < forest_.bindings.symbols.size() &&
            temporary.value < forest_.bindings.symbols.size() - base->value, Error::kBinding);
    const SymbolId symbol{base->value + temporary.value};
    const auto* binding = std::get_if<CycleBinding>(&forest_.bindings.symbols[symbol.value]);
    Require(binding && binding->bundle == bundle && binding->temporary == temporary, Error::kBinding);
    return symbol;
  }
  SymbolId CopyDestination(const CopyTarget& target, std::uint32_t bundle) {
    if (const auto* temporary = std::get_if<CopyTempId>(&target))
      return CycleSymbol(bundle, *temporary);
    const auto value = std::get<analysis::ValueId>(target);
    Require(value.value < forest_.bindings.values.size(), Error::kBinding);
    const auto* symbol = std::get_if<SymbolId>(&forest_.bindings.values[value.value]);
    Require(symbol && symbol->value < forest_.bindings.symbols.size(), Error::kBinding);
    const auto* phi = std::get_if<PhiBinding>(&forest_.bindings.symbols[symbol->value]);
    Require(phi && phi->value == value, Error::kBinding);
    return *symbol;
  }
  void EdgeAssignments() {
    if (!evaluation_.phis) return;
    for (std::size_t i = 0; i < evaluation_.phis->bundles.size(); ++i) {
      Step();
      const auto& bundle = evaluation_.phis->bundles[i];
      const TreeOriginId origin{static_cast<std::uint32_t>(forest_.origins.size())};
      if (const auto* edge = std::get_if<analysis::EdgeId>(&bundle.incoming))
        forest_.origins.emplace_back(EdgeOrigin{*edge});
      else forest_.origins.emplace_back(EntryOrigin{});
      const auto first = static_cast<std::uint32_t>(forest_.edge_assignments.size());
      for (const auto& copy : bundle.schedule.steps) {
        Step();
        const auto destination = CopyDestination(copy.destination, static_cast<std::uint32_t>(i));
        ExprId source;
        if (const auto* temporary = std::get_if<CopyTempId>(&copy.source)) {
          source = Add(SymbolExpr{CycleSymbol(static_cast<std::uint32_t>(i), *temporary)}, origin);
        } else {
          const auto& operand = std::get<analysis::ValueOperand>(copy.source);
          // All incoming operation results must already be snapshots. Do not
          // reuse/inline an operation tree here: doing so moves effects across
          // the branch and gives one expression occurrence multiple owners.
          if (const auto* value = std::get_if<analysis::ValueId>(&operand)) {
            Require(value->value < forest_.bindings.values.size() &&
                    !std::holds_alternative<UnboundValue>(forest_.bindings.values[value->value]),
                    Error::kUnavailableValue);
          }
          source = Operand(operand, origin);
        }
        forest_.edge_assignments.push_back(Add(AssignmentExpr{SymbolTarget{destination}, source}, origin));
      }
      forest_.edges.push_back({bundle.incoming, first, static_cast<std::uint32_t>(bundle.schedule.steps.size())});
    }
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
    }, operand);
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
      else if constexpr (std::is_same_v<T, analysis::ValueBranch>)
        return Unary(operation.predicate, operands[0], graph_.uses[op.first_use.value].operand, origin);
      else if constexpr (std::is_same_v<T, analysis::ValueBinary>)
        return Add(BinaryExpr{operation.op, operands[0], operands[1]}, origin);
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
      } else if constexpr (std::is_same_v<T, analysis::ValueReturn>)
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
        static_assert(std::is_same_v<T, analysis::ValueGuard> ||
                      std::is_same_v<T, analysis::ValueSymbolConstructor> ||
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
};
}  // namespace

std::variant<ExpressionForest, ExpressionError> BuildExpressionForest(
    const analysis::ValueProgram& graph, const EvaluationPlan& evaluation,
    std::uint32_t parameter_count, FunctionOwnerId owner,
    ExpressionConstants constants, analysis::SemanticBudget budget, std::span<const ModuleSymbol> modules) {
  auto bindings = BuildBindingPlan(graph, evaluation, parameter_count, budget);
  if (const auto* error = std::get_if<BindingError>(&bindings)) {
    if (*error == BindingError::kResourceLimit) return Error::kResourceLimit;
    if (*error == BindingError::kWorkLimit) return Error::kWorkLimit;
    return Error::kBinding;
  }
  try {
    return ForestBuilder(graph, evaluation, owner, constants, budget,
                         std::get<BindingPlan>(std::move(bindings)), modules).Build();
  } catch (Error error) { return error;
  } catch (const std::bad_alloc&) { return Error::kResourceLimit;
  } catch (const std::length_error&) { return Error::kResourceLimit; }
}
}  // namespace abcd::source
