import Foundation
import UniformTypeIdentifiers

#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

private final class AetherNativeLaunchFilePicker: NSObject, @unchecked Sendable {
    static let shared = AetherNativeLaunchFilePicker()

    private let lock = NSLock()
    private var presenting = false
    private var resultJSON: String?

#if os(iOS)
    private var documentPicker: UIDocumentPickerViewController?
#elseif os(macOS)
    private var openPanel: NSOpenPanel?
#endif

    private override init() {
        super.init()
    }

    func present(title: String, initialDirectory: String) -> Bool {
        lock.lock()
        guard !presenting else {
            lock.unlock()
            return false
        }
        presenting = true
        resultJSON = nil
        lock.unlock()

        DispatchQueue.main.async { [weak self] in
            self?.presentOnMainThread(title: title, initialDirectory: initialDirectory)
        }
        return true
    }

    func takeResultJSON() -> UnsafeMutablePointer<CChar>? {
        lock.lock()
        let value = resultJSON
        resultJSON = nil
        lock.unlock()
        guard let value else { return nil }
        return value.withCString { strdup($0) }
    }

    private func complete(status: String, path: String = "", error: String = "") {
        var payload: [String: String] = ["status": status]
        if !path.isEmpty {
            payload["path"] = path
        }
        if !error.isEmpty {
            payload["error"] = error
        }
        let encoded: String
        if let data = try? JSONSerialization.data(withJSONObject: payload),
           let json = String(data: data, encoding: .utf8) {
            encoded = json
        } else {
            encoded = "{\"status\":\"error\",\"error\":\"Unable to encode file picker result\"}"
        }
        lock.lock()
        presenting = false
        resultJSON = encoded
        lock.unlock()
    }

    private func directoryURL(for path: String) -> URL? {
        guard !path.isEmpty else { return nil }
        var isDirectory: ObjCBool = false
        guard FileManager.default.fileExists(atPath: path, isDirectory: &isDirectory),
              isDirectory.boolValue else {
            return nil
        }
        return URL(fileURLWithPath: path, isDirectory: true)
    }

#if os(iOS)
    private func presentOnMainThread(title: String, initialDirectory: String) {
        guard let presenter = topViewController() else {
            complete(status: "error", error: "Unable to find a window for the system file picker")
            return
        }
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: [.data],
            asCopy: false
        )
        picker.delegate = self
        picker.allowsMultipleSelection = false
        picker.modalPresentationStyle = .formSheet
        picker.directoryURL = directoryURL(for: initialDirectory)
        documentPicker = picker
        presenter.present(picker, animated: true)
    }

    private func topViewController() -> UIViewController? {
        let scenes = UIApplication.shared.connectedScenes.compactMap { $0 as? UIWindowScene }
        let window = scenes
            .flatMap { $0.windows }
            .first(where: { $0.isKeyWindow })
            ?? scenes.flatMap { $0.windows }.first
        var current = window?.rootViewController
        while let presented = current?.presentedViewController {
            current = presented
        }
        if let navigation = current as? UINavigationController {
            current = navigation.visibleViewController
        } else if let tabs = current as? UITabBarController {
            current = tabs.selectedViewController
        }
        return current
    }
#elseif os(macOS)
    private func presentOnMainThread(title: String, initialDirectory: String) {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.resolvesAliases = true
        panel.allowedContentTypes = [
            UTType(filenameExtension: "exe"),
            UTType(filenameExtension: "xp3"),
        ].compactMap { $0 }
        panel.directoryURL = directoryURL(for: initialDirectory)
        openPanel = panel
        panel.begin { [weak self] response in
            guard let self else { return }
            self.openPanel = nil
            if response == .OK, let url = panel.url {
                self.complete(status: "selected", path: url.standardizedFileURL.path)
            } else {
                self.complete(status: "cancelled")
            }
        }
    }
#else
    private func presentOnMainThread(title: String, initialDirectory: String) {
        complete(status: "error", error: "System file picker is unavailable on this platform")
    }
#endif
}

#if os(iOS)
extension AetherNativeLaunchFilePicker: UIDocumentPickerDelegate {
    func documentPicker(
        _ controller: UIDocumentPickerViewController,
        didPickDocumentsAt urls: [URL]
    ) {
        documentPicker = nil
        guard let url = urls.first else {
            complete(status: "cancelled")
            return
        }
        let scoped = url.startAccessingSecurityScopedResource()
        let path = url.standardizedFileURL.path
        if scoped {
            url.stopAccessingSecurityScopedResource()
        }
        complete(status: "selected", path: path)
    }

    func documentPickerWasCancelled(_ controller: UIDocumentPickerViewController) {
        documentPicker = nil
        complete(status: "cancelled")
    }
}
#endif

@_cdecl("aether_native_launch_file_picker_present")
public func aetherNativeLaunchFilePickerPresent(
    _ titlePointer: UnsafePointer<CChar>?,
    _ initialDirectoryPointer: UnsafePointer<CChar>?
) -> Int32 {
    let title = titlePointer.map { String(cString: $0) } ?? ""
    let initialDirectory = initialDirectoryPointer.map { String(cString: $0) } ?? ""
    return AetherNativeLaunchFilePicker.shared.present(
        title: title,
        initialDirectory: initialDirectory
    ) ? 1 : 0
}

@_cdecl("aether_native_launch_file_picker_copy_result_json")
public func aetherNativeLaunchFilePickerCopyResultJSON() -> UnsafeMutablePointer<CChar>? {
    return AetherNativeLaunchFilePicker.shared.takeResultJSON()
}

@_cdecl("aether_native_launch_file_picker_free_string")
public func aetherNativeLaunchFilePickerFreeString(_ pointer: UnsafeMutablePointer<CChar>?) {
    free(pointer)
}
