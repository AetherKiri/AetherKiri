import Foundation
import Security
import StoreKit

private let aetherListLimitProductID = "com.aether.list.limit"
private let aetherCoffeeProductID = "com.aether.coffee"
private let aetherCoffeeAccessDuration: TimeInterval = 30.0 * 24.0 * 60.0 * 60.0

private struct AetherStoreSnapshot: Codable {
    var available = true
    var revision: UInt64 = 0
    var productID = ""
    var productState = "idle"
    var productType = ""
    var displayName = ""
    var productDescription = ""
    var displayPrice = ""
    var entitled = false
    var entitlementState = "idle"
    var entitlementExpiration: TimeInterval = 0
    var entitlementExpirationDisplay = ""
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
        case productType = "product_type"
        case displayName = "display_name"
        case productDescription = "product_description"
        case displayPrice = "display_price"
        case entitled
        case entitlementState = "entitlement_state"
        case entitlementExpiration = "entitlement_expiration"
        case entitlementExpirationDisplay = "entitlement_expiration_display"
        case entitlementCheckRequested = "entitlement_check_requested"
        case entitlementCheckCompleted = "entitlement_check_completed"
        case operationState = "operation_state"
        case operationSerial = "operation_serial"
        case lastError = "last_error"
    }
}

private struct AetherCoffeeGrant: Codable {
    var transactionID: String
    var purchaseDate: TimeInterval
    var quantity: Int
}

private struct AetherCoffeeLedger: Codable {
    var grants: [AetherCoffeeGrant] = []
    var lastObservedTime: TimeInterval = 0
}

private enum AetherStoreError: LocalizedError {
    case productNotFound(String)
    case wrongProductType(String)
    case consumableCannotBeRestored(String)

    var errorDescription: String? {
        switch self {
        case .productNotFound(let productID):
            return "Product not found: \(productID)"
        case .wrongProductType(let productID):
            return "Unexpected App Store product type: \(productID)"
        case .consumableCannotBeRestored(let productID):
            return "Consumable purchases cannot be restored: \(productID)"
        }
    }
}

private final class AetherStoreKitManager: @unchecked Sendable {
    static let shared = AetherStoreKitManager()

    private let lock = NSLock()
    private var snapshots: [String: AetherStoreSnapshot] = [:]
    private var products: [String: Product] = [:]
    private var nextSerial: UInt64 = 1
    private var transactionListener: Task<Void, Never>?
    private let coffeeKeychainService = "com.aether.storekit.beta-access.v1"

    private init() {}

    deinit {
        transactionListener?.cancel()
    }

    private func expectedProductType(_ productID: String) -> Product.ProductType? {
        switch productID {
        case aetherListLimitProductID:
            return .nonConsumable
        case aetherCoffeeProductID:
            return .consumable
        default:
            return nil
        }
    }

    private func mutate(
        productID: String,
        _ body: (inout AetherStoreSnapshot) -> Void
    ) {
        lock.lock()
        var value = snapshots[productID] ?? AetherStoreSnapshot(productID: productID)
        body(&value)
        value.revision &+= 1
        snapshots[productID] = value
        lock.unlock()
    }

    private func serial() -> UInt64 {
        lock.lock()
        let value = nextSerial
        nextSerial &+= 1
        lock.unlock()
        return value
    }

    private func cachedProduct(productID: String) -> Product? {
        lock.lock()
        let value = products[productID]
        lock.unlock()
        return value
    }

    private func cacheProduct(_ value: Product) {
        lock.lock()
        products[value.id] = value
        lock.unlock()
    }

    private func isEntitled(productID: String) -> Bool {
        lock.lock()
        let value = snapshots[productID]?.entitled ?? false
        lock.unlock()
        return value
    }

    func copyStateJSON(productID: String) -> UnsafeMutablePointer<CChar>? {
        guard !productID.isEmpty else { return nil }
        lock.lock()
        let current = snapshots[productID] ?? AetherStoreSnapshot(productID: productID)
        lock.unlock()
        guard let data = try? JSONEncoder().encode(current),
              let json = String(data: data, encoding: .utf8) else {
            return nil
        }
        return json.withCString { strdup($0) }
    }

    func start(productID: String) {
        guard !productID.isEmpty else { return }
        let shouldLoadProduct = cachedProduct(productID: productID) == nil
        mutate(productID: productID) {
            if shouldLoadProduct && ($0.productState == "idle" || $0.productState == "error") {
                $0.productState = "loading"
            }
        }
        startTransactionListenerIfNeeded()
        if shouldLoadProduct {
            Task { [weak self] in
                await self?.loadProduct(productID: productID)
            }
        }
    }

