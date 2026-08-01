import Foundation
import StoreKit

private struct AetherStoreSnapshot: Codable {
    var available = true
    var revision: UInt64 = 0
    var productID = ""
    var productState = "idle"
    var displayName = ""
    var productDescription = ""
    var displayPrice = ""
    var entitled = false
    var entitlementState = "idle"
    var entitlementCheckRequested: UInt64 = 0
    var entitlementCheckCompleted: UInt64 = 0
    var operationState = "idle"
    var operationSerial: UInt64 = 0
    var lastError = ""

    enum CodingKeys: String, CodingKey {
        case available
        case revision
        case productID = "product_id"
        case productState = "product_state"
        case displayName = "display_name"
        case productDescription = "product_description"
        case displayPrice = "display_price"
        case entitled
        case entitlementState = "entitlement_state"
        case entitlementCheckRequested = "entitlement_check_requested"
        case entitlementCheckCompleted = "entitlement_check_completed"
        case operationState = "operation_state"
        case operationSerial = "operation_serial"
        case lastError = "last_error"
    }
}

private final class AetherStoreKitManager: @unchecked Sendable {
    static let shared = AetherStoreKitManager()

    private let lock = NSLock()
    private var snapshot = AetherStoreSnapshot()
    private var product: Product?
    private var nextSerial: UInt64 = 1
    private var transactionListener: Task<Void, Never>?

    private init() {}

    deinit {
        transactionListener?.cancel()
    }

    private func mutate(_ body: (inout AetherStoreSnapshot) -> Void) {
        lock.lock()
        body(&snapshot)
        snapshot.revision &+= 1
        lock.unlock()
    }

    private func serial() -> UInt64 {
        lock.lock()
        let value = nextSerial
        nextSerial &+= 1
        lock.unlock()
        return value
    }

    private func cachedProduct() -> Product? {
        lock.lock()
        let value = product
        lock.unlock()
        return value
    }

    private func cacheProduct(_ value: Product) {
        lock.lock()
        product = value
        lock.unlock()
    }

    private func isEntitled() -> Bool {
        lock.lock()
        let value = snapshot.entitled
        lock.unlock()
        return value
    }

    func copyStateJSON() -> UnsafeMutablePointer<CChar>? {
        lock.lock()
        let current = snapshot
        lock.unlock()
        guard let data = try? JSONEncoder().encode(current),
              let json = String(data: data, encoding: .utf8) else {
            return nil
        }
        return json.withCString { strdup($0) }
    }

    func start(productID: String) {
        guard !productID.isEmpty else { return }
        let shouldLoadProduct = cachedProduct()?.id != productID
        mutate {
            $0.productID = productID
            if shouldLoadProduct &&
                ($0.productState == "idle" || $0.productState == "error") {
                $0.productState = "loading"
            }
        }
        if transactionListener == nil {
            transactionListener = Task { [weak self] in
                for await result in Transaction.updates {
                    guard let self else { return }
                    switch result {
                    case .verified(let transaction):
                        if transaction.productID == productID {
                            await transaction.finish()
                            _ = self.refreshEntitlement(productID: productID)
                        }
                    case .unverified(let transaction, let error):
                        if transaction.productID == productID {
                            self.mutate {
                                $0.entitled = false
                                $0.entitlementState = "unverified"
                                $0.lastError = error.localizedDescription
                            }
                        }
                    }
                }
            }
        }
        if shouldLoadProduct {
            Task { [weak self] in
                await self?.loadProduct(productID: productID)
            }
        }
    }

    private func loadProduct(productID: String) async {
        do {
            let products = try await Product.products(for: [productID])
            guard let loaded = products.first(where: { $0.id == productID }) else {
                mutate {
                    $0.productState = "not_found"
                    $0.lastError = "Product not found: \(productID)"
                }
                return
            }
            guard loaded.type == .nonConsumable else {
                mutate {
                    $0.productState = "wrong_type"
                    $0.lastError = "Product must be configured as non-consumable: \(productID)"
                }
                return
            }
            cacheProduct(loaded)
            mutate {
                $0.productState = "ready"
                $0.displayName = loaded.displayName
                $0.productDescription = loaded.description
                $0.displayPrice = loaded.displayPrice
                $0.lastError = ""
            }
        } catch {
            mutate {
                $0.productState = "error"
                $0.lastError = error.localizedDescription
            }
        }
    }

    func refreshEntitlement(productID: String) -> UInt64 {
        guard !productID.isEmpty else { return 0 }
        start(productID: productID)
        let request = serial()
        mutate {
            $0.entitlementCheckRequested = request
            $0.entitlementState = "checking"
            $0.lastError = ""
        }
        Task { [weak self] in
            await self?.performEntitlementCheck(
                productID: productID,
                request: request
            )
        }
        return request
    }

    private func performEntitlementCheck(
        productID: String,
        request: UInt64
    ) async {
        var entitled = false
        var foundUnverified = false
        var verificationMessage = ""

        for await result in Transaction.currentEntitlements {
            switch result {
            case .verified(let transaction):
                if transaction.productID == productID &&
                    transaction.revocationDate == nil {
                    entitled = true
                }
            case .unverified(let transaction, let error):
                if transaction.productID == productID {
                    foundUnverified = true
                    verificationMessage = error.localizedDescription
                }
            }
        }

        mutate {
            if request >= $0.entitlementCheckCompleted {
                $0.entitled = entitled
                $0.entitlementCheckCompleted = request
                if entitled {
                    $0.entitlementState = "verified"
                    $0.lastError = ""
                } else if foundUnverified {
                    $0.entitlementState = "unverified"
                    $0.lastError = verificationMessage
                } else {
                    $0.entitlementState = "not_purchased"
                    $0.lastError = ""
                }
            }
        }
    }

