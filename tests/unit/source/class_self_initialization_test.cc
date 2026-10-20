#include "../../../src/source/class_self_initialization.h"

#include <iostream>
#include <type_traits>

#include "../../../src/source/class_self_initialization_file.h"
#include "../../../src/source/class_self_initialization_json.h"
#include "../../../src/source/class_self_scope.h"
#include "../../../src/source/class_tree.h"
#include "../../../tools/record_failure_json.h"
#include "analysis/ssa_fixture.h"
#include "class_self_enclosure_checks.h"
using namespace abcd;
using namespace abcd::source;
namespace {
template <class T>
void Error(const T& e) {
  if constexpr (std::is_enum_v<T>)
    std::cerr << source::FailureDomain<T>() << "=" << static_cast<unsigned>(e)
              << " ";
  else if constexpr (requires { std::variant_size<T>::value; })
    std::visit([](const auto& v) { Error(v); }, e);
  else if constexpr (requires { e.cause; })
    Error(e.cause);
  else if constexpr (requires { e.kind; })
    Error(e.kind);
  else if constexpr (requires { e.error; })
    Error(e.error);
}
template <class T, class E>
T Take(std::variant<T, E> value) {
  if (auto* p = std::get_if<T>(&value)) return std::move(*p);
  Error(std::get<E>(value));
  std::cerr << "unexpected failure variant " << value.index() << '\n';
  std::abort();
}
void Mutations(const ClassSelfInitializationInput& input,
               const ClassSelfInitializationProof& proof) {
  using E = ClassSelfInitializationError;
  const auto reject = [&](ClassSelfInitialization changed, E expected) {
    const auto result = VerifyClassSelfInitialization(input, changed);
    test::Check(std::holds_alternative<ClassSelfInitializationFailure>(result));
    const auto& failure = std::get<ClassSelfInitializationFailure>(result);
    test::Check(failure.kind == expected);
    if (expected == E::kGuard)
      test::Check(failure.read && failure.guard && failure.location.method &&
                  failure.location.pc);
  };
  auto changed = proof.Evidence();
  ++changed.slot.value;
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  ++changed.allocation.value;
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  changed.store = changed.call.call;
  reject(changed, E::kInitialization);
  changed = proof.Evidence();
  ++changed.occurrence.member.value;
  reject(changed, E::kOccurrence);
  changed = proof.Evidence();
  changed.reads.pop_back();
  reject(changed, E::kGuard);
  changed = proof.Evidence();
  changed.reads.push_back(changed.reads.front());
  reject(changed, E::kGuard);
  changed = proof.Evidence();
  ++changed.reads.front().guard.value;
  reject(changed, E::kGuard);
  changed = proof.Evidence();
  ++changed.captures.front().incoming.depth.value;
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  changed.cleanup.clear();
  reject(changed, E::kCatchState);
  changed = proof.Evidence();
  ++changed.cleanup.front().rethrow.value;
  reject(changed, E::kCatchState);
  changed = proof.Evidence();
  ++changed.owners.front().revision.value;
  reject(changed, E::kCorrespondence);
  changed = proof.Evidence();
  changed.captures.push_back(changed.captures.front());
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  changed.reads.front().exception =
      analysis::ValueHandler{{{0}, {0}, {123}}, {0}};
  reject(changed, E::kCatchState);
  changed = proof.Evidence();
  changed.initializer_exception.reset();
  reject(changed, E::kCatchState);
  // Missing/extra families cannot evade the constructor/literal/definition
  // inventory.
  auto missing = input;
  missing.families = input.families.first(input.families.size() - 1);
  test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
      ClassSelfInitializationProof::Build(missing)));
  std::vector<ClassSelfFamilyInput> extra(input.families.begin(),
                                          input.families.end());
  extra.push_back(extra.front());
  auto duplicate = input;
  duplicate.families = extra;
  test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
      ClassSelfInitializationProof::Build(duplicate)));
  auto wrong_call = input;
  ++wrong_call.call.receiver.value;
  test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
      ClassSelfInitializationProof::Build(wrong_call)));
  // Exact work and a binary-searched exact byte boundary. Peak scratch of
  // nested independent analyses need not equal the retained-proof byte
  // accounting.
  analysis::SemanticBudget exact;
  exact.work = proof.Cost().work;
  test::Check(std::holds_alternative<ClassSelfInitializationProof>(
      ClassSelfInitializationProof::Build(input, exact)));
  --exact.work;
  auto limited = ClassSelfInitializationProof::Build(input, exact);
  test::Check(std::get<ClassSelfInitializationFailure>(limited).kind ==
              E::kWorkLimit);
  std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto result = ClassSelfInitializationProof::Build(
        input, {mid, analysis::SemanticBudget{}.work});
    if (std::holds_alternative<ClassSelfInitializationProof>(result))
      high = mid;
    else {
      if (std::get<ClassSelfInitializationFailure>(result).kind !=
          E::kResourceLimit)
        std::cerr << "budget " << mid << " kind "
                  << static_cast<unsigned>(
                         std::get<ClassSelfInitializationFailure>(result).kind)
                  << "\n";
      test::Check(std::get<ClassSelfInitializationFailure>(result).kind ==
                  E::kResourceLimit);
      low = mid + 1;
    }
  }
  test::Check(low > 0 &&
              std::holds_alternative<ClassSelfInitializationProof>(
                  ClassSelfInitializationProof::Build(
                      input, {low, analysis::SemanticBudget{}.work})));
  limited = ClassSelfInitializationProof::Build(
      input, {low - 1, analysis::SemanticBudget{}.work});
  test::Check(std::get<ClassSelfInitializationFailure>(limited).kind ==
              E::kResourceLimit);
  std::cout << "exact bytes " << low << " work " << proof.Cost().work << '\n';
}
void OriginalMutations(const ClassSelfInitializationInput& input,
                       const ClassSelfInitializationProof& proof,
                       FileFunction& parent, FileFamily& initializer) {
  const auto reject = [&] {
    test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
        ClassSelfInitializationProof::Build(input)));
  };
  auto& store = std::get<analysis::ValueLexicalStore>(
      parent.values.graph.ops[proof.Evidence().store.value].operation);
  const auto address = store.address;
  ++store.address.slot.value;
  reject();
  store.address = address;
  ++store.address.depth.value;
  reject();
  store.address = address;
  const auto& read = proof.Evidence().reads.front();
  test::Check(read.family == 0 && read.member < initializer.members.size());
  auto& graph = initializer.members[read.member].function.Normal().values.graph;
  auto& guard = graph.ops[read.guard.value];
  const auto original = guard.operation;
  guard.operation = analysis::ValueNop{};
  reject();
  guard.operation = original;
  const auto exceptional = guard.exceptional;
  guard.exceptional = analysis::ExceptionalCompletion::kImpossible;
  reject();
  guard.exceptional = exceptional;
  auto& self_store = parent.values.graph.ops[proof.Evidence().store.value];
  auto& call = parent.values.graph.ops[input.call.call.value];
  std::swap(self_store.order, call.order);
  reject();
  std::swap(self_store.order, call.order);
  if (initializer.members.size() > 1) {
    auto descendant = std::move(initializer.members.back());
    initializer.members.pop_back();
    reject();
    initializer.members.push_back(std::move(descendant));
  }
  auto cleanup = std::move(parent.cleanup);
  parent.cleanup.reset();
  reject();
  parent.cleanup = std::move(cleanup);
  test::Check(std::holds_alternative<ClassSelfInitializationCost>(
      VerifyClassSelfInitialization(input, proof.Evidence())));
}
void ScopeMutations(const ClassSelfInitializationInput& input,
                    const ClassSelfInitializationProof& raw) {
  using E = ClassSelfInitializationError;
  auto built = ClassSelfScopeProof::Build(input, raw.Evidence());
  if (const auto* e = std::get_if<ClassSelfInitializationFailure>(&built))
    Error(e->kind);
  const auto proof = Take<ClassSelfScopeProof>(std::move(built));
  const auto& reference = proof.Evidence().references.front();
  test::Check(proof.FindReference(reference.owner, reference.read) ==
              &reference);
  auto unlisted = reference.owner;
  ++unlisted.family;
  test::Check(!proof.FindReference(unlisted, reference.read));
  unlisted = reference.owner;
  ++unlisted.revision.value;
  test::Check(!proof.FindReference(unlisted, reference.read));
  test::Check(std::holds_alternative<ClassSelfInitializationCost>(
      VerifyClassSelfScope(input, raw.Evidence(), proof.Evidence())));
  const auto verification = std::get<ClassSelfInitializationCost>(
      VerifyClassSelfScope(input, raw.Evidence(), proof.Evidence()));
  analysis::SemanticBudget verify_exact;
  verify_exact.work = verification.work;
  test::Check(
      std::holds_alternative<ClassSelfInitializationCost>(VerifyClassSelfScope(
          input, raw.Evidence(), proof.Evidence(), verify_exact)));
  --verify_exact.work;
  const auto verify_limited = VerifyClassSelfScope(
      input, raw.Evidence(), proof.Evidence(), verify_exact);
  test::Check(std::get<ClassSelfInitializationFailure>(verify_limited).kind ==
              E::kWorkLimit);
  const auto reject = [&](ClassSelfScope proposed, E expected) {
    const auto result = VerifyClassSelfScope(input, raw.Evidence(), proposed);
    test::Check(std::holds_alternative<ClassSelfInitializationFailure>(result));
    test::Check(std::get<ClassSelfInitializationFailure>(result).kind ==
                expected);
  };
  auto changed = proof.Evidence();
  ++changed.binding.slot.value;
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  ++changed.binding.allocation.value;
  reject(changed, E::kStorage);
  changed = proof.Evidence();
  changed.owners.pop_back();
  reject(changed, E::kScope);
  changed = proof.Evidence();
  ++changed.ranges.front().count;
  reject(changed, E::kScope);
  changed = proof.Evidence();
  changed.requirements.clear();
  reject(changed, E::kScope);
  changed = proof.Evidence();
  for (auto& requirement : changed.requirements) {
    if (requirement.kind == ClassSelfRequirementKind::kOuter) {
      requirement.kind = ClassSelfRequirementKind::kClassInner;
      reject(changed, E::kScope);
      break;
    }
  }
  changed = proof.Evidence();
  // A forged reference from an unlisted occurrence cannot borrow the same
  // incoming address, even if the method entity and slot number are equal.
  ++changed.references.front().owner.family;
  reject(changed, E::kGuard);
  changed = proof.Evidence();
  changed.references.front().incoming.slot.value++;
  reject(changed, E::kGuard);
  changed = proof.Evidence();
  changed.ports.front().kind = ClassSelfPortKind::kPublicCommit;
  reject(changed, E::kOrder);
  changed = proof.Evidence();
  changed.ports[1].operation = raw.Evidence().call.call;
  reject(changed, E::kOrder);
  if (!proof.Evidence().installations.empty()) {
    changed = proof.Evidence();
    changed.installations.pop_back();
    reject(changed, E::kOrder);
  }
  for (std::size_t p = 0; p < proof.Evidence().ports.size(); ++p) {
    if (proof.Evidence().ports[p].kind != ClassSelfPortKind::kCompletedClass)
      continue;
    changed = proof.Evidence();
    changed.ports.erase(changed.ports.begin() + p);
    reject(changed, E::kOrder);
    changed = proof.Evidence();
    ++changed.ports[p].value->value;
    reject(changed, E::kOrder);
    changed = proof.Evidence();
    ++changed.ports[p].use->value;
    reject(changed, E::kOrder);
    changed = proof.Evidence();
    changed.ports[p].operation = raw.Evidence().store;
    reject(changed, E::kOrder);  // Before successful static completion.
  }
  changed = proof.Evidence();
  changed.cleanup.clear();
  reject(changed, E::kCatchState);
  changed = proof.Evidence();
  changed.initializer_exception.reset();
  reject(changed, E::kCatchState);
  auto extra = raw.Evidence();
  ++extra.reads.front().family;
  test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
      ClassSelfScopeProof::Build(input, extra)));
  // Original creator read moved before successful initializer completion must
  // not acquire an initialized class-inner interpretation.
  auto& graph = const_cast<FileFunction&>(input.creator).values.graph;
  const auto cell =
      FindCell(*input.creator.values.cells,
               AllocatedCell{raw.Evidence().allocation, raw.Evidence().slot});
  test::Check(cell.has_value());
  const auto& local = input.creator.values.cells->cells[cell->value];
  for (std::uint32_t a = 0; a < local.count; ++a) {
    const auto& access = input.creator.values.cells->accesses[local.first + a];
    if (access.kind != CellAccessKind::kLoad) continue;
    auto& read = graph.ops[access.operation.value];
    auto& call = graph.ops[input.call.call.value];
    std::swap(read.order, call.order);
    test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
        ClassSelfScopeProof::Build(input, raw.Evidence())));
    std::swap(read.order, call.order);
    break;
  }
  analysis::SemanticBudget exact;
  exact.work = proof.Cost().work;
  test::Check(std::holds_alternative<ClassSelfScopeProof>(
      ClassSelfScopeProof::Build(input, raw.Evidence(), exact)));
  --exact.work;
  const auto work = ClassSelfScopeProof::Build(input, raw.Evidence(), exact);
  test::Check(std::get<ClassSelfInitializationFailure>(work).kind ==
              E::kWorkLimit);
  std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto result = ClassSelfScopeProof::Build(
        input, raw.Evidence(), {mid, analysis::SemanticBudget{}.work});
    if (std::holds_alternative<ClassSelfScopeProof>(result))
      high = mid;
    else {
      test::Check(std::get<ClassSelfInitializationFailure>(result).kind ==
                  E::kResourceLimit);
      low = mid + 1;
    }
  }
  test::Check(
      low &&
      std::holds_alternative<ClassSelfScopeProof>(ClassSelfScopeProof::Build(
          input, raw.Evidence(), {low, analysis::SemanticBudget{}.work})));
  const auto memory = ClassSelfScopeProof::Build(
      input, raw.Evidence(), {low - 1, analysis::SemanticBudget{}.work});
  test::Check(std::get<ClassSelfInitializationFailure>(memory).kind ==
              E::kResourceLimit);
  std::cout << "scope exact bytes " << low << " work " << proof.Cost().work
            << " owners " << proof.Evidence().owners.size() << " references "
            << proof.Evidence().references.size() << " ports "
            << proof.Evidence().ports.size() << '\n';
}
void DraftMutations(const ClassSelfInitializationInput& input,
                    const ClassSelfInitialization& raw,
                    const ClassSelfScope& scope, ClassTree& layout,
                    FileFunction& creator, const RecordModuleContext& module,
                    ClassSelfClassDraft& draft) {
  using E = ClassSelfInitializationError;
  const auto reject = [&] {
    test::Check(std::holds_alternative<ClassSelfInitializationFailure>(
        VerifyClassSelfClassDraft(input, raw, scope, layout, draft.evaluation,
                                  draft.initializer)));
  };
  auto& tree = draft.initializer.tree;
  const auto original_binding = tree.class_inner->binding;
  ++tree.class_inner->binding.slot.value;
  reject();
  tree.class_inner->binding = original_binding;
  ++tree.class_inner->binding.allocation.value;
  reject();
  tree.class_inner->binding = original_binding;
  const auto inner_scope = tree.class_inner->scope;
  tree.class_inner->scope = tree.parameter_scope;
  reject();
  tree.class_inner->scope = inner_scope;
  const auto ports = layout.class_self->ports;
  layout.class_self->ports.front().kind = ClassSelfPortKind::kPublicCommit;
  reject();
  layout.class_self->ports = ports;
  auto cleanup = std::move(layout.class_self->cleanup);
  layout.class_self->cleanup.clear();
  reject();
  layout.class_self->cleanup = std::move(cleanup);
  ++layout.class_self->owner_end;
  reject();
  --layout.class_self->owner_end;
  for (auto& expression : tree.expressions) {
    const auto* symbol = std::get_if<SymbolExpr>(&expression.expression);
    if (!symbol ||
        tree.symbols[symbol->symbol.value].kind != BindingKind::kClassInner)
      continue;
    const auto origin = expression.origin;
    expression.origin = TreeOriginId{UINT32_MAX};
    reject();
    expression.origin = origin;
    auto& binding = tree.symbols[symbol->symbol.value];
    binding.declaration = DeclarationKind::kLet;
    reject();
    binding.declaration = DeclarationKind::kConst;
    break;
  }
  if (!tree.capture_requirements.empty()) {
    ++tree.capture_requirements.front().storage.slot.value;
    reject();
    --tree.capture_requirements.front().storage.slot.value;
  }
  test::Check(std::holds_alternative<ClassSelfInitializationCost>(
      VerifyClassSelfClassDraft(input, raw, scope, layout, draft.evaluation,
                                draft.initializer)));
  // Exhaustion cannot turn a conditional draft into a publishable function.
  const auto build = [&](analysis::SemanticBudget budget) {
    return BuildClassSelfClassDraft(input, raw, scope, layout, creator, budget,
                                    &module);
  };
  const auto full = Take<ClassSelfClassDraft>(build({}));
  analysis::SemanticBudget exact;
  exact.work = full.work;
  test::Check(std::holds_alternative<ClassSelfClassDraft>(build(exact)));
  --exact.work;
  test::Check(std::get<ClassSelfInitializationFailure>(build(exact)).kind ==
              E::kWorkLimit);
  std::size_t low = 0, high = analysis::SemanticBudget{}.bytes;
  while (low < high) {
    const auto mid = low + (high - low) / 2;
    const auto result = build({mid, analysis::SemanticBudget{}.work});
    if (std::holds_alternative<ClassSelfClassDraft>(result))
      high = mid;
    else {
      test::Check(std::get<ClassSelfInitializationFailure>(result).kind ==
                  E::kResourceLimit);
      low = mid + 1;
    }
  }
  test::Check(low && std::holds_alternative<ClassSelfClassDraft>(
                         build({low, analysis::SemanticBudget{}.work})));
  test::Check(std::get<ClassSelfInitializationFailure>(
                  build({low - 1, analysis::SemanticBudget{}.work}))
                  .kind == E::kResourceLimit);
  std::cout << "draft exact bytes " << low << " work " << full.work << '\n';
}
void Run(const char* path, std::optional<abc::EntityOffset> requested,
         bool expect_tdz_gate, bool certificate_only) {
  auto file =
      Take<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(path));
  const auto fingerprint = Take<corpus::FileFingerprint>(file->Fingerprint());
  const auto header = Take<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size),
      fingerprint.size));
  auto identity = MakeRouteInputIdentity(fingerprint, header);
  identity.contract = SourceContract::kLanguageSourceV5;
  const auto index =
      Take<abc::MetadataIndex>(abc::BuildMetadataIndex(*file, header));
  const auto lookup =
      Take<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = Take<analysis::MethodRoleIndex>(
      analysis::MethodRoleIndex::Build(*file, header, index, lookup));
  const auto sites = Take<analysis::FunctionCreationSites>(
      analysis::FunctionCreationSites::Build(*file, header, index, lookup));
  const auto classes = Take<analysis::ClassMethodSites>(
      analysis::ClassMethodSites::Build(*file, header, index, lookup));
  bool seen = false;
  for (std::uint32_t n = 0; n < index.methods.size(); ++n) {
    const auto& method = index.methods[n];
    if (!method.code || !method.code->try_count ||
        (requested && method.offset != *requested))
      continue;
    const auto record = std::find_if(
        index.records.begin(), index.records.end(),
        [&](const auto& r) { return r.offset == method.owner_offset; });
    const auto packaging = Take<abc::FilePackaging>(
        abc::DetectFilePackaging(*file, header, index));
    const auto module = Take<RecordModuleContext>(
        ReadRecordModuleContext(*file, header, index, *record, packaging));
    auto loaded = ReadFunctionValues(
        *file, header, index, {n}, identity, InputCaptureMode::kConditional, {},
        &module, nullptr, nullptr, FunctionBodyMode::kCleanupEntry);
    auto* parent = std::get_if<FileFunction>(&loaded);
    if (!parent) {
      Error(std::get<FunctionFileFailure>(loaded));
      std::cerr << " creator " << method.offset.value << "\n";
      continue;
    }
    if (parent->values.initializers.calls.empty()) continue;
    for (const auto& call : parent->values.initializers.calls) {
      auto initializer = Take<FunctionContext>(ResolveClassInitializerContext(
          *file, header, index, lookup, roles, sites, *parent, call));
      const auto& instruction =
          parent->values.graph.origins
              [parent->values.graph.ops[call.method.value].origin.value];
      ParentModuleInput modules{parent->input, {instruction.site.value}};
      auto family = Take<FileFamily>(ReadFunctionFamily(
          *file, header, index, lookup, roles, sites, initializer.Position(),
          identity, {}, &module, &initializer, {}, FamilyClassMode::kReject,
          &modules, {}, nullptr, &classes, nullptr,
          ClassRootBinding::kRequirements));
      std::vector<ClassDefinitionSite> definitions;
      for (const auto& d : parent->values.definitions.sites)
        if (d.creation == call.creation) definitions.push_back(d);
      auto prepared = Take<ClassPreparation>(PrepareClassTree(
          *file, header, index, lookup, roles, sites, classes, *parent,
          call.creation, identity, nullptr, {}, nullptr, definitions,
          {static_cast<std::uint32_t>(family.members.size())},
          RecoveryMode::kCompleteOnly, nullptr,
          ClassRootBinding::kRequirements));
      std::vector<ClassSelfFamilyInput> families;
      families.push_back({family, initializer});
      for (const auto& item : prepared.bodies) {
        const auto& body = std::get<FamilyPreparation>(item);
        families.push_back({body.Input(), body.Context()});
      }
      ClassSelfInitializationInput input{
          *file, header,  index,   lookup,
          roles, classes, *parent, {parent->input.Method(), {}, {}},
          call,  families};
      auto result = ClassSelfInitializationProof::Build(input);
      if (const auto* e =
              std::get_if<ClassSelfInitializationFailure>(&result)) {
        std::cerr << "m" << method.offset.value << " failure "
                  << static_cast<unsigned>(e->kind) << " method "
                  << e->location.method->value << " pc "
                  << (e->location.pc ? e->location.pc->value : UINT32_MAX)
                  << '\n';
      }
      test::Check(std::holds_alternative<ClassSelfInitializationProof>(result));
      const auto& proof = std::get<ClassSelfInitializationProof>(result);
      test::Check(std::holds_alternative<ClassSelfInitializationCost>(
          VerifyClassSelfInitialization(input, proof.Evidence())));
      // Valid raw-body mutation: store the initialized receiver into the self
      // cell instead of loading it. Even an idempotent write cannot certify a
      // native immutable class-inner binding. Original ABC bytes stay
      // untouched.
      if (family.members.size() == 1) {
        auto& root = family.members.front().function.Normal();
        auto body = root.body;
        const auto& read = proof.Evidence().reads.front();
        test::Check(read.family == 0 && read.member == 0);
        const auto site =
            root.values.graph
                .origins[root.values.graph.ops[read.read.value].origin.value]
                .site.value;
        const auto& decoded = body.instructions[site].decoded;
        test::Check(decoded.opcode == abc::Opcode::kLDLEXVAR_IMM4_IMM4);
        constexpr unsigned kNibbleBits = 4;
        const std::uint8_t operands = static_cast<std::uint8_t>(
            decoded.operands[0].raw_bits |
            (decoded.operands[1].raw_bits << kNibbleBits));
        const std::array<std::uint8_t, 2> bytes{
            static_cast<std::uint8_t>(abc::Opcode::kSTLEXVAR_IMM4_IMM4),
            operands};
        body.instructions[site].decoded = Take<abc::DecodedInstruction>(
            abc::DecodeInstruction(bytes, header.profile));
        auto writer_input = Take<FunctionInput>(FunctionInput::Build(
            identity, root.input.Method(), root.input.Record(), body,
            root.frame, InputCaptureMode::kConditional, {}, &initializer,
            &modules));
        auto writer_values = Take<FunctionValues>(
            BuildFunctionValues(writer_input, body, root.frame));
        const auto writer_frame = root.frame;
        auto saved = std::move(family.members.front().function);
        family.members.front().function =
            FileFunction{std::move(body), writer_frame, std::move(writer_input),
                         std::move(writer_values)};
        const auto writer = ClassSelfInitializationProof::Build(input);
        test::Check(
            std::holds_alternative<ClassSelfInitializationFailure>(writer) &&
            std::get<ClassSelfInitializationFailure>(writer).kind ==
                ClassSelfInitializationError::kWrite);
        family.members.front().function = std::move(saved);
      }
      Mutations(input, proof);
      ScopeMutations(input, proof);
      OriginalMutations(input, proof, *parent, family);
      test::Check(std::holds_alternative<ClassInitializerReadFailure>(
          ReadClassInitializerFunction(*file, header, index, lookup, roles,
                                       sites, *parent, call, {}, &module)));
      const auto diagnostic = DiagnoseClassSelfInitialization(
          *file, header, index, lookup, roles, sites, classes, *parent, call,
          {parent->input.Method(), {}, {}}, {}, &module);
      test::Check(diagnostic.status ==
                      ClassSelfInitializationStatus::kVerified &&
                  !diagnostic.failure);
      test::Check(diagnostic.scope_view &&
                  diagnostic.scope_view->status ==
                      ClassSelfInitializationStatus::kVerified &&
                  !diagnostic.scope_view->failure &&
                  diagnostic.scope_view->references ==
                      proof.Evidence().reads.size());
      const auto exhausted = DiagnoseClassSelfInitialization(
          *file, header, index, lookup, roles, sites, classes, *parent, call,
          {parent->input.Method(), {}, {}}, {0, analysis::kDefaultSemanticWork},
          &module);
      test::Check(exhausted.status == ClassSelfInitializationStatus::kMissing &&
                  exhausted.failure &&
                  exhausted.failure->kind ==
                      ClassSelfInitializationError::kResourceLimit);
      const auto no_work = DiagnoseClassSelfInitialization(
          *file, header, index, lookup, roles, sites, classes, *parent, call,
          {parent->input.Method(), {}, {}},
          {analysis::SemanticBudget{}.bytes, 0}, &module);
      test::Check(no_work.status == ClassSelfInitializationStatus::kMissing &&
                  no_work.failure &&
                  no_work.failure->kind ==
                      ClassSelfInitializationError::kWorkLimit);
      const auto json = ClassSelfInitializationJson(diagnostic);
      const auto parsed =
          ParseClassSelfInitialization(nlohmann::json::parse(json.dump()));
      test::Check(parsed && ClassSelfInitializationJson(*parsed) == json);
      RouteFailure failure{
          RouteFailureClass::kUnproved, ValueRouteError::kTreeStage, {}};
      failure.class_self_initialization = diagnostic;
      MethodRouteReceipt receipt;
      receipt.identity = identity;
      receipt.method = parent->input.Method();
      receipt.record = parent->input.Record();
      receipt.stage = SourceStage::kValuesVerified;
      receipt.work_revision = 0;
      receipt.coverage = Coverage::kUnsupported;
      receipt.failure = failure;
      const auto serialized = Take<std::string>(SerializeRouteReceipt(receipt));
      const auto restored =
          Take<MethodRouteReceipt>(ParseRouteReceipt(serialized));
      test::Check(restored.failure &&
                  restored.failure->class_self_initialization);
      nlohmann::json output;
      abcd::tools::DescribeRecordFailure(RouteRefusal{failure}, output);
      test::Check(output["source_failure"]["class_self_initialization"] ==
                  json);
      RecordProgramFailure record_failure{RecordProgramError::kBody};
      record_failure.class_self_initialization = diagnostic;
      abcd::tools::DescribeRecordFailure(record_failure, output);
      test::Check(output["source_class_self_initialization"] == json);
      auto malformed = json;
      malformed["status"] = 8U;
      test::Check(!ParseClassSelfInitialization(malformed));
      malformed = json;
      malformed["slot"] = 65536U;
      test::Check(!ParseClassSelfInitialization(malformed));
      malformed = json;
      malformed["scope_view"]["status"] = 8U;
      test::Check(!ParseClassSelfInitialization(malformed));
      malformed = json;
      malformed["scope_view"]["owners"] = 0U;
      test::Check(!ParseClassSelfInitialization(malformed));
      auto missing_scope = diagnostic;
      missing_scope.scope_view->status =
          ClassSelfInitializationStatus::kMissing;
      missing_scope.scope_view->failure = ClassSelfInitializationFailure{
          ClassSelfInitializationError::kPreparationGate};
      const auto missing_json = ClassSelfInitializationJson(missing_scope);
      test::Check(ParseClassSelfInitialization(missing_json).has_value());
      malformed = missing_json;
      malformed["scope_view"]["failure"]["kind"] = 255U;
      test::Check(!ParseClassSelfInitialization(malformed));
      std::cout << "creator m" << method.offset.value << " initializer m"
                << initializer.Target().value << " reads "
                << proof.Evidence().reads.size() << " captures "
                << proof.Evidence().captures.size() << " cleanup "
                << proof.Evidence().cleanup.size() << '\n';
      const auto scoped = Take<ClassSelfScopeProof>(
          ClassSelfScopeProof::Build(input, proof.Evidence()));
      auto consumed =
          BuildClassSelfClassDraft(input, proof.Evidence(), scoped.Evidence(),
                                   prepared.layout, *parent, {}, &module);
      if (const auto* f =
              std::get_if<ClassSelfInitializationFailure>(&consumed)) {
        std::cerr << "consumption failed kind "
                  << static_cast<unsigned>(f->kind) << " method "
                  << (f->location.method ? f->location.method->value : 0)
                  << " pc " << (f->location.pc ? f->location.pc->value : 0)
                  << '\n';
      }
      auto draft = Take<ClassSelfClassDraft>(std::move(consumed));
      test::Check(draft.initializer.tree.class_inner.has_value());
      test::Check(std::holds_alternative<ClassSelfInitializationCost>(
          VerifyClassSelfClassDraft(input, proof.Evidence(), scoped.Evidence(),
                                    prepared.layout, draft.evaluation,
                                    draft.initializer)));
      test::Check(std::holds_alternative<TreeStructureError>(
          VerifyTreeStructure(draft.initializer.tree)));
      DraftMutations(input, proof.Evidence(), scoped.Evidence(),
                     prepared.layout, *parent, module, draft);
      const auto gate = ConsumeClassSelfInitialization(
          *file, header, index, lookup, roles, sites, classes, *parent, call,
          {parent->input.Method(), {}, {}}, {}, &module);
      test::Check(gate.recovery &&
                  gate.recovery->kind ==
                      ClassSelfInitializationError::kRecoveryGate &&
                  gate.recovery->projection && gate.tree_references > 0 &&
                  gate.syntax_owners >= gate.families);
      const auto gate_json = ClassSelfInitializationJson(gate);
      test::Check(ParseClassSelfInitialization(gate_json).has_value());
      test::Check(*gate.recovery->projection ==
                  (draft.initializer.tree.capture_requirements.empty()
                       ? ClassSelfInitializationFailure::Projection::
                             kClassInitializationPorts
                       : ClassSelfInitializationFailure::Projection::
                             kCaptureEnclosure));
      failure.class_self_initialization = gate;
      receipt.failure = failure;
      const auto gate_receipt = Take<MethodRouteReceipt>(
          ParseRouteReceipt(Take<std::string>(SerializeRouteReceipt(receipt))));
      test::Check(gate_receipt.failure &&
                  ClassSelfInitializationJson(
                      *gate_receipt.failure->class_self_initialization) ==
                      gate_json);
      abcd::tools::DescribeRecordFailure(RouteRefusal{failure}, output);
      test::Check(output["source_failure"]["class_self_initialization"] ==
                  gate_json);
      record_failure.class_self_initialization = gate;
      abcd::tools::DescribeRecordFailure(record_failure, output);
      test::Check(output["source_class_self_initialization"] == gate_json);
      malformed = gate_json;
      malformed["recovery"]["projection"] = 255U;
      test::Check(!ParseClassSelfInitialization(malformed));
      auto abi_gate = gate;
      abi_gate.recovery->projection =
          ClassSelfInitializationFailure::Projection::kMemberAbi;
      abi_gate.recovery->abi_lengths =
          ClassSelfInitializationFailure::AbiLengths{parent->input.Method(), 1,
                                                     7};
      const auto abi_json = ClassSelfInitializationJson(abi_gate);
      test::Check(ParseClassSelfInitialization(abi_json).has_value());
      auto broken_abi = abi_json;
      broken_abi["recovery"].erase("abi_lengths");
      test::Check(!ParseClassSelfInitialization(broken_abi));
      broken_abi = abi_json;
      broken_abi["recovery"]["abi_lengths"]["signature"] = 1;
      test::Check(!ParseClassSelfInitialization(broken_abi));
      std::cout << "recovery projection "
                << static_cast<unsigned>(*gate.recovery->projection)
                << " tree references " << gate.tree_references << " owners "
                << gate.syntax_owners << '\n';
      if (!certificate_only)
        test::CheckClassSelfEnclosure(*file, header, index, lookup, roles,
                                      sites, classes, *parent, expect_tdz_gate);
      seen = true;
    }
  }
  test::Check(seen);
}
}  // namespace
int main(int argc, char** argv) {
  static_assert(!std::is_default_constructible_v<ClassSelfInitializationProof>);
  test::Check(argc == 2 || argc == 3);
  const bool tdz = argc == 3 && std::string_view{argv[2]} == "--tdz-gate";
  const bool certificate_only =
      argc == 3 && std::string_view{argv[2]} == "--certificate-only";
  Run(argv[1],
      argc == 3 && !tdz && !certificate_only ? std::optional{abc::EntityOffset{
                              static_cast<std::uint32_t>(std::stoul(argv[2]))}}
                        : std::nullopt,
      tdz, certificate_only);
}
