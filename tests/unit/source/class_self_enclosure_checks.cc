#include "class_self_enclosure_checks.h"

#include <iostream>
#include <map>

#include "../../../src/source/tree_constants.h"
#include "../../../src/source/class_self_initialization_json.h"
#include "../../../tools/record_failure_json.h"
#include "analysis/ssa_fixture.h"

namespace abcd::test {
using namespace source;
namespace {
template <class T, class... U>
T Take(std::variant<T, U...> result) {
  if (auto* value = std::get_if<T>(&result)) return std::move(*value);
  std::cerr << "enclosure helper failure " << result.index() << '\n';
  std::abort();
}
}  // namespace
void CheckClassSelfEnclosure(const corpus::AbcFile& file,
                             const abc::Header& header,
                             const abc::MetadataIndex& index,
                             const abc::MethodLookup& lookup,
                             const analysis::MethodRoleIndex& roles,
                             const analysis::ClassMethodSites& classes,
                             FileFunction& creator, bool expect_tdz_gate) {
  if (!creator.class_self) return;
  auto& p = *creator.class_self;
  if (!p.diagnostic.scope_view->outer) return;
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
  if (!result.bound_outer)
    std::cerr << "enclosure did not bind; cause "
              << static_cast<unsigned>(result.recovery->kind) << '\n';
  Check(result.bound_outer == p.diagnostic.scope_view->outer);
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
  auto& child = p.initializer.tree
                    .symbols[p.initializer.tree.captures.front().symbol.value];
  const auto name = child.name;
  child.name = p.initializer.tree.class_inner->name;
  reject();
  child.name = name;
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
}
}  // namespace abcd::test
