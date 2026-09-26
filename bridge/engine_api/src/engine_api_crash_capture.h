#ifndef AETHERKIRI_ENGINE_API_CRASH_CAPTURE_H_
#define AETHERKIRI_ENGINE_API_CRASH_CAPTURE_H_

namespace aetherkiri::engine_api {

// Installs process-wide diagnostics handlers (SIGABRT, std::terminate, CRT
// invalid parameter, pure virtual call, and an unhandled-exception filter)
// that append a reason plus a resolved stack backtrace to a log file under
// %LOCALAPPDATA%\AetherKiri\crash (override with AETHERKIRI_CRASH_LOG_DIR).
// Unhandled faults still continue to WER so a full dump is preserved.
// Safe to call multiple times; only the first call installs handlers.
void InstallCrashCapture();

// Appends a crash-style report (reason, detail, current-thread backtrace)
// without terminating. Used to record caught exceptions at engine
// boundaries so failures stay visible outside a debugger.
void WriteCrashReport(const char* reason, const char* detail);

}  // namespace aetherkiri::engine_api

#endif  // AETHERKIRI_ENGINE_API_CRASH_CAPTURE_H_
