#pragma once

#include <string>

struct User {
    int id{};
    std::string username;
    std::string fullName;
    std::string passwordHash;
    bool admin{};
    bool canViewStock{};
    bool canManageArticles{};
    bool canManageMovements{};
    bool canManageWarehouses{};
    bool canManageSuppliers{};
    bool canViewCosts{};
    bool canPrint{};
    bool canManageUsers{};
    bool active{true};
    std::string createdAt;
    std::string lastLogin;
};

struct Product {
    int id{};
    std::string sku;
    std::string name;
    std::string category;
    std::string unit;
    std::string description;
    std::string barcode;
    std::string stockType{"Stock courant"};
    std::string flowPolicy{"Flux tirés · demande réelle"};
    double customerCommitments{};
    double pendingInbound{};
    double projectedStock{};
    double minStock{};
    double safetyStock{};
    double maxStock{};
    int coverageDays{30};
    int leadDays{};
    double unitCost{};
    double salePrice{};
    double holdingRate{};
    double orderCost{};
    double shortageCost{};
    double volumeM3{};
    bool perishable{};
    double onHand{};
    std::string createdAt;
    std::string updatedAt;
};

struct Warehouse {
    int id{};
    std::string name;
    std::string address;
    double capacityM3{};
    double usedM3{};
    std::string createdAt;
};

struct Supplier {
    int id{};
    std::string name;
    std::string contact;
    std::string phone;
    std::string email;
    int leadDays{};
    int administrativeDays{};
    int preparationDays{};
    int transportDays{};
    int inspectionDays{};
};

struct Movement {
    int id{};
    std::string type;
    std::string sku;
    std::string product;
    std::string warehouse;
    std::string location;
    std::string qualityStatus;
    double quantity{};
    double unitCost{};
    std::string lotCode;
    std::string expiry;
    std::string reference;
    std::string reason;
    std::string user;
    std::string createdAt;
};

struct ReorderSuggestion {
    Product product;
    std::string supplier;
    double dailyDemand{};
    double pendingQty{};
    double customerCommitments{};
    double projectedQty{};
    double reorderPoint{};
    double targetStock{};
    double suggestedQty{};
    int leadDays{};
    bool capacityLimited{};
    double economicLot{};
    double annualOrderCost{};
    double annualHoldingCost{};
    double shortageExposure{};
};

struct AbcRow {
    int productId{};
    std::string sku;
    std::string name;
    double consumptionValue{};
    double cumulativeShare{};
    double cumulativeArticleShare{};
    char category{'C'};
};

struct CustomerOrder {
    int id{};
    std::string reference;
    std::string customer;
    std::string status;
    std::string sku;
    std::string product;
    std::string unit;
    double quantity{};
    double shippedQuantity{};
    std::string requestedDate;
    std::string note;
    std::string createdBy;
    std::string createdAt;
};

struct PurchaseOrder {
    int id{};
    std::string reference;
    std::string status;
    std::string supplier;
    std::string sku;
    std::string product;
    double quantity{};
    double receivedQuantity{};
    double unitCost{};
    double total{};
    std::string createdBy;
    std::string createdAt;
};

struct Customer {
    int id{};
    std::string code;
    std::string name;
    std::string contact;
    std::string phone;
    std::string email;
    std::string address;
    std::string taxId;
    double discountRate{};
    std::string paymentTerms;
    std::string notes;
    bool active{true};
    std::string createdAt;
};

struct SalesLineInput {
    int productId{};
    double quantity{};
    double unitPrice{};
    double discountRate{};
    double taxRate{};
    std::string expiry;
};

struct SalesLine {
    int id{};
    int documentId{};
    int productId{};
    std::string sku;
    std::string name;
    std::string unit;
    double quantity{};
    double unitPrice{};
    double unitCost{};
    double discountRate{};
    double taxRate{};
    double lineTotal{};
    std::string expiry;
};

struct SalesDocument {
    int id{};
    std::string reference;
    std::string type;
    int customerId{};
    std::string customerName;
    std::string status;
    std::string documentDate;
    std::string dueDate;
    int warehouseId{};
    std::string warehouse;
    int sourceDocumentId{};
    double subtotal{};
    double discountTotal{};
    double taxRate{};
    double taxTotal{};
    double total{};
    std::string note;
    std::string createdBy;
    std::string createdAt;
    int lineCount{};
};

struct DeadStockRow {
    int productId{};
    std::string sku;
    std::string name;
    std::string category;
    double quantity{};
    double value{};
    std::string lastOut;
    int daysSinceOut{};
};

struct FlowStatRow {
    int productId{};
    std::string sku;
    std::string name;
    std::string unit;
    double inbound{};
    double outbound{};
    double inboundValue{};
    double outboundValue{};
    double currentQuantity{};
    double averageDailyOut{};
    double coverageDays{};
    std::string lastMovement;
    std::string status;
};

struct PmpRow {
    int productId{};
    std::string sku;
    std::string name;
    std::string unit;
    double quantity{};
    double value{};
    double pmp{};
    double unitCost{};
    double variance{};
};
