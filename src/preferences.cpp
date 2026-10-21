#include "preferences.hpp"

#include "database.hpp"

#include <gdk/gdk.h>

#include <algorithm>
#include <string>

namespace {
Preferences current;

std::string valueOr(const std::string& value, const std::string& fallback) {
    return value.empty() ? fallback : value;
}

int intOr(const std::string& value, int fallback) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(value, &used);
        return used == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

std::string colorOrEmpty(const std::string& value) {
    if (value.empty()) return {};
    GdkRGBA color{};
    return gdk_rgba_parse(&color, value.c_str()) ? value : std::string{};
}

double doubleOr(const std::string& value, double fallback) {
    try {
        std::size_t used = 0;
        const double parsed = std::stod(value, &used);
        return used == value.size() ? parsed : fallback;
    } catch (...) {
        return fallback;
    }
}

bool boolOr(const std::string& value, bool fallback) {
    if (value == "1" || value == "true" || value == "oui") return true;
    if (value == "0" || value == "false" || value == "non") return false;
    return fallback;
}

void loadColors(Database& database, ColorOverrides& colors) {
    colors.background = colorOrEmpty(database.appSetting("ui.color.background", ""));
    colors.surface = colorOrEmpty(database.appSetting("ui.color.surface", ""));
    colors.surfaceAlt = colorOrEmpty(database.appSetting("ui.color.surface_alt", ""));
    colors.text = colorOrEmpty(database.appSetting("ui.color.text", ""));
    colors.textStrong = colorOrEmpty(database.appSetting("ui.color.text_strong", ""));
    colors.muted = colorOrEmpty(database.appSetting("ui.color.muted", ""));
    colors.border = colorOrEmpty(database.appSetting("ui.color.border", ""));
    colors.accent = colorOrEmpty(database.appSetting("ui.color.accent", ""));
    colors.sidebarTop = colorOrEmpty(database.appSetting("ui.color.sidebar_top", ""));
    colors.sidebarBottom = colorOrEmpty(database.appSetting("ui.color.sidebar_bottom", ""));
    colors.danger = colorOrEmpty(database.appSetting("ui.color.danger", ""));
}

void saveColors(Database& database, const ColorOverrides& colors) {
    database.setAppSetting("ui.color.background", colors.background);
    database.setAppSetting("ui.color.surface", colors.surface);
    database.setAppSetting("ui.color.surface_alt", colors.surfaceAlt);
    database.setAppSetting("ui.color.text", colors.text);
    database.setAppSetting("ui.color.text_strong", colors.textStrong);
    database.setAppSetting("ui.color.muted", colors.muted);
    database.setAppSetting("ui.color.border", colors.border);
    database.setAppSetting("ui.color.accent", colors.accent);
    database.setAppSetting("ui.color.sidebar_top", colors.sidebarTop);
    database.setAppSetting("ui.color.sidebar_bottom", colors.sidebarBottom);
    database.setAppSetting("ui.color.danger", colors.danger);
}
}

bool ColorOverrides::empty() const {
    return background.empty() && surface.empty() && surfaceAlt.empty() && text.empty() && textStrong.empty()
        && muted.empty() && border.empty() && accent.empty() && sidebarTop.empty() && sidebarBottom.empty() && danger.empty();
}

void ColorOverrides::clear() { *this = ColorOverrides{}; }

Preferences& preferences() { return current; }

void loadPreferences(Database& database) {
    current.theme = themeModeFromKey(database.appSetting("ui.theme", "dark"));
    current.accent = accentFromKey(database.appSetting("ui.accent", "teal"));
    current.language = languageFromKey(database.appSetting("ui.language", "fr"));
    current.compact = database.appSetting("ui.compact", "0") == "1";
    current.fontScale = std::clamp(intOr(database.appSetting("ui.font_scale", "100"), 100), 85, 130);
    current.pageSize = std::clamp(intOr(database.appSetting("ui.page_size", "50"), 50), 10, 500);
    current.dateFormat = valueOr(database.appSetting("ui.date_format", "fr"), "fr");
    current.currency = valueOr(database.appSetting("ui.currency", "DA"), "DA");
    current.paperSize = valueOr(database.appSetting("print.paper", "auto"), "auto");
    current.orientation = valueOr(database.appSetting("print.orientation", "auto"), "auto");
    current.signatures = database.appSetting("print.signatures", "1") == "1";
    current.autoOpenDocuments = database.appSetting("print.auto_open", "1") == "1";
    current.rememberLastUser = boolOr(database.appSetting("ui.remember_last_user", "1"), true);
    current.confirmDestructive = boolOr(database.appSetting("ui.confirm_destructive", "1"), true);
    current.lowStockAlerts = boolOr(database.appSetting("ui.low_stock_alerts", "1"), true);
    current.lastUser = database.appSetting("ui.last_user", "");
    current.dlcAlertDays = std::clamp(intOr(database.appSetting("inventory.dlc_days", "30"), 30), 1, 3650);
    current.deadStockMonths = std::clamp(intOr(database.appSetting("inventory.dead_stock_months", "6"), 6), 1, 120);
    current.startupPage = std::clamp(intOr(database.appSetting("ui.startup_page", "0"), 0), 0, 9);
    current.companyName = valueOr(database.appSetting("company.name", "Gestion des stocks"), "Gestion des stocks");
    current.companyAddress = database.appSetting("company.address", "");
    current.companyPhone = database.appSetting("company.phone", "");
    current.companyEmail = database.appSetting("company.email", "");
    current.companyTaxId = database.appSetting("company.tax_id", "");
    current.documentFooter = database.appSetting("company.footer", "");
    current.invoicePrefix = valueOr(database.appSetting("sales.invoice_prefix", "FAC"), "FAC");
    current.proformaPrefix = valueOr(database.appSetting("sales.proforma_prefix", "PRO"), "PRO");
    current.deliveryPrefix = valueOr(database.appSetting("sales.delivery_prefix", "BL"), "BL");
    current.counterPrefix = valueOr(database.appSetting("sales.counter_prefix", "CPT"), "CPT");
    current.returnPrefix = valueOr(database.appSetting("sales.return_prefix", "RET"), "RET");
    current.bmmPrefix = valueOr(database.appSetting("stock.bmm_prefix", "BMM"), "BMM");
    current.defaultTaxRate = std::clamp(doubleOr(database.appSetting("sales.tax_rate", "19"), 19.0), 0.0, 100.0);
    current.pricesIncludeTax = boolOr(database.appSetting("sales.prices_include_tax", "0"), false);
    current.labelWidthMm = std::clamp(intOr(database.appSetting("labels.width_mm", "50"), 50), 20, 150);
    current.labelHeightMm = std::clamp(intOr(database.appSetting("labels.height_mm", "30"), 30), 12, 120);
    current.labelShowPrice = boolOr(database.appSetting("labels.show_price", "1"), true);
    current.labelShowName = boolOr(database.appSetting("labels.show_name", "1"), true);
    loadColors(database, current.colors);
    setLanguage(current.language);
}

