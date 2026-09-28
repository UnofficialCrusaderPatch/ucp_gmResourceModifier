#pragma once
// Test host boundary: the real resource implementation only logs through UCP.
// Production DLL builds use the framework SDK, never this include directory.
enum ucp_NamedVerbosity { Verbosity_FATAL=-3, Verbosity_ERROR=-2,
  Verbosity_WARNING=-1, Verbosity_INFO=0, Verbosity_1=1 };
extern "C" void ucp_log(ucp_NamedVerbosity level, const char* message);
extern "C" void* ucp_getProcAddressFromLibraryInModule(const char*, const char*, const char*);
