#include "process_limit.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>

namespace {
void Check(bool value) {
  if (!value) std::abort();
}
}  // namespace

int main() {
  using namespace abcd::tools;
  InstallAllocationFailureMonitor();
  Check(!TakeAllocationFailure());
  // An ordinary allocation never sets the flag.
  delete new int(1);
  Check(!TakeAllocationFailure());
  // A real allocation failure is observed even if the caller swallows it.
  bool thrown = false;
  try {
    void* impossible = ::operator new(std::numeric_limits<std::size_t>::max() / 2);
    ::operator delete(impossible);
  } catch (const std::bad_alloc&) {
    thrown = true;
  }
  Check(thrown && TakeAllocationFailure());
  Check(!TakeAllocationFailure());  // Taking clears the observation.
  // The report always states whether a limit was established.
  nlohmann::json report;
  ReportProcessMemory(report, EstablishProcessLimit());
  Check(report.contains("process_limit") && report["process_limit"].contains("established"));
  std::cout << "process_limit_test: ok\n";
  return EXIT_SUCCESS;
}