void savePreferences(Database& database) {
    database.setAppSetting("ui.theme", themeModeKey(current.theme));
    database.setAppSetting("ui.accent", accentKey(current.accent));
    database.setAppSetting("ui.language", languageKey(current.language));
    database.setAppSetting("ui.compact", current.compact ? "1" : "0");
    database.setAppSetting("ui.font_scale", std::to_string(current.fontScale));
    database.setAppSetting("ui.page_size", std::to_string(current.pageSize));
    database.setAppSetting("ui.date_format", current.dateFormat);
    database.setAppSetting("ui.currency", current.currency);
    database.setAppSetting("print.paper", current.paperSize);
    database.setAppSetting("print.orientation", current.orientation);
    database.setAppSetting("print.signatures", current.signatures ? "1" : "0");
    database.setAppSetting("print.auto_open", current.autoOpenDocuments ? "1" : "0");
    database.setAppSetting("ui.remember_last_user", current.rememberLastUser ? "1" : "0");
    database.setAppSetting("ui.confirm_destructive", current.confirmDestructive ? "1" : "0");
    database.setAppSetting("ui.low_stock_alerts", current.lowStockAlerts ? "1" : "0");
    database.setAppSetting("ui.last_user", current.lastUser);
    database.setAppSetting("inventory.dlc_days", std::to_string(current.dlcAlertDays));
    database.setAppSetting("inventory.dead_stock_months", std::to_string(current.deadStockMonths));
    database.setAppSetting("ui.startup_page", std::to_string(current.startupPage));
    database.setAppSetting("company.name", current.companyName);
    database.setAppSetting("company.address", current.companyAddress);
    database.setAppSetting("company.phone", current.companyPhone);
    database.setAppSetting("company.email", current.companyEmail);
    database.setAppSetting("company.tax_id", current.companyTaxId);
    database.setAppSetting("company.footer", current.documentFooter);
    database.setAppSetting("sales.invoice_prefix", current.invoicePrefix);
    database.setAppSetting("sales.proforma_prefix", current.proformaPrefix);
    database.setAppSetting("sales.delivery_prefix", current.deliveryPrefix);
    database.setAppSetting("sales.counter_prefix", current.counterPrefix);
    database.setAppSetting("sales.return_prefix", current.returnPrefix);
    database.setAppSetting("stock.bmm_prefix", current.bmmPrefix);
    database.setAppSetting("sales.tax_rate", std::to_string(current.defaultTaxRate));
    database.setAppSetting("sales.prices_include_tax", current.pricesIncludeTax ? "1" : "0");
    database.setAppSetting("labels.width_mm", std::to_string(current.labelWidthMm));
    database.setAppSetting("labels.height_mm", std::to_string(current.labelHeightMm));
    database.setAppSetting("labels.show_price", current.labelShowPrice ? "1" : "0");
    database.setAppSetting("labels.show_name", current.labelShowName ? "1" : "0");
    saveColors(database, current.colors);
    setLanguage(current.language);
}

std::string themeModeKey(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return "light";
        case ThemeMode::Dark: return "dark";
        default: return "system";
    }
}

ThemeMode themeModeFromKey(const std::string& value) {
    if (value == "light") return ThemeMode::Light;
    if (value == "dark") return ThemeMode::Dark;
    if (value == "system") return ThemeMode::System;
    return ThemeMode::Dark;
}

std::string accentKey(AccentColor accent) {
    switch (accent) {
        case AccentColor::Blue: return "blue";
        case AccentColor::Violet: return "violet";
        case AccentColor::Amber: return "amber";
        case AccentColor::Rose: return "rose";
        default: return "teal";
    }
}

AccentColor accentFromKey(const std::string& value) {
    if (value == "blue") return AccentColor::Blue;
    if (value == "violet") return AccentColor::Violet;
    if (value == "amber") return AccentColor::Amber;
    if (value == "rose") return AccentColor::Rose;
    return AccentColor::Teal;
}

std::string languageKey(Language language) {
    switch (language) {
        case Language::English: return "en";
        case Language::Spanish: return "es";
        case Language::German: return "de";
        default: return "fr";
    }
}

Language languageFromKey(const std::string& value) {
    if (value == "en") return Language::English;
    if (value == "es") return Language::Spanish;
    if (value == "de") return Language::German;
    return Language::French;
}
