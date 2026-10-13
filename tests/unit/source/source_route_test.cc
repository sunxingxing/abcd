#include "abcd/source/source_route.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <algorithm>
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
  identity.contract = SourceContract::kLanguageAndHostV2;
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
           left.failure->pc == right.failure->pc));
}
}  // namespace

// Not executed while the user's test pause is in force (task T00). The
// assertions document the versioned, identity-bound receipt boundary, the
// central validator; run via
// `ctest --test-dir build -R '^source_route_test$'` after the pause is
// lifted.
int main() {
  Check(kSourceContract == SourceContract::kLanguageAndHostV3);
  Check(Arithmetic(kSourceContract) == analysis::ArithmeticSemantics::kPinnedRuntime);
  Check(Arithmetic(SourceContract::kLanguageArithmeticV1) == analysis::ArithmeticSemantics::kSourceLanguage);
  Check(Arithmetic(SourceContract::kLanguageNumericV1) == analysis::ArithmeticSemantics::kSourceNumeric);
  for (const auto base : {kSourceContract, SourceContract::kLanguageArithmeticV1, SourceContract::kLanguageNumericV1}) {
    const auto combined = WithLanguageConstructors(base);
    Check(Arithmetic(combined) == Arithmetic(base));
    Check(Constructors(base) == ConstructorSemantics::kPinnedRuntime);
    Check(Constructors(combined) == ConstructorSemantics::kSourceLanguage);
    Check(CallableText(combined) == CallableText(base));
    Check(ResourceFailures(combined) == ResourceFailures(base));
  }
  Check(CallableText(SourceContract::kLanguageNumericV1) == CallableText(kSourceContract));
  Check(CallableText(SourceContract::kLanguageArithmeticV1) == CallableText(kSourceContract));
  Check(ResourceFailures(SourceContract::kLanguageArithmeticV1) == ResourceFailures(kSourceContract));
  Check(CallableText(SourceContract::kLanguageAndHostV1) == CallableTextPolicy::kUnspecified);
  Check(CallableText(SourceContract::kLanguageAndHostV2) == CallableTextPolicy::kUnspecified);
  Check(CallableText(kSourceContract) == CallableTextPolicy::kRecovered);
  Check(PendingExceptions(kSourceContract) == PendingExceptions(SourceContract::kLanguageAndHostV2));
  Check(NormalCallResults(kSourceContract) == NormalCallResults(SourceContract::kLanguageAndHostV2));
  Check(RealmBinding(kSourceContract) == RealmBinding(SourceContract::kLanguageAndHostV2));
  Check(ResourceFailures(kSourceContract) == ResourceFailures(SourceContract::kLanguageAndHostV2));
  // LanguageSourceV1 extends numeric+constructors; its own premises are
  // reachable only through their predicates.
  Check(Arithmetic(SourceContract::kLanguageSourceV1) == analysis::ArithmeticSemantics::kSourceNumeric);
  Check(Constructors(SourceContract::kLanguageSourceV1) == ConstructorSemantics::kSourceLanguage);
  Check(WithLanguageConstructors(SourceContract::kLanguageSourceV1) == SourceContract::kLanguageSourceV1);
  Check(CaptureReads(SourceContract::kLanguageSourceV1) == CaptureReadSemantics::kSourceTdz);
  Check(CaptureReads(SourceContract::kLanguageNumericConstructorsV1) == CaptureReadSemantics::kPinnedRuntime);
  Check(CaptureReads(kSourceContract) == CaptureReadSemantics::kPinnedRuntime);
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
  // Every contract that can be written can be read back (dense enum range).
  for (std::size_t raw = 0; raw <= static_cast<std::size_t>(SourceContract::kLanguageSourceV1); ++raw) {
    const auto contract = static_cast<SourceContract>(raw);
    Check(ConstructorForwarding(contract) == (contract == SourceContract::kLanguageSourceV1
        ? ConstructorForwardingSemantics::kSourceRestSpread
        : ConstructorForwardingSemantics::kInvocationFrame));
    auto receipt = RefusedReceipt();
    receipt.identity.contract = contract;
    Check(!ValidateRouteReceipt(receipt));
    const auto encoded = std::get<std::string>(SerializeRouteReceipt(receipt));
    Check(nlohmann::json::parse(encoded)["identity"]["source_contract"] ==
          SourceContractName(contract));
    Check(SameReceipt(receipt, std::get<MethodRouteReceipt>(ParseRouteReceipt(encoded))));
  }

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
  reject("language_and_host_v2", "language_and_host_v4",
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
