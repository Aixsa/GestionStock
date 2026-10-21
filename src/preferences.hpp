#pragma once

#include "i18n.hpp"

#include <string>

class Database;

enum class ThemeMode { System = 0, Light = 1, Dark = 2 };
enum class AccentColor { Teal = 0, Blue = 1, Violet = 2, Amber = 3, Rose = 4 };

struct ColorOverrides {
    std::string background;
    std::string surface;
    std::string surfaceAlt;
    std::string text;
    std::string textStrong;
    std::string muted;
    std::string border;
    std::string accent;
    std::string sidebarTop;
    std::string sidebarBottom;
    std::string danger;
    bool empty() const;
    void clear();
};

struct Preferences {
    ThemeMode theme{ThemeMode::Dark};
    AccentColor accent{AccentColor::Teal};
    Language language{Language::French};
    bool compact{false};
    int fontScale{100};
    int pageSize{50};
    std::string dateFormat{"fr"};
    std::string currency{"DA"};
    std::string paperSize{"auto"};
    std::string orientation{"auto"};
    bool signatures{true};
    bool autoOpenDocuments{true};
    bool rememberLastUser{true};
    bool confirmDestructive{true};
    bool lowStockAlerts{true};
    std::string lastUser;
    int dlcAlertDays{30};
    int deadStockMonths{6};
    int startupPage{0};
    std::string companyName{"Gestion des stocks"};
    std::string companyAddress;
    std::string companyPhone;
    std::string companyEmail;
    std::string companyTaxId;
    std::string documentFooter;
    std::string invoicePrefix{"FAC"};
    std::string proformaPrefix{"PRO"};
    std::string deliveryPrefix{"BL"};
    std::string counterPrefix{"CPT"};
    std::string returnPrefix{"RET"};
    std::string bmmPrefix{"BMM"};
    double defaultTaxRate{19.0};
    bool pricesIncludeTax{false};
    int labelWidthMm{50};
    int labelHeightMm{30};
    bool labelShowPrice{true};
    bool labelShowName{true};
    ColorOverrides colors;
};

Preferences& preferences();

void loadPreferences(Database& database);
void savePreferences(Database& database);

std::string themeModeKey(ThemeMode mode);
ThemeMode themeModeFromKey(const std::string& value);
std::string accentKey(AccentColor accent);
AccentColor accentFromKey(const std::string& value);
std::string languageKey(Language language);
Language languageFromKey(const std::string& value);
