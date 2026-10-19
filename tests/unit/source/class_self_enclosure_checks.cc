#include "class_self_enclosure_checks.h"

#include <iostream>
#include <map>
#include <sstream>

#include "../../../src/source/class_initialization_ports.h"
#include "../../../src/source/class_inner_scope.h"
#include "../../../src/source/class_self_initialization_json.h"
#include "../../../src/source/recovery_flow.h"
#include "../../../src/source/tree_constants.h"
#include "../../../tools/record_failure_json.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
using namespace source;
namespace {
template<class E> void Explain(const E& error){
  if constexpr(std::is_enum_v<E>)std::cerr<<source::FailureDomain<E>()<<"="<<static_cast<unsigned>(error)<<" ";
  else if constexpr(requires{std::variant_size<E>::value;})std::visit([](const auto& e){Explain(e);},error);
  else if constexpr(requires{error.cause;})Explain(error.cause);
  else if constexpr(requires{error.kind;})Explain(error.kind);
}
template <class T, class... U>
T Take(std::variant<T, U...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::visit([](const auto& e){Explain(e);},result);
  std::cerr << "enclosure helper failure " << result.index() << '\n';
  std::abort();
}
}  // namespace
void CheckClassSelfEnclosure(const corpus::AbcFile& file,
                             const abc::Header& header,
                             const abc::MetadataIndex& index,
                             const abc::MethodLookup& lookup,
                             const analysis::MethodRoleIndex& roles,
                             const analysis::FunctionCreationSites& functions,
                             const analysis::ClassMethodSites& classes,
                             FileFunction& creator, bool expect_tdz_gate) {
  if (!creator.class_self) return;
  auto& p = *creator.class_self;
  const auto& graph = creator.values.graph;
  const auto& raw = p.initialization.Evidence();
  auto constants = Take<TreeConstants>(
      ResolveTreeConstants(file, header, index, creator.input.Method(), graph));
  std::vector<ClassDefinitionSite> definitions;
  for (const auto& site : creator.values.definitions.sites)
    if (site.creation == raw.call.creation) definitions.push_back(site);
  auto layout = std::make_shared<ClassTree>(Take<ClassTree>(BuildClassLayout(
      file, header, index, lookup, roles, classes, creator, raw.call.creation,
      creator.input.Identity(), {}, &raw.call, definitions)));
  constants.operations[raw.call.creation.value] = layout;
  // Real decoded creator, common evaluation/tree checks. The test adapter
  // chooses legal export aliases; it does not manufacture cell/flow facts.
  FunctionSyntax syntax{};
  syntax.identity = creator.input.Identity();
  syntax.method = creator.input.Method();
  syntax.owner = {0};
  syntax.callable = CallableKind::kFunction;
  syntax.strictness = Strictness::kStrict;
  syntax.parameters.resize(creator.frame.parameters);
  std::map<std::uint32_t, NameId> exports;
  std::map<std::uint32_t, bool> written;
  for (std::size_t n = 0; n < graph.ops.size(); ++n) {
    const auto& op = graph.ops[n];
    if (op.life != analysis::ValueLife::kLive) continue;
    const auto* store = std::get_if<analysis::ValueModuleWrite>(&op.operation);
    if (!store) continue;
    if (!exports.contains(store->slot.value)) {
      const NameId name{static_cast<std::uint32_t>(constants.names.size())};
      exports.emplace(store->slot.value, name);
      auto spelling = std::to_string(store->slot.value);
      std::u16string text = u"$export";
      text.append(spelling.begin(), spelling.end());
      constants.names.push_back(std::move(text));
      syntax.modules.push_back({{{0}, {store->slot.value}},
                                name,
                                ModuleAccess::kMutable,
                                NameEvidence::kSynthesized,
                                true});
    }
    constants.operations[n] = ModuleStoreConstant{
        store->slot, {{0}, {store->slot.value}}, !written[store->slot.value]};
    written[store->slot.value] = true;
  }
  syntax.names = constants.names;
  syntax.texts = constants.texts;
  const auto view = constants.View();
  auto evaluation = Take<EvaluationPlan>(BuildEvaluationPlan(
      graph, {}, IncomingCells::kReject, {}, {}, creator.values.cells->demands,
      creator.values.initializers.calls, analysis::ClosureWrites::kLanguage,
      creator.values.definitions.sites, {}, {}, &*creator.self_order, {},
      {creator.input.Identity().contract, view, syntax.names}));
  auto control = Take<ControlPlan>(BuildControlPlan(graph));
  auto built = Take<ConstructedFunction>(
      BuildFunctionTree(graph, control, evaluation, std::move(syntax), view));
  Check(built.flow.has_value());
  const CaptureTree parent{built.tree, *built.flow};
  const FunctionOwnerId owner{
      static_cast<std::uint32_t>(p.scope.Evidence().owners.size() + 1)};
  const auto result =
      BindDeferredClassSelf(file, header, index, lookup, roles, classes,
                            creator, p, *layout, owner, parent, nullptr);
  if (expect_tdz_gate) {
    Check(!result.bound_outer && result.recovery &&
          result.recovery->kind ==
              ClassSelfInitializationError::kRecoveryGate &&
          result.recovery->projection ==
              ClassSelfInitializationFailure::Projection::kCaptureEnclosure);
    Check(p.initializer.tree.captures.empty() &&
          !p.initializer.tree.capture_requirements.empty());
    std::cout << "unfulfilled native TDZ declaration stays kCaptureEnclosure\n";
    return;
  }
  if (result.recovery &&
      result.recovery->projection ==
          ClassSelfInitializationFailure::Projection::kFamilyEnclosure) {
    bool missing = false;
    for (const auto& r : p.scope.Evidence().references)
      missing |= bool(r.owner.family || r.owner.member);
    for (const auto& e : p.initializer.tree.expressions)
      missing |= std::holds_alternative<FunctionExpr>(e.expression);
    Check(missing && !p.flow && !p.initializer.tree.class_scope);
    std::cout << "member/child inner binding requires family enclosure\n";
    return;
  }
  if (p.diagnostic.scope_view->outer && !result.bound_outer)
    std::cerr << "enclosure did not bind; cause "
              << static_cast<unsigned>(result.recovery->kind) << '\n';
  Check(result.bound_outer == p.diagnostic.scope_view->outer);
  Check(bool(p.initializer.tree.class_scope));
  std::cout << "common initializer flow " << p.flow.has_value() << '\n';
  Check(p.flow.has_value());
  Check(result.recovery &&
        result.recovery->kind == ClassSelfInitializationError::kRecoveryGate &&
        result.recovery->projection ==
            ClassSelfInitializationFailure::Projection::
                kClassInitializationPorts);
  const auto json = ClassSelfInitializationJson(result);
  const auto parsed = ParseClassSelfInitialization(json);
  Check(parsed && ClassSelfInitializationJson(*parsed) == json);
  RouteFailure failure{
      RouteFailureClass::kUnproved, ValueRouteError::kTreeStage, {}};
  failure.class_self_initialization = result;
  MethodRouteReceipt receipt;
  receipt.identity = creator.input.Identity();
  receipt.method = creator.input.Method();
  receipt.record = creator.input.Record();
  receipt.stage = SourceStage::kValuesVerified;
  receipt.work_revision = creator.values.graph.revision.value;
  receipt.coverage = Coverage::kUnsupported;
  receipt.failure = failure;
  const auto restored = Take<MethodRouteReceipt>(
      ParseRouteReceipt(Take<std::string>(SerializeRouteReceipt(receipt))));
  Check(restored.failure && restored.failure->class_self_initialization &&
        ClassSelfInitializationJson(
            *restored.failure->class_self_initialization) == json);
  nlohmann::json output;
  tools::DescribeRecordFailure(RouteRefusal{failure}, output);
  Check(output["source_failure"]["class_self_initialization"] == json);
  RecordProgramFailure record_failure{RecordProgramError::kBody};
  record_failure.class_self_initialization = result;
  tools::DescribeRecordFailure(record_failure, output);
  Check(output["source_class_self_initialization"] == json);
  auto invalid = json;
  invalid["bound_outer"] = result.scope_view->outer + 1;
  Check(!ParseClassSelfInitialization(invalid));
  invalid = json;
  invalid.erase("recovery");
  Check(!ParseClassSelfInitialization(invalid));
  const auto verify = [&](analysis::SemanticBudget budget =
                              analysis::SemanticBudget{}) {
    return VerifyClassSelfEnclosure(file, header, index, lookup, roles, classes,
                                    creator, p, *layout, owner, parent, nullptr,
                                    budget);
  };
  const auto full = Take<ClassSelfInitializationCost>(verify());
  const auto reject = [&] {
    Check(std::holds_alternative<ClassSelfInitializationFailure>(verify()));
  };
  if (!p.bindings.empty()) {
    const auto target = p.bindings.front().target;
    ++p.bindings.front().target.owner.value;
    reject();
    p.bindings.front().target = target;
    ++p.bindings.front().storage.slot.value;
    reject();
    --p.bindings.front().storage.slot.value;
    const auto domain = p.bindings.front().domain;
    p.bindings.front().domain = domain == analysis::CaptureDomain::kGuarded
                                    ? analysis::CaptureDomain::kLanguage
                                    : analysis::CaptureDomain::kGuarded;
    reject();
    p.bindings.front().domain = domain;
    ++p.initializer.tree.captures.front().target.symbol.value;
    reject();
    p.initializer.tree.captures.front().target = target;
    auto& child =
        p.initializer.tree
            .symbols[p.initializer.tree.captures.front().symbol.value];
    const auto name = child.name;
    child.name = p.initializer.tree.class_inner->name;
    reject();
    child.name = name;
  }
  const auto class_name = layout->name;
  layout->name += u"Changed";
  reject();
  layout->name = class_name;
  auto cleanup = std::move(p.members.layout.class_self->cleanup);
  p.members.layout.class_self->cleanup.clear();
  reject();
  p.members.layout.class_self->cleanup = std::move(cleanup);
  const auto port = p.members.layout.class_self->ports.front();
  p.members.layout.class_self->ports.front().kind =
      ClassSelfPortKind::kPublicCommit;
  reject();
  p.members.layout.class_self->ports.front() = port;
  Take<ClassSelfInitializationCost>(verify());
  analysis::SemanticBudget exact;
  exact.work = full.work;
  Take<ClassSelfInitializationCost>(verify(exact));
  --exact.work;
  Check(std::get<ClassSelfInitializationFailure>(verify(exact)).kind ==
        ClassSelfInitializationError::kWorkLimit);
  std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto result = verify({mid, analysis::SemanticBudget{}.work});
    if (std::holds_alternative<ClassSelfInitializationCost>(result))
      high = mid;
    else {
      Check(std::get<ClassSelfInitializationFailure>(result).kind ==
            ClassSelfInitializationError::kResourceLimit);
      low = mid + 1;
    }
  }
  Take<ClassSelfInitializationCost>(
      verify({low, analysis::SemanticBudget{}.work}));
  Check(std::get<ClassSelfInitializationFailure>(
            verify({low - 1, analysis::SemanticBudget{}.work}))
            .kind == ClassSelfInitializationError::kResourceLimit);
  std::cout << "enclosure exact bytes " << low << " work " << full.work
            << " bound " << result.bound_outer << '\n';
  {
    TreeCheckBudget q({});
    VerifyClassInnerScope(p.initializer.tree, q);
    TreeCheckBudget exact_scope({q.Bytes(), q.Work()});
    VerifyClassInnerScope(p.initializer.tree, exact_scope);
    for (const auto limit :
         {analysis::SemanticBudget{q.Bytes() - 1, q.Work()},
          analysis::SemanticBudget{q.Bytes(), q.Work() - 1}}) {
      bool exhausted = false;
      try {
        TreeCheckBudget limited(limit);
        VerifyClassInnerScope(p.initializer.tree, limited);
      } catch (TreeStructureError error) {
        Check(error == TreeStructureError::kResourceLimit ||
              error == TreeStructureError::kWorkLimit);
        exhausted = true;
      }
      Check(exhausted);
    }
    const auto proof = p.initializer.tree.class_scope;
    p.initializer.tree.class_scope.reset();
    Check(std::holds_alternative<TreeStructureError>(
        VerifyTreeStructure(p.initializer.tree)));
    p.initializer.tree.class_scope = proof;
    auto& tree = p.initializer.tree;
    const auto method = tree.method;
    ++tree.method.value;
    Check(
        std::holds_alternative<TreeStructureError>(VerifyTreeStructure(tree)));
    tree.method = method;
    for (auto& e : tree.expressions)
      if (const auto* s = std::get_if<SymbolExpr>(&e.expression);
          s && tree.symbols[s->symbol.value].kind == BindingKind::kClassInner) {
        const auto saved = e.origin;
        e.origin = {static_cast<std::uint32_t>(tree.origins.size())};
        Check(std::holds_alternative<TreeStructureError>(
            VerifyTreeStructure(tree)));
        e.origin = saved;
        break;
      }
  }
  // Complete the actual member families before deriving closed-cell facts.
  // An unfinished metadata layout deliberately marks closure access unknown.
  const GlobalScope globals{};  // The fixture has no extra enclosing wrapper.
  const auto first_owner =
      FunctionOwnerId{owner.value - static_cast<std::uint32_t>(
                                        p.scope.Evidence().owners.size() - 1)};
  auto preparation = Take<ClassPreparation>(PrepareClassTree(
      file, header, index, lookup, roles, functions, classes, creator,
      raw.call.creation, creator.input.Identity(), &globals, {}, &raw.call,
      definitions, first_owner, RecoveryMode::kCompleteOnly));
  Check(preparation.next_owner == owner.value + 1);
  preparation.layout.class_self = layout->class_self;
  ClassStaticFields fields{
      raw.call, std::move(p.context), p.initializer.tree,
      p.initializer_family.members.front().function.Record(), *p.flow};
  *layout = Take<ClassTree>(BuildClassTree(
      file, header, index, lookup, roles, functions, classes, creator,
      raw.call.creation, creator.input.Identity(), &globals, {}, &raw.call,
      definitions, RecoveryMode::kCompleteOnly, first_owner, &parent,
      &preparation, nullptr, &fields));
  if (!layout->initializer->tree.captures.empty()) {
    std::ostringstream detached;
    const auto printed=PrintClassTree(*layout,detached,{}, {},&globals);
    Check(std::holds_alternative<TreePrintFailure>(printed));
  }
  auto names = constants.View();
  names.name_count = built.tree.names.size();
  names.text_count = built.tree.texts.size();
  auto semantics = Take<TreeSemantics>(
      VerifyTreeSemantics(graph, control, evaluation, built.tree, names));
  auto original =
      std::const_pointer_cast<OriginalFlow>(semantics.flow.original);
  Check(original->class_initializations.size() == 1);
  std::vector<std::optional<SymbolId>> aliases(built.tree.symbols.size());
  std::vector<std::optional<PrimitiveId>> literals(built.tree.symbols.size());
  std::vector<std::optional<ExprId>> replacements(
      built.tree.expressions.size());
  std::vector<std::uint8_t> omitted(built.tree.statements.size()),
      normal(built.tree.expressions.size());
  std::vector<ExpressionEvaluation> effects(built.tree.expressions.size(),
                                            ExpressionEvaluation::kRequired);
  LegacyFlowProjection projection{aliases, literals, replacements,
                                  omitted, effects,  normal};
  projection.original = original.get();
  auto flow = Take<RecoveryFlow>(BuildRecoveryFlow(built.tree, projection));
  Check(flow.class_initializations.size() == 1);
  const auto verify_ports = [&](analysis::SemanticBudget budget =
                                    analysis::SemanticBudget{}) {
    TreeCheckBudget q(budget);
    VerifyClassInitializationRecovery(built.tree, flow, q);
    return ClassSelfInitializationCost{q.Bytes(), q.Work()};
  };
  const auto port_cost = verify_ports();
  const auto reject_ports = [&] {
    bool rejected = false;
    try {
      verify_ports();
    } catch (TreeStructureError) {
      rejected = true;
    }
    Check(rejected);
  };
  auto& ports = flow.class_initializations.front();
  std::swap(ports.inner, ports.commit);
  reject_ports();
  std::swap(ports.inner, ports.commit);
  const auto before = ports.before;
  ports.before = ports.allocation;
  reject_ports();
  ports.before = before;
  const auto thrown = ports.exceptions.thrown;
  ports.exceptions.thrown = ports.commit;
  reject_ports();
  ports.exceptions.thrown = thrown;
  auto& raw_ports = original->class_initializations.front().ports;
  const auto saved = raw_ports.front();
  raw_ports.front().kind = ClassInitializationPortKind::kPublicPublish;
  reject_ports();
  raw_ports.front() = saved;
  for (const auto& port : raw_ports) {
    if (port.kind != ClassInitializationPortKind::kPublicPublish) continue;
    auto& action = flow.original_actions[port.operation.value];
    Check(action.has_value());
    const auto saved_action = *action;
    action->before = ports.inner;
    reject_ports();  // Public publication cannot precede static Commit.
    action = saved_action;
    break;
  }
  std::size_t cache_mutations = 0;
  for (auto& definition : flow.definitions) {
    if (!definition.original) continue;
    const auto* value =
        std::get_if<OriginalSavedDefinition>(&definition.original->origin);
    const auto* operation =
        value ? std::get_if<analysis::OpId>(&value->placement) : nullptr;
    if (!operation || *operation != raw.store) continue;
    ++cache_mutations;
    auto& access = flow.bindings[definition.binding.value].cell_access;
    const auto saved_access = access;
    access = CellVisibility::kExternal;
    reject_ports();
    access = saved_access;
    const auto point = definition.point;
    definition.point = ports.inner;
    reject_ports();
    definition.point = point;
  }
  Check(cache_mutations > 0);
  std::optional<std::size_t> exception;
  for (auto edge = flow.events[ports.before.value].outgoing; edge;
       edge = flow.edges[*edge].outgoing)
    if (flow.edges[*edge].completion == FlowCompletion::kThrow)
      exception = *edge;
  Check(exception.has_value());
  flow.edges[*exception].completion = FlowCompletion::kNormal;
  reject_ports();
  flow.edges[*exception].completion = FlowCompletion::kThrow;
  verify_ports({port_cost.bytes, port_cost.work});
  for (const auto limit :
       {analysis::SemanticBudget{port_cost.bytes - 1, port_cost.work},
        analysis::SemanticBudget{port_cost.bytes, port_cost.work - 1}}) {
    bool refused = false;
    try {
      verify_ports(limit);
    } catch (TreeStructureError e) {
      Check(e == TreeStructureError::kResourceLimit ||
            e == TreeStructureError::kWorkLimit);
      refused = true;
    }
    Check(refused);
  }
  std::cout << "class ports exact bytes " << port_cost.bytes << " work "
            << port_cost.work << '\n';
}
}  // namespace abcd::test
