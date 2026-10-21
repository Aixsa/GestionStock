#pragma once

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

std::string htmlEscape(const std::string& value);
std::string csvEscape(const std::string& value);

struct ReportDocument {
    std::string title;
    std::string subtitle;
    std::string category{"DOCUMENT INTERNE"};
    std::vector<std::string> headers;
    std::vector<std::vector<std::string>> rows;
    std::string footnote;
    std::vector<std::pair<std::string, std::string>> highlights;
    std::vector<std::pair<std::string, std::string>> details;
    std::string operatorName;
    bool signatures{true};
    std::string pageSize{"auto"};
    std::string orientation{"auto"};
    std::string barcodeValue;
};

struct BarcodeLabel {
    std::string name;
    std::string sku;
    std::string barcode;
    double price{};
};

std::filesystem::path makePrintableReport(const ReportDocument& document);
std::filesystem::path makePrintableReport(const std::string& title, const std::string& subtitle,
                                          const std::vector<std::string>& headers,
                                          const std::vector<std::vector<std::string>>& rows,
                                          const std::string& footnote = {});
std::filesystem::path makeBarcodeLabels(const std::vector<BarcodeLabel>& labels);
std::filesystem::path exportCsv(const std::string& fileName,
                                const std::vector<std::string>& headers,
                                const std::vector<std::vector<std::string>>& rows);
bool openLocalDocument(const std::filesystem::path& path);
std::string formatNumber(double value, int decimals = 2);
std::string formatMoney(double value);
std::filesystem::path exportsDirectory();
