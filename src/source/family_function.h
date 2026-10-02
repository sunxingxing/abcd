#ifndef ABCD_SOURCE_FAMILY_FUNCTION_H_
#define ABCD_SOURCE_FAMILY_FUNCTION_H_

#include "function_file.h"
#include "phased_function_file.h"

namespace abcd::source {
// Distinct owned flows, one family occurrence. Never manufacture a normal
// input from a phase graph. Only normal residuals support default projection.
class FamilyFunction {
 public:
  FamilyFunction(FileFunction function) : input_(std::move(function)) {}
  FamilyFunction(FilePhasedFunction function) : input_(std::move(function)) {}
  FileFunction& Normal() { return std::get<FileFunction>(input_); }
  const FileFunction& Normal() const { return std::get<FileFunction>(input_); }
  const FilePhasedFunction* Phase() const { return std::get_if<FilePhasedFunction>(&input_); }
  const abc::MethodBody& Body() const { return Phase() ? Phase()->Body() : Normal().body; }
  const analysis::ValueProgram& Graph() const { return Phase() ? Phase()->Phases().Graph() : Normal().values.graph; }
  const RouteInputIdentity& Identity() const { return Phase() ? Phase()->Phases().Identity() : Normal().input.Identity(); }
  abc::EntityOffset Method() const { return Phase() ? Phase()->Phases().Target() : Normal().input.Method(); }
  abc::EntityOffset Record() const { return Phase() ? Phase()->Record() : Normal().input.Record(); }
  CaptureChild Capture() const {
    return Phase() ? CaptureChild{*Phase(), Storage()} : CaptureChild{NormalCapture()};
  }
  CaptureFunction NormalCapture() const { return {Normal().input, Normal().values, Storage(), Normal().module_capture_prefix ? &*Normal().module_capture_prefix : nullptr}; }
  const CaptureStorage* Storage() const { return storage_ ? &*storage_ : nullptr; }
  const CellPlan* Cells() const { return Capture().Cells(); }
  void SetStorage(CaptureStorage storage) { storage_ = std::move(storage); }
  void ClearStorage() { storage_.reset(); }
  std::span<const analysis::InstructionId> Children() const { return Phase() ? Phase()->Children() : Normal().input.Children(); }
  std::span<const analysis::SemanticInstruction> Instructions() const {
    return Phase() ? std::span<const analysis::SemanticInstruction>(Phase()->Phases().TreeInput(Phase()->Body()).input.flow.instructions)
                   : std::span<const analysis::SemanticInstruction>(Normal().input.Input().instructions);
  }
 private:
  std::variant<FileFunction, FilePhasedFunction> input_;
  std::optional<CaptureStorage> storage_;
};
}  // namespace abcd::source
#endif
