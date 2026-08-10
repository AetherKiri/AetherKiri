import Foundation
import UniformTypeIdentifiers

#if os(iOS)
import UIKit
#elseif os(macOS)
import AppKit
#endif

private final class AetherNativeLaunchFilePicker: NSObject, @unchecked Sendable {
    static let shared = AetherNativeLaunchFilePicker()

    private enum Purpose {
        case launchFile
        case coverImage(destinationDirectory: String)
    }

    private let lock = NSLock()
    private var presenting = false
    private var resultJSON: String?

#if os(iOS)
    private var documentPicker: UIDocumentPickerViewController?
    private var documentPickerPurpose: Purpose = .launchFile
#elseif os(macOS)
    private var openPanel: NSOpenPanel?
#endif

    private override init() {
        super.init()
    }

    private func present(
        title: String,
        initialDirectory: String,
        purpose: Purpose
    ) -> Bool {
        lock.lock()
        guard !presenting else {
            lock.unlock()
            return false
        }
        presenting = true
        resultJSON = nil
        lock.unlock()

        DispatchQueue.main.async { [weak self] in
            self?.presentOnMainThread(
                title: title,
                initialDirectory: initialDirectory,
                purpose: purpose
            )
        }
        return true
    }

    func presentLaunchFile(title: String, initialDirectory: String) -> Bool {
        return present(
            title: title,
            initialDirectory: initialDirectory,
            purpose: .launchFile
        )
    }

    func presentCoverImage(
        title: String,
        initialDirectory: String,
        destinationDirectory: String
    ) -> Bool {
        return present(
            title: title,
            initialDirectory: initialDirectory,
            purpose: .coverImage(destinationDirectory: destinationDirectory)
        )
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

    private func importedCoverPath(
        from sourceURL: URL,
        destinationDirectory: String
    ) throws -> String {
        let allowedExtensions = Set(["png", "jpg", "jpeg", "webp"])
        let fileExtension = sourceURL.pathExtension.lowercased()
        guard allowedExtensions.contains(fileExtension) else {
            throw NSError(
                domain: "AetherNativeFilePicker",
                code: 1,
                userInfo: [NSLocalizedDescriptionKey: "The selected file is not a supported cover image"]
            )
        }
        guard !destinationDirectory.isEmpty else {
            throw NSError(
                domain: "AetherNativeFilePicker",
                code: 2,
                userInfo: [NSLocalizedDescriptionKey: "The cover image destination is unavailable"]
            )
        }

        let destinationRoot = URL(
            fileURLWithPath: destinationDirectory,
            isDirectory: true
        )
        try FileManager.default.createDirectory(
            at: destinationRoot,
            withIntermediateDirectories: true
        )
        let destinationURL = destinationRoot.appendingPathComponent(
            "cover-\(UUID().uuidString).\(fileExtension)",
            isDirectory: false
        )
        try FileManager.default.copyItem(at: sourceURL, to: destinationURL)
        return destinationURL.standardizedFileURL.path
    }

#if os(iOS)
    private func presentOnMainThread(
        title: String,
        initialDirectory: String,
        purpose: Purpose
    ) {
        guard let presenter = topViewController() else {
            complete(status: "error", error: "Unable to find a window for the system file picker")
            return
        }
        let contentTypes: [UTType]
        switch purpose {
        case .launchFile:
            contentTypes = [.data]
        case .coverImage:
            contentTypes = [
                .png,
                .jpeg,
                UTType(filenameExtension: "webp"),
            ].compactMap { $0 }
        }
        let picker = UIDocumentPickerViewController(
            forOpeningContentTypes: contentTypes,
            asCopy: false
        )
        picker.delegate = self
        picker.allowsMultipleSelection = false
        picker.modalPresentationStyle = .formSheet
        picker.directoryURL = directoryURL(for: initialDirectory)
        documentPickerPurpose = purpose
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
    private func presentOnMainThread(
        title: String,
        initialDirectory: String,
        purpose: Purpose
    ) {
        let panel = NSOpenPanel()
        panel.title = title
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.resolvesAliases = true
        switch purpose {
        case .launchFile:
            panel.allowedContentTypes = [
                UTType(filenameExtension: "exe"),
                UTType(filenameExtension: "xp3"),
            ].compactMap { $0 }
        case .coverImage:
            panel.allowedContentTypes = [
                .png,
                .jpeg,
                UTType(filenameExtension: "webp"),
            ].compactMap { $0 }
        }
        panel.directoryURL = directoryURL(for: initialDirectory)
        openPanel = panel
        panel.begin { [weak self] response in
            guard let self else { return }
            self.openPanel = nil
            if response == .OK, let url = panel.url {
                switch purpose {
                case .launchFile:
                    self.complete(status: "selected", path: url.standardizedFileURL.path)
                case .coverImage(let destinationDirectory):
                    do {
                        let path = try self.importedCoverPath(
                            from: url,
                            destinationDirectory: destinationDirectory
                        )
                        self.complete(status: "selected", path: path)
                    } catch {
                        self.complete(status: "error", error: error.localizedDescription)
                    }
                }
            } else {
                self.complete(status: "cancelled")
            }
        }
    }
#else
    private func presentOnMainThread(
        title: String,
        initialDirectory: String,
        purpose: Purpose
    ) {
        _ = title
        _ = initialDirectory
        _ = purpose
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
        defer {
            if scoped {
                url.stopAccessingSecurityScopedResource()
            }
        }
        switch documentPickerPurpose {
        case .launchFile:
            complete(status: "selected", path: url.standardizedFileURL.path)
        case .coverImage(let destinationDirectory):
            do {
                let path = try importedCoverPath(
                    from: url,
                    destinationDirectory: destinationDirectory
                )
                complete(status: "selected", path: path)
            } catch {
                complete(status: "error", error: error.localizedDescription)
            }
        }
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
    return AetherNativeLaunchFilePicker.shared.presentLaunchFile(
        title: title,
        initialDirectory: initialDirectory
    ) ? 1 : 0
}

@_cdecl("aether_native_cover_file_picker_present")
public func aetherNativeCoverFilePickerPresent(
    _ titlePointer: UnsafePointer<CChar>?,
    _ initialDirectoryPointer: UnsafePointer<CChar>?,
    _ destinationDirectoryPointer: UnsafePointer<CChar>?
) -> Int32 {
    let title = titlePointer.map { String(cString: $0) } ?? ""
    let initialDirectory = initialDirectoryPointer.map { String(cString: $0) } ?? ""
    let destinationDirectory = destinationDirectoryPointer.map { String(cString: $0) } ?? ""
    return AetherNativeLaunchFilePicker.shared.presentCoverImage(
        title: title,
        initialDirectory: initialDirectory,
        destinationDirectory: destinationDirectory
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
