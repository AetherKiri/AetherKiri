//
// iOS platform implementation for KrKr2 engine
// Adapted from macOS platform.mm
//
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <sstream>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/task.h>
#include <mach/vm_statistics.h>
#include <mach/mach_host.h>
#include <mach/mach_init.h>
#include <unistd.h>

#include <sys/stat.h>
#include <sys/time.h>
#include <sys/sysctl.h>

#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <spdlog/spdlog.h>

#include "StorageImpl.h"
#include "EventIntf.h"
#include "Platform.h"
#include "SysInitImpl.h"
#include "tjsString.h"

// ---- File operations (POSIX compatible, reused from macOS) ----

bool TVPDeleteFile(const std::string &filename) {
    return unlink(filename.c_str()) == 0;
}

bool TVPRenameFile(const std::string &from, const std::string &to) {
    tjs_int ret = rename(from.c_str(), to.c_str());
    return !ret;
}

bool TVPCreateFolders(const ttstr &folder);

static bool _TVPCreateFolders(const ttstr &folder) {
    if(folder.IsEmpty())
        return true;

    if(TVPCheckExistentLocalFolder(folder))
        return true;

    const tjs_char *p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if(p[i] == TJS_W(':'))
        return true;

    while(i >= 0 && (p[i] == TJS_W('/') || p[i] == TJS_W('\\')))
        i--;

    if(i >= 0 && p[i] == TJS_W(':'))
        return true;

    for(; i >= 0; i--) {
        if(p[i] == TJS_W(':') || p[i] == TJS_W('/') || p[i] == TJS_W('\\'))
            break;
    }

    ttstr parent(p, i + 1);
    if(!TVPCreateFolders(parent))
        return false;

    return !std::filesystem::create_directory(folder.AsStdString().c_str());
}

bool TVPCreateFolders(const ttstr &folder) {
    if(folder.IsEmpty())
        return true;

    const tjs_char *p = folder.c_str();
    tjs_int i = folder.GetLen() - 1;

    if(p[i] == TJS_W(':'))
        return true;

    if(p[i] == TJS_W('/') || p[i] == TJS_W('\\'))
        i--;

    return _TVPCreateFolders(ttstr(p, i + 1));
}

// ---- Path functions (iOS sandbox) ----

std::vector<std::string> TVPGetDriverPath() {
    std::vector<std::string> result;
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
        result.emplace_back([paths[0] UTF8String]);
    }
    return result;
}

std::string TVPGetDefaultFileDir() {
    // iOS: return the app bundle resource path
    NSString *path = [[NSBundle mainBundle] resourcePath];
    return path ? std::string([path UTF8String]) : "";
}

std::vector<std::string> TVPGetAppStoragePath() {
    std::vector<std::string> ret;
    NSArray *paths = NSSearchPathForDirectoriesInDomains(
        NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count > 0) {
        ret.emplace_back([paths[0] UTF8String]);
    }
    return ret;
}

// ---- Memory info (Mach API, shared between macOS and iOS) ----

void TVPGetMemoryInfo(TVPMemoryInfo &m) {
    m.MemTotal = 0;
    m.MemFree = 0;
    m.SwapTotal = 0;
    m.SwapFree = 0;
    m.VirtualTotal = 0;
    m.VirtualUsed = 0;

    // Total physical memory
    int mib[2];
    int64_t total_memory;
    size_t length = sizeof(total_memory);

    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    if (sysctl(mib, 2, &total_memory, &length, nullptr, 0) == 0) {
        m.MemTotal = total_memory / 1024; // KB
    }

    // Memory statistics
    vm_size_t page_size;
    vm_statistics64_data_t vm_stats;
    mach_port_t mach_port = mach_host_self();
    mach_msg_type_number_t count = sizeof(vm_stats) / sizeof(natural_t);

    if (host_page_size(mach_port, &page_size) == KERN_SUCCESS &&
        host_statistics64(mach_port, HOST_VM_INFO, (host_info64_t)&vm_stats, &count) == KERN_SUCCESS) {
        int64_t free_memory = (vm_stats.free_count + vm_stats.inactive_count) * page_size;
        m.MemFree = free_memory / 1024; // KB
    }

    // iOS does not have swap, set to 0
    m.SwapTotal = 0;
    m.SwapFree = 0;

    // Virtual memory info from task
    struct task_basic_info_64 info{};
    mach_msg_type_number_t info_count = TASK_BASIC_INFO_64_COUNT;

    if (task_info(mach_task_self(), TASK_BASIC_INFO_64, (task_info_t)&info, &info_count) == KERN_SUCCESS) {
        m.VirtualTotal = info.virtual_size / 1024; // KB
        m.VirtualUsed = info.resident_size / 1024;  // KB
    }
}

