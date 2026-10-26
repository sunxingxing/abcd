#include "abcd/source/source_route.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <algorithm>
#include <array>
#include <string>
#include <variant>

namespace {
using namespace abcd;
using namespace abcd::source;

void Check(bool value,
           std::source_location where = std::source_location::current()) {
  if (value) return;
  std::cerr << "source_route_test: check failed at line " << where.line() << '\n';
  std::abort();
}

// Pinned identity: the pasteboard development slice's recorded content hash
// with a 12.0.6.0 version; entity numbers below come from the same frozen
// slice contract.
RouteInputIdentity SampleIdentity() {
  RouteInputIdentity identity;
  const std::string_view hash_hex =
      "854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e";
  for (std::size_t i = 0; i < identity.file_hash.bytes.size(); ++i) {
    const auto digit = [&](std::size_t at) {
      return static_cast<std::uint8_t>(hash_hex[at] <= '9'
                                           ? hash_hex[at] - '0'
                                           : hash_hex[at] - 'a' + 10);
    };
    identity.file_hash.bytes[i] = static_cast<std::uint8_t>(
        digit(2 * i) << 4 | digit(2 * i + 1));
  }
  identity.file_size = 123456;
  identity.abc_version = {{12, 0, 6, 0}};
  identity.profile = abc::ProfileId::kDynamic1206;
  identity.contract = SourceContract::kLanguageSourceV2;
  return identity;
}

// A method whose value-tree context stage refused it right after decode.
MethodRouteReceipt RefusedReceipt() {
  MethodRouteReceipt receipt;
  receipt.identity = SampleIdentity();
  receipt.method = abc::EntityOffset{7586};
  receipt.record = abc::EntityOffset{6897};
  receipt.route = SourceRoute::kValueTree;
  receipt.stage = SourceStage::kDecoded;
  receipt.coverage = Coverage::kUnsupported;
  receipt.failure = RouteFailure{RouteFailureClass::kUnsupported,
                                 ValueRouteError::kContextStage, {}};
  return receipt;
}

bool SameReceipt(const MethodRouteReceipt& left,
                 const MethodRouteReceipt& right) {
  return left.identity == right.identity && left.method == right.method &&
         left.record == right.record && left.route == right.route &&
         left.stage == right.stage && left.coverage == right.coverage &&
         left.work_revision == right.work_revision &&
         (!left.failure || !right.failure ||
          (left.failure->causes.count == right.failure->causes.count &&
           left.failure->causes.truncated == right.failure->causes.truncated &&
           std::equal(left.failure->causes.View().begin(), left.failure->causes.View().end(),
               right.failure->causes.View().begin(), [](const auto& a, const auto& b) {
                 return a.domain == b.domain && a.value == b.value;
               }))) &&
         left.failure.has_value() == right.failure.has_value() &&
         (!left.failure ||
          (left.failure->failure_class == right.failure->failure_class &&
           left.failure->reason == right.failure->reason &&
           left.failure->pc == right.failure->pc && left.failure->method == right.failure->method &&
           left.failure->working_pc == right.failure->working_pc &&
           left.failure->creation_method == right.failure->creation_method &&
           left.failure->creation_pc == right.failure->creation_pc &&
           left.failure->call_reference == right.failure->call_reference &&
           left.failure->capture == right.failure->capture &&
           left.failure->control == right.failure->control &&
           left.failure->exception_region == right.failure->exception_region));
}
}  // namespace

