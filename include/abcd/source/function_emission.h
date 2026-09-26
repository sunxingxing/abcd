#ifndef ABCD_SOURCE_FUNCTION_EMISSION_H_
#define ABCD_SOURCE_FUNCTION_EMISSION_H_

#include "abcd/source/tree_printer.h"

namespace abcd::source {
struct GeneratedOffset { std::size_t byte = 0; };
// Owned semantics/positions for enclosing class/record assembly; emitted bytes
// stay in the caller's stream. Keep the
// verified tree (including ABI/signature and origin DAG) with its position
// tables; no borrowed file/index, source reparsing or duplicated ancestor PCs.
// Position tables are local to destination.begin; transport is one offset,
// not a rewrite of every span. A complete METHOD receipt never certifies the
// containing class/record or its other methods.
class FunctionEmission {
 public:
  FunctionEmission(FunctionEmission&&) noexcept = default;
  FunctionEmission& operator=(FunctionEmission&&) noexcept = default;
  [[nodiscard]] static std::variant<FunctionEmission, TreePrintError> Write(
      VerifiedFunction function, std::ostream& output, GeneratedOffset destination,
      EmitBudget output_budget = {}, analysis::SemanticBudget scratch = {});
  [[nodiscard]] const VerifiedFunction& Function() const { return function_; }
  [[nodiscard]] const PrintedFunction& Positions() const { return positions_; }
  [[nodiscard]] GeneratedRange Destination() const { return destination_; }
  [[nodiscard]] const MethodRouteReceipt& Receipt() const { return receipt_; }
 private:
  FunctionEmission(VerifiedFunction function, PrintedFunction positions,
                   GeneratedRange destination, MethodRouteReceipt receipt)
      : function_(std::move(function)), positions_(std::move(positions)),
        destination_(destination), receipt_(receipt) {}
  VerifiedFunction function_;
  PrintedFunction positions_;
  GeneratedRange destination_;
  MethodRouteReceipt receipt_;
};
}  // namespace abcd::source
#endif