tjs_int TVPGetSelfUsedMemory() {
    mach_task_basic_info info{};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;

    kern_return_t kr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                (task_info_t)&info, &count);
    if (kr != KERN_SUCCESS) {
        return -1;
    }

    // Return resident memory size in MB
    return info.resident_size / (1024 * 1024);
}

tjs_int TVPGetSystemFreeMemory() {
    vm_statistics64_data_t vm_stats;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    kern_return_t kern_return = host_statistics64(mach_host_self(),
                                                HOST_VM_INFO64,
                                                (host_info64_t)&vm_stats,
                                                &count);

    if (kern_return != KERN_SUCCESS) {
        return -1;
    }

    vm_size_t page_size;
    host_page_size(mach_host_self(), &page_size);

    // Free + inactive memory
    int64_t free_memory = ((int64_t)vm_stats.free_count + vm_stats.inactive_count) * page_size;

    // Return in MB
    return (tjs_int)(free_memory / (1024 * 1024));
}

// ---- UI dialogs (UIKit) ----

// Helper: find the topmost view controller for presenting alerts.
static UIViewController *TVPFindTopViewController() {
    UIWindow *window = nil;
    for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
        if (scene.activationState == UISceneActivationStateForegroundActive) {
            for (UIWindow *w in scene.windows) {
                if (w.isKeyWindow) {
                    window = w;
                    break;
                }
            }
        }
        if (window) break;
    }
    UIViewController *rootVC = window.rootViewController;
    while (rootVC.presentedViewController) {
        rootVC = rootVC.presentedViewController;
    }
    return rootVC;
}

int TVPShowSimpleMessageBox(const ttstr &text, const ttstr &caption,
                            const std::vector<ttstr> &vecButtons) {
    __block int selectedIndex = -1;

    std::string utf8Text = text.AsStdString();
    std::string utf8Caption = caption.AsStdString();
    // Copy button labels for use in the block
    std::vector<std::string> utf8Buttons;
    utf8Buttons.reserve(vecButtons.size());
    for (const auto &btn : vecButtons) {
        utf8Buttons.emplace_back(btn.AsStdString());
    }

    // Block that creates and presents the alert.
    auto showAlert = ^{
        UIAlertController *alert = [UIAlertController
            alertControllerWithTitle:[NSString stringWithUTF8String:utf8Caption.c_str()]
            message:[NSString stringWithUTF8String:utf8Text.c_str()]
            preferredStyle:UIAlertControllerStyleAlert];

        // When called on the main thread we use a run-loop based wait
        // instead of a semaphore to avoid deadlocking.
        __block BOOL dismissed = NO;

        for (int i = 0; i < (int)utf8Buttons.size(); i++) {
            const std::string &label = utf8Buttons[i];
            [alert addAction:[UIAlertAction
                actionWithTitle:[NSString stringWithUTF8String:label.c_str()]
                style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) {
                    selectedIndex = i;
                    dismissed = YES;
                }]];
        }

        UIViewController *rootVC = TVPFindTopViewController();
        if (rootVC) {
            [rootVC presentViewController:alert animated:YES completion:nil];
            // Spin the run loop until the user dismisses the alert.
            // This keeps the UI responsive and avoids the semaphore deadlock.
            while (!dismissed) {
                [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
                                         beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
            }
        } else {
            // No VC available — just log and return immediately
            spdlog::warn("TVPShowSimpleMessageBox: no root view controller available, cannot present alert");
            selectedIndex = 0;
        }
    };

    if ([NSThread isMainThread]) {
        // Already on the main thread — present directly to avoid deadlock.
        showAlert();
    } else {
        // Off the main thread — dispatch and wait via semaphore.
        dispatch_semaphore_t sem = dispatch_semaphore_create(0);
        dispatch_async(dispatch_get_main_queue(), ^{
            // Wrap the alert so the semaphore is signalled after dismissal.
            __block BOOL dismissed = NO;

            UIAlertController *alert = [UIAlertController
                alertControllerWithTitle:[NSString stringWithUTF8String:utf8Caption.c_str()]
                message:[NSString stringWithUTF8String:utf8Text.c_str()]
                preferredStyle:UIAlertControllerStyleAlert];

            for (int i = 0; i < (int)utf8Buttons.size(); i++) {
                const std::string &label = utf8Buttons[i];
                [alert addAction:[UIAlertAction
                    actionWithTitle:[NSString stringWithUTF8String:label.c_str()]
                    style:UIAlertActionStyleDefault
                    handler:^(UIAlertAction *action) {
                        selectedIndex = i;
                        dispatch_semaphore_signal(sem);
                    }]];
            }

            UIViewController *rootVC = TVPFindTopViewController();
            if (rootVC) {
                [rootVC presentViewController:alert animated:YES completion:nil];
            } else {
                dispatch_semaphore_signal(sem);
            }
        });
        dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
    }

    return selectedIndex;
}

