#include "../../../src/source/class_self_initialization.h"

#include <iostream>
#include <type_traits>

#include "../../../src/source/class_self_initialization_file.h"
#include "../../../src/source/class_self_initialization_json.h"
#include "../../../src/source/class_tree.h"
#include "../../../tools/record_failure_json.h"
#include "analysis/ssa_fixture.h"
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
void Run(const char* path, std::optional<abc::EntityOffset> requested) {
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
          call.creation, identity, nullptr, {}, nullptr, definitions, {},
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
      std::cout << "creator m" << method.offset.value << " initializer m"
                << initializer.Target().value << " reads "
                << proof.Evidence().reads.size() << " captures "
                << proof.Evidence().captures.size() << " cleanup "
                << proof.Evidence().cleanup.size() << '\n';
      seen = true;
    }
  }
  test::Check(seen);
}
}  // namespace
int main(int argc, char** argv) {
  static_assert(!std::is_default_constructible_v<ClassSelfInitializationProof>);
  test::Check(argc == 2 || argc == 3);
  Run(argv[1], argc == 3 ? std::optional{abc::EntityOffset{
                               static_cast<std::uint32_t>(std::stoul(argv[2]))}}
                         : std::nullopt);
}