// Versioned, identity-bound receipt boundary and contract predicates.
int main() {
  Check(kSourceContract == SourceContract::kLanguageSourceV10);
  Check(GlobalScripts(SourceContract::kLanguageSourceV9));
  Check(!GlobalScripts(SourceContract::kLanguageSourceV8));
  Check(SourceContractName(SourceContract::kLanguageSourceV10) == "language_source_v10");
  Check(NativeArguments(SourceContract::kLanguageSourceV6));
  Check(!NativeArguments(SourceContract::kLanguageSourceV5));
  Check(kSourceArithmetic == analysis::ArithmeticSemantics::kSourceNumeric);
  Check(kSourceCallResults == analysis::CallResultValues::kLanguage);
  Check(kSourcePendingExceptions == analysis::PendingExceptionValues::kLanguage);
  Check(kSourceResourceFailures == ResourceFailureScope::kExcluded);
  // Central validation (R2): each state below is checked against the stages
  // that could actually prove it.
  const auto valid = [](MethodRouteReceipt receipt) {
    return !ValidateRouteReceipt(receipt);
  };
  Check(valid(RefusedReceipt()));
  // Every refusal names the stage that refused it; with a single always
  // available route there is no stage-less "route unavailable" receipt.
  MethodRouteReceipt unstaged_record = RefusedReceipt();
  unstaged_record.method = {};
  unstaged_record.stage = {};
  Check(ValidateRouteReceipt(unstaged_record) == RouteReceiptError::kStage);
  MethodRouteReceipt tree_only = RefusedReceipt();
  tree_only.failure = {};
  tree_only.stage = SourceStage::kTreeVerified;
  tree_only.work_revision = 0;
  tree_only.coverage = Coverage::kIncomplete;
  Check(valid(tree_only));  // TreeVerified alone is incomplete, not complete
  tree_only.coverage = Coverage::kComplete;
  Check(ValidateRouteReceipt(tree_only) == RouteReceiptError::kStage);
  MethodRouteReceipt complete_value = tree_only;
  complete_value.stage = SourceStage::kPrinted;
  complete_value.coverage = Coverage::kComplete;
  Check(valid(complete_value));
  MethodRouteReceipt complete_early = complete_value;
  complete_early.stage = SourceStage::kDecoded;
  complete_early.work_revision.reset();
  Check(ValidateRouteReceipt(complete_early) == RouteReceiptError::kStage);
  MethodRouteReceipt complete_failed = complete_value;
  complete_failed.failure = RouteFailure{RouteFailureClass::kUnproved,
                                         ValueRouteError::kTreeStage, {}};
  Check(ValidateRouteReceipt(complete_failed) == RouteReceiptError::kCoverage);
  MethodRouteReceipt unsupported_silent = RefusedReceipt();
  unsupported_silent.failure = {};  // unsupported without a typed reason
  Check(ValidateRouteReceipt(unsupported_silent) ==
        RouteReceiptError::kCoverage);
  MethodRouteReceipt anonymous = RefusedReceipt();
  anonymous.method = {};
  anonymous.record = {};
  Check(ValidateRouteReceipt(anonymous) == RouteReceiptError::kIdentity);
  auto unbound = complete_value;
  unbound.identity = {};
  Check(ValidateRouteReceipt(unbound) == RouteReceiptError::kIdentity);
  auto invented_revision = RefusedReceipt();
  invented_revision.work_revision = 0;
  Check(ValidateRouteReceipt(invented_revision) == RouteReceiptError::kSchema);
  auto published_refusal = RefusedReceipt();
  published_refusal.stage = SourceStage::kPublished;
  published_refusal.work_revision = 0;
  Check(ValidateRouteReceipt(published_refusal) == RouteReceiptError::kStage);
  auto wrong_failure = RefusedReceipt();
  wrong_failure.failure->failure_class = static_cast<RouteFailureClass>(255);
  Check(ValidateRouteReceipt(wrong_failure) == RouteReceiptError::kFailure);
  auto invalid_stage = complete_value;
  invalid_stage.stage = static_cast<SourceStage>(255);
  Check(ValidateRouteReceipt(invalid_stage) == RouteReceiptError::kStage);

  // Receipt round trip through the versioned boundary: a refused decoded
  // method, a refusal with a typed cause chain (including a truncated chain
  // whose last entry is the true leaf) and a complete value-tree product.
  auto caused = RefusedReceipt();
  caused.failure->method = abc::EntityOffset{91};
  caused.failure->working_pc = abc::CodeOffset{31};
  caused.failure->creation_method = abc::EntityOffset{13};
  caused.failure->creation_pc = abc::CodeOffset{7};
  caused.failure->control = ControlOwnershipEvidence{
      {17, ControlOriginKind::kPhase, 17, abc::CodeOffset{73}, abc::CodeOffset{31}}, 19,
      {{5, ControlOriginKind::kPhase, 5}, ControlLocation{13, ControlOriginKind::kTryExit, 2}},
      {{0, ControlOriginKind::kPhase, 0}, {}}};
  caused.failure->control->decision = DecisionContinuationEvidence{
      {1, ControlOriginKind::kPhase, 1}, {2, ControlOriginKind::kPhase, 2},
      {4, ControlOriginKind::kPhase, 4}, {3, ControlOriginKind::kPhase, 3},
      {3, ControlOriginKind::kPhase, 3}, {6, ControlOriginKind::kPhase, 6},
      {6, ControlOriginKind::kPhase, 6, abc::CodeOffset{73}, abc::CodeOffset{31}}};
  caused.failure->capture = CaptureOccurrence{3, {{2}, {7}}, analysis::CaptureDomain::kLanguage,
      CaptureOrigin{{456}, abc::CodeOffset{31}, abc::CodeOffset{17}}};
  caused.failure->call_reference = CallReferenceEvidence{CallReferenceError::kNotInline,
      {17}, abcd::analysis::OpId{4}, EvaluationKind::kRoot, TemporaryReason::kCrossRegionValue, {}};
  caused.failure->call_reference->checked = GuardedCallFailure{GuardedCallError::kCatchState, {17}, {5}, {6}, {9}};
  caused.failure->call_reference->checked->read_pc = abc::CodeOffset{0};
  caused.failure->call_reference->checked->guard_pc = abc::CodeOffset{73};
  caused.failure->call_reference->checked->guard_working_pc = abc::CodeOffset{31};
  AppendFailureCause(caused.failure->causes, RouteFailureClass::kUnproved);
  AppendFailureCause(caused.failure->causes, ValueRouteError::kTreeStage);
  auto deep = RefusedReceipt();
  for (unsigned i = 0; i < 6; ++i)
    AppendFailureCause(deep.failure->causes, static_cast<RouteFailureClass>(i));
  for (unsigned i = 0; i < 6; ++i)
    AppendFailureCause(deep.failure->causes, static_cast<ValueRouteError>(i));
  Check(deep.failure->causes.count == kMaxFailureCauses && deep.failure->causes.truncated);
  Check(deep.failure->causes.View().back().value == 5 &&
        deep.failure->causes.View().back().domain == "abcd::source::ValueRouteError");
  const auto capture_json = nlohmann::json::parse(std::get<std::string>(SerializeRouteReceipt(caused)));
  for (const auto& [field, invalid] : std::vector<std::pair<std::string, nlohmann::json>>{
      {"requested", 256}, {"requested", 4}, {"slot", 65536}, {"ordinal", -1}, {"depth", "zero"}}) {
    auto malformed = capture_json; malformed["failure"]["capture"][field] = invalid;
    Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(malformed.dump())));
  }
  for (const auto& [field, invalid] : std::vector<std::pair<std::string, nlohmann::json>>{
      {"kind", 255}, {"call", 18}, {"read", -1}, {"guard_pc", "zero"}}) {
    auto malformed = capture_json; malformed["failure"]["call_reference"]["checked"][field] = invalid;
    Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(malformed.dump())));
  }
  auto no_method = capture_json; no_method["failure"]["capture"].erase("method");
  Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(no_method.dump())));
  caused.failure->exception_region=ExceptionRegionEvidence{{1},abc::CodeOffset{0},{},{12},{19}};
  // Real PC zero, normalized PCs and a synthetic nested-try exit are distinct.
  caused.failure->exception_region->exits = std::array<ExceptionExitEvidence,2>{
      ExceptionExitEvidence{analysis::EdgeId{0}, analysis::WorkBlockId{0}, {1}, {2},
          abc::CodeOffset{0}, abc::CodeOffset{12}, abc::CodeOffset{4}, abc::CodeOffset{16}},
      ExceptionExitEvidence{analysis::TryId{3}, {}, {4}, {4}}};
  const auto region_roundtrip=std::get<MethodRouteReceipt>(ParseRouteReceipt(std::get<std::string>(SerializeRouteReceipt(caused))));
  Check(region_roundtrip.failure->exception_region==caused.failure->exception_region);
  const auto region_json = nlohmann::json::parse(std::get<std::string>(SerializeRouteReceipt(caused)));
  const auto bad_exit = [&](const auto& change) {
    auto malformed = region_json;
    change(malformed["failure"]["exception_region"]["exits"]);
    Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(malformed.dump())));
  };
  bad_exit([](auto& exits) { exits[1]["continuation"] = exits[0]["continuation"]; });
  bad_exit([](auto& exits) { exits[0]["origin"] = 2; });
  bad_exit([](auto& exits) { exits[0].erase("from"); });
  bad_exit([](auto& exits) { exits[1]["pc"] = 0; });
  bad_exit([](auto& exits) { exits[0]["source"] = -1; });
  bad_exit([](auto& exits) { exits.erase(1); });
  caused.failure->non_executable = analysis::NonExecutable(abc::Opcode::kJSTRICTEQZ_IMM8);
  const auto branch_json = nlohmann::json::parse(std::get<std::string>(SerializeRouteReceipt(caused)));
  const auto branch_roundtrip = std::get<MethodRouteReceipt>(ParseRouteReceipt(branch_json.dump()));
  Check(branch_roundtrip.failure->non_executable == caused.failure->non_executable);
  for (const auto& key : {"opcode", "evidence"}) {
    auto bad = branch_json; bad["failure"]["non_executable"][key] = 65536;
    Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(bad.dump())));
  }
  auto supported_branch = branch_json;
  supported_branch["failure"]["non_executable"]["opcode"] = static_cast<unsigned>(abc::Opcode::kJEQZ_IMM8);
  Check(std::holds_alternative<RouteReceiptError>(ParseRouteReceipt(supported_branch.dump())));
  const auto samples = {RefusedReceipt(), caused, deep, complete_value};
  for (const auto& receipt : samples) {
    const auto serialized = SerializeRouteReceipt(receipt);
    const auto* json = std::get_if<std::string>(&serialized);
    Check(json != nullptr);
    const auto parsed = ParseRouteReceipt(*json);
    const auto* recovered = std::get_if<MethodRouteReceipt>(&parsed);
    Check(recovered != nullptr);
    Check(SameReceipt(receipt, *recovered));
  }
  // Independent wire expectations catch accidental label/ID reinterpretation,
  // including a registry entry that round-trips under its own wrong label.
  constexpr std::array<std::string_view, 10> historical_labels{
      "language_source_v1", "language_source_v2", "language_source_v3",
      "language_source_v4", "language_source_v5", "language_source_v6",
      "language_source_v7", "language_source_v8", "language_source_v9",
      "language_source_v10"};
  static_assert(kSourceContracts.size() == historical_labels.size());
  for (std::size_t i = 0; i < historical_labels.size(); ++i) {
    Check(kSourceContracts[i].label == historical_labels[i]);
    constexpr unsigned kFirstPublishedSourceId = 8;
    Check(static_cast<unsigned>(kSourceContracts[i].contract) == kFirstPublishedSourceId + i);
  }
  // Every registered historical Source identity round-trips without renumbering.
  for (const auto& entry : kSourceContracts) {
    const auto contract = entry.contract;
    Check(IsSourceContract(contract));
    Check(ParseSourceContract(entry.label) == contract);
    Check(ExternalExceptionNoHole(contract) == (contract == SourceContract::kLanguageSourceV1 ||
        contract == SourceContract::kLanguageSourceV2 || contract == SourceContract::kLanguageSourceV3
        ? analysis::ExternalExceptionPolicy::kPinnedRuntime : analysis::ExternalExceptionPolicy::kNoHoleV1));
    const bool directives = contract != SourceContract::kLanguageSourceV1 && contract != SourceContract::kLanguageSourceV2;
    Check(ConcurrentOutput(contract) == (directives ? ConcurrentOutputPolicy::kArkDirectiveV1 : ConcurrentOutputPolicy::kNotEnabled));
    Check(SendableOutput(contract) == (directives ? SendableOutputPolicy::kArkDirectiveV1 : SendableOutputPolicy::kNotEnabled));
    Check(SharedModuleOutput(contract) == (directives ? SharedModuleOutputPolicy::kArkDirectiveV1 : SharedModuleOutputPolicy::kNotEnabled));
    Check(IterationAcquisition(contract) == (contract == SourceContract::kLanguageSourceV1
        ? IterationAcquisitionSemantics::kPinnedRuntime : IterationAcquisitionSemantics::kStandardV1));
    Check(ComputedFunctionNames(contract)==(contract==SourceContract::kLanguageSourceV10));
    Check(DynamicImports(contract)==(contract==SourceContract::kLanguageSourceV10));
    Check(ModuleNamespaces(contract)==(contract==SourceContract::kLanguageSourceV10));
    Check(ArrayPatternAcquisition(contract)==(contract==SourceContract::kLanguageSourceV10));
    Check(WideRangeCalls(contract)==(contract==SourceContract::kLanguageSourceV8 || (contract==SourceContract::kLanguageSourceV9 || contract==SourceContract::kLanguageSourceV10)));
    Check(TaggedTemplates(contract)==((contract == SourceContract::kLanguageSourceV7 || (contract == SourceContract::kLanguageSourceV8 || (contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10)))));
    Check(ObjectSpreads(contract)==((contract == SourceContract::kLanguageSourceV7 || (contract == SourceContract::kLanguageSourceV8 || (contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10)))));
    Check(ObjectRests(contract)==((contract == SourceContract::kLanguageSourceV7 || (contract == SourceContract::kLanguageSourceV8 || (contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10)))));
    Check(NativeNewTarget(contract)==((contract == SourceContract::kLanguageSourceV7 || (contract == SourceContract::kLanguageSourceV8 || (contract == SourceContract::kLanguageSourceV9 || contract == SourceContract::kLanguageSourceV10)))));
    auto receipt = RefusedReceipt();
    receipt.identity.contract = contract;
    Check(!ValidateRouteReceipt(receipt));
    const auto encoded = std::get<std::string>(SerializeRouteReceipt(receipt));
    Check(nlohmann::json::parse(encoded)["identity"]["source_contract"] ==
          SourceContractName(contract));
    Check(SameReceipt(receipt, std::get<MethodRouteReceipt>(ParseRouteReceipt(encoded))));
  }

  for (unsigned raw = 0; raw < 8; ++raw) {
    auto receipt = RefusedReceipt();
    receipt.identity.contract = static_cast<SourceContract>(raw);
    Check(ValidateRouteReceipt(receipt) == RouteReceiptError::kIdentity);
    Check(std::get<RouteReceiptError>(SerializeRouteReceipt(receipt)) == RouteReceiptError::kIdentity);
  }
  for (const auto label : {"language_and_host_v1", "language_and_host_v2", "language_and_host_v3",
      "language_arithmetic_v1", "language_numeric_v1", "language_constructors_v1",
      "language_arithmetic_constructors_v1", "language_numeric_constructors_v1"}) {
    auto json = nlohmann::json::parse(std::get<std::string>(SerializeRouteReceipt(RefusedReceipt())));
    json["identity"]["source_contract"] = label;
    Check(!ParseSourceContract(label));
    Check(std::get<RouteReceiptError>(ParseRouteReceipt(json.dump())) == RouteReceiptError::kIdentity);
  }
  static_assert(static_cast<unsigned>(SourceContract::kLanguageSourceV1) == 8);
  static_assert(static_cast<unsigned>(SourceContract::kLanguageSourceV10) == 17);

  // Unknown versions, labels, out-of-range integers, missing fields and
  // contradictory states are rejected; a parse never defaults into success.
  // Each rejection works on a fresh copy.
  const auto text = std::get<std::string>(SerializeRouteReceipt(RefusedReceipt()));
  const auto reject = [&text](std::string_view from, std::string_view to,
                              RouteReceiptError expected) {
    std::string mutated{text};
    const auto at = mutated.find(from);
    Check(at != std::string::npos);
    mutated.replace(at, from.size(), to);
    Check(std::get<RouteReceiptError>(ParseRouteReceipt(mutated)) == expected);
  };
  // Schema 3 carried the removed legacy-writer field and schema 4 lacked the
  // cause chain; neither is reinterpreted.
  reject("\"schema\":5", "\"schema\":3", RouteReceiptError::kSchema);
  reject("\"schema\":5", "\"schema\":4", RouteReceiptError::kSchema);
  reject("\"schema\":5", "\"schema\":\"5\"", RouteReceiptError::kSchema);
  // 4294967297 must not narrow to 1 on this platform (R4).
  reject("\"schema\":5", "\"schema\":4294967297", RouteReceiptError::kSchema);
  reject("\"schema\":5", "\"schema\":2.5", RouteReceiptError::kSchema);
  const auto rules = "\"evidence_rules\":" + std::to_string(kRouteEvidenceRules);
  reject(rules, "\"evidence_rules\":23",
         RouteReceiptError::kEvidence);
  reject(rules, "\"evidence_rules\":4294967297",
         RouteReceiptError::kEvidence);
  reject("\"method\":7586", "\"method\":4294967297",
         RouteReceiptError::kIdentity);
  reject("\"method\":7586", "\"method\":\"7586\"",
         RouteReceiptError::kIdentity);
  reject("\"pc\":null", "\"pc\":4294967297", RouteReceiptError::kFailure);
  reject("854fb0d9bbc1df0e", "854fb0d9bbc1df0g",
         RouteReceiptError::kIdentity);
  reject("854fb0d9bbc1df0e0e9b2ab4b0b5adedaf78831561dbe8692e323128f572ba9e",
         "854fb0d9bbc1df0e", RouteReceiptError::kIdentity);
  reject("[12,0,6,0]", "[12,0,6]", RouteReceiptError::kIdentity);
  reject("\"profile\":4", "\"profile\":99", RouteReceiptError::kIdentity);
  reject("language_source_v2", "language_and_host_v2",
         RouteReceiptError::kIdentity);
  reject("\"work_revision\":null", "\"work_revision\":0",
         RouteReceiptError::kSchema);  // revisions are typed absent, invented
  reject("\"route\":\"value_tree\"", "\"route\":\"third_route\"",
         RouteReceiptError::kRoute);
  reject("\"stage\":\"decoded\"", "\"stage\":\"half_decoded\"",
         RouteReceiptError::kStage);
  reject("\"coverage\":\"unsupported\"", "\"coverage\":\"complete-ish\"",
         RouteReceiptError::kCoverage);
  reject("\"reason\":\"context_stage\"", "\"reason\":\"mystery\"",
         RouteReceiptError::kFailure);
  reject("\"reason\":\"context_stage\"", "\"reason\":\"route_unavailable\"",
         RouteReceiptError::kFailure);
  std::string numeric_stage{text};
  const std::string_view stage_field = "\"stage\":\"decoded\"";
  Check(numeric_stage
            .replace(numeric_stage.find(stage_field), stage_field.size(),
                     "\"stage\":7")
            .find("\"stage\":7") != std::string::npos);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(numeric_stage)) ==
        RouteReceiptError::kStage);
  // Absent keys are typed rejections, never defaults (R4).
  const auto remove_field = [&text](std::string_view field) {
    auto mutated = nlohmann::json::parse(text);
    Check(mutated.erase(std::string(field)) == 1);
    return mutated.dump();
  };
  Check(std::get<RouteReceiptError>(
            ParseRouteReceipt(remove_field("stage"))) ==
        RouteReceiptError::kStage);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(
            remove_field("evidence_rules"))) ==
        RouteReceiptError::kEvidence);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(remove_field("identity"))) ==
        RouteReceiptError::kIdentity);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(remove_field("failure"))) ==
        RouteReceiptError::kFailure);
  // Contradictory receipts are rejected by the same central validator the
  // producers use (R2/R4).
  const auto rejected = [](const MethodRouteReceipt& receipt,
                           RouteReceiptError error) {
    const auto result = SerializeRouteReceipt(receipt);
    const auto* actual = std::get_if<RouteReceiptError>(&result);
    Check(actual != nullptr && *actual == error);
  };
  rejected(complete_early, RouteReceiptError::kStage);
  rejected(complete_failed, RouteReceiptError::kCoverage);
  rejected(unbound, RouteReceiptError::kIdentity);
  rejected(invented_revision, RouteReceiptError::kSchema);
  rejected(published_refusal, RouteReceiptError::kStage);
  // Invalid external evidence must be rejected independently of publication.
  auto contradiction = nlohmann::json::parse(text);
  contradiction["stage"] = "published";
  contradiction["work_revision"] = 0;
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(contradiction.dump())) ==
        RouteReceiptError::kStage);
  // An identity-free receipt never parses (R6: compare the error alternative,
  // not the variant index).
  const auto identity_free =
      ParseRouteReceipt(nlohmann::json{{"schema", kRouteReceiptSchema},
          {"evidence_rules", kRouteEvidenceRules}}.dump());
  const auto* identity_error = std::get_if<RouteReceiptError>(&identity_free);
  Check(identity_error != nullptr &&
        *identity_error == RouteReceiptError::kIdentity);
  MethodRouteReceipt parse_anonymous = RefusedReceipt();
  parse_anonymous.method = {};
  parse_anonymous.record = {};
  rejected(parse_anonymous, RouteReceiptError::kIdentity);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt("{")) ==
        RouteReceiptError::kSyntax);
  auto transform_refusal = RefusedReceipt();
  transform_refusal.stage = SourceStage::kValuesImported;
  transform_refusal.work_revision = 0;
  transform_refusal.failure = RouteFailure{RouteFailureClass::kWorkLimit,
                                           ValueRouteError::kTransformStage, {}};
  Check(!ValidateRouteReceipt(transform_refusal));
  const auto transform_json = SerializeRouteReceipt(transform_refusal);
  Check(std::holds_alternative<MethodRouteReceipt>(
      ParseRouteReceipt(std::get<std::string>(transform_json))));
  transform_refusal.stage = SourceStage::kValuesVerified;
  rejected(transform_refusal, RouteReceiptError::kStage);

  auto tree_refusal = transform_refusal;
  tree_refusal.failure->reason = ValueRouteError::kTreeStage;
  Check(!ValidateRouteReceipt(tree_refusal));
  tree_refusal.stage = SourceStage::kControlFrozen;
  Check(!ValidateRouteReceipt(tree_refusal));
  tree_refusal.stage = SourceStage::kTreeVerified;
  rejected(tree_refusal, RouteReceiptError::kStage);
  tree_refusal.failure->reason = ValueRouteError::kPrintStage;
  Check(!ValidateRouteReceipt(tree_refusal));
  const auto print_json = SerializeRouteReceipt(tree_refusal);
  Check(std::holds_alternative<MethodRouteReceipt>(
      ParseRouteReceipt(std::get<std::string>(print_json))));
  tree_refusal.stage = SourceStage::kPrinted;
  rejected(tree_refusal, RouteReceiptError::kStage);
  auto context_refusal = RefusedReceipt();
  context_refusal.failure->reason = ValueRouteError::kContextStage;
  Check(!ValidateRouteReceipt(context_refusal));

  // The stage ledger only moves forward along the value-tree sequence: a
  // fresh ledger starts at its first stage and never jumps.
  RouteStageLedger ledger{SourceRoute::kValueTree};
  Check(!ledger.reached());
  Check(!ledger.Advance(SourceStage::kPublished));
  Check(!ledger.Advance(SourceStage::kTreeVerified));
  Check(ledger.Advance(SourceStage::kDecoded));
  Check(ledger.Advance(SourceStage::kInputVerified));
  Check(ledger.Advance(SourceStage::kValuesImported));
  Check(!ledger.Advance(SourceStage::kValuesImported));
  Check(!ledger.Advance(SourceStage::kDecoded));
  Check(ledger.Advance(SourceStage::kValuesVerified));
  Check(ledger.Advance(SourceStage::kControlFrozen));
  Check(ledger.Advance(SourceStage::kBindingsFrozen));
  Check(ledger.Advance(SourceStage::kTreeVerified));
  Check(ledger.Advance(SourceStage::kPrinted));
  Check(ledger.Advance(SourceStage::kPublished));
  Check(!ledger.Advance(SourceStage::kPublished));
  Check(ledger.reached() == SourceStage::kPublished);

  // Child obligations close only on receipts that bind to the expected
  // input identity and are valid and Complete on the value-tree route (A02).
  const auto closes = [](MethodRouteReceipt receipt) {
    const ChildRouteObligation obligation{abc::EntityOffset{7586},
                                          SampleIdentity(), receipt};
    return RouteObligationsClosed({&obligation, 1});
  };
  Check(closes(complete_value));
  Check(!closes(RefusedReceipt()));
  Check(!closes(tree_only));
  auto other_input = complete_value;
  other_input.identity.file_size += 1;
  Check(!closes(other_input));
  ChildRouteObligation missing{abc::EntityOffset{7586}, SampleIdentity(), {}};
  Check(!RouteObligationsClosed({&missing, 1}));
  Check(RouteObligationsClosed({}));
  auto mismatched = complete_value;
  mismatched.method = abc::EntityOffset{7559};
  Check(!closes(mismatched));
  const ChildRouteObligation unbound_child{abc::EntityOffset{7586}, {}, unbound};
  Check(!RouteObligationsClosed({&unbound_child, 1}));
  Check(!closes(invented_revision));

  std::cout << "source_route_test: ok\n";
  return EXIT_SUCCESS;
}
