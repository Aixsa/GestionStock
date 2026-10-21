#pragma once

#include "database.hpp"

#include <gtk/gtk.h>

#include <memory>
#include <functional>
#include <string>
#include <vector>

class App {
public:
    App(GtkApplication* application, const std::filesystem::path& databasePath);
    void activate();

private:
    enum class Page { Dashboard, Articles, Mouvements, Commandes, Ventes, Entrepots, Fournisseurs, Rapports, Utilisateurs, Parametres };
    struct NavItem { Page page; GtkWidget* button; };

    void installStyle();
    void applyStyle();
    void showSetup();
    void showLogin(const std::string& error = {});
    void showMain();
    void buildHeaderBar();
    void refreshShell();
    void showToast(const std::string& text, bool error = false);
    bool darkThemeActive() const;
    int pageSize() const;
    void logout();
    void navigate(Page page);
    void buildDashboard();
    void buildProducts();
    void buildMovements();
    void buildOrders();
    void buildSales();
    void buildWarehouses();
    void buildSuppliers();
    void buildReports();
    void buildUsers();
    void buildSettings();
    void renderProducts(const std::string& filter);
    void openProductForm(const Product* product = nullptr);
    void openMovementForm(int productId = 0);
    void openOrderForm(int productId = 0, double quantity = 0);
    void openCustomerOrderForm(int productId = 0, double quantity = 0);
    void openCustomerForm(const Customer* customer = nullptr);
    void openCustomerPricesForm(const Customer& customer);
    void openSalesForm(const std::string& type);
    void printSalesDocument(int documentId);
    void printBarcodeLabels();
    void openShipCustomerOrderForm(const CustomerOrder& order);
    void openWarehouseForm(const Warehouse* warehouse = nullptr);
    void openSupplierForm(const Supplier* supplier = nullptr);
    void openUserForm(const User* user = nullptr);
    void openAccountForm();
    void openReceiveOrderForm(const PurchaseOrder& order);
    void openLotQualityForm(int lotId, const std::string& currentStatus, const std::string& currentNote);
    void showMovementReceipt(int movementId);
    void showProductDetails(const Product& product);
    void printInventory();
    void printMovements();
    void printReorder();
    void printAbc();
    void printExpiring();
    void exportInventory();
    void exportMovements();
    void backupDatabase();
    bool canViewStock() const;
    void addSectionTitle(const std::string& title, const std::string& subtitle = {});
    GtkWidget* makePanel(const std::string& title = {});
    GtkWidget* makeMetric(const std::string& label, const std::string& value, const std::string& detail, const std::string& tone = "");
    GtkWidget* makeButton(const std::string& text, const std::string& css = "");
    GtkWidget* makeLabel(const std::string& text, const std::string& css = "");
    GtkWidget* makeTable(const std::vector<std::string>& headers,
                         const std::vector<std::vector<std::string>>& rows);
    void addEmptyState(GtkWidget* parent, const std::string& title, const std::string& detail);
    void showMessage(const std::string& text, bool error = false);
    void showDocument(const std::filesystem::path& path);
    void append(GtkWidget* parent, GtkWidget* child);
    void clear(GtkWidget* box);
    GtkWidget* wrapScrollable(GtkWidget* child, int minimumWidth = 0);

    GtkApplication* application_{};
    std::unique_ptr<Database> database_;
    GtkWidget* window_{};
    GtkWidget* pageContainer_{};
    GtkWidget* navBox_{};
    GtkWidget* statusLabel_{};
    GtkWidget* sidebarUser_{};
    GtkWidget* toastLayer_{};
    GtkWidget* headerTitle_{};
    GtkWidget* headerSubtitle_{};
    GtkWidget* themeButton_{};
    GtkWidget* settingsButton_{};
    GtkCssProvider* styleProvider_{};
    std::vector<NavItem> navItems_;
    User session_;
    Page currentPage_{Page::Dashboard};
    std::string productFilter_;
    int productPage_{};
    int movementPage_{};
    int customerOrderPage_{};
    int supplierOrderPage_{};
    int supplierPage_{};
    int warehousePage_{};
    int abcPage_{};
    int qualityLotPage_{};
    int salesPage_{};
    std::string salesFilter_;
};
