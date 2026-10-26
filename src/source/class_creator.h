#ifndef ABCD_SOURCE_CLASS_CREATOR_H_
#define ABCD_SOURCE_CLASS_CREATOR_H_
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "../analysis/value_heritage.h"
#include "abcd/analysis/exception_inputs.h"
#include "abcd/source/source_route.h"

namespace abcd::analysis {
struct ValueEnvironments;
}
namespace abcd::source {
struct CellPlan;
struct ClassDefinitionSite;
struct ClassInitializerCall;
struct ClassInstanceInitializer;
struct FileFunction;
class FilePhasedFunction;
struct ClassSelfDeferred;
class ClassSelfOrder;
class FunctionContext;
// Working coordinates own Before/Commit and captures. Original coordinates
// select immutable file indexes, including across await/yield normalization.
struct PhasedClassPoint {
  analysis::OpId operation;
  analysis::InstructionId instruction;
  abc::CodeOffset pc, original;
  analysis::ExceptionNodeId before, commit;
  analysis::EnvironmentRef environment;
  std::optional<analysis::DispatchTarget> exception;
  bool operator==(const PhasedClassPoint&) const = default;
};
struct PhasedClassSites {
  analysis::ValueHeritages heritage;
  struct Source {
    abc::CodeOffset pc;
    analysis::InstructionId instruction;
  };
  std::vector<Source> sources;
  std::size_t allocated_bytes = 0;
  std::uint64_t work = 0;
};
std::variant<PhasedClassSites, analysis::ValueHeritageError>
BuildPhasedClassSites(const analysis::ValueProgram&, analysis::SemanticBudget);
// Borrowed discriminated creator, never a normal SSA wrapper around phase SSA.
// Null represents an absent external creator at the existing preparation API.
class ClassCreator {
 public:
  ClassCreator() = default;
  ClassCreator(std::nullptr_t) {}
  ClassCreator(const FileFunction& f) : input_(&f) {}
  ClassCreator(const FileFunction* f) : input_(f) {}
  ClassCreator(const FilePhasedFunction& f) : input_(&f) {}
  const FileFunction* Normal() const;
  const FilePhasedFunction* Phase() const;
  explicit operator bool() const { return Normal() || Phase(); }
  const analysis::ValueProgram& Graph() const;
  const RouteInputIdentity& Identity() const;
  abc::EntityOffset Method() const;
  abc::EntityOffset Record() const;
  const abc::MethodBody& Body() const;
  std::span<const analysis::SemanticInstruction> Instructions() const;
  std::span<const analysis::ValueClassHeritage> Classes() const;
  std::span<const ClassDefinitionSite> Definitions() const;
  std::span<const ClassInitializerCall> Initializers() const;
  std::span<const ClassInstanceInitializer> Instances() const;
  const ClassSelfOrder* SelfOrder() const;
  std::shared_ptr<ClassSelfDeferred> Self() const;
  const analysis::ValueEnvironments* Environments() const;
  const CellPlan* Cells() const;
  std::optional<analysis::InstructionId> FindSite(
      abc::CodeOffset original) const;
  std::optional<analysis::InstructionId> Site(const FunctionContext&) const;
  std::optional<PhasedClassPoint> Point(analysis::OpId) const;

 private:
  std::variant<const FileFunction*, const FilePhasedFunction*> input_{
      static_cast<const FileFunction*>(nullptr)};
};
// Minted only after all prepared roots bind to the actual creator storage.
// The enclosing tree and whole-family write checks still own final visibility.
class PhasedClassBinding {
 public:
  bool Matches(ClassCreator, analysis::OpId) const;

 private:
  friend struct CreatorClassBindingAccess;
  PhasedClassBinding(ClassCreator c, PhasedClassPoint p)
      : identity_(c.Identity()),
        method_(c.Method()),
        revision_(c.Graph().revision),
        cfg_(c.Graph().cfg_revision),
        point_(p) {}
  RouteInputIdentity identity_;
  abc::EntityOffset method_;
  analysis::IrRevision revision_;
  analysis::CfgRevision cfg_;
  PhasedClassPoint point_;
};
}  // namespace abcd::source
#endif