    func purchase(productID: String) -> UInt64 {
        guard !productID.isEmpty else { return 0 }
        start(productID: productID)
        let operation = serial()
        mutate {
            $0.operationSerial = operation
            $0.operationState = "purchasing"
            $0.lastError = ""
        }
        Task { [weak self] in
            await self?.performPurchase(
                productID: productID,
                operation: operation
            )
        }
        return operation
    }

    private func currentProduct(productID: String) async throws -> Product {
        let cached = cachedProduct()
        if let cached, cached.id == productID {
            return cached
        }
        let products = try await Product.products(for: [productID])
        guard let loaded = products.first(where: { $0.id == productID }) else {
            throw AetherStoreError.productNotFound(productID)
        }
        guard loaded.type == .nonConsumable else {
            throw AetherStoreError.wrongProductType(productID)
        }
        cacheProduct(loaded)
        mutate {
            $0.productState = "ready"
            $0.displayName = loaded.displayName
            $0.productDescription = loaded.description
            $0.displayPrice = loaded.displayPrice
        }
        return loaded
    }

    private func performPurchase(
        productID: String,
        operation: UInt64
    ) async {
        do {
            let loaded = try await currentProduct(productID: productID)
            let result = try await loaded.purchase()
            switch result {
            case .success(let verification):
                switch verification {
                case .verified(let transaction):
                    guard transaction.productID == productID,
                          transaction.revocationDate == nil else {
                        mutate {
                            $0.entitled = false
                            $0.operationState = "error"
                            $0.lastError = "Purchased transaction does not grant this product."
                        }
                        return
                    }
                    await transaction.finish()
                    mutate {
                        $0.entitled = true
                        $0.entitlementState = "verified"
                        $0.operationSerial = operation
                        $0.operationState = "purchased"
                        $0.lastError = ""
                    }
                case .unverified(_, let error):
                    mutate {
                        $0.entitled = false
                        $0.entitlementState = "unverified"
                        $0.operationSerial = operation
                        $0.operationState = "error"
                        $0.lastError = error.localizedDescription
                    }
                }
            case .pending:
                mutate {
                    $0.operationSerial = operation
                    $0.operationState = "pending"
                }
            case .userCancelled:
                mutate {
                    $0.operationSerial = operation
                    $0.operationState = "cancelled"
                }
            @unknown default:
                mutate {
                    $0.operationSerial = operation
                    $0.operationState = "error"
                    $0.lastError = "Unknown StoreKit purchase result."
                }
            }
        } catch {
            mutate {
                $0.operationSerial = operation
                $0.operationState = "error"
                $0.lastError = error.localizedDescription
            }
        }
    }

    func restore(productID: String) -> UInt64 {
        guard !productID.isEmpty else { return 0 }
        start(productID: productID)
        let operation = serial()
        mutate {
            $0.operationSerial = operation
            $0.operationState = "restoring"
            $0.lastError = ""
        }
        Task { [weak self] in
            guard let self else { return }
            do {
                try await AppStore.sync()
                let request = self.serial()
                await self.performEntitlementCheck(
                    productID: productID,
                    request: request
                )
                let restored = self.isEntitled()
                self.mutate {
                    $0.operationSerial = operation
                    $0.operationState = restored ? "restored" : "not_purchased"
                }
            } catch {
                self.mutate {
                    $0.operationSerial = operation
                    $0.operationState = "error"
                    $0.lastError = error.localizedDescription
                }
            }
        }
        return operation
    }
}

private enum AetherStoreError: LocalizedError {
    case productNotFound(String)
    case wrongProductType(String)

    var errorDescription: String? {
        switch self {
        case .productNotFound(let productID):
            return "Product not found: \(productID)"
        case .wrongProductType(let productID):
            return "Product must be configured as non-consumable: \(productID)"
        }
    }
}

@_cdecl("aether_storekit_start")
public func aetherStoreKitStart(
    _ productIDPointer: UnsafePointer<CChar>?
) -> Int32 {
    guard let productIDPointer else { return 0 }
    let productID = String(cString: productIDPointer)
    guard !productID.isEmpty else { return 0 }
    AetherStoreKitManager.shared.start(productID: productID)
    return 1
}

@_cdecl("aether_storekit_refresh_entitlement")
public func aetherStoreKitRefreshEntitlement(
    _ productIDPointer: UnsafePointer<CChar>?
) -> UInt64 {
    guard let productIDPointer else { return 0 }
    return AetherStoreKitManager.shared.refreshEntitlement(
        productID: String(cString: productIDPointer)
    )
}

@_cdecl("aether_storekit_purchase")
public func aetherStoreKitPurchase(
    _ productIDPointer: UnsafePointer<CChar>?
) -> UInt64 {
    guard let productIDPointer else { return 0 }
    return AetherStoreKitManager.shared.purchase(
        productID: String(cString: productIDPointer)
    )
}

@_cdecl("aether_storekit_restore")
public func aetherStoreKitRestore(
    _ productIDPointer: UnsafePointer<CChar>?
) -> UInt64 {
    guard let productIDPointer else { return 0 }
    return AetherStoreKitManager.shared.restore(
        productID: String(cString: productIDPointer)
    )
}

@_cdecl("aether_storekit_copy_state_json")
public func aetherStoreKitCopyStateJSON() -> UnsafeMutablePointer<CChar>? {
    return AetherStoreKitManager.shared.copyStateJSON()
}

@_cdecl("aether_storekit_free_string")
public func aetherStoreKitFreeString(_ pointer: UnsafeMutablePointer<CChar>?) {
    free(pointer)
}
