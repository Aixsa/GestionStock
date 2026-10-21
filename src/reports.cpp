#include "reports.hpp"
#include "i18n.hpp"
#include "path_utils.hpp"
#include "preferences.hpp"
#include "barcode.hpp"

#include <glib.h>
#include <gio/gio.h>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace {
std::string fileTimestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    static std::atomic<unsigned int> sequence{};
    const auto micros=std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()%1000000;
    std::ostringstream out;out<<std::put_time(&tm,"%Y%m%d-%H%M%S")<<'-'<<std::setfill('0')<<std::setw(6)<<micros<<'-'<<std::setw(3)<<sequence.fetch_add(1,std::memory_order_relaxed);return out.str();
}

std::string displayTimestamp() {
    const auto now=std::chrono::system_clock::now();const auto t=std::chrono::system_clock::to_time_t(now);std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm,&t);
#else
    localtime_r(&t,&tm);
#endif
    std::ostringstream out;
    if(preferences().dateFormat=="iso")out<<std::put_time(&tm,"%Y-%m-%d %H:%M");
    else out<<std::put_time(&tm,"%d/%m/%Y")<<(isEnglish()?" at ":" à ")<<std::put_time(&tm,"%H:%M");
    return out.str();
}

std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    return value;
}

bool containsAny(const std::string& value, std::initializer_list<std::string_view> needles) {
    const auto haystack=lower(value);
    for(const auto needle:needles)if(haystack.find(needle)!=std::string::npos)return true;
    return false;
}

bool numericHeader(const std::string& header) {
    return containsAny(header,{"qté","quantit","coût","cout","valeur","total","montant","stock","seuil","couverture","délai","delai","demande","prix","budget","écart","ecart","compté","compte","poids","volume","m³","%","qty","quantity","cost","value","amount","level","coverage","lead","price","budget","variance","counted"});
}

std::string localizedValue(const std::string& value) {
    static const std::vector<std::string> known={
        "CONFORME","BLOQUÉ","À contrôler","PÉRIMÉ",
        "Entrée","Sortie · FIFO","Correction +","Correction −","Déclassement / destruction","Retour / récupération","Transfert sortant","Transfert entrant",
        "Brouillon","Envoyée","Réception partielle","Reçue","Annulée",
        "À préparer","Livraison partielle","Livrée"
    };
    if(std::find(known.begin(),known.end(),value)!=known.end())return tr(value);
    return value;
}

std::string badgeClass(const std::string& header,const std::string& value) {
    const auto head=lower(header),cell=lower(value);
    if(head.find("classe")!=std::string::npos||head.find("class")!=std::string::npos){
        if(cell.find("a")!=std::string::npos&&cell.size()<=3)return "badge badge-a";
        if(cell.find("b")!=std::string::npos&&cell.size()<=3)return "badge badge-b";
        if(cell.find("c")!=std::string::npos&&cell.size()<=3)return "badge badge-c";
    }
    if(containsAny(header,{"statut","qualité","qualite","état","etat","status","quality"})){
        if(containsAny(value,{"conforme","compliant","reçu","recue","livrée","livree","ok"}))return "badge badge-ok";
        if(containsAny(value,{"bloqué","bloque","blocked","périmé","perime","expired","annulée","annulee","insuffisant"}))return "badge badge-danger";
        if(containsAny(value,{"contrôler","controler","attente","pending","partielle","partial","préparer","preparer","brouillon","draft"}))return "badge badge-warn";
    }
    return {};
}

std::string groupInteger(std::string digits,bool english) {
    std::string grouped;
    const std::size_t first=digits.size()%3==0?3:digits.size()%3;
    grouped.append(digits,0,first);
    for(std::size_t i=first;i<digits.size();i+=3){
        grouped += english ? ',' : ' ';
        grouped.append(digits,i,3);
    }
    return grouped;
}
}

