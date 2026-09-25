#include "abcd/source/source_route.h"

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
MethodRouteReceipt SampleReceipt() {
  MethodRouteReceipt receipt;
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
  return left.method == right.method && left.record == right.record &&
         left.route == right.route && left.stage == right.stage &&
         left.coverage == right.coverage &&
         left.legacy_writer_used == right.legacy_writer_used &&
         left.failure.has_value() == right.failure.has_value() &&
         (!left.failure ||
          (left.failure->failure_class == right.failure->failure_class &&
           left.failure->reason == right.failure->reason &&
           left.failure->pc == right.failure->pc));
}
}  // namespace

// Not executed while the user's test pause is in force (task T00). The
// assertions document the versioned receipt boundary and the A01 admission
// contract; run via `ctest --test-dir build -R '^source_route_test$'` after
// the pause is lifted.
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

  // Receipt round trip through the versioned boundary, with and without
  // optional identity/stage/failure content.
  const auto samples = {SampleReceipt(), [] {
                          MethodRouteReceipt record_only;
                          record_only.record = abc::EntityOffset{4092};
                          record_only.route = SourceRoute::kLegacy;
                          record_only.stage = SourceStage::kPrinted;
                          record_only.coverage = Coverage::kComplete;
                          record_only.legacy_writer_used = true;
                          return record_only;
                        }()};
  for (const auto& receipt : samples) {
    const auto parsed =
        ParseRouteReceipt(SerializeRouteReceipt(receipt));
    const auto* recovered = std::get_if<MethodRouteReceipt>(&parsed);
    Check(recovered != nullptr);
    Check(SameReceipt(receipt, *recovered));
  }

  // Unknown versions, labels and identity-free receipts are rejected; a
  // parse never defaults into success. Each rejection works on a fresh copy.
  const auto text = SerializeRouteReceipt(SampleReceipt());
  const auto reject = [&text](std::string_view from, std::string_view to,
                              RouteReceiptError expected) {
    std::string mutated{text};
    const auto at = mutated.find(from);
    Check(at != std::string::npos);
    mutated.replace(at, from.size(), to);
    Check(std::get<RouteReceiptError>(ParseRouteReceipt(mutated)) == expected);
  };
  reject("\"schema\":1", "\"schema\":2", RouteReceiptError::kSchema);
  reject("\"schema\":1", "\"schema\":\"1\"", RouteReceiptError::kSchema);
  reject("\"route\":\"value_tree\"", "\"route\":\"third_route\"",
         RouteReceiptError::kRoute);
  reject("\"stage\":\"decoded\"", "\"stage\":\"half_decoded\"",
         RouteReceiptError::kStage);
  MethodRouteReceipt unstaged = SampleReceipt();
  unstaged.stage = {};
  const auto unstaged_text = SerializeRouteReceipt(unstaged);
  Check(SameReceipt(unstaged,
                    std::get<MethodRouteReceipt>(
                        ParseRouteReceipt(unstaged_text))));
  std::string numeric_stage{unstaged_text};
  Check(numeric_stage.replace(numeric_stage.find("\"stage\":null"), 12,
                              "\"stage\":7")
            .find("\"stage\":7") != std::string::npos);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt(numeric_stage)) ==
        RouteReceiptError::kStage);
  reject("\"coverage\":\"unsupported\"", "\"coverage\":\"complete-ish\"",
         RouteReceiptError::kCoverage);
  reject("\"reason\":\"route_unavailable\"", "\"reason\":\"mystery\"",
         RouteReceiptError::kFailure);
  Check(ParseRouteReceipt("{\"schema\":1}").index() ==
        static_cast<std::size_t>(RouteReceiptError::kIdentity));
  MethodRouteReceipt anonymous = SampleReceipt();
  anonymous.method = {};
  anonymous.record = {};
  Check(std::get<RouteReceiptError>(
            ParseRouteReceipt(SerializeRouteReceipt(anonymous))) ==
        RouteReceiptError::kIdentity);
  Check(std::get<RouteReceiptError>(ParseRouteReceipt("{")) ==
        RouteReceiptError::kSyntax);

  // Stage ledgers only move forward.
  RouteStageLedger ledger{SourceRoute::kValueTree};
  Check(!ledger.reached());
  Check(ledger.Advance(SourceStage::kDecoded));
  Check(ledger.Advance(SourceStage::kInputVerified));
  Check(!ledger.Advance(SourceStage::kDecoded));
  Check(!ledger.Advance(SourceStage::kInputVerified));
  Check(ledger.reached() == SourceStage::kInputVerified);

  // Child obligations close only on the policy's route with complete,
  // unfailed receipts (A02).
  const auto closed = [](MethodRouteReceipt receipt) {
    ChildRouteObligation obligation{abc::EntityOffset{7586}, receipt};
    return RouteObligationsClosed(RoutePolicy::kRequireValueTree,
                                  {&obligation, 1});
  };
  auto complete = SampleReceipt();
  complete.coverage = Coverage::kComplete;
  complete.failure = {};
  Check(closed(complete));
  auto legacy_child = complete;
  legacy_child.route = SourceRoute::kLegacy;
  Check(!closed(legacy_child));  // legacy child never closes a value-tree parent
  Check(!closed(SampleReceipt()));  // unsupported coverage keeps it open
  ChildRouteObligation missing{abc::EntityOffset{7586}, {}};
  Check(!RouteObligationsClosed(RoutePolicy::kRequireValueTree,
                                {&missing, 1}));
  Check(RouteObligationsClosed(RoutePolicy::kRequireValueTree, {}));
  auto complete_legacy = complete;
  complete_legacy.route = SourceRoute::kLegacy;
  ChildRouteObligation legacy_pair{abc::EntityOffset{7586}, complete_legacy};
  Check(RouteObligationsClosed(RoutePolicy::kLegacyAllowed,
                               {&legacy_pair, 1}));
  auto mismatched = complete;
  mismatched.method = abc::EntityOffset{7559};
  Check(!closed(mismatched));  // receipt must match the obligation identity

  std::cout << "source_route_test: ok\n";
  return EXIT_SUCCESS;
}
