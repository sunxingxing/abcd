#ifndef ABCD_TESTS_PHASED_FILE_CHECKS_H_
#define ABCD_TESTS_PHASED_FILE_CHECKS_H_

#include <cstdlib>
#include <type_traits>

#include "../../src/source/phased_function_file.h"

namespace abcd::test {
inline void CheckPhasedFile(const corpus::AbcFile& file, const abc::Header& header,
    const abc::MetadataIndex& index, const source::FunctionContext& context) {
  using namespace source;
  static_assert(!std::is_copy_constructible_v<FilePhasedFunction>);
  static_assert(std::is_nothrow_move_constructible_v<FilePhasedFunction>);
  static_assert(std::is_same_v<decltype(std::declval<FilePhasedFunction&>().Body()), const abc::MethodBody&>);
  const auto check = [](bool ok) { if (!ok) std::abort(); };
  auto value = std::get<FilePhasedFunction>(FilePhasedFunction::Read(file, header, index, context));
  check(value.Record() == index.methods[context.Position().value].owner_offset);
  check(value.MatchesOwner(context));
  check(value.Locals().writes == analysis::ClosureWrites::kLanguage);
  const auto* body = &value.Body();
  const auto* graph = &value.Phases().Graph();
  const auto* scopes = &value.Locals();
  const auto view = value.Phases().TreeInput(value.Body());
  std::vector<std::optional<FilePhasedFunction>> owners;
  owners.emplace_back(std::move(value));
  for (const std::size_t count : {200, 201, 402}) {
    owners.resize(count);
    check(&owners[0]->Body() == body && &owners[0]->Phases().Graph() == graph && &owners[0]->Locals() == scopes);
    check(&view.body == body && view.input.flow.instructions.size() == body->instructions.size());
  }
  auto moved = std::move(*owners[0]); owners.clear();
  check(&moved.Body() == body && &moved.Phases().Graph() == graph);
  check(std::holds_alternative<std::uint64_t>(VerifyExceptionControl(
      *body, *graph, view.input, view.regions, view.control)));
  auto budget = analysis::SemanticBudget{}; budget.work = moved.Work();
  check(std::holds_alternative<FilePhasedFunction>(FilePhasedFunction::Read(file, header, index, context, budget)));
  --budget.work;
  check(std::holds_alternative<PhasedFileFailure>(FilePhasedFunction::Read(file, header, index, context, budget)));
  const auto check_error = [&](auto result, PhasedFileError expected) {
    check(std::get<PhasedFileError>(std::get<PhasedFileFailure>(result)) == expected);
  };
  budget = {}; budget.bytes = 0;
  check_error(FilePhasedFunction::Read(file, header, index, context, budget), PhasedFileError::kResourceLimit);
  budget = {}; budget.work = 0;
  check_error(FilePhasedFunction::Read(file, header, index, context, budget), PhasedFileError::kWorkLimit);
  auto wrong_header = header; ++wrong_header.file_size;
  check_error(FilePhasedFunction::Read(file, wrong_header, index, context), PhasedFileError::kIdentity);
  auto wrong_index = index;
  auto& method = wrong_index.methods[context.Position().value];
  ++method.offset.value;
  check_error(FilePhasedFunction::Read(file, header, wrong_index, context), PhasedFileError::kMethod);
  --method.offset.value; ++method.code->registers;
  check_error(FilePhasedFunction::Read(file, header, wrong_index, context), PhasedFileError::kFrame);
  --method.code->registers; ++method.code->try_count;
  const auto malformed = FilePhasedFunction::Read(file, header, wrong_index, context);
  check(std::get<abc::BodyFailure>(std::get<PhasedFileFailure>(malformed)).kind == abc::BodyError::kMetadataMismatch);
  method.code.reset();
  check_error(FilePhasedFunction::Read(file, header, wrong_index, context), PhasedFileError::kMethod);
}
}  // namespace abcd::test
#endif
