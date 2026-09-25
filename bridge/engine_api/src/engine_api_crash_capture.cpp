#include "engine_api_crash_capture.h"

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <cstdlib>

namespace aetherkiri::engine_api {
namespace {

constexpr size_t kMaxFrames = 64;

std::atomic<bool> g_installed{false};
std::atomic<bool> g_reporting{false};
SRWLOCK g_symbol_lock = SRWLOCK_INIT;
bool g_symbols_ready = false;

bool BuildCrashLogPath(char* buffer, size_t buffer_size) {
  char root[MAX_PATH];
  const DWORD env_len = GetEnvironmentVariableA("AETHERKIRI_CRASH_LOG_DIR",
                                                root, sizeof(root));
  if (env_len > 0 && env_len < sizeof(root)) {
    if (GetFileAttributesA(root) == INVALID_FILE_ATTRIBUTES) {
      CreateDirectoryA(root, nullptr);
    }
    return snprintf(buffer, buffer_size,
                    "%s\\engine_api_crash_%lu.log", root,
                    static_cast<unsigned long>(GetCurrentProcessId())) > 0;
  }
  char local_app[MAX_PATH];
  const DWORD local_len = GetEnvironmentVariableA("LOCALAPPDATA", local_app,
                                                  sizeof(local_app));
  if (local_len == 0 || local_len >= sizeof(local_app)) {
    return false;
  }
  char dir[MAX_PATH];
  if (snprintf(dir, sizeof(dir), "%s\\AetherKiri", local_app) <= 0) {
    return false;
  }
  CreateDirectoryA(dir, nullptr);
  if (snprintf(dir, sizeof(dir), "%s\\AetherKiri\\crash", local_app) <= 0) {
    return false;
  }
  CreateDirectoryA(dir, nullptr);
  return snprintf(buffer, buffer_size,
                  "%s\\engine_api_crash_%lu.log", dir,
                  static_cast<unsigned long>(GetCurrentProcessId())) > 0;
}

void ResolveAddress(void* address, char* line, size_t line_size) {
  if (!g_symbols_ready) {
    snprintf(line, line_size, "%p", address);
    return;
  }
  char module_name[MAX_PATH] = "<unknown>";
  SYMBOL_INFO_PACKAGE package{};
  IMAGEHLP_LINE64 image_line{};
  DWORD64 displacement = 0;
  DWORD64 module_base = 0;
  {
    AcquireSRWLockExclusive(&g_symbol_lock);
    module_base = SymGetModuleBase64(GetCurrentProcess(),
                                     reinterpret_cast<DWORD64>(address));
    if (module_base != 0) {
      IMAGEHLP_MODULE64 module_info{};
      module_info.SizeOfStruct = sizeof(module_info);
      if (SymGetModuleInfo64(GetCurrentProcess(), module_base, &module_info)) {
        const char* full = module_info.ImageName;
        const char* base = full;
        for (const char* scan = full; *scan != '\0'; ++scan) {
          if (*scan == '\\' || *scan == '/') base = scan + 1;
        }
        snprintf(module_name, sizeof(module_name), "%s", base);
      }
    }
    package.si.SizeOfStruct = sizeof(SYMBOL_INFO);
    package.si.MaxNameLen = sizeof(package.name);
    const DWORD64 address64 = reinterpret_cast<DWORD64>(address);
    if (SymFromAddr(GetCurrentProcess(), address64, &displacement,
                    &package.si)) {
      image_line.SizeOfStruct = sizeof(image_line);
      DWORD line_displacement = 0;
      if (SymGetLineFromAddr64(GetCurrentProcess(), address64,
                               &line_displacement, &image_line)) {
        const char* full = image_line.FileName;
        const char* base = full;
        for (const char* scan = full; *scan != '\0'; ++scan) {
          if (*scan == '\\' || *scan == '/') base = scan + 1;
        }
        snprintf(line, line_size, "%s!%s+0x%llx (%s:%lu)",
                 module_name, package.si.Name,
                 static_cast<unsigned long long>(displacement),
                 base, static_cast<unsigned long>(image_line.LineNumber));
        ReleaseSRWLockExclusive(&g_symbol_lock);
        return;
      }
      snprintf(line, line_size, "%s!%s+0x%llx", module_name, package.si.Name,
               static_cast<unsigned long long>(displacement));
      ReleaseSRWLockExclusive(&g_symbol_lock);
      return;
    }
    ReleaseSRWLockExclusive(&g_symbol_lock);
  }
  if (module_base != 0) {
    snprintf(line, line_size, "%s+0x%llx", module_name,
             static_cast<unsigned long long>(
                 reinterpret_cast<DWORD64>(address) - module_base));
  } else {
    snprintf(line, line_size, "%p", address);
  }
}

void AppendCrashReport(const char* reason, const char* detail) {
  bool expected = false;
  if (!g_reporting.compare_exchange_strong(expected, true)) {
    return;  // Reentrant fault while writing a report.
  }
  char path[MAX_PATH];
  if (!BuildCrashLogPath(path, sizeof(path))) {
    g_reporting.store(false);
    return;
  }
  HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    g_reporting.store(false);
    return;
  }
  char header[512];
  SYSTEMTIME now{};
  GetLocalTime(&now);
  int written = snprintf(header, sizeof(header),
                         "\r\n==== engine_api crash ====\r\n"
                         "pid=%lu tid=%lu time=%04u-%02u-%02u %02u:%02u:%02u.%03u\r\n"
                         "reason=%s\r\n",
                         static_cast<unsigned long>(GetCurrentProcessId()),
                         static_cast<unsigned long>(GetCurrentThreadId()),
                         now.wYear, now.wMonth, now.wDay, now.wHour,
                         now.wMinute, now.wSecond, now.wMilliseconds,
                         reason != nullptr ? reason : "unknown");
  DWORD ignored = 0;
  if (written > 0) {
    WriteFile(file, header, static_cast<DWORD>(written), &ignored, nullptr);
  }
  if (detail != nullptr && detail[0] != '\0') {
    char detail_line[1024];
    written = snprintf(detail_line, sizeof(detail_line), "detail=%s\r\n",
                       detail);
    if (written > 0) {
      WriteFile(file, detail_line, static_cast<DWORD>(written), &ignored,
                nullptr);
    }
  }
  void* frames[kMaxFrames];
  const WORD frame_count = RtlCaptureStackBackTrace(0, kMaxFrames, frames,
                                                    nullptr);
  char frame_line[1024];
  for (WORD index = 0; index < frame_count; ++index) {
    char resolved[900];
    ResolveAddress(frames[index], resolved, sizeof(resolved));
    written = snprintf(frame_line, sizeof(frame_line), "  #%02u %s\r\n",
                       static_cast<unsigned>(index), resolved);
    if (written > 0) {
      WriteFile(file, frame_line, static_cast<DWORD>(written), &ignored,
                nullptr);
    }
  }
  FlushFileBuffers(file);
  CloseHandle(file);
  g_reporting.store(false);
}

void SigAbrtHandler(int signal_number) {
  (void)signal_number;
  AppendCrashReport("abort() raised (SIGABRT)", nullptr);
}

void PurecallHandler() {
  AppendCrashReport("pure virtual call", nullptr);
}

void InvalidParameterHandler(const wchar_t* expression,
                             const wchar_t* function,
                             const wchar_t* file,
                             unsigned int line,
                             uintptr_t reserved) {
  (void)reserved;
  char detail[512];
  if (expression != nullptr) {
    char narrow[384];
    WideCharToMultiByte(CP_UTF8, 0, expression, -1, narrow, sizeof(narrow),
                        nullptr, nullptr);
    snprintf(detail, sizeof(detail),
             "expression=%s function=%ls file=%ls line=%u", narrow,
             function != nullptr ? function : L"<null>",
             file != nullptr ? file : L"<null>", line);
  } else {
    snprintf(detail, sizeof(detail),
             "release-mode invalid parameter (no detail), line=%u", line);
  }
  AppendCrashReport("CRT invalid parameter", detail);
}

void TerminateHandler() {
  AppendCrashReport("std::terminate called", nullptr);
  std::abort();
}

LONG WINAPI UnhandledExceptionFilter(EXCEPTION_POINTERS* info) {
  char reason[160];
  const EXCEPTION_RECORD* record = info != nullptr ? info->ExceptionRecord
                                                   : nullptr;
  if (record != nullptr) {
    snprintf(reason, sizeof(reason),
             "unhandled SEH exception code=0x%08lx address=%p",
             record->ExceptionCode, record->ExceptionAddress);
  } else {
    snprintf(reason, sizeof(reason), "unhandled SEH exception");
  }
  AppendCrashReport(reason, nullptr);
  return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void InstallCrashCapture() {
  bool expected = false;
  if (!g_installed.compare_exchange_strong(expected, true)) {
    return;
  }
  SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS |
                SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS);
  {
    AcquireSRWLockExclusive(&g_symbol_lock);
    g_symbols_ready =
        SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    ReleaseSRWLockExclusive(&g_symbol_lock);
  }
  std::signal(SIGABRT, SigAbrtHandler);
  _set_purecall_handler(PurecallHandler);
  _set_invalid_parameter_handler(InvalidParameterHandler);
  std::set_terminate(TerminateHandler);
  SetUnhandledExceptionFilter(UnhandledExceptionFilter);
}

void WriteCrashReport(const char* reason, const char* detail) {
  AppendCrashReport(reason, detail);
}

}  // namespace aetherkiri::engine_api

#else

namespace aetherkiri::engine_api {

void InstallCrashCapture() {}

void WriteCrashReport(const char* reason, const char* detail) {
  (void)reason;
  (void)detail;
}

}  // namespace aetherkiri::engine_api

#endif
