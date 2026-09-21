#include "abcd/analysis/exception_dispatch.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <variant>

namespace {

using namespace abcd;
using analysis::BuildExceptionDispatch;
using analysis::DispatchError;

void Check(bool condition) {
  if (!condition) {
    std::cerr << "exception dispatch assertion failed\n";
    std::abort();
  }
}

abc::MethodBody Body(std::uint32_t instruction_count) {
  abc::MethodBody body;
  for (std::uint32_t pc = 0; pc < instruction_count; ++pc) {
    abc::BodyInstruction instruction;
    instruction.offset.value = pc;
    instruction.decoded.size = 1;
    body.instructions.push_back(instruction);
  }
  return body;
}

void Add(abc::MethodBody& body, std::uint32_t begin, std::uint32_t length,
         std::uint32_t handlers = 1) {
  abc::TryRegion region;
  region.offset.value = begin;
  region.length = length;
  region.first_handler = body.handlers.size();
  region.handler_count = handlers;
  body.tries.push_back(region);
  for (std::uint32_t i = 0; i < handlers; ++i) {
    abc::CatchHandler catch_handler;
    catch_handler.offset.value = 0;
    catch_handler.length = 1;
    if (i == 0) {
      catch_handler.type = abc::CatchTypeIndex{1};
    }
    body.handlers.push_back(catch_handler);
  }
}

analysis::ExceptionDispatch Build(const abc::MethodBody& body) {
  auto result = BuildExceptionDispatch(body, abc::ProfileId::kDynamic1206);
  Check(std::holds_alternative<analysis::ExceptionDispatch>(result));
  return std::get<analysis::ExceptionDispatch>(std::move(result));
}

void ExpectFailure(const abc::MethodBody& body, analysis::DispatchError error,
                   abc::ProfileId profile = abc::ProfileId::kDynamic1206,
                   analysis::DispatchBudget budget = {}) {
  auto result = BuildExceptionDispatch(body, profile, budget);
  Check(std::holds_alternative<analysis::DispatchFailure>(result));
  Check(std::get<analysis::DispatchFailure>(result).kind == error);
}

void BasicCases() {
  auto body = Body(8);
  Check(Build(body).spans.empty());
  Add(body, 4, 2);
  Add(body, 1, 5);
  Add(body, 2, 2);
  auto dispatch = Build(body);
  Check(dispatch.Find({0}) == std::nullopt);
  Check(dispatch.Find({1})->region.value == 1);
  Check(dispatch.Find({3})->region.value == 1);
  Check(dispatch.Find({4})->region.value == 0);
  Check(dispatch.Find({5})->handler.value == 0);
  Check(dispatch.Find({6}) == std::nullopt);
  Check(dispatch.spans.size() == 2);
  Check(dispatch.spans[0].begin.value == 1);
  Check(dispatch.spans[0].end.value == 4);
  Check(dispatch.spans[1].end.value == 6);

  auto identical = Body(8);
  Add(identical, 2, 4, 2);
  Add(identical, 2, 4);
  Check(Build(identical).Find({3})->region.value == 0);
  Check(Build(identical).Find({3})->handler.value == 0);

  auto empty = Body(8);
  Add(empty, 2, 4, 0);
  Add(empty, 2, 4);
  Add(empty, 1, 0);
  Check(Build(empty).Find({3})->region.value == 1);

  auto disjoint = Body(8);
  Add(disjoint, 1, 2);
  Add(disjoint, 5, 2);
  auto separate = Build(disjoint);
  Check(separate.Find({2})->region.value == 0);
  Check(separate.Find({4}) == std::nullopt);
  Check(separate.Find({6})->region.value == 1);
}

void InvalidCases() {
  ExpectFailure(Body(0), DispatchError::kEmptyBody);
  auto body = Body(4);
  ExpectFailure(body, DispatchError::kUnsupportedProfile,
                abc::ProfileId::kStatic0107);
  ExpectFailure(body, DispatchError::kUnsupportedProfile,
                abc::ProfileId::kUnknown);
  Add(body, 1, 2);
  ExpectFailure(body, DispatchError::kResourceLimit,
                abc::ProfileId::kDynamic1206, {0});
  body.instructions[1].offset.value = 2;
  ExpectFailure(body, DispatchError::kMalformedBody);
  body = Body(4);
  Add(body, 1, 4);
  ExpectFailure(body, DispatchError::kInvalidBoundary);
  body = Body(4);
  Add(body, 1, 2);
  body.handlers[0].offset.value = 4;
  ExpectFailure(body, DispatchError::kInvalidBoundary);
  body = Body(4);
  Add(body, 1, 2);
  body.handlers[0].length = 5;
  ExpectFailure(body, DispatchError::kInvalidBoundary);
  body = Body(4);
  Add(body, 1, 2);
  body.tries[0].first_handler = 1;
  ExpectFailure(body, DispatchError::kMalformedBody);

  body = Body(3);
  body.instructions[0].decoded.size = 2;
  body.instructions[1].offset.value = 2;
  body.instructions[2].offset.value = 3;
  Add(body, 1, 1);
  ExpectFailure(body, DispatchError::kInvalidBoundary);
  body.tries[0].offset.value = 2;
  body.handlers[0].length = 2;
  Check(Build(body).Find({2}).has_value());
}

void RandomOracle() {
  std::mt19937 random(1701);
  for (int iteration = 0; iteration < 100; ++iteration) {
    auto body = Body(32);
    for (int i = 0; i < 24; ++i) {
      const auto begin = random() % 33;
      const auto length = random() % (33 - begin);
      Add(body, begin, length, random() % 3);
    }
    auto dispatch = Build(body);
    for (std::uint32_t pc = 0; pc < 32; ++pc) {
      std::optional<std::uint32_t> expected;
      for (std::uint32_t region = 0; region < body.tries.size(); ++region) {
        const auto& trial = body.tries[region];
        if (trial.handler_count > 0 && trial.offset.value <= pc &&
            pc < trial.offset.value + trial.length) {
          expected = region;
          break;
        }
      }
      auto actual = dispatch.Find({pc});
      Check(actual.has_value() == expected.has_value());
      if (expected) {
        Check(actual->region.value == *expected);
      }
    }
  }
}

void Scaling() {
  for (std::uint32_t count : {200u, 201u, 2000u}) {
    auto body = Body(count + 2);
    for (std::uint32_t i = 0; i < count; ++i) {
      Add(body, count - i, 1);
    }
    auto dispatch = Build(body);
    Check(dispatch.spans.size() == count);
    Check(dispatch.Find({1})->region.value == count - 1);
  }
}

}  // namespace

int main() {
  BasicCases();
  InvalidCases();
  RandomOracle();
  Scaling();
}