    private func startTransactionListenerIfNeeded() {
        lock.lock()
        if transactionListener != nil {
            lock.unlock()
            return
        }
        transactionListener = Task { [weak self] in
            // Recover purchases whose delivery was interrupted by a crash or
            // process termination before finish(). This sequence is finite;
            // updates then keeps listening for later account-side changes.
            for await result in Transaction.unfinished {
                guard let self else { return }
                await self.processTransactionUpdate(result)
            }
            for await result in Transaction.updates {
                guard let self else { return }
                await self.processTransactionUpdate(result)
            }
        }
        lock.unlock()
    }

    private func processTransactionUpdate(
        _ result: VerificationResult<Transaction>
    ) async {
        switch result {
        case .verified(let transaction):
            guard expectedProductType(transaction.productID) != nil else { return }
            if transaction.productID == aetherCoffeeProductID,
               transaction.revocationDate == nil {
                recordCoffeeGrant(transaction)
            }
            await transaction.finish()
            _ = refreshEntitlement(productID: transaction.productID)
        case .unverified(let transaction, let error):
            guard expectedProductType(transaction.productID) != nil else { return }
            mutate(productID: transaction.productID) {
                $0.entitled = false
                $0.entitlementState = "unverified"
                $0.lastError = error.localizedDescription
            }
        }
    }

    private func loadProduct(productID: String) async {
        do {
            let loaded = try await fetchProduct(productID: productID)
            cacheProduct(loaded)
            mutate(productID: productID) {
                $0.productState = "ready"
                $0.productType = loaded.type == .consumable ? "consumable" : "non_consumable"
                $0.displayName = loaded.displayName
                $0.productDescription = loaded.description
                $0.displayPrice = loaded.displayPrice
                $0.lastError = ""
            }
        } catch {
            mutate(productID: productID) {
                $0.productState = error is AetherStoreError ? "wrong_type" : "error"
                $0.lastError = error.localizedDescription
            }
        }
    }

    private func fetchProduct(productID: String) async throws -> Product {
        if let cached = cachedProduct(productID: productID) {
            return cached
        }
        let loadedProducts = try await Product.products(for: [productID])
        guard let loaded = loadedProducts.first(where: { $0.id == productID }) else {
            throw AetherStoreError.productNotFound(productID)
        }
        guard let expected = expectedProductType(productID), loaded.type == expected else {
            throw AetherStoreError.wrongProductType(productID)
        }
        return loaded
    }

    func refreshEntitlement(productID: String) -> UInt64 {
        guard expectedProductType(productID) != nil else { return 0 }
        start(productID: productID)
        let request = serial()
        mutate(productID: productID) {
            $0.entitlementCheckRequested = request
            $0.entitlementState = "checking"
            $0.lastError = ""
        }
        Task { [weak self] in
            await self?.performEntitlementCheck(productID: productID, request: request)
        }
        return request
    }

    private func performEntitlementCheck(productID: String, request: UInt64) async {
        if productID == aetherCoffeeProductID {
            await performCoffeeEntitlementCheck(request: request)
        } else {
            await performNonConsumableEntitlementCheck(
                productID: productID,
                request: request
            )
        }
    }

