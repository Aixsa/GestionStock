#pragma once

#include "models.hpp"

#include <sqlite3.h>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class Database {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    const std::filesystem::path& path() const { return path_; }
    int userCount() const;
    std::optional<User> authenticate(const std::string& username, const std::string& password);
    std::vector<User> users() const;
    void createFirstAdmin(const std::string& fullName, const std::string& username, const std::string& password);
    void createWorker(const User& user, const std::string& password, int actorId);
    void updateWorker(const User& user, int actorId);
    void updateAdminAccount(const User& user, const std::string& newPassword, int actorId);
    void resetPassword(int userId, const std::string& password, int actorId);
    void setUserActive(int userId, bool active, int actorId);

    std::vector<Product> products(const std::string& filter = {}) const;
    std::vector<Product> productsPage(const std::string& filter, int limit, int offset) const;
    int productCount(const std::string& filter = {}) const;
    Product product(int id) const;
    std::vector<std::vector<std::string>> productLots(int productId) const;
    std::vector<std::vector<std::string>> inventoryRows(bool includeCosts) const;
    std::vector<std::vector<std::string>> qualityLots() const;
    void setLotQuality(int lotId, const std::string& status, const std::string& note, int actorId);
    void saveProduct(const Product& product, int actorId);
    void archiveProduct(int productId, int actorId);
    std::vector<Warehouse> warehouses() const;
    void saveWarehouse(const Warehouse& warehouse, int actorId);
    std::vector<Supplier> suppliers() const;
    void saveSupplier(const Supplier& supplier, int actorId);

    int addMovement(int productId, const std::string& type, double quantity, double unitCost,
                    int warehouseId, const std::string& location, const std::string& lotCode, const std::string& expiry,
                    const std::string& qualityStatus, const std::string& qualityNote,
                    const std::string& reference, const std::string& reason, int actorId);
    std::pair<int,int> transferStock(int productId, double quantity, int sourceWarehouseId,
                                     int destinationWarehouseId, const std::string& destinationLocation,
                                     const std::string& reference, const std::string& note, int actorId);
    std::vector<Movement> movements(int limit = 500, int offset = 0) const;
    std::optional<Movement> movementById(int movementId) const;
    int movementCount() const;
    std::vector<std::vector<std::string>> movementLines(int batchId) const;
    std::vector<ReorderSuggestion> reorderSuggestions() const;
    std::vector<AbcRow> abcAnalysis() const;
    std::vector<PurchaseOrder> purchaseOrders() const;
    std::vector<PurchaseOrder> purchaseOrdersPage(int limit, int offset) const;
    int purchaseOrderCount() const;
    std::vector<Customer> customers(const std::string& filter = {}) const;
    Customer customer(int customerId) const;
    void saveCustomer(const Customer& customer, int actorId);
    void setCustomerActive(int customerId, bool active, int actorId);
    double customerPrice(int customerId, int productId, double basePrice) const;
    void setCustomerPrice(int customerId, int productId, double price, int actorId);
    std::vector<SalesDocument> salesDocuments(int limit = 500, int offset = 0, const std::string& type = {}) const;
    int salesDocumentCount(const std::string& type = {}) const;
    SalesDocument salesDocument(int documentId) const;
    std::vector<SalesLine> salesLines(int documentId) const;
    int createSalesDocument(const std::string& type, int customerId, const std::string& customerName,
                            int warehouseId, const std::string& documentDate, const std::string& note,
                            double taxRate, double discountRate, const std::vector<SalesLineInput>& lines,
                            int sourceDocumentId, int actorId);
    int invoiceFromDelivery(int documentId, int actorId);
    std::vector<DeadStockRow> deadStockRows(int months) const;
    std::vector<FlowStatRow> flowStats(int days) const;
    std::vector<PmpRow> pmpValuation() const;
    int expiringLotCount(int days) const;
    std::vector<CustomerOrder> customerOrders(int limit = 500, int offset = 0) const;
    int customerOrderCount() const;
    int createCustomerOrder(const std::string& customer, int productId, double quantity,
                            const std::string& requestedDate, const std::string& note, int actorId);
    int shipCustomerOrder(int orderId, double quantity, int warehouseId,
                          const std::string& location, int actorId);
    void cancelCustomerOrder(int orderId, int actorId);
    int createPurchaseOrder(int productId, int supplierId, double quantity, double unitCost, int actorId);
    void cancelPurchaseOrder(int orderId, int actorId);
    void markPurchaseOrderSent(int orderId, int actorId);
    int receivePurchaseOrder(int orderId, double quantity, int warehouseId, const std::string& location, const std::string& lotCode,
                             const std::string& expiry, const std::string& qualityStatus,
                             const std::string& qualityNote, int actorId);
    std::vector<std::vector<std::string>> expiringLots(int days = 30) const;
    double totalStockValue() const;
    double totalHoldingCost() const;
    double totalVolume() const;
    double physicalStock(int productId) const;
    int lowStockCount() const;
    int zeroStockCount() const;
    int overStockCount() const;
    std::vector<std::vector<std::string>> auditLog(int limit = 200) const;
    std::string appSetting(const std::string& key, const std::string& fallback = {}) const;
    void setAppSetting(const std::string& key, const std::string& value);
    void backupTo(const std::filesystem::path& destination) const;
    void restoreFrom(const std::filesystem::path& source);
    std::string databaseTime() const;

private:
    void initialize();
    void audit(int actorId, const std::string& action, const std::string& entity, int entityId,
               const std::string& detail);
    void exec(const std::string& sql) const;

    sqlite3* db_{};
    std::filesystem::path path_;
};