extern "C" int TVPShowSimpleMessageBox(const char *pszText, const char *pszTitle,
                            unsigned int nButton, const char **btnText) {
    std::vector<ttstr> vecButtons{};
    for(unsigned int i = 0; i < nButton; ++i) {
        vecButtons.emplace_back(btnText[i]);
    }
    return TVPShowSimpleMessageBox(pszText, pszTitle, vecButtons);
}

int TVPShowSimpleInputBox(ttstr &text, const ttstr &caption,
                          const ttstr &prompt,
                          const std::vector<ttstr> &vecButtons) {
    __block int selectedIndex = -1;
    __block NSString *inputText = nil;
    dispatch_semaphore_t sem = dispatch_semaphore_create(0);

    std::string utf8Caption = caption.AsStdString();
    std::string utf8Prompt = prompt.AsStdString();
    std::string utf8Default = text.AsStdString();

    dispatch_async(dispatch_get_main_queue(), ^{
        UIAlertController *alert = [UIAlertController
            alertControllerWithTitle:[NSString stringWithUTF8String:utf8Caption.c_str()]
            message:[NSString stringWithUTF8String:utf8Prompt.c_str()]
            preferredStyle:UIAlertControllerStyleAlert];

        [alert addTextFieldWithConfigurationHandler:^(UITextField *textField) {
            textField.text = [NSString stringWithUTF8String:utf8Default.c_str()];
        }];

        for (int i = 0; i < (int)vecButtons.size(); i++) {
            std::string utf8Button = vecButtons[i].AsStdString();
            [alert addAction:[UIAlertAction
                actionWithTitle:[NSString stringWithUTF8String:utf8Button.c_str()]
                style:UIAlertActionStyleDefault
                handler:^(UIAlertAction *action) {
                    selectedIndex = i;
                    inputText = alert.textFields.firstObject.text;
                    dispatch_semaphore_signal(sem);
                }]];
        }

        UIWindow *window = nil;
        for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive) {
                for (UIWindow *w in scene.windows) {
                    if (w.isKeyWindow) {
                        window = w;
                        break;
                    }
                }
            }
            if (window) break;
        }

        UIViewController *rootVC = window.rootViewController;
        while (rootVC.presentedViewController) {
            rootVC = rootVC.presentedViewController;
        }

        if (rootVC) {
            [rootVC presentViewController:alert animated:YES completion:nil];
        } else {
            dispatch_semaphore_signal(sem);
        }
    });

    dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);

    if (inputText) {
        text = ttstr([inputText UTF8String]);
    }
    return selectedIndex;
}

// ---- Misc functions ----

void TVPRelinquishCPU() {
    sched_yield();
}

void TVPSendToOtherApp(const std::string &filename) {
    NSString *filePath = [NSString stringWithUTF8String:filename.c_str()];
    NSURL *fileURL = [NSURL fileURLWithPath:filePath];

    dispatch_async(dispatch_get_main_queue(), ^{
        UIActivityViewController *activityVC = [[UIActivityViewController alloc]
            initWithActivityItems:@[fileURL]
            applicationActivities:nil];

        UIWindow *window = nil;
        for (UIWindowScene *scene in UIApplication.sharedApplication.connectedScenes) {
            if (scene.activationState == UISceneActivationStateForegroundActive) {
                for (UIWindow *w in scene.windows) {
                    if (w.isKeyWindow) {
                        window = w;
                        break;
                    }
                }
            }
            if (window) break;
        }

        UIViewController *rootVC = window.rootViewController;
        while (rootVC.presentedViewController) {
            rootVC = rootVC.presentedViewController;
        }

        if (rootVC) {
            // Required for iPad: set popover source
            if (activityVC.popoverPresentationController) {
                activityVC.popoverPresentationController.sourceView = rootVC.view;
                activityVC.popoverPresentationController.sourceRect = CGRectMake(
                    rootVC.view.bounds.size.width / 2, rootVC.view.bounds.size.height / 2, 0, 0);
            }
            [rootVC presentViewController:activityVC animated:YES completion:nil];
        }
    });
}

