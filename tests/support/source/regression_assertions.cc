// This target uses the same directory compile options as the engine/tests.
// A configuration that drops assertions is not the optimized Debug gate.
#ifdef NDEBUG
#error "Source regression requires assertions"
#endif
#if (defined(__clang__) || defined(__GNUC__)) && !defined(__OPTIMIZE__)
#error "Source regression requires compiler optimization"
#endif