std::string htmlEscape(const std::string& value) {
    std::string out; out.reserve(value.size());
    for (const char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string csvEscape(const std::string& value) {
    std::string safe=value;
    const auto first=safe.find_first_not_of(" \t\r\n");
    if(first!=std::string::npos){const char c=safe[first];const bool formula=c=='='||c=='+'||c=='@'||(c=='-'&&(first+1==safe.size()||(!std::isdigit(static_cast<unsigned char>(safe[first+1]))&&safe[first+1]!='.')));if(formula)safe.insert(0,1,'\'');}
    std::string out="\"";
    for (const char c : safe) { if (c=='"') out += "\"\""; else out += c; }
    out += '"'; return out;
}

std::filesystem::path exportsDirectory() {
    const char* documents = g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS);
    auto base = documents && *documents ? pathFromUtf8(documents) : std::filesystem::temp_directory_path();
    auto folder = base / "Gestion des stocks" / "Exports";
    std::filesystem::create_directories(folder);
#ifndef _WIN32
    std::filesystem::permissions(folder,std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
#endif
    return folder;
}

std::filesystem::path makePrintableReport(const ReportDocument& input) {
    ReportDocument document=input;
    const auto stamp=fileTimestamp();
    const auto created=displayTimestamp();
    const auto path=exportsDirectory()/("document-"+stamp+".html");

    if(document.rows.size()>1000000)throw std::runtime_error("Le document est trop volumineux pour être imprimé.");
    for(auto& row:document.rows){
        if(row.size()<document.headers.size())row.resize(document.headers.size());
        else if(row.size()>document.headers.size())row.resize(document.headers.size());
    }
    const bool hasNumberColumn=std::any_of(document.headers.begin(),document.headers.end(),[](const std::string& header){return header=="N°"||header=="#";});
    if(!hasNumberColumn&&!document.headers.empty()){
        document.headers.insert(document.headers.begin(),"N°");
        for(std::size_t i=0;i<document.rows.size();++i)document.rows[i].insert(document.rows[i].begin(),std::to_string(i+1));
    }

    std::string paper=document.pageSize;
    if(paper.empty()||paper=="auto")paper=preferences().paperSize;
    if(paper.empty()||paper=="auto")paper=document.headers.size()>13?"A3":"A4";
    std::string orientation=document.orientation;
    if(orientation.empty()||orientation=="auto")orientation=preferences().orientation;
    if(orientation.empty()||orientation=="auto")orientation=document.headers.size()>7?"landscape":"portrait";
    if(orientation!="portrait"&&orientation!="landscape")orientation="landscape";
    const std::string pageSize=paper+" "+orientation;
    const int columns=static_cast<int>(document.headers.size());
    const std::string tableFont=columns>18?"7.2px":(columns>13?"8px":(columns>9?"9px":"10px"));

    std::vector<bool> numeric(static_cast<std::size_t>(columns),false);
    for(int i=0;i<columns;++i)numeric[static_cast<std::size_t>(i)]=numericHeader(document.headers[static_cast<std::size_t>(i)]);

    std::ofstream out(path, std::ios::binary);
    if(!out)throw std::runtime_error("Impossible de créer le document imprimable dans le dossier Exports.");
    out<<"<!doctype html><html lang=\""<<(isEnglish()?"en":"fr")<<"\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\"><title>"<<htmlEscape(tr(document.title))<<"</title><style>"
       <<":root{--ink:#172b3a;--muted:#647687;--line:#dbe4ea;--soft:#f4f7f9;--brand:#0d7a68;--brand-dark:#0a5c50;--brand-soft:#e8f4f1;--warn:#a45a0b;--danger:#a63236;--paper:#ffffff}"
       <<"@page{size:"<<pageSize<<";margin:13mm 12mm 16mm}@page{@bottom-right{content:'Page ' counter(page) ' / ' counter(pages);font:8.5px 'Segoe UI',Arial;color:#71818c}}"
       <<"*{box-sizing:border-box}html{background:#e7edf1}body{font:12px 'Segoe UI','Inter',Arial,sans-serif;color:var(--ink);margin:0;background:#e7edf1}"
       <<".toolbar{max-width:1180px;margin:16px auto 0;display:flex;align-items:center;gap:10px;padding:0 6px;color:#526474;font-size:12px}.toolbar .spacer{flex:1}.toolbar button{border:1px solid #cbd7df;background:#fff;color:#20394b;border-radius:7px;padding:8px 13px;font:600 12px 'Segoe UI',Arial;cursor:pointer}.toolbar button.primary{background:#0d7a68;border-color:#0d7a68;color:#fff}.toolbar button:hover{filter:brightness(.96)}"
       <<".sheet{background:var(--paper);width:calc(100% - 32px);max-width:"<<(paper=="A3"?"1500px":"1120px")<<";margin:16px auto 34px;padding:"<<(columns>13?"18px 18px 26px":"26px 30px 34px")<<";border:1px solid #d8e2e8;border-radius:6px;box-shadow:0 18px 50px rgba(27,47,62,.13)}"
       <<".masthead{display:grid;grid-template-columns:auto 1fr auto;gap:20px;align-items:start;border-bottom:2px solid var(--brand);padding-bottom:16px;margin-bottom:16px}"
       <<".identity{display:flex;gap:11px;align-items:center;min-width:190px}.logo{width:42px;height:42px;border-radius:11px;background:linear-gradient(135deg,var(--brand),var(--brand-dark));display:flex;align-items:center;justify-content:center;color:#fff;font-weight:800;letter-spacing:.4px;font-size:14px}.brand{font-size:11px;font-weight:800;letter-spacing:.12em;text-transform:uppercase;color:var(--brand-dark);line-height:1.35}.brand-sub{font-size:10px;color:var(--muted);margin-top:2px}"
       <<".doc-title{text-align:center}.eyebrow{font-size:9px;font-weight:800;letter-spacing:.16em;text-transform:uppercase;color:var(--brand);margin-bottom:5px}h1{font-size:23px;line-height:1.18;margin:0 0 5px;color:var(--ink);letter-spacing:-.2px}.doc-title p{margin:0;color:var(--muted);font-size:11px;line-height:1.5}"
       <<".doc-meta{text-align:right;font-size:9.5px;line-height:1.65;color:var(--muted);min-width:175px}.doc-meta strong{display:block;color:var(--ink);font-size:11px}.doc-meta .ref{color:var(--brand-dark);font-family:'SFMono-Regular',Consolas,monospace;font-size:10px}.doc-meta .barcode{margin-top:7px;display:flex;justify-content:flex-end}.doc-meta .barcode svg{width:165px;height:auto;max-height:46px}"
       <<".meta-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:8px 18px;background:var(--soft);border:1px solid var(--line);border-radius:8px;padding:11px 14px;margin:0 0 14px}.meta-item{font-size:9.5px;color:var(--muted)}.meta-item strong{display:block;color:var(--ink);font-size:11px;font-weight:650;margin-top:1px}"
       <<".highlights{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:9px;margin:0 0 15px}.highlight{border:1px solid var(--line);border-top:3px solid var(--brand);border-radius:7px;padding:9px 11px;background:#fff}.highlight .label{font-size:9px;text-transform:uppercase;letter-spacing:.06em;color:var(--muted);font-weight:700}.highlight .value{font-size:17px;font-weight:800;color:var(--ink);margin-top:2px;line-height:1.2}.highlight .note{font-size:9px;color:var(--muted);margin-top:2px}"
       <<".section-title{display:flex;justify-content:space-between;align-items:baseline;margin:15px 0 7px;gap:10px}.section-title h2{font-size:12px;margin:0;text-transform:uppercase;letter-spacing:.08em;color:var(--brand-dark)}.section-title span{font-size:9.5px;color:var(--muted)}"
       <<".table-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:6px}table{border-collapse:collapse;width:100%;table-layout:auto;font-size:"<<tableFont<<"}thead{display:table-header-group}tr{break-inside:avoid;page-break-inside:avoid}th{text-align:left;background:var(--brand-dark);color:#fff;padding:8px 7px;border-right:1px solid rgba(255,255,255,.16);font-size:8.5px;font-weight:700;letter-spacing:.04em;text-transform:uppercase;vertical-align:bottom}th.num,td.num{text-align:right;font-variant-numeric:tabular-nums}td{padding:7px;border-bottom:1px solid #e7edf1;border-right:1px solid #eef2f5;vertical-align:top;overflow-wrap:anywhere;white-space:pre-line;line-height:1.35}tbody tr:nth-child(even) td{background:#f8fafb}tbody tr:hover td{background:#f1f7f5}td:last-child,th:last-child{border-right:0}.badge{display:inline-block;border-radius:999px;padding:2px 7px;font-size:8.5px;font-weight:700;white-space:nowrap}.badge-ok{background:#e7f5ef;color:#0f6b55}.badge-warn{background:#fdf3e2;color:#8d520a}.badge-danger{background:#fbeaea;color:#9d2e33}.badge-a{background:#e7f5ef;color:#0f6b55}.badge-b{background:#eaf1fb;color:#24589a}.badge-c{background:#f1f3f5;color:#546573}.empty{text-align:center;color:var(--muted);padding:22px}"
       <<".note{margin-top:13px;padding:10px 12px;border-left:3px solid var(--brand);background:var(--brand-soft);color:#3e5a57;line-height:1.55;font-size:10px;border-radius:0 5px 5px 0}.note strong{color:var(--brand-dark)}"
       <<".signatures{display:grid;grid-template-columns:repeat(3,1fr);gap:26px;margin:28px 0 6px;break-inside:avoid}.signature{border-top:1px solid #91a49c;padding-top:6px;color:#677a73;font-size:9.5px;line-height:1.5}.signature strong{display:block;color:#38514c;font-size:10px}.signature .space{height:30px}"
       <<".footer{border-top:1px solid #e2eae6;margin-top:14px;padding-top:7px;color:#84938d;font-size:8.5px;display:flex;justify-content:space-between;gap:12px}.footer .legal{text-align:right}"
       <<"@media print{html,body{background:#fff}.toolbar{display:none}.sheet{width:100%;max-width:none;margin:0;padding:0;border:0;border-radius:0;box-shadow:none}.table-wrap{overflow:visible;border-radius:0}.note,th,tbody tr:nth-child(even) td,.badge,.highlight{-webkit-print-color-adjust:exact;print-color-adjust:exact}.section-title{break-after:avoid}thead{display:table-header-group}.footer{position:fixed;bottom:0;left:0;right:0}}"
       <<"</style></head><body>"
       <<"<div class=\"toolbar no-print\"><strong>"<<htmlEscape(tr("Documents imprimables"))<<"</strong><span>"<<htmlEscape(tr(document.title))<<"</span><span class=\"spacer\"></span><button type=\"button\" onclick=\"window.print()\">"<<htmlEscape(tr("Imprimer"))<<"</button><button type=\"button\" class=\"primary\" onclick=\"window.close()\">"<<htmlEscape(tr("Fermer"))<<"</button></div>"
       <<"<main class=\"sheet\"><header class=\"masthead\"><div class=\"identity\"><div class=\"logo\">GS</div><div><div class=\"brand\">"<<htmlEscape(preferences().companyName.empty()?tr("Gestion des stocks"):preferences().companyName)<<"</div><div class=\"brand-sub\">"<<htmlEscape(tr("Gestion des stocks et logistique"))<<(!preferences().companyAddress.empty()?" · "+htmlEscape(preferences().companyAddress):"")<<(!preferences().companyPhone.empty()?" · "+htmlEscape(preferences().companyPhone):"")<<(!preferences().companyEmail.empty()?" · "+htmlEscape(preferences().companyEmail):"")<<"</div></div></div>"
       <<"<div class=\"doc-title\"><div class=\"eyebrow\">"<<htmlEscape(tr(document.category))<<"</div><h1>"<<htmlEscape(tr(document.title))<<"</h1><p>"<<htmlEscape(tr(document.subtitle))<<"</p></div>"
       <<"<div class=\"doc-meta\"><strong>"<<htmlEscape(tr("Référence"))<<"</strong><span class=\"ref\">"<<htmlEscape(document.barcodeValue.empty()?"GS-"+stamp:document.barcodeValue)<<"</span><span>"<<htmlEscape(tr("Édité le"))<<" "<<htmlEscape(created)<<"</span>"
       <<(!document.operatorName.empty()?"<span>"+htmlEscape(tr("Opérateur"))+" · "+htmlEscape(document.operatorName)+"</span>":"")
       <<"</div></header>";
    try {
        const std::string barcodeValue=document.barcodeValue.empty()?"GS-"+stamp:document.barcodeValue;
        out<<"<div class=\"doc-meta barcode\">"<<barcodeSvg(barcodeValue,1.2,36,false)<<"</div>";
    } catch(...) {}
    if(!document.details.empty()){
        out<<"<div class=\"meta-grid\">";
        for(const auto& [label,value]:document.details)out<<"<div class=\"meta-item\">"<<htmlEscape(tr(label))<<"<strong>"<<htmlEscape(value)<<"</strong></div>";
        out<<"</div>";
    }
    if(!document.highlights.empty()){
        out<<"<div class=\"highlights\">";
        for(const auto& [label,value]:document.highlights)out<<"<div class=\"highlight\"><div class=\"label\">"<<htmlEscape(tr(label))<<"</div><div class=\"value\">"<<htmlEscape(value)<<"</div></div>";
        out<<"</div>";
    }
    out<<"<div class=\"section-title\"><h2>"<<htmlEscape(tr("Détail des lignes"))<<"</h2><span>"<<document.rows.size()<<" "<<htmlEscape(tr(document.rows.size()==1?"ligne":"lignes"))<<" · "<<document.headers.size()<<" "<<htmlEscape(tr("colonnes"))<<"</span></div><div class=\"table-wrap\"><table><thead><tr>";
    for(std::size_t i=0;i<document.headers.size();++i)out<<"<th"<<(numeric[i]?" class=\"num\"":"")<<">"<<htmlEscape(tr(document.headers[i]))<<"</th>";
    out<<"</tr></thead><tbody>";
    if(document.rows.empty())out<<"<tr><td class=\"empty\" colspan=\""<<std::max<std::size_t>(1,document.headers.size())<<"\">"<<htmlEscape(tr("Aucune donnée à afficher pour cette période."))<<"</td></tr>";
    for(const auto& row:document.rows){
        out<<"<tr>";
        for(std::size_t i=0;i<document.headers.size();++i){
            const std::string value=i<row.size()?localizedValue(row[i]):"";
            const std::string badge=badgeClass(document.headers[i],row[i]);
            out<<"<td"<<(numeric[i]?" class=\"num\"":"")<<">";
            if(!badge.empty())out<<"<span class=\""<<badge<<"\">"<<htmlEscape(value)<<"</span>";
            else out<<htmlEscape(value);
            out<<"</td>";
        }
        out<<"</tr>";
    }
    out<<"</tbody></table></div>";
    if(!document.footnote.empty())out<<"<div class=\"note\"><strong>"<<htmlEscape(tr("Note"))<<" :</strong> "<<htmlEscape(tr(document.footnote))<<"</div>";
    if(document.signatures&&preferences().signatures){
        out<<"<div class=\"signatures\"><div class=\"signature\"><strong>"<<htmlEscape(tr("Préparé par"))<<"</strong><div class=\"space\"></div>"<<htmlEscape(tr("Nom et signature"))<<"</div><div class=\"signature\"><strong>"<<htmlEscape(tr("Vérifié par"))<<"</strong><div class=\"space\"></div>"<<htmlEscape(tr("Nom et signature"))<<"</div><div class=\"signature\"><strong>"<<htmlEscape(tr("Date de contrôle"))<<"</strong><div class=\"space\"></div>"<<htmlEscape(tr("Date et cachet"))<<"</div></div>";
    }
    out<<"<footer class=\"footer\"><span>"<<htmlEscape(preferences().companyName.empty()?tr("Gestion des stocks"):preferences().companyName)<<(!preferences().companyTaxId.empty()?" · "+htmlEscape(preferences().companyTaxId):"")<<(!preferences().documentFooter.empty()?" · "+htmlEscape(preferences().documentFooter):"")<<"</span><span class=\"legal\">"<<htmlEscape(tr("Document généré localement"))<<" · "<<htmlEscape(created)<<" · "<<htmlEscape(tr("Données locales · aucune transmission externe"))<<"</span></footer></main></body></html>";
    out.close();
    if(!out)throw std::runtime_error("Le document imprimable n'a pas pu être écrit complètement.");
#ifndef _WIN32
    std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
#endif
    return path;
}

std::filesystem::path makePrintableReport(const std::string& title, const std::string& subtitle,
                                          const std::vector<std::string>& headers,
                                          const std::vector<std::vector<std::string>>& rows,
                                          const std::string& footnote) {
    ReportDocument document;
    document.title=title;
    document.subtitle=subtitle;
    document.headers=headers;
    document.rows=rows;
    document.footnote=footnote;
    return makePrintableReport(document);
}

std::filesystem::path exportCsv(const std::string& fileName,
                                const std::vector<std::string>& headers,
                                const std::vector<std::vector<std::string>>& rows) {
    const auto path=exportsDirectory()/(fileName+"-"+fileTimestamp()+".csv");
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Impossible de créer le fichier CSV dans le dossier Exports.");
    out << "\xEF\xBB\xBF";
    for(std::size_t i=0;i<headers.size();++i) { if(i)out<<';';out<<csvEscape(headers[i]); } out<<"\r\n";
    for(const auto& row:rows) { for(std::size_t i=0;i<headers.size();++i){if(i)out<<';';out<<csvEscape(i<row.size()?row[i]:"");}out<<"\r\n"; }
    out.close();
    if(!out)throw std::runtime_error("Le fichier CSV n'a pas pu être écrit complètement.");
#ifndef _WIN32
    std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
#endif
    return path;
}

bool openLocalDocument(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto result=reinterpret_cast<std::intptr_t>(ShellExecuteW(nullptr,L"open",path.c_str(),nullptr,nullptr,SW_SHOWNORMAL));
    return result>32;
#else
    const auto filename=pathToUtf8(path);gchar* uri=g_filename_to_uri(filename.c_str(),nullptr,nullptr);
    if(!uri)return false;
    GError* error=nullptr;
    const gboolean ok=g_app_info_launch_default_for_uri(uri,nullptr,&error);
    g_free(uri);
    if(error)g_error_free(error);
    return ok;
#endif
}

std::string formatNumber(double value,int decimals) {
    if(!std::isfinite(value))return "—";
    const bool negative=value<0;
    if(negative)value=-value;
    decimals=std::clamp(decimals,0,6);
    std::ostringstream raw;
    raw.imbue(std::locale::classic());
    raw<<std::fixed<<std::setprecision(decimals)<<value;
    std::string text=raw.str();
    const auto dot=text.find('.');
    std::string integer=dot==std::string::npos?text:text.substr(0,dot);
    const std::string fraction=dot==std::string::npos?"":text.substr(dot+1);
    if(integer.empty())integer="0";
    const std::string grouped=groupInteger(integer,isEnglish());
    std::string result=(negative?"−":"")+grouped;
    if(decimals>0)result+=(isEnglish()?".":",")+fraction;
    return result;
}

std::string formatMoney(double value) {
    const std::string currency=preferences().currency.empty()?"DA":preferences().currency;
    const std::string amount=formatNumber(value,2);
    return isEnglish()?currency+" "+amount:amount+" "+currency;
}

std::filesystem::path makeBarcodeLabels(const std::vector<BarcodeLabel>& labels) {
    const auto stamp=fileTimestamp();
    const auto path=exportsDirectory()/("etiquettes-"+stamp+".html");
    const int labelWidth=std::clamp(preferences().labelWidthMm,20,150);
    const int labelHeight=std::clamp(preferences().labelHeightMm,12,120);
    std::ofstream out(path,std::ios::binary);
    if(!out)throw std::runtime_error("Impossible de créer la planche d'étiquettes dans le dossier Exports.");
    out<<"<!doctype html><html lang=\""<<(isEnglish()?"en":"fr")<<"\"><head><meta charset=\"utf-8\"><title>"<<htmlEscape(tr("Étiquettes articles"))<<"</title><style>"
       <<"@page{size:A4 portrait;margin:8mm}*{box-sizing:border-box}html,body{margin:0;background:#eef2f4;font-family:'Segoe UI',Arial,sans-serif;color:#172b3a}"
       <<".toolbar{max-width:210mm;margin:12px auto 0;display:flex;gap:10px;align-items:center;color:#526474;font-size:12px}.toolbar .spacer{flex:1}.toolbar button{border:1px solid #cbd7df;background:#fff;border-radius:6px;padding:7px 11px;font:600 12px 'Segoe UI';cursor:pointer}.toolbar button.primary{background:#0d7a68;border-color:#0d7a68;color:#fff}"
       <<".sheet{background:#fff;width:210mm;min-height:297mm;margin:12px auto 30px;padding:8mm;display:grid;grid-template-columns:repeat(auto-fill,minmax("<<labelWidth<<"mm,1fr));gap:3mm;align-content:start;box-shadow:0 14px 40px rgba(27,47,62,.14)}"
       <<".label{width:"<<labelWidth<<"mm;height:"<<labelHeight<<"mm;border:1px dashed #d5dfe5;border-radius:2mm;padding:2mm;display:flex;flex-direction:column;align-items:center;justify-content:space-between;overflow:hidden;break-inside:avoid;page-break-inside:avoid}"
       <<".label .name{font-size:8.5px;font-weight:700;text-align:center;line-height:1.2;max-height:2.4em;overflow:hidden;width:100%}.label .sku{font:7px monospace;color:#5f7180;letter-spacing:.04em}.label .code{width:100%;display:flex;justify-content:center;min-height:12mm}.label .code svg{width:96%;height:12mm}.label .price{font-size:8px;font-weight:700;color:#0a5c50}"
       <<"@media print{html,body{background:#fff}.toolbar{display:none}.sheet{margin:0;box-shadow:none;width:auto;padding:0;background:#fff}.label{border:1px solid #d5dfe5}}</style></head><body>"
       <<"<div class=\"toolbar no-print\"><strong>"<<htmlEscape(tr("Étiquettes articles"))<<"</strong><span>"<<labels.size()<<" "<<htmlEscape(tr("étiquettes"))<<"</span><span class=\"spacer\"></span><button type=\"button\" onclick=\"window.print()\">"<<htmlEscape(tr("Imprimer"))<<"</button><button type=\"button\" class=\"primary\" onclick=\"window.close()\">"<<htmlEscape(tr("Fermer"))<<"</button></div><main class=\"sheet\">";
    for(const auto& label:labels){
        std::string svg;
        try{svg=barcodeSvg(label.barcode.empty()?label.sku:label.barcode,1.0,30,false);}catch(...){svg.clear();}
        out<<"<div class=\"label\">";
        if(preferences().labelShowName)out<<"<div class=\"name\">"<<htmlEscape(label.name)<<"</div>";
        out<<"<div class=\"sku\">"<<htmlEscape(label.sku)<<"</div><div class=\"code\">"<<svg<<"</div>";
        if(preferences().labelShowPrice)out<<"<div class=\"price\">"<<htmlEscape(formatMoney(label.price))<<"</div>";
        out<<"</div>";
    }
    out<<"</main></body></html>";
    out.close();
    if(!out)throw std::runtime_error("La planche d'étiquettes n'a pas pu être écrite complètement.");
#ifndef _WIN32
    std::filesystem::permissions(path,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
#endif
    return path;
}
