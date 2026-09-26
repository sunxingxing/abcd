#include "abcd/source/source_route.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

namespace {
using namespace abcd;
using namespace abcd::source;

void Check(bool value) {
  if (!value) std::abort();
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

// The T00 product: a method whose requested value-tree route was refused at
// admission after decode. No legacy stage ran and no legacy text exists.
MethodRouteReceipt RefusedReceipt() {
  MethodRouteReceipt receipt;
  receipt.identity = SampleIdentity();
  receipt.method = abc::EntityOffset{7586};
  receipt.record = abc::EntityOffset{6897};
  receipt.route = SourceRoute::kValueTree;
  receipt.stage = SourceStage::kDecoded;
  receipt.coverage = Coverage::kUnsupported;
  receipt.legacy_writer_used = false;
  receipt.failure = RouteFailure{RouteFailureClass::kUnsupported,
                                 ValueRouteError::kRouteUnavailable, {}};
  return receipt;
}

bool SameReceipt(const MethodRouteReceipt& left,
                 const MethodRouteReceipt& right) {
  return left.identity == right.identity && left.method == right.method &&
         left.record == right.record && left.route == right.route &&
         left.stage == right.stage && left.coverage == right.coverage &&
         left.legacy_writer_used == right.legacy_writer_used &&
         left.work_revision == right.work_revision &&
         left.failure.has_value() == right.failure.has_value() &&
         (!left.failure ||
          (left.failure->failure_class == right.failure->failure_class &&
           left.failure->reason == right.failure->reason &&
           left.failure->pc == right.failure->pc));
}
}  // namespace

// Not executed while the user's test pause is in force (task T00). The
// assertions document the versioned, identity-bound receipt boundary, the
// central validator and the A01 admission contract; run via
// `ctest --test-dir build -R '^source_route_test$'` after the pause is
// lifted.
int main() {
  // Admission: legacy stays permitted only under LegacyAllowed; a required
  // value tree is a typed refusal, never a legacy fallback (A01).
  const auto legacy = AdmitLegacyWriter(RoutePolicy::kLegacyAllowed);
  Check(legacy.legacy_permitted && !legacy.value_route_failure);
  const auto refused = AdmitLegacyWriter(RoutePolicy::kRequireValueTree);
  Check(!refused.legacy_permitted && refused.value_route_failure);
  Check(refused.value_route_failure->failure_class ==
        RouteFailureClass::kUnsupported);
  Check(refused.value_route_failure->reason ==
        ValueRouteError::kRouteUnavailable);
  Check(!refused.value_route_failure->pc);

  // Central validation (R2): each state below is checked against the stages
  // that could actually prove it.
  const auto valid = [](MethodRouteReceipt receipt) {
    return !ValidateRouteReceipt(receipt);
  };
  Check(valid(RefusedReceipt()));
  MethodRouteReceipt unstaged_record = RefusedReceipt();
  unstaged_record.method = {};
  unstaged_record.stage = {};  // a record where no route stage ran at all
  Check(valid(unstaged_record));
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
                                         ValueRouteError::kRouteUnavailable, {}};
  Check(ValidateRouteReceipt(complete_failed) == RouteReceiptError::kCoverage);
  MethodRouteReceipt unsupported_silent = RefusedReceipt();
  unsupported_silent.failure = {};  // unsupported without a typed reason
  Check(ValidateRouteReceipt(unsupported_silent) ==
        RouteReceiptError::kCoverage);
  MethodRouteReceipt value_writer = RefusedReceipt();
  value_writer.legacy_writer_used = true;
  Check(ValidateRouteReceipt(value_writer) == RouteReceiptError::kWriter);
  MethodRouteReceipt legacy_complete = RefusedReceipt();
  legacy_complete.route = SourceRoute::kLegacy;
  legacy_complete.stage = SourceStage::kPrinted;
  legacy_complete.coverage = Coverage::kComplete;
  legacy_complete.legacy_writer_used = true;
  legacy_complete.failure = {};
  Check(valid(legacy_complete));
  MethodRouteReceipt legacy_incomplete = legacy_complete;
  legacy_incomplete.stage = SourceStage::kPublished;
  legacy_incomplete.coverage = Coverage::kIncomplete;
  Check(valid(legacy_incomplete));
  MethodRouteReceipt legacy_early = legacy_complete;
  legacy_early.stage = SourceStage::kDecoded;
  Check(ValidateRouteReceipt(legacy_early) == RouteReceiptError::kStage);
  MethodRouteReceipt legacy_value_stage = legacy_complete;
  legacy_value_stage.stage = SourceStage::kTreeVerified;
  Check(ValidateRouteReceipt(legacy_value_stage) == RouteReceiptError::kStage);
  MethodRouteReceipt legacy_unwritten = legacy_complete;
  legacy_unwritten.legacy_writer_used = false;
  Check(ValidateRouteReceipt(legacy_unwritten) == RouteReceiptError::kWriter);
  MethodRouteReceipt legacy_failed = legacy_complete;
  legacy_failed.failure = RouteFailure{RouteFailureClass::kOutputFailure,
                                       ValueRouteError::kRouteUnavailable, {}};
  Check(ValidateRouteReceipt(legacy_failed) == RouteReceiptError::kFailure);
  MethodRouteReceipt legacy_unsupported = legacy_complete;
  legacy_unsupported.coverage = Coverage::kUnsupported;
  Check(ValidateRouteReceipt(legacy_unsupported) ==
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
  wrong_failure.failure->failure_class = RouteFailureClass::kUnproved;
  Check(ValidateRouteReceipt(wrong_failure) == RouteReceiptError::kFailure);
  auto invalid_stage = complete_value;
  invalid_stage.stage = static_cast<SourceStage>(255);
  Check(ValidateRouteReceipt(invalid_stage) == RouteReceiptError::kStage);

  // Receipt round trip through the versioned boundary: a refusal with no
  // stage, a refused decoded method, a complete value-tree product and a
  // legacy writer artifact.
  const auto samples = {RefusedReceipt(), unstaged_record, complete_value,
                        legacy_complete};
  for (const auto& receipt : samples) {
    const auto serialized = SerializeRouteReceipt(receipt);
    const auto* json = std::get_if<std::string>(&serialized);
    Check(json != nullptr);
    const auto parsed = ParseRouteReceipt(*json);
    const auto* recovered = std::get_if<MethodRouteReceipt>(&parsed);
    Check(recovered != nullptr);
    Check(SameReceipt(receipt, *recovered));
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
  reject("\"schema\":3", "\"schema\":2", RouteReceiptError::kSchema);
  reject("\"schema\":3", "\"schema\":\"3\"", RouteReceiptError::kSchema);
  // 4294967297 must not narrow to 1 on this platform (R4).
  reject("\"schema\":3", "\"schema\":4294967297", RouteReceiptError::kSchema);
  reject("\"schema\":3", "\"schema\":2.5", RouteReceiptError::kSchema);
  reject("\"evidence_rules\":17", "\"evidence_rules\":16",
         RouteReceiptError::kEvidence);
  reject("\"evidence_rules\":17", "\"evidence_rules\":4294967297",
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
  reject("language_and_host_v2", "language_and_host_v3",
         RouteReceiptError::kIdentity);
  reject("\"work_revision\":null", "\"work_revision\":0",
         RouteReceiptError::kSchema);  // revisions are typed absent, invented
  reject("\"route\":\"value_tree\"", "\"route\":\"third_route\"",
         RouteReceiptError::kRoute);
  reject("\"stage\":\"decoded\"", "\"stage\":\"half_decoded\"",
         RouteReceiptError::kStage);
  reject("\"coverage\":\"unsupported\"", "\"coverage\":\"complete-ish\"",
         RouteReceiptError::kCoverage);
  reject("\"reason\":\"route_unavailable\"", "\"reason\":\"mystery\"",
         RouteReceiptError::kFailure);
  reject("\"legacy_writer_used\":false", "\"legacy_writer_used\":\"false\"",
         RouteReceiptError::kWriter);
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
            remove_field("legacy_writer_used"))) ==
        RouteReceiptError::kWriter);
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
  rejected(value_writer, RouteReceiptError::kWriter);
  rejected(complete_early, RouteReceiptError::kStage);
  rejected(complete_failed, RouteReceiptError::kCoverage);
  rejected(legacy_failed, RouteReceiptError::kFailure);
  rejected(unbound, RouteReceiptError::kIdentity);
  rejected(invented_revision, RouteReceiptError::kSchema);
  rejected(published_refusal, RouteReceiptError::kStage);
  // Invalid external evidence must be rejected independently of publication.
  auto contradiction = nlohmann::json::parse(text);
  contradiction["legacy_writer_used"] = true;
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(contradiction.dump())) ==
        RouteReceiptError::kWriter);
  contradiction = nlohmann::json::parse(text);
  contradiction["stage"] = "published";
  contradiction["work_revision"] = 0;
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(contradiction.dump())) ==
        RouteReceiptError::kStage);
  // An identity-free receipt never parses (R6: compare the error alternative,
  // not the variant index).
  const auto identity_free =
      ParseRouteReceipt("{\"schema\":3,\"evidence_rules\":17}");
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

  // Stage ledgers only move forward along their own route's sequence: a
  // fresh ledger starts at its first stage, never jumps, and the legacy
  // ledger can never claim a value stage.
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
  RouteStageLedger legacy_ledger{SourceRoute::kLegacy};
  Check(legacy_ledger.Advance(SourceStage::kDecoded));
  Check(!legacy_ledger.Advance(SourceStage::kValuesImported));
  Check(!legacy_ledger.Advance(SourceStage::kTreeVerified));
  Check(legacy_ledger.Advance(SourceStage::kInputVerified));
  Check(legacy_ledger.Advance(SourceStage::kPrinted));
  Check(legacy_ledger.Advance(SourceStage::kPublished));
  Check(legacy_ledger.reached() == SourceStage::kPublished);

  // Child obligations close only on receipts that bind to the expected
  // input identity and are valid and Complete on the policy's accepted
  // routes (A02).
  const auto closes = [](RoutePolicy policy, MethodRouteReceipt receipt) {
    const ChildRouteObligation obligation{abc::EntityOffset{7586},
                                          SampleIdentity(), receipt};
    return RouteObligationsClosed(policy, {&obligation, 1});
  };
  Check(closes(RoutePolicy::kRequireValueTree, complete_value));
  Check(closes(RoutePolicy::kLegacyAllowed, complete_value));
  Check(closes(RoutePolicy::kLegacyAllowed, legacy_complete));
  Check(!closes(RoutePolicy::kRequireValueTree, legacy_complete));
  Check(!closes(RoutePolicy::kRequireValueTree, RefusedReceipt()));
  Check(!closes(RoutePolicy::kLegacyAllowed, tree_only));
  auto other_input = complete_value;
  other_input.identity.file_size += 1;
  Check(!closes(RoutePolicy::kRequireValueTree, other_input));
  ChildRouteObligation missing{abc::EntityOffset{7586}, SampleIdentity(), {}};
  Check(!RouteObligationsClosed(RoutePolicy::kRequireValueTree,
                                {&missing, 1}));
  Check(RouteObligationsClosed(RoutePolicy::kRequireValueTree, {}));
  auto mismatched = complete_value;
  mismatched.method = abc::EntityOffset{7559};
  Check(!closes(RoutePolicy::kRequireValueTree, mismatched));
  const ChildRouteObligation unbound_child{abc::EntityOffset{7586}, {}, unbound};
  Check(!RouteObligationsClosed(RoutePolicy::kRequireValueTree,
                                {&unbound_child, 1}));
  Check(!closes(RoutePolicy::kRequireValueTree, invented_revision));

  std::cout << "source_route_test: ok\n";
  return EXIT_SUCCESS;
}
