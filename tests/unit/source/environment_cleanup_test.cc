#include "../../../src/source/cleanup_body.h"
#include "analysis/ssa_fixture.h"
#include "../../../src/source/function_values.h"
#include "../../../src/source/function_tree.h"
#include "../../../src/source/class_self.h"
#include "../../../src/source/class_initializer_file.h"
#include "../../../src/source/literal_class_fields.h"
#include "../../../src/source/instruction_sources.h"
#include "../../../tools/tree_position_json.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include "abcd/abc/call_type.h"
#include <algorithm>
#include <limits>
#include <string>

#include <type_traits>

namespace {
using namespace abcd;
using namespace abcd::source;
enum class Mutation { kNone, kOverwrite, kReturn, kEntry, kOuterPop, kDebugger, kTyped, kDoublePop };
static_assert(!std::is_default_constructible_v<EnvironmentCleanup>);
static_assert(!std::is_copy_constructible_v<EnvironmentCleanup>);
abc::MethodBody Body(unsigned count, Mutation mutation = Mutation::kNone) {
  const auto op = [](abc::Opcode code) { return static_cast<std::uint8_t>(code); };
  constexpr unsigned kByteBits = 8;
  constexpr auto kThrow = static_cast<std::uint16_t>(abc::Opcode::kTHROW_PREF_NONE);
  std::vector<std::uint8_t> bytes;
  std::vector<abc::TryRegion> tries;
  std::vector<abc::CatchHandler> handlers;
  for (unsigned i = 0; i < count; ++i) {
    if (mutation != Mutation::kOuterPop) bytes.insert(bytes.end(),{op(abc::Opcode::kNEWLEXENV_IMM8),1});
    if (mutation == Mutation::kDoublePop) bytes.insert(bytes.end(),{op(abc::Opcode::kNEWLEXENV_IMM8),1});
    const auto begin = static_cast<std::uint32_t>(bytes.size());
    bytes.insert(bytes.end(),{op(abc::Opcode::kTRYLDGLOBALBYNAME_IMM8_ID16),0,0,0});
    tries.push_back({{begin},static_cast<std::uint32_t>(bytes.size()-begin),i,1});
    if (mutation == Mutation::kEntry) {
      test::Check(count == 1);
      constexpr std::uint8_t kJumpSize = 2;
      bytes.insert(bytes.end(),{op(abc::Opcode::kJMP_IMM8),kJumpSize});
    } else {
      bytes.push_back(op(abc::Opcode::kPOPLEXENV));
      if (mutation == Mutation::kDoublePop) bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    }
  }
  if (mutation != Mutation::kEntry) bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
  for (unsigned i = 0; i < count; ++i) {
    const auto begin = static_cast<std::uint32_t>(bytes.size());
    if (mutation == Mutation::kOverwrite) bytes.push_back(op(abc::Opcode::kLDUNDEFINED));
    if (mutation == Mutation::kDebugger) bytes.push_back(op(abc::Opcode::kDEBUGGER));
    bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    if (mutation == Mutation::kDoublePop) bytes.push_back(op(abc::Opcode::kPOPLEXENV));
    if (mutation == Mutation::kReturn) bytes.push_back(op(abc::Opcode::kRETURNUNDEFINED));
    else bytes.insert(bytes.end(),{static_cast<std::uint8_t>(kThrow),static_cast<std::uint8_t>(kThrow >> kByteBits)});
    handlers.push_back({mutation == Mutation::kTyped ? std::optional{abc::CatchTypeIndex{0}} : std::nullopt,
        {begin},static_cast<std::uint32_t>(bytes.size()-begin)});
  }
  auto body = test::Body(bytes); body.tries = std::move(tries); body.handlers = std::move(handlers); return body;
}
}
int main(int argc, char** argv) {
  if (argc == 3) {
    auto file = std::get<std::unique_ptr<corpus::AbcFile>>(corpus::AbcFile::Open(argv[1]));
    const auto fingerprint = std::get<corpus::FileFingerprint>(file->Fingerprint());
    const auto header = std::get<abc::Header>(abc::ParseHeader(std::span(fingerprint.prefix).first(fingerprint.prefix_size),fingerprint.size));
    test::Check(header.checksum == fingerprint.adler32);
    const auto index = std::get<abc::MetadataIndex>(abc::BuildMetadataIndex(*file,header));
    const auto id = std::stoul(argv[2]); test::Check(id <= std::numeric_limits<std::uint32_t>::max());
    const auto method = std::find_if(index.methods.begin(),index.methods.end(),[&](const auto& item) { return item.offset.value == id; });
    test::Check(method != index.methods.end() && method->code);
    const auto call = std::get<abc::CallType>(abc::ReadCallType(*file,header,index,method->annotations));
    const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(method->code->registers,method->code->arguments,abc::DecodeCallFlags(call.value)));
    const auto body = std::get<abc::MethodBody>(abc::DecodeMethodBody(*file,header.profile,*method->code));
    const auto proof = EnvironmentCleanup::Build(body,header.profile,frame);
    test::Check(std::holds_alternative<EnvironmentCleanup>(proof));
    const auto& cleanup = std::get<EnvironmentCleanup>(proof);
    test::Check(cleanup.Handlers().size() == body.handlers.size());
    for (const auto& pop : cleanup.Pops())
      std::cout << "pop PC " << body.instructions[pop.instruction.value].offset.value
                << " allocation PC " << body.instructions[pop.allocation.value].offset.value << '\n';
    for (const auto& handler : cleanup.Handlers())
      std::cout << "handler " << handler.handler.value << " rethrow PC " << body.instructions[handler.rethrow.value].offset.value << '\n';
    const auto projection = std::get<CleanupBody>(CleanupBody::Build(body,header.profile,frame));
    const auto& projected = projection.Normal();
    test::Check(projection.Original().handlers.size() == body.handlers.size());
    for (const auto& pop : cleanup.Pops()) test::Check(projection.Kind(pop.instruction) == CleanupInstructionKind::kPop);
    RouteInputIdentity identity; identity.profile = header.profile; identity.abc_version = header.version; identity.file_size = header.file_size;
    auto input = FunctionInput::Build(identity,method->offset,method->owner_offset,projected,frame,InputCaptureMode::kConditional);
    test::Check(std::holds_alternative<FunctionInput>(input));
    auto values = BuildFunctionValues(std::get<FunctionInput>(input),projected,frame);
    test::Check(std::holds_alternative<FunctionValues>(values));
    std::cout << "normal-path input and values accepted; no source publication\n";
    const auto& working = std::get<FunctionValues>(values);
    test::Check(working.environments && working.cells);
    const auto self = std::get<ClassSelfStores>(FindClassSelfStores(working.graph,*working.environments,*working.cells,working.initializers.calls));
    test::Check(self.stores.size() == 1);
    for (const auto& binding : self.stores)
      std::cout << "class self: creation PC " << working.graph.origins[working.graph.ops[binding.creation.value].origin.value].pc.value
          << " store PC " << working.graph.origins[working.graph.ops[binding.store.value].origin.value].pc.value
          << " initializer PC " << working.graph.origins[working.graph.ops[binding.initializer.value].origin.value].pc.value << '\n';
    const auto lookup = std::get<abc::MethodLookup>(abc::MethodLookup::Build(index.methods));
    const auto roles = std::get<analysis::MethodRoleIndex>(analysis::MethodRoleIndex::Build(*file,header,index,lookup));
    const auto sites = std::get<analysis::FunctionCreationSites>(analysis::FunctionCreationSites::Build(*file,header,index,lookup));
    FileFunction parent{projected,frame,std::get<FunctionInput>(std::move(input)),std::get<FunctionValues>(std::move(values))};
    test::Check(parent.values.initializers.calls.size() == 1);
    auto loaded = ReadClassInitializerFunction(*file,header,index,lookup,roles,sites,parent,parent.values.initializers.calls[0]);
    test::Check(std::holds_alternative<ClassInitializerFunction>(loaded));
    const auto& initializer = std::get<ClassInitializerFunction>(loaded).function;
    test::Check(initializer.input.Captures().empty());
    const auto fields = LiteralClassFields::Build(initializer.values.graph);
    test::Check(std::holds_alternative<LiteralClassFields>(fields));
    test::Check(std::get<LiteralClassFields>(fields).Fields().size() == 1);
    unsigned writes = 0, returns = 0;
    for (const auto& operation : initializer.values.graph.ops) {
      if (operation.life != analysis::ValueLife::kLive) continue;
      if (std::holds_alternative<analysis::ValueOwnWrite>(operation.operation)) {
        const auto& stored = initializer.values.graph.uses[operation.first_use.value+1].operand;
        test::Check(std::get<analysis::SpecialValue>(stored) == analysis::SpecialValue::kUndefined); ++writes;
      } else if (std::holds_alternative<analysis::ValueReturn>(operation.operation)) ++returns;
      else test::Check(std::holds_alternative<analysis::ValueCopy>(operation.operation));
    }
    test::Check(writes == 1 && returns == 1);
    std::cout << "initializer: closed input, one undefined own-field write, no self read\n";
    return 0;
  }
  test::Check(argc == 1);
  const auto frame = std::get<abc::FrameLayout>(abc::BuildFrameLayout(0,0,{false,false,false,false}));
  for (const auto profile : {abc::ProfileId::kDynamic1202,abc::ProfileId::kDynamic1206,abc::ProfileId::kDynamic1301}) {
    std::uint64_t previous = 0;
    for (const auto count : {1U,2U,200U,201U,402U}) {
      const auto body = Body(count);
      const auto result = EnvironmentCleanup::Build(body,profile,frame);
      test::Check(std::holds_alternative<EnvironmentCleanup>(result));
      const auto& proof = std::get<EnvironmentCleanup>(result);
      const auto projection = std::get<CleanupBody>(CleanupBody::Build(body,profile,frame));
      test::Check(projection.Normal().tries.empty() && projection.Normal().handlers.empty());
      test::Check(projection.Original().tries.size() == body.tries.size() && projection.Normal().instructions.size() == body.instructions.size());
      for (std::size_t i = 0; i < body.instructions.size(); ++i) {
        test::Check(projection.Normal().instructions[i].offset == body.instructions[i].offset &&
            projection.Normal().instructions[i].branch_target == body.instructions[i].branch_target &&
            projection.Normal().instructions[i].decoded.opcode == body.instructions[i].decoded.opcode &&
            projection.Normal().instructions[i].decoded.size == body.instructions[i].decoded.size);
      }
      for (const auto& pop : proof.Pops()) test::Check(projection.Kind(pop.instruction) == CleanupInstructionKind::kPop);
      for (const auto& handler : proof.Handlers()) test::Check(projection.Kind(handler.rethrow) == CleanupInstructionKind::kRethrow);

      test::Check(proof.Handlers().size() == count && proof.Pops().size() == count);
      for (unsigned i = 0; i < count; ++i) {
        test::Check(proof.Handlers()[i].pop_count == 1 && proof.Handlers()[i].first_pop == i);
        test::Check(body.instructions[proof.Pops()[i].allocation.value].decoded.opcode == abc::Opcode::kNEWLEXENV_IMM8);
        test::Check(body.instructions[proof.Pops()[i].instruction.value].offset == body.handlers[i].offset);
        test::Check(body.instructions[proof.Handlers()[i].rethrow.value].decoded.opcode == abc::Opcode::kTHROW_PREF_NONE);
      }
      if (count == 402) test::Check(proof.Work() < 3*previous);
      previous = proof.Work();
    }
    {
      const auto original = Body(1);
      const auto projection = std::get<CleanupBody>(CleanupBody::Build(original,profile,frame));
      RouteInputIdentity identity; identity.profile = profile;
      const auto input = std::get<FunctionInput>(FunctionInput::Build(identity,{1},{2},projection.Normal(),frame,InputCaptureMode::kConditional));
      const auto values = std::get<FunctionValues>(BuildFunctionValues(input,projection.Normal(),frame));
      const auto control = std::get<ControlPlan>(BuildControlPlan(values.graph));
      const auto evaluation = std::get<EvaluationPlan>(BuildEvaluationPlan(values.graph));
      FunctionSyntax syntax{}; syntax.identity = identity; syntax.method = {1}; syntax.owner = {0};
      syntax.callable = CallableKind::kFunction; syntax.strictness = Strictness::kStrict; syntax.names = {u"observed"};
      std::vector<ExpressionConstant> constants(values.graph.ops.size());
      for (std::size_t i = 0; i < values.graph.ops.size(); ++i)
        if (std::holds_alternative<analysis::ValueGlobalRead>(values.graph.ops[i].operation)) constants[i] = NameId{0};
      auto tree = std::get<ConstructedFunction>(BuildFunctionTree(values.graph,control,evaluation,std::move(syntax),{constants,1})).tree;
      const auto sources = std::get<InstructionSources>(BuildInstructionSources(input,values.graph,tree,{},&projection));
      unsigned cleanup_count = 0;
      for (const auto& source : sources.occurrences) {
        const auto kind = projection.Kind({source.site.value});
        if (!kind) continue;
        test::Check(std::get<CleanupInstruction>(source.target).kind == *kind); ++cleanup_count;
      }
      test::Check(cleanup_count == 2);
      std::ostringstream generated;
      const GlobalScope globals{};
      const auto printed = std::get<PrintedFunction>(PrintFunctionTree(tree,generated,{}, {}, &globals));
      std::ostringstream serialized; tools::WriteTreePositions(serialized,tree,printed,&sources);
      const auto json = nlohmann::json::parse(serialized.str());
      unsigned mapped = 0;
      for (const auto& source : json.at("instruction_sources")) {
        const auto kind = projection.Kind({source.at("site").get<std::uint32_t>()});
        if (!kind) continue;
        test::Check(source.at("kind") == "environment_cleanup" && source.at("range").is_null());
        test::Check(source.at("cleanup") == (*kind == CleanupInstructionKind::kPop ? "pop" : "rethrow"));
        ++mapped;
      }
      test::Check(mapped == 2);

      const auto other = std::get<CleanupBody>(CleanupBody::Build(Body(1,Mutation::kDoublePop),profile,frame));
      test::Check(std::holds_alternative<InstructionSourceError>(BuildInstructionSources(input,values.graph,tree,{},&other)));
    }
    const auto nested_body = Body(1,Mutation::kDoublePop);
    const auto nested = std::get<EnvironmentCleanup>(EnvironmentCleanup::Build(nested_body,profile,frame));
    test::Check(nested.Handlers().size() == 1 && nested.Handlers()[0].pop_count == 2);
    test::Check(nested.Pops()[0].allocation.value > nested.Pops()[1].allocation.value);
    const auto bounded_body = Body(1);
    const auto owned = std::get<CleanupBody>(CleanupBody::Build(bounded_body,profile,frame));
    analysis::SemanticBudget projection_budget; projection_budget.work = owned.Work();
    test::Check(std::holds_alternative<CleanupBody>(CleanupBody::Build(bounded_body,profile,frame,projection_budget)));
    --projection_budget.work;
    test::Check(std::holds_alternative<CleanupFailure>(CleanupBody::Build(bounded_body,profile,frame,projection_budget)));
    const auto measured = std::get<EnvironmentCleanup>(EnvironmentCleanup::Build(bounded_body,profile,frame));
    analysis::SemanticBudget exact; exact.work = measured.Work();
    test::Check(std::holds_alternative<EnvironmentCleanup>(EnvironmentCleanup::Build(bounded_body,profile,frame,exact)));
    --exact.work;
    test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(bounded_body,profile,frame,exact)));
    for (const auto mutation : {Mutation::kOverwrite,Mutation::kReturn,Mutation::kEntry,
        Mutation::kOuterPop,Mutation::kDebugger,Mutation::kTyped})
      test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(Body(1,mutation),profile,frame)));
    test::Check(std::holds_alternative<CleanupFailure>(EnvironmentCleanup::Build(Body(1),profile,frame,{0,1000})));
    test::Check(std::get<CleanupError>(std::get<CleanupFailure>(EnvironmentCleanup::Build(Body(1),profile,frame,{1024*1024,0}))) == CleanupError::kWorkLimit);
  }
}
