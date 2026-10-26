#include "../../src/source/function_tree.h"
#include "../../src/source/syntax_resolution.h"
#include "../../src/source/function_abi.h"
#include "../../src/source/implicit_constructor.h"
#include "../../src/source/constructor_forwarding.h"
#include "../../src/source/expression_forest.h"
#include "../../src/source/tree_structure.h"
#include "../../src/source/tree_semantics.h"
#include "analysis/ssa_fixture.h"
#include <climits>
#include "../../src/source/derived_projection.h"
#include "../../src/source/evaluation_plan.h"
#include "../../src/source/family_tree.h"
#include "../../src/source/family_print.h"
#include "../../src/source/prepared_family.h"
#include "../../src/source/family_emission.h"
#include "abcd/corpus/abc_file.h"
#include "abcd/source/source_route.h"
#include "abcd/source/record_source.h"

#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <source_location>
#include <sstream>

namespace {
void Check(bool value, const std::source_location& where = std::source_location::current()) {
  if (!value) { std::cerr << "check failed at " << where.line() << '\n'; std::abort(); }
}
}
int main(int argc, char** argv) {
  using namespace abcd;
  using namespace abcd::source;
  using namespace abcd::analysis;
  Check(argc == 2);
  const auto opened = corpus::AbcFile::Open(argv[1]);
  const auto& file = *std::get<std::unique_ptr<corpus::AbcFile>>(opened);
  const auto fingerprint = std::get<corpus::FileFingerprint>(file.Fingerprint());
  const auto header = std::get<abc::Header>(abc::ParseHeader(
      std::span(fingerprint.prefix).first(fingerprint.prefix_size), fingerprint.size));
  auto identity = MakeRouteInputIdentity(fingerprint, header);
  identity.contract = SourceContract::kLanguageSourceV1;
  const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(file, header));
  const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
  const auto roles = std::get<MethodRoleIndex>(MethodRoleIndex::Build(file, header, index, lookup));
  // The committed compiler fixture supplies a genuine class owner. All
  // protocol variations below replace only its decoded test body.
  Check(index.methods.size() == 2);
  const auto creator_position = lookup.Find(index.methods.front().offset); Check(creator_position.has_value());
  auto creator = ReadFunctionValues(file, header, index, *creator_position, identity,
      InputCaptureMode::kClosed);
  Check(std::holds_alternative<FileFunction>(creator));
  const auto& parent = std::get<FileFunction>(creator);
  std::optional<OpId> creation;
  for (std::uint32_t i = 0; i < parent.values.graph.ops.size(); ++i) {
    const auto& op = parent.values.graph.ops[i];
    if (op.life != ValueLife::kLive || !std::holds_alternative<ValueClassCreation>(op.operation)) continue;
    Check(!creation); creation = OpId{i};
  }
  Check(creation.has_value());
  auto resolved = ClassTreeContext::ResolveConstructor(file, header, index, lookup,
      roles, parent, *creation, identity);
  Check(std::holds_alternative<ClassTreeContext>(resolved));
  FunctionContext context{std::get<ClassTreeContext>(std::move(resolved))};
  Check(context.Target() == index.methods.back().offset && context.Callable() == CallableKind::kDerivedConstructor);
  const auto position = lookup.Find(context.Target()); Check(position.has_value());
  auto loaded = ReadFunctionValues(file, header, index, *position, identity,
      InputCaptureMode::kClosed, {}, nullptr, &context);
  Check(std::holds_alternative<FileFunction>(loaded));
  auto& function = std::get<FileFunction>(loaded);
  std::cerr << "frame parameters=" << function.frame.parameters << " extra=" << function.frame.extra_arguments << " length=" << context.Length() << '\n';
  std::size_t forwards = 0, assignments = 0;
  for (const auto& instruction : function.input.Input().instructions) {
    forwards += std::holds_alternative<SuperForward>(instruction.operation);
  }
  for (const auto& op : function.values.graph.ops)
    assignments += op.life == ValueLife::kLive && std::holds_alternative<ValueNamedWrite>(op.operation);
  Check(forwards == 1 && assignments == 2);
  Check(std::get<DerivedProjection>(ProjectDerivedConstructor(function, context)).form == DerivedConstructorForm::kInvocationRest);
  const auto build = [&](const std::vector<std::uint8_t>& bytes) {
    auto body = test::Body(bytes);
    auto built = FunctionInput::Build(identity, context.Target(), function.input.Record(), body,
        function.frame, InputCaptureMode::kClosed, {}, &context);
    Check(std::holds_alternative<FunctionInput>(built));
    auto input = std::get<FunctionInput>(std::move(built));
    auto values = BuildFunctionValues(input, body, function.frame);
    Check(std::holds_alternative<FunctionValues>(values));
    return FileFunction{std::move(body), function.frame, std::move(input), std::get<FunctionValues>(std::move(values))};
  };
  const auto emit = [](std::vector<std::uint8_t>& bytes, abc::Opcode opcode) {
    const auto wire = static_cast<std::uint16_t>(opcode);
    bytes.push_back(static_cast<std::uint8_t>(wire));
    if (wire > UINT8_MAX) bytes.push_back(static_cast<std::uint8_t>(wire >> CHAR_BIT));
  };
  const auto forward = [&](std::vector<std::uint8_t>& bytes) {
    emit(bytes, abc::Opcode::kCALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8);
    bytes.push_back(static_cast<std::uint8_t>(function.frame.function->value));
  };
  const auto returned = [&](std::vector<std::uint8_t>& bytes) {emit(bytes, abc::Opcode::kRETURN);};
  const auto check = [&](const std::vector<std::uint8_t>& bytes, std::optional<DerivedProjectionError> error) {
    auto candidate = build(bytes);
    const auto result = ProveConstructorForwarding(candidate, context);
    if (const auto* failure = std::get_if<DerivedProjectionError>(&result)) {
      if (error != *failure) std::cerr << "unexpected failure " << static_cast<unsigned>(*failure) << '\n';
      Check(error == *failure);
    } else {
      Check(!error); const auto& proof = std::get<ConstructorForwardingProof>(result);
      Check(!VerifyConstructorForwarding(candidate, context, proof));
      const auto projection = std::get<DerivedProjection>(ProjectDerivedConstructor(candidate, context));
      Check(!VerifyDerivedProjection(candidate, context, projection));
      if (projection.form == DerivedConstructorForm::kInvocationRest) {
        const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(candidate.values.graph, {},
            IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, {}, projection.guards));
        std::vector<ExpressionConstant> entries(candidate.values.graph.ops.size());
        ExpressionConstants constants{entries};
        constants.receiver_guards = projection.guards;
        constants.derived_constructor = &candidate;
        constants.forwarding_owner = &context;
        auto built = BuildExpressionForest(candidate.values.graph, evaluation, candidate.frame.parameters, {0}, constants,
            {}, {}, RecoveryMode::kCompleteOnly, {}, {}, {}, CallableKind::kDerivedConstructor,
            kSourceArithmetic, ParameterForm::kInvocationRest);
        if (const auto* failure = std::get_if<ExpressionFailure>(&built))
          std::cerr << "forest failure " << static_cast<unsigned>(failure->kind) << '\n';
        Check(std::holds_alternative<ExpressionForest>(built));
        const auto& forest = std::get<ExpressionForest>(built);
        for (const auto call : proof.calls) {
          Check(forest.operations[call.value].has_value());
          const auto& expression = std::get<SuperCallExpr>(forest.expressions[forest.operations[call.value]->value].expression);
          Check(expression.source == SuperArgumentSource::kInvocationFrame);
          const auto& arguments = std::get<SpreadArguments>(expression.arguments);
          Check(std::get<SymbolExpr>(forest.expressions[arguments.iterable.value].expression).symbol == SymbolId{0});
        }
      }
      if (projection.form == DerivedConstructorForm::kInvocationRest) {
        const auto control = std::get<ControlPlan>(BuildControlPlan(candidate.values.graph));
        const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(candidate.values.graph, {},
            IncomingCells::kReject, {}, {}, {}, {}, ClosureWrites::kUnknown, {}, projection.guards));
        auto syntax = std::get<ResolvedSyntax>(ResolveFunctionSyntax(file, header, index, context,
            candidate.body, candidate.values.graph, {0}));
        syntax.syntax.parameter_form = ParameterForm::kInvocationRest;
        auto constants = syntax.View(); constants.receiver_guards = projection.guards;
        constants.derived_constructor = &candidate; constants.forwarding_owner = &context;
        const auto built = BuildFunctionTree(candidate.values.graph, control, evaluation, std::move(syntax.syntax), constants);
        if (const auto* failure = std::get_if<FunctionTreeFailure>(&built))
          std::cerr << "tree failure " << static_cast<unsigned>(failure->kind) << '\n';
        Check(std::holds_alternative<ConstructedFunction>(built));
        const auto& tree = std::get<ConstructedFunction>(built).tree;
        const auto abi = BuildFunctionAbi(candidate.values.graph, tree, candidate.frame, context.Length());
        if (const auto* failure = std::get_if<FunctionAbiError>(&abi))
          std::cerr << "abi failure " << static_cast<unsigned>(*failure) << '\n';
        Check(std::holds_alternative<ConstructedAbi>(abi));
        ConstructedFamily draft;
        draft.root = {0}; draft.parents.resize(1); draft.order.push_back({0});
        draft.members.push_back({tree, *std::get<ConstructedFunction>(built).flow});
        std::ostringstream output;
        const GlobalScope destination{};
        const auto printed = PrintClassFamilyDraft(draft, *context.ClassContext(), output, {}, {}, &destination);
        Check(std::holds_alternative<PrintedFamilyDraft>(printed));
        Check(output.str().find("constructor(...") != std::string::npos);
        Check(output.str().find("super(...") != std::string::npos);
        auto forged = tree; forged.parameters.front().rest = false;
        Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(forged)));
        Check(std::holds_alternative<FunctionAbiError>(BuildFunctionAbi(candidate.values.graph, forged, candidate.frame, context.Length())));
        forged = tree; forged.parameter_form = ParameterForm::kFrameFormals;
        Check(std::holds_alternative<TreeStructureError>(VerifyTreeStructure(forged)));
        auto wrong = constants; wrong.forwarding_owner = nullptr;
        Check(std::holds_alternative<TreeSemanticError>(VerifyTreeSemantics(candidate.values.graph, control, evaluation, tree, wrong)));
      }
      auto forged = proof; forged.calls.clear();
      Check(VerifyConstructorForwarding(candidate, context, forged) == DerivedProjectionError::kCoverage);
      forged = proof; forged.frame.extra_arguments = false;
      Check(VerifyConstructorForwarding(candidate, context, forged) == DerivedProjectionError::kCoverage);
      auto& call = candidate.values.graph.ops[proof.calls.front().value];
      candidate.values.graph.uses[call.first_use.value].operand = SpecialValue::kUndefined;
      Check(VerifyConstructorForwarding(candidate, context, proof) == DerivedProjectionError::kOperand);
    }
  };
  {
    std::vector<std::uint8_t> bytes; forward(bytes); returned(bytes);
    const auto candidate = build(bytes);
    Check(std::get<DerivedProjection>(ProjectDerivedConstructor(candidate, context)).form == DerivedConstructorForm::kImplicit);
  }
  std::uint64_t previous_work = 0; std::size_t previous_bytes = 0;
  for (const auto count : {0U, 1U, 200U, 201U, 400U, 800U}) {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kDEBUGGER); // Preserved observable prefix, not implicit elision.
    forward(bytes);
    for (unsigned i = 0; i < count; ++i) emit(bytes, abc::Opcode::kDEBUGGER);
    returned(bytes); check(bytes, {});
    auto candidate = build(bytes);
    Check(std::holds_alternative<DerivedProjectionError>(ProveImplicitConstructor(candidate)));
    const auto proof = std::get<ConstructorForwardingProof>(ProveConstructorForwarding(candidate, context));
    Check(std::holds_alternative<ConstructorForwardingProof>(ProveConstructorForwarding(candidate, context,
        {proof.allocated_bytes, proof.work})));
    Check(std::get<DerivedProjectionError>(ProveConstructorForwarding(candidate, context,
        {proof.allocated_bytes - 1, proof.work})) == DerivedProjectionError::kResourceLimit);
    Check(std::get<DerivedProjectionError>(ProveConstructorForwarding(candidate, context,
        {proof.allocated_bytes, proof.work - 1})) == DerivedProjectionError::kWorkLimit);
    if (count >= 201) Check(proof.work <= previous_work * 3 && proof.allocated_bytes <= previous_bytes * 3);
    previous_work = proof.work; previous_bytes = proof.allocated_bytes;
    std::cout << "forwarding " << count << " work=" << proof.work << " bytes=" << proof.allocated_bytes << '\n';
  }
  {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kDEBUGGER);
    emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(static_cast<std::uint8_t>(function.frame.function->value));
    emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);
    emit(bytes, abc::Opcode::kJMP_IMM8); bytes.push_back(2);
    emit(bytes, abc::Opcode::kCALLRUNTIME_SUPERCALLFORWARDALLARGS_PREF_V8); bytes.push_back(0);
    returned(bytes); check(bytes, {}); // Copy and block splitting preserve identity.
  }
  {
    std::vector<std::uint8_t> bytes; emit(bytes, abc::Opcode::kLDGLOBAL);
    const auto branch = bytes.size(); emit(bytes, abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
    forward(bytes); returned(bytes);
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    forward(bytes); returned(bytes); check(bytes, {}); // Distinct exclusive forwarding sites.
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes); forward(bytes); returned(bytes);
    check(bytes, DerivedProjectionError::kForwardingRepeated);
  }
  for (const auto slot : {function.frame.first_parameter.value, function.frame.new_target->value}) {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kLDUNDEFINED); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(static_cast<std::uint8_t>(slot));
    forward(bytes); returned(bytes); check(bytes, DerivedProjectionError::kForwardingFrameChanged);
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes);
    emit(bytes, abc::Opcode::kLDUNDEFINED); returned(bytes);
    check(bytes, DerivedProjectionError::kForwardingReturn);
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes); emit(bytes, abc::Opcode::kJMP_IMM8);
    bytes.push_back(static_cast<std::uint8_t>(-static_cast<int>(bytes.size() - 1)));
    check(bytes, DerivedProjectionError::kForwardingRepeated);
  }
  {
    std::vector<std::uint8_t> bytes; forward(bytes); returned(bytes);
    auto candidate = build(bytes);
    candidate.body.tries.resize(1);
    Check(std::get<DerivedProjectionError>(ProveConstructorForwarding(candidate, context)) == DerivedProjectionError::kForwardingHandlers);
  }
  {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kLDGLOBAL);
    const auto branch = bytes.size(); emit(bytes, abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
    forward(bytes); returned(bytes);
    bytes[branch + 1] = static_cast<std::uint8_t>(bytes.size() - branch);
    emit(bytes, abc::Opcode::kLDUNDEFINED); returned(bytes);
    check(bytes, DerivedProjectionError::kForwardingMissing);
  }
  {
    std::vector<std::uint8_t> bytes; emit(bytes, abc::Opcode::kGETUNMAPPEDARGS);
    forward(bytes); returned(bytes); check(bytes, DerivedProjectionError::kForwardingArguments);
  }
  {
    std::vector<std::uint8_t> bytes; emit(bytes, abc::Opcode::kCOPYRESTARGS_IMM8); bytes.push_back(0);
    forward(bytes); returned(bytes); check(bytes, DerivedProjectionError::kForwardingParameters);
  }
  for (const auto slot : {function.frame.first_parameter.value, function.frame.new_target->value}) {
    std::vector<std::uint8_t> bytes;
    emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(static_cast<std::uint8_t>(slot));
    emit(bytes, abc::Opcode::kISTRUE); // Observation, unlike a dead scalar copy.
    forward(bytes); returned(bytes);
    check(bytes, slot == function.frame.first_parameter.value ?
        DerivedProjectionError::kForwardingParameters : DerivedProjectionError::kForwardingNewTarget);
  }
  for (const bool before : {false, true}) {
    std::vector<std::uint8_t> bytes;
    if (!before) {forward(bytes); emit(bytes, abc::Opcode::kSTA_V8); bytes.push_back(0);}
    const auto loop = bytes.size(); emit(bytes, abc::Opcode::kLDGLOBAL);
    const auto exit = bytes.size(); emit(bytes, abc::Opcode::kJEQZ_IMM8); bytes.push_back(0);
    emit(bytes, abc::Opcode::kDEBUGGER);
    const auto back = bytes.size(); emit(bytes, abc::Opcode::kJMP_IMM8);
    bytes.push_back(static_cast<std::uint8_t>(static_cast<int>(loop) - static_cast<int>(back)));
    bytes[exit + 1] = static_cast<std::uint8_t>(bytes.size() - exit);
    if (before) forward(bytes);
    else {emit(bytes, abc::Opcode::kLDA_V8); bytes.push_back(0);}
    returned(bytes); check(bytes, {});
  }
}