    private func performNonConsumableEntitlementCheck(
        productID: String,
        request: UInt64
    ) async {
        var entitled = false
        var foundUnverified = false
        var verificationMessage = ""

        for await result in Transaction.currentEntitlements {
            switch result {
            case .verified(let transaction):
                if transaction.productID == productID && transaction.revocationDate == nil {
                    entitled = true
                }
            case .unverified(let transaction, let error):
                if transaction.productID == productID {
                    foundUnverified = true
                    verificationMessage = error.localizedDescription
                }
            }
        }

        mutate(productID: productID) {
            guard request >= $0.entitlementCheckCompleted else { return }
            $0.entitled = entitled
            $0.entitlementCheckCompleted = request
            $0.entitlementExpiration = 0
            $0.entitlementExpirationDisplay = ""
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

    private func performCoffeeEntitlementCheck(request: UInt64) async {
        var verifiedGrants: [AetherCoffeeGrant] = []
        var revokedTransactionIDs = Set<String>()
        var foundUnverified = false
        var verificationMessage = ""

        // SKIncludeConsumableInAppPurchaseHistory keeps finished consumables in
        // this signed StoreKit history. Keychain is only an offline cache; it
        // never creates a grant that did not originate in a verified transaction.
        for await result in Transaction.all {
            switch result {
            case .verified(let transaction):
                guard transaction.productID == aetherCoffeeProductID else { continue }
                let transactionID = String(transaction.id)
                if transaction.revocationDate != nil {
                    revokedTransactionIDs.insert(transactionID)
                } else {
                    verifiedGrants.append(AetherCoffeeGrant(
                        transactionID: transactionID,
                        purchaseDate: transaction.purchaseDate.timeIntervalSince1970,
                        quantity: max(1, transaction.purchasedQuantity)
                    ))
                }
            case .unverified(let transaction, let error):
                if transaction.productID == aetherCoffeeProductID {
                    foundUnverified = true
                    verificationMessage = error.localizedDescription
                }
            }
        }

        var ledger = loadCoffeeLedger()
        if !verifiedGrants.isEmpty || !revokedTransactionIDs.isEmpty {
            var byID = Dictionary(uniqueKeysWithValues: ledger.grants.map {
                ($0.transactionID, $0)
            })
            for transactionID in revokedTransactionIDs {
                byID.removeValue(forKey: transactionID)
            }
            for grant in verifiedGrants {
                byID[grant.transactionID] = grant
            }
            ledger.grants = Array(byID.values)
        }
        let now = Date().timeIntervalSince1970
        let effectiveNow = max(now, ledger.lastObservedTime)
        ledger.lastObservedTime = effectiveNow
        saveCoffeeLedger(ledger)
        let expiration = coffeeExpiration(for: ledger.grants)
        let entitled = expiration > effectiveNow

        mutate(productID: aetherCoffeeProductID) {
            guard request >= $0.entitlementCheckCompleted else { return }
            $0.entitled = entitled
            $0.entitlementCheckCompleted = request
            $0.entitlementExpiration = expiration
            $0.entitlementExpirationDisplay = formatExpiration(expiration)
            if entitled {
                $0.entitlementState = "verified"
                $0.lastError = ""
            } else if foundUnverified {
                $0.entitlementState = "unverified"
                $0.lastError = verificationMessage
            } else if ledger.grants.isEmpty {
                $0.entitlementState = "not_purchased"
                $0.lastError = ""
            } else {
                $0.entitlementState = "expired"
                $0.lastError = ""
            }
        }
    }

    func purchase(productID: String) -> UInt64 {
        guard expectedProductType(productID) != nil else { return 0 }
        start(productID: productID)
        let operation = serial()
        mutate(productID: productID) {
            $0.operationSerial = operation
            $0.operationState = "purchasing"
            $0.lastError = ""
        }
        Task { [weak self] in
            await self?.performPurchase(productID: productID, operation: operation)
        }
        return operation
    }

    private func performPurchase(productID: String, operation: UInt64) async {
        do {
            let loaded = try await fetchProduct(productID: productID)
            cacheProduct(loaded)
            let result = try await loaded.purchase()
            switch result {
            case .success(let verification):
                switch verification {
                case .verified(let transaction):
                    guard transaction.productID == productID,
                          transaction.revocationDate == nil else {
                        mutate(productID: productID) {
                            $0.entitled = false
                            $0.operationState = "error"
                            $0.lastError = "Purchased transaction does not grant this product."
                        }
                        return
                    }
                    if productID == aetherCoffeeProductID {
                        recordCoffeeGrant(transaction)
                    }
                    await transaction.finish()
                    let expiration = productID == aetherCoffeeProductID
                        ? coffeeExpiration(for: loadCoffeeLedger().grants) : 0
                    mutate(productID: productID) {
                        $0.entitled = true
                        $0.entitlementState = "verified"
                        $0.entitlementExpiration = expiration
                        $0.entitlementExpirationDisplay = formatExpiration(expiration)
                        $0.operationSerial = operation
                        $0.operationState = "purchased"
                        $0.lastError = ""
                    }
                case .unverified(_, let error):
                    mutate(productID: productID) {
                        $0.entitled = false
                        $0.entitlementState = "unverified"
                        $0.operationSerial = operation
                        $0.operationState = "error"
                        $0.lastError = error.localizedDescription
                    }
                }
            case .pending:
                mutate(productID: productID) {
                    $0.operationSerial = operation
                    $0.operationState = "pending"
                }
            case .userCancelled:
                mutate(productID: productID) {
                    $0.operationSerial = operation
                    $0.operationState = "cancelled"
                }
            @unknown default:
                mutate(productID: productID) {
                    $0.operationSerial = operation
                    $0.operationState = "error"
                    $0.lastError = "Unknown StoreKit purchase result."
                }
            }
        } catch {
            mutate(productID: productID) {
                $0.operationSerial = operation
                $0.operationState = "error"
                $0.lastError = error.localizedDescription
            }
        }
    }

    func restore(productID: String) -> UInt64 {
        guard expectedProductType(productID) == .nonConsumable else { return 0 }
        start(productID: productID)
        let operation = serial()
        mutate(productID: productID) {
            $0.operationSerial = operation
            $0.operationState = "restoring"
            $0.lastError = ""
        }
        Task { [weak self] in
            guard let self else { return }
            do {
                try await AppStore.sync()
                let request = self.serial()
                await self.performNonConsumableEntitlementCheck(
                    productID: productID,
                    request: request
                )
                let restored = self.isEntitled(productID: productID)
                self.mutate(productID: productID) {
                    $0.operationSerial = operation
                    $0.operationState = restored ? "restored" : "not_purchased"
                }
            } catch {
                self.mutate(productID: productID) {
                    $0.operationSerial = operation
                    $0.operationState = "error"
                    $0.lastError = error.localizedDescription
                }
            }
        }
        return operation
    }

    private func recordCoffeeGrant(_ transaction: Transaction) {
        guard transaction.productID == aetherCoffeeProductID,
              transaction.revocationDate == nil else { return }
        var ledger = loadCoffeeLedger()
        let transactionID = String(transaction.id)
        guard !ledger.grants.contains(where: { $0.transactionID == transactionID }) else {
            return
        }
        ledger.grants.append(AetherCoffeeGrant(
            transactionID: transactionID,
            purchaseDate: transaction.purchaseDate.timeIntervalSince1970,
            quantity: max(1, transaction.purchasedQuantity)
        ))
        ledger.lastObservedTime = max(
            ledger.lastObservedTime,
            Date().timeIntervalSince1970
        )
        saveCoffeeLedger(ledger)
    }

    private func coffeeExpiration(for grants: [AetherCoffeeGrant]) -> TimeInterval {
        var expiration: TimeInterval = 0
        let ordered = grants.sorted {
            if $0.purchaseDate == $1.purchaseDate {
                return $0.transactionID < $1.transactionID
            }
            return $0.purchaseDate < $1.purchaseDate
        }
        for grant in ordered {
            expiration = max(expiration, grant.purchaseDate)
            expiration += aetherCoffeeAccessDuration * Double(max(1, grant.quantity))
        }
        return expiration
    }

    private func formatExpiration(_ timestamp: TimeInterval) -> String {
        guard timestamp > 0 else { return "" }
        let formatter = DateFormatter()
        formatter.locale = .current
        formatter.timeZone = .current
        formatter.dateStyle = .medium
        formatter.timeStyle = .short
        return formatter.string(from: Date(timeIntervalSince1970: timestamp))
    }

    private func loadCoffeeLedger() -> AetherCoffeeLedger {
#if DEBUG
        // Local Debug apps are ad-hoc signed, so their designated requirement
        // changes after every rebuild. Reading a ledger created by the previous
        // build would make macOS ask for Keychain access each time. Debug does
        // not enforce beta access, so keep the ledger ephemeral and avoid the
        // Keychain entirely.
        return AetherCoffeeLedger()
#else
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: coffeeKeychainService,
            kSecAttrAccount as String: aetherCoffeeProductID,
            kSecMatchLimit as String: kSecMatchLimitOne,
            kSecReturnData as String: true,
        ]
        var result: CFTypeRef?
        let status = SecItemCopyMatching(query as CFDictionary, &result)
        guard status == errSecSuccess,
              let data = result as? Data,
              let ledger = try? JSONDecoder().decode(AetherCoffeeLedger.self, from: data) else {
            return AetherCoffeeLedger()
        }
        return ledger
#endif
    }

    private func saveCoffeeLedger(_ ledger: AetherCoffeeLedger) {
#if DEBUG
        // See loadCoffeeLedger(): Debug entitlement state intentionally lives
        // only for the current process and must never touch the Keychain.
        return
#else
        guard let data = try? JSONEncoder().encode(ledger) else { return }
        let baseQuery: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: coffeeKeychainService,
            kSecAttrAccount as String: aetherCoffeeProductID,
        ]
        let updateStatus = SecItemUpdate(
            baseQuery as CFDictionary,
            [kSecValueData as String: data] as CFDictionary
        )
        if updateStatus == errSecItemNotFound {
            var addQuery = baseQuery
            addQuery[kSecValueData as String] = data
            addQuery[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
            SecItemAdd(addQuery as CFDictionary, nil)
        }
#endif
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

@_cdecl("aether_storekit_copy_state_json_for_product")
public func aetherStoreKitCopyStateJSONForProduct(
    _ productIDPointer: UnsafePointer<CChar>?
) -> UnsafeMutablePointer<CChar>? {
    guard let productIDPointer else { return nil }
    return AetherStoreKitManager.shared.copyStateJSON(
        productID: String(cString: productIDPointer)
    )
}

// Keep the original symbol for older GDExtension binaries. New callers always
// request a product explicitly so two StoreKit operations cannot overwrite one
// another's state.
@_cdecl("aether_storekit_copy_state_json")
public func aetherStoreKitCopyStateJSON() -> UnsafeMutablePointer<CChar>? {
    return AetherStoreKitManager.shared.copyStateJSON(
        productID: aetherListLimitProductID
    )
}

@_cdecl("aether_storekit_free_string")
public func aetherStoreKitFreeString(_ pointer: UnsafeMutablePointer<CChar>?) {
    free(pointer)
}
