#ifndef ABCD_SOURCE_PHASED_PARAMETER_DEFAULTS_H_
#define ABCD_SOURCE_PHASED_PARAMETER_DEFAULTS_H_

#include "phased_body.h"
#include "tree_checks.h"

namespace abcd::source {
enum class PhasedParameterError : std::uint8_t {
  kInput,
  kOrdinal,
  kDecision,
  kInitializer,
  kOrder,
  kEscape,
  kCapture,
  kCorrespondence,
  kResourceLimit,
  kWorkLimit
};
using PhasedDefaultLiteral =
    std::variant<analysis::SpecialValue, analysis::IntegerLiteral,
                 analysis::Float64Bits, abc::PoolIndex>;
struct PhasedParameterCommit {
  analysis::OpId store;
  analysis::OpId initial_store;
  CellId cell;
  analysis::WorkBlockId before, commit;
};
struct PhasedParameterDefault {
  std::uint32_t ordinal;
  analysis::ValueId entry;
  analysis::OpId comparison, decision, initializer;
  analysis::EdgeId provided, missing;
  analysis::WorkBlockId join;
  std::vector<analysis::ValueId> initialized;
  std::vector<analysis::OpId> initializer_ops;
  std::vector<PhasedParameterCommit> captures;
  PhasedDefaultLiteral literal;
};
struct PhasedParameterCertificate {
  std::uint32_t formals = 0, length = 0;
  std::vector<PhasedParameterDefault> defaults;
  std::vector<analysis::OpId> prefix;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<PhasedParameterCertificate, PhasedParameterError>
ProvePhasedParameterDefaults(const PhasedBody&, const abc::MethodBody&,
                             const abc::FrameLayout&, std::uint32_t length,
                             analysis::SemanticBudget = {});
std::variant<std::uint64_t, PhasedParameterError> VerifyPhasedParameterDefaults(
    const PhasedBody&, const abc::MethodBody&, const abc::FrameLayout&,
    const PhasedParameterCertificate&, analysis::SemanticBudget = {});
// Native initialized formals are the residual's entry contract. The original
// phase input remains owned here, independently replayable by every verifier.
// Only decisions change; instruction IDs, byte offsets and catch ports do not.
struct PhasedParameterResidual {
  abc::MethodBody body;
  std::size_t allocated_bytes;
  std::uint64_t work;
};
class PhasedParameterDefaults {
 public:
  static std::variant<std::shared_ptr<const PhasedParameterDefaults>,
                      PhasedParameterError>
  Build(PhasedBody, abc::MethodBody, const abc::FrameLayout&, std::uint32_t,
        analysis::SemanticBudget = {});
  const PhasedParameterCertificate& Evidence() const { return evidence_; }
  const PhasedBody& Original() const { return original_; }
  const abc::MethodBody& Body() const { return body_; }
  const abc::FrameLayout& Frame() const { return frame_; }
  std::span<const SignatureString> Strings() const { return strings_; }
  std::variant<PhasedParameterResidual, PhasedParameterError> Residual(
      analysis::SemanticBudget = {}) const;
  std::size_t AllocatedBytes() const { return evidence_.allocated_bytes; }
  std::uint64_t Work() const { return evidence_.work; }

 private:
  PhasedParameterDefaults(PhasedBody p, abc::MethodBody b, abc::FrameLayout f,
                          PhasedParameterCertificate c)
      : original_(std::move(p)),
        body_(std::move(b)),
        frame_(f),
        evidence_(std::move(c)) {}
  PhasedBody original_;
  abc::MethodBody body_;
  abc::FrameLayout frame_;
  PhasedParameterCertificate evidence_;
  std::vector<SignatureString> strings_;
};
void BuildPhasedDefaultSignature(std::shared_ptr<const PhasedParameterDefaults>,
                                 const analysis::ValueProgram&, FunctionTree&,
                                 const ExpressionConstants&, TreeCheckBudget&);
void VerifyPhasedDefaultSignature(const FunctionTree&, TreeCheckBudget&);
void VerifyPhasedDefaultInput(const analysis::ValueProgram&,
                              const FunctionTree&, TreeCheckBudget&);
struct RecoveryFlow;
void VerifyPhasedDefaultRecovery(const FunctionTree&, const RecoveryFlow&,
                                 TreeCheckBudget&);
}  // namespace abcd::source
#endif