bool TVPCheckStartupArg() {
    return false;
}

void TVPControlAdDialog(int, int, int) { /* pass */ }

void TVPExitApplication(int code) {
    TVPDeliverCompactEvent(TVP_COMPACT_LEVEL_MAX);
    TVPTerminated = true;
    TVPTerminateCode = code;
    // iOS: Don't call exit(). The app lifecycle is managed by the system.
    // Let Application host handle the shutdown gracefully.
}

void TVPForceSwapBuffer() { /* pass */ }

bool TVPWriteDataToFile(const ttstr &filepath, const void *data,
                        unsigned int len) {
    FILE *handle = fopen(filepath.AsStdString().c_str(), "wb");
    if(handle) {
        bool ret = fwrite(data, 1, len, handle) == len;
        fclose(handle);
        return ret;
    }
    return false;
}

bool TVPCheckStartupPath(const std::string &path) { return true; }

std::string TVPGetCurrentLanguage() {
    CFArrayRef preferredLanguages = CFLocaleCopyPreferredLanguages();
    if (preferredLanguages == nullptr || CFArrayGetCount(preferredLanguages) == 0) {
        if (preferredLanguages) CFRelease(preferredLanguages);
        return "en_US";
    }

    auto preferredLanguage = (CFStringRef)CFArrayGetValueAtIndex(preferredLanguages, 0);
    char buffer[256];
    CFStringGetCString(preferredLanguage, buffer, sizeof(buffer), kCFStringEncodingUTF8);
    std::string localeStr(buffer);
    CFRelease(preferredLanguages);

    // Replace hyphens with underscores
    std::replace(localeStr.begin(), localeStr.end(), '-', '_');

    // Split string
    std::vector<std::string> parts;
    std::istringstream iss(localeStr);
    std::string part;
    while (std::getline(iss, part, '_')) {
        parts.push_back(part);
    }

    if (parts.empty()) {
        return "en_US";
    }

    std::string language = parts[0];

    // Find country code
    std::string country;
    for (size_t i = parts.size() - 1; i > 0; --i) {
        if (parts[i].size() == 2) {
            country = parts[i];
            break;
        }
    }

    if (country.empty() && parts.size() > 1) {
        const std::string& lastPart = parts.back();
        if (lastPart.size() == 2) {
            country = lastPart;
        }
    }

    std::transform(country.begin(), country.end(), country.begin(), ::tolower);

    if (!country.empty()) {
        return language + "_" + country;
    } else {
        return language;
    }
}

void TVPProcessInputEvents() { /* pass */ }

tjs_uint32 TVPGetRoughTickCount32() {
    tjs_uint32 uptime = 0;
    timespec on{};
    if(clock_gettime(CLOCK_MONOTONIC, &on) == 0)
        uptime = on.tv_sec * 1000 + on.tv_nsec / 1000000;
    return uptime;
}

std::string TVPGetPackageVersionString() {
    return "ios";
}

bool TVP_stat(const char *name, tTVP_stat &s) {
    struct stat t{};
    if(stat(name, &t) != 0) {
        return false;
    }

    s.st_mode = t.st_mode;
    s.st_size = t.st_size;
    s.st_atime = t.st_atimespec.tv_sec;
    s.st_mtime = t.st_mtimespec.tv_sec;
    s.st_ctime = t.st_ctimespec.tv_sec;

    return true;
}

bool TVP_stat(const tjs_char *name, tTVP_stat &s) {
    return TVP_stat(ttstr{ name }.AsStdString().c_str(), s);
}

bool TVP_utime(const char *name, time_t modtime) {
    timeval mt[2];
    mt[0].tv_sec = modtime;
    mt[0].tv_usec = 0;
    mt[1].tv_sec = modtime;
    mt[1].tv_usec = 0;
    return utimes(name, mt) == 0;
}
