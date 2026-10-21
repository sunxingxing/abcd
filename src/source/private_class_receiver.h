#ifndef ABCD_SOURCE_PRIVATE_CLASS_RECEIVER_H_
#define ABCD_SOURCE_PRIVATE_CLASS_RECEIVER_H_
#include "abcd/analysis/value_ir.h"
namespace abcd::source {
enum class PrivateClassReceiverError : std::uint8_t {
  kInput, kObservation, kOrder, kResourceLimit, kWorkLimit
};
struct PrivateClassException {
  analysis::OpId operation;
  analysis::OriginId origin;
  analysis::LanguageEffect before_throw;
  bool operator==(const PrivateClassException&) const = default;
};
struct PrivateClassReceiver {
  analysis::IrRevision revision;
  analysis::CfgRevision control;
  std::vector<analysis::OpId> effects, fields;
  std::vector<PrivateClassException> exceptions;
  bool operator==(const PrivateClassReceiver&) const = default;
};
struct PrivateClassReceiverProof {
  PrivateClassReceiver evidence;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
// Uncaptured initializer only: the caller proves the receiver is its private
// fresh class and closes all child capture obligations. Arbitrary RHS effects stay in place; no receiver read, escape,
// capture, returned value or repeated effect is authorized by this proof.
std::variant<PrivateClassReceiverProof, PrivateClassReceiverError>
BuildPrivateClassReceiver(const analysis::ValueProgram&, analysis::SemanticBudget = {});
std::variant<PrivateClassReceiverProof, PrivateClassReceiverError>
VerifyPrivateClassReceiver(const analysis::ValueProgram&, const PrivateClassReceiver&,
                           analysis::SemanticBudget = {});
}  // namespace abcd::source
#endif
