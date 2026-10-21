#include "app.hpp"
#include "i18n.hpp"
#include "path_utils.hpp"
#include "preferences.hpp"
#include "reports.hpp"
#include "security.hpp"
#include "barcode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <functional>
#include <iomanip>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include <gio/gio.h>

namespace {
void connectClicked(GtkWidget* widget, std::function<void()> action) {
    auto* callback = new std::function<void()>(std::move(action));
    g_signal_connect_data(widget, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
        (*static_cast<std::function<void()>*>(data))();
    }), callback, +[](gpointer data, GClosure*) { delete static_cast<std::function<void()>*>(data); }, G_CONNECT_DEFAULT);
}

void connectActivate(GtkWidget* widget,std::function<void()> action) {
    auto* callback=new std::function<void()>(std::move(action));
    g_signal_connect_data(widget,"activate",G_CALLBACK(+[](GtkEntry*,gpointer data){(*static_cast<std::function<void()>*>(data))();}),callback,
        +[](gpointer data,GClosure*){delete static_cast<std::function<void()>*>(data);},G_CONNECT_DEFAULT);
}

void connectNotify(GtkWidget* widget,const char* signal,std::function<void()> action) {
    auto* callback=new std::function<void()>(std::move(action));
    g_signal_connect_data(widget,signal,G_CALLBACK(+[](GObject*,GParamSpec*,gpointer data){(*static_cast<std::function<void()>*>(data))();}),callback,
        +[](gpointer data,GClosure*){delete static_cast<std::function<void()>*>(data);},G_CONNECT_DEFAULT);
}

std::string actionTooltip(const std::string& label) {
    static const std::unordered_map<std::string,std::string> known={
        {"Se connecter","Ouvre une session avec l'identifiant et le mot de passe saisis."},
        {"Déconnexion","Ferme la session en cours et revient à l'écran de connexion."},
        {"Rechercher","Filtre la liste affichée avec le texte saisi."},
        {"Réinitialiser les filtres","Efface les filtres et réaffiche la liste complète."},
        {"Enregistrer","Valide le formulaire et enregistre les modifications."},
        {"Enregistrer le document","Valide les lignes, réserve le stock et enregistre le document."},
        {"Annuler","Annule l'opération en cours sans valider les modifications."},
        {"Fermer","Ferme cette fenêtre."},
        {"Confirmer","Confirme l'opération demandée."},
        {"Modifier","Ouvre le formulaire de modification de cet élément."},
        {"Supprimer","Demande confirmation puis supprime définitivement cet élément."},
        {"Archiver","Archive cet élément tout en conservant son historique."},
        {"Réactiver","Réactive cet élément et rétablit son accès."},
        {"Désactiver","Désactive cet élément sans supprimer son historique."},
        {"Tarifs","Ouvre la grille des prix spécifiques de ce client."},
        {"Étiquette","Génère et imprime l'étiquette code-barres de cet article."},
        {"Fiche","Ouvre la fiche détaillée avec l'historique de cet élément."},
        {"Expédier","Prépare et enregistre l'expédition de cette commande."},
        {"Bon","Génère le bon imprimable correspondant à cette opération."},
        {"Actualiser","Recharge les données depuis la base locale."},
        {"Tout","Affiche tous les éléments sans filtre de statut."},
        {"Auto","Rétablit la couleur d'accent automatique du thème."},
        {"Imprimer","Génère le document imprimable correspondant."},
        {"Exporter CSV","Exporte les données affichées dans un fichier CSV."}
    };
    std::string normalized=label;
    const auto first=normalized.find_first_not_of(" \t");
    if(first==std::string::npos)return "Exécute l'action de cette zone.";
    if(first>0)normalized.erase(0,first);
    if(const auto it=known.find(normalized);it!=known.end())return it->second;
    if(normalized.rfind("＋",0)==0)return "Ouvre le formulaire de création correspondant.";
    if(normalized.rfind("Imprimer",0)==0)return "Génère et ouvre le document imprimable.";
    if(normalized.rfind("Exporter",0)==0)return "Exporte les données affichées dans un fichier.";
    if(normalized.rfind("Ajouter",0)==0)return "Ajoute une ligne ou un élément à l'opération en cours.";
    if(normalized.rfind("Retirer",0)==0)return "Retire cette ligne de l'opération en cours.";
    if(normalized.find("Vue d'ensemble")!=std::string::npos)return "Ouvre le tableau de bord des stocks, alertes et opérations récentes.";
    if(normalized.find("Articles")!=std::string::npos)return "Ouvre le catalogue des articles, seuils, lots et emplacements.";
    if(normalized.find("Mouvements")!=std::string::npos)return "Ouvre le journal des entrées, sorties et ajustements de stock.";
    if(normalized.find("Réapprovisionnement")!=std::string::npos)return "Ouvre les commandes fournisseurs et les flux d'approvisionnement.";
    if(normalized.find("Ventes et clients")!=std::string::npos)return "Ouvre les clients, devis, ventes, factures et retours.";
    if(normalized.find("Entrepôts")!=std::string::npos)return "Ouvre les entrepôts, emplacements et capacités de stockage.";
    if(normalized.find("Fournisseurs")!=std::string::npos)return "Ouvre le répertoire des fournisseurs et les délais d'achat.";
    if(normalized.find("Rapports et méthodes")!=std::string::npos)return "Ouvre les rapports, analyses ABC, PEPS/FIFO et recommandations.";
    if(normalized.find("Utilisateurs")!=std::string::npos)return "Ouvre la gestion des comptes et des permissions.";
    if(normalized.find("Réglages")!=std::string::npos)return "Ouvre les préférences d'apparence, documents, entreprise et données.";
    return "Exécute cette action.";
}

std::string fieldTooltip(const std::string& label,GtkWidget* control) {
    static const std::unordered_map<std::string,std::string> known={
        {"Identifiant de connexion","Utilisé pour vous connecter. 64 caractères maximum."},
        {"Nom complet","Nom affiché dans l'application et sur les documents."},
        {"Nouveau mot de passe","Au moins 8 caractères. Laissez vide pour conserver le mot de passe actuel."},
        {"Confirmer le nouveau mot de passe","Saisissez à nouveau le nouveau mot de passe."},
        {"Mot de passe","Mot de passe du compte. La saisie est masquée."},
        {"Confirmer le mot de passe","Saisissez à nouveau le mot de passe. La saisie est masquée."}
    };
    if(const auto it=known.find(label);it!=known.end())return it->second;
    if(GTK_IS_SPIN_BUTTON(control))return "Saisissez une valeur numérique ou utilisez les flèches.";
    if(GTK_IS_DROP_DOWN(control))return "Choisissez une option dans la liste.";
    if(GTK_IS_CHECK_BUTTON(control))return "Activez ou désactivez cette option.";
    if(GTK_IS_SCROLLED_WINDOW(control))return "Saisissez les précisions utiles dans cette zone de texte.";
    if(GTK_IS_ENTRY(control)){
        if(!gtk_editable_get_editable(GTK_EDITABLE(control)))return "Cette valeur est en lecture seule.";
        if(!gtk_entry_get_visibility(GTK_ENTRY(control)))return "La saisie est masquée.";
        const char* placeholder=gtk_entry_get_placeholder_text(GTK_ENTRY(control));
        if(placeholder&&*placeholder)return std::string("Format attendu : ")+placeholder+".";
        return "Saisissez la valeur demandée.";
    }
    return "Renseignez la valeur demandée.";
}

std::string widgetTooltip(GtkWidget* widget) {
    if(GTK_IS_COLOR_DIALOG_BUTTON(widget))return "Choisissez une couleur personnalisée ou revenez au réglage automatique.";
    if(GTK_IS_CHECK_BUTTON(widget))return "Activez ou désactivez cette option.";
    if(GTK_IS_BUTTON(widget)){
        const char* label=gtk_button_get_label(GTK_BUTTON(widget));
        if(label&&*label)return actionTooltip(label);
        return "Exécute l'action de cette zone.";
    }
    if(GTK_IS_ENTRY(widget)){
        if(!gtk_editable_get_editable(GTK_EDITABLE(widget)))return "Cette valeur est en lecture seule.";
        if(!gtk_entry_get_visibility(GTK_ENTRY(widget)))return "La saisie est masquée.";
        const char* placeholder=gtk_entry_get_placeholder_text(GTK_ENTRY(widget));
        return placeholder&&*placeholder?std::string("Format attendu : ")+placeholder+".":"Saisissez la valeur demandée.";
    }
    if(GTK_IS_SPIN_BUTTON(widget))return "Saisissez un nombre ou utilisez les flèches.";
    if(GTK_IS_DROP_DOWN(widget))return "Choisissez une option dans la liste.";
    if(GTK_IS_SWITCH(widget))return "Activez ou désactivez cette option.";
    if(GTK_IS_TEXT_VIEW(widget))return "Cliquez pour modifier le contenu de cette zone de texte.";
    if(GTK_IS_SCROLLED_WINDOW(widget))return "Utilisez la molette ou la barre de défilement pour parcourir le contenu.";
    return {};
}

void applyTooltips(GtkWidget* widget) {
    if(!widget)return;
    if(gtk_widget_get_visible(widget)&&!gtk_widget_get_has_tooltip(widget)){
        const std::string text=widgetTooltip(widget);
        if(!text.empty())gtk_widget_set_tooltip_text(widget,text.c_str());
    }
    for(GtkWidget* child=gtk_widget_get_first_child(widget);child;child=gtk_widget_get_next_sibling(child))applyTooltips(child);
}

void idleCall(std::function<void()> action) {
    auto* callback=new std::function<void()>(std::move(action));
    g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,+[](gpointer data){
        auto* action=static_cast<std::function<void()>*>(data);
        (*action)();
        delete action;
        return G_SOURCE_REMOVE;
    },callback,nullptr);
}

GtkWidget* themedDialog(GtkWindow* parent,const std::string& title,const std::string& message,const std::string& icon,const std::string& iconCss,GtkWidget** actionsOut) {
    GtkWidget* dialog=gtk_window_new();
    gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog),parent);
    gtk_window_set_default_size(GTK_WINDOW(dialog),460,-1);
    gtk_window_set_resizable(GTK_WINDOW(dialog),FALSE);
    gtk_window_set_title(GTK_WINDOW(dialog),tr(title).c_str());
    GtkWidget* header=gtk_header_bar_new();
    gtk_widget_add_css_class(header,"app-headerbar");
    GtkWidget* headerTitle=gtk_label_new(tr(title).c_str());
    gtk_widget_add_css_class(headerTitle,"header-title");
    gtk_header_bar_set_title_widget(GTK_HEADER_BAR(header),headerTitle);
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header),TRUE);
    gtk_window_set_titlebar(GTK_WINDOW(dialog),header);
    GtkWidget* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);
    gtk_widget_add_css_class(root,"panel");
    gtk_widget_set_margin_start(root,16);gtk_widget_set_margin_end(root,16);
    gtk_widget_set_margin_top(root,16);gtk_widget_set_margin_bottom(root,16);
    GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
    GtkWidget* iconLabel=gtk_label_new(icon.c_str());
    {std::istringstream classList(iconCss);std::string cls;while(classList>>cls)gtk_widget_add_css_class(iconLabel,cls.c_str());}
    gtk_widget_set_valign(iconLabel,GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(row),iconLabel);
    GtkWidget* body=gtk_label_new(tr(message).c_str());
    gtk_widget_add_css_class(body,"muted");
    gtk_label_set_xalign(GTK_LABEL(body),0);
    gtk_label_set_wrap(GTK_LABEL(body),TRUE);
    gtk_label_set_selectable(GTK_LABEL(body),TRUE);
    gtk_widget_set_hexpand(body,TRUE);
    gtk_box_append(GTK_BOX(row),body);
    gtk_box_append(GTK_BOX(root),row);
    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    gtk_widget_set_halign(actions,GTK_ALIGN_END);
    gtk_widget_set_margin_top(actions,4);
    gtk_box_append(GTK_BOX(root),actions);
    *actionsOut=actions;
    gtk_window_set_child(GTK_WINDOW(dialog),root);
    return dialog;
}

void confirm(GtkWindow* parent,const std::string& title,const std::string& message,std::function<void()> yes) {
    GtkWidget* actions=nullptr;
    GtkWidget* dialog=themedDialog(parent,title,message,"!","pill pill-warn",&actions);
    GtkWidget* cancel=gtk_button_new_with_label(tr("Annuler").c_str());
    gtk_widget_add_css_class(cancel,"secondary-button");
    GtkWidget* accept=gtk_button_new_with_label(tr("Confirmer").c_str());
    gtk_widget_add_css_class(accept,"danger-button");
    gtk_box_append(GTK_BOX(actions),cancel);
    gtk_box_append(GTK_BOX(actions),accept);
    connectClicked(cancel,[dialog]{gtk_window_destroy(GTK_WINDOW(dialog));});
    connectClicked(accept,[dialog,yes=std::move(yes)]{gtk_window_destroy(GTK_WINDOW(dialog));yes();});
    applyTooltips(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

std::string trim(std::string value) {
    const auto first=value.find_first_not_of(" \t\n\r");
    if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\n\r");
    return value.substr(first,last-first+1);
}

std::filesystem::path unusedExportPath(const std::string& stem,const std::string& extension) {
    const auto directory=exportsDirectory();
    auto path=directory/(stem+extension);
    for(unsigned int suffix=2;std::filesystem::exists(path);++suffix)path=directory/(stem+"-"+std::to_string(suffix)+extension);
    return path;
}

std::string physicalConstraintWarning(const Database& database,const Product& product,int warehouseId) {
    std::vector<std::string> warnings;
    if(product.maxStock>0){const double physical=database.physicalStock(product.id);if(physical>product.maxStock+1e-9)warnings.push_back("le stock physique de "+product.sku+" atteint "+formatNumber(physical,2)+" "+product.unit+", au-dessus du maximum de "+formatNumber(product.maxStock,2));}
    for(const auto& warehouse:database.warehouses())if(warehouse.id==warehouseId&&warehouse.capacityM3>0&&warehouse.usedM3>warehouse.capacityM3+1e-9)warnings.push_back("l'entrepôt « "+warehouse.name+" » occupe "+formatNumber(warehouse.usedM3,3)+" m³ pour une capacité de "+formatNumber(warehouse.capacityM3,3)+" m³");
    if(warnings.empty())return {};
    std::string message="Attention : ";for(std::size_t i=0;i<warnings.size();++i){if(i)message+=" ; ";message+=warnings[i];}return message+". Vérifiez l'emplacement et le réapprovisionnement.";
}

std::string formatDate(const std::string& value) {
    const bool iso=preferences().dateFormat=="iso";
    if(value.size()>=16 && value[4]=='-' && value[7]=='-'){
        const std::string date=iso?value.substr(0,10):value.substr(8,2)+"/"+value.substr(5,2)+"/"+value.substr(0,4);
        return date+"  "+value.substr(11,5);
    }
    if(value.size()==10 && value[4]=='-')return iso?value:value.substr(8,2)+"/"+value.substr(5,2)+"/"+value.substr(0,4);
    return value.empty()?"—":value;
}

std::string csvNumber(double value) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::fixed<<std::setprecision(2)<<value;
    return out.str();
}

std::string typeLabel(const std::string& type) {
    if(type=="IN")return tr("Entrée");
    if(type=="OUT")return tr("Sortie · FIFO");
    if(type=="ADJUST_IN")return tr("Correction +");
    if(type=="ADJUST_OUT")return tr("Correction −");
    if(type=="WRITE_OFF")return tr("Déclassement / destruction");
    if(type=="RETURN")return tr("Retour / récupération");
    if(type=="TRANSFER_OUT")return tr("Transfert sortant");
    if(type=="TRANSFER_IN")return tr("Transfert entrant");
    return type;
}

std::string orderStatusLabel(const std::string& status) {
    if(status=="BROUILLON")return tr("Brouillon");
    if(status=="ENVOYÉE")return tr("Envoyée");
    if(status=="PARTIELLE")return tr("Réception partielle");
    if(status=="REÇUE")return tr("Reçue");
    if(status=="ANNULÉE")return tr("Annulée");
    return status;
}

std::string customerOrderStatusLabel(const std::string& status) {
    if(status=="OUVERTE")return tr("À préparer");
    if(status=="PARTIELLE")return tr("Livraison partielle");
    if(status=="LIVRÉE")return tr("Livrée");
    if(status=="ANNULÉE")return tr("Annulée");
    return status;
}

std::string salesTypeLabel(const std::string& type) {
    if(type=="PROFORMA")return tr("Facture proforma");
    if(type=="BL")return tr("Bon de livraison");
    if(type=="COUNTER")return tr("Vente comptoir");
    if(type=="INVOICE")return tr("Facture");
    if(type=="RETURN")return tr("Retour client");
    return type;
}

std::string salesStatusLabel(const std::string& status) {
    if(status=="BROUILLON")return tr("Brouillon");
    if(status=="VALIDÉ")return tr("Validé");
    if(status=="LIVRÉ")return tr("Livré");
    if(status=="ANNULÉ")return tr("Annulé");
    return status;
}

void addClasses(GtkWidget* widget,const std::string& classes) {
    std::istringstream input(classes);std::string value;while(input>>value)gtk_widget_add_css_class(widget,value.c_str());
}

GtkWidget* paginationBar(int page,int total,int pageSize,std::function<void(int)> changePage) {
    GtkWidget* bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_add_css_class(bar,"pagination-bar");
    const int pages=std::max(1,(total+pageSize-1)/pageSize);
    const int first=total?std::min(total,page*pageSize+1):0;const int last=std::min(total,(page+1)*pageSize);
    const std::string previousText="‹ "+tr("Précédent"),nextText=tr("Suivant")+" ›";
    GtkWidget* previous=gtk_button_new_with_label(previousText.c_str());gtk_widget_add_css_class(previous,"secondary-button");gtk_widget_set_sensitive(previous,page>0);gtk_box_append(GTK_BOX(bar),previous);
    const std::string summary=(total?std::to_string(first)+"–"+std::to_string(last):"0")+(isEnglish()?" of ":" sur ")+std::to_string(total)+" · "+(isEnglish()?"page ":"page ")+std::to_string(page+1)+" / "+std::to_string(pages);GtkWidget* label=gtk_label_new(summary.c_str());gtk_widget_add_css_class(label,"muted");gtk_widget_set_hexpand(label,TRUE);gtk_label_set_xalign(GTK_LABEL(label),0);gtk_box_append(GTK_BOX(bar),label);
    GtkWidget* next=gtk_button_new_with_label(nextText.c_str());gtk_widget_add_css_class(next,"secondary-button");gtk_widget_set_sensitive(next,page+1<pages);gtk_box_append(GTK_BOX(bar),next);
    connectClicked(previous,[changePage,page]{changePage(page-1);});connectClicked(next,[changePage,page]{changePage(page+1);});return bar;
}

class FormDialog : public std::enable_shared_from_this<FormDialog> {
public:
    FormDialog(GtkWindow* parent, const std::string& title, const std::string& description) {
        window=GTK_WINDOW(gtk_window_new());
        gtk_window_set_title(window,tr(title).c_str());
        gtk_window_set_default_size(window,700,740);
        gtk_window_set_modal(window,TRUE);
        if(parent)gtk_window_set_transient_for(window,parent);
        GtkWidget* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
        gtk_widget_set_margin_start(root,24);gtk_widget_set_margin_end(root,24);
        gtk_widget_set_margin_top(root,20);gtk_widget_set_margin_bottom(root,20);
        GtkWidget* heading=gtk_label_new(tr(title).c_str());gtk_widget_add_css_class(heading,"dialog-title");gtk_label_set_xalign(GTK_LABEL(heading),0);gtk_box_append(GTK_BOX(root),heading);
        if(!description.empty()){GtkWidget* sub=gtk_label_new(tr(description).c_str());gtk_widget_add_css_class(sub,"muted");gtk_label_set_xalign(GTK_LABEL(sub),0);gtk_label_set_wrap(GTK_LABEL(sub),TRUE);gtk_box_append(GTK_BOX(root),sub);}
        GtkWidget* scroll=gtk_scrolled_window_new();gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
        grid=GTK_GRID(gtk_grid_new());gtk_grid_set_row_spacing(grid,11);gtk_grid_set_column_spacing(grid,18);
        gtk_widget_set_margin_top(GTK_WIDGET(grid),8);gtk_widget_set_margin_bottom(GTK_WIDGET(grid),8);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),GTK_WIDGET(grid));gtk_box_append(GTK_BOX(root),scroll);
        GtkWidget* footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_halign(footer,GTK_ALIGN_END);
        cancel=gtk_button_new_with_label(tr("Annuler").c_str());gtk_widget_add_css_class(cancel,"secondary-button");
        save=gtk_button_new_with_label(tr("Enregistrer").c_str());gtk_widget_add_css_class(save,"primary-button");
        gtk_box_append(GTK_BOX(footer),cancel);gtk_box_append(GTK_BOX(footer),save);gtk_box_append(GTK_BOX(root),footer);
        gtk_window_set_child(window,root);
    }

    GtkWidget* text(const std::string& key,const std::string& label,const std::string& value={},bool password=false,const std::string& hint={}) {
        GtkWidget* entry=gtk_entry_new();gtk_editable_set_text(GTK_EDITABLE(entry),value.c_str());
        if(password)gtk_entry_set_visibility(GTK_ENTRY(entry),FALSE);
        if(!hint.empty())gtk_entry_set_placeholder_text(GTK_ENTRY(entry),tr(hint).c_str());
        addField(key,label,entry);return entry;
    }
    GtkWidget* display(const std::string& key,const std::string& label,const std::string& value) {
        GtkWidget* entry=text(key,label,value);gtk_editable_set_editable(GTK_EDITABLE(entry),FALSE);return entry;
    }
    GtkWidget* multiline(const std::string& key,const std::string& label,const std::string& value={},int height=160) {
        GtkWidget* view=gtk_text_view_new();
        gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
        gtk_text_view_set_accepts_tab(GTK_TEXT_VIEW(view),FALSE);
        gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view),10);
        gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view),10);
        gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view),9);
        gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view),9);
        gtk_widget_add_css_class(view,"description-editor-text");
        GtkTextBuffer* buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view));
        gtk_text_buffer_set_text(buffer,value.c_str(),-1);
        GtkWidget* scroll=gtk_scrolled_window_new();
        gtk_widget_add_css_class(scroll,"description-editor");
        gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),height);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),view);
        addField(key,label,scroll);
        textBuffers[key]=buffer;
        return scroll;
    }
    GtkWidget* displayMultiline(const std::string& key,const std::string& label,const std::string& value) {
        GtkWidget* view=gtk_text_view_new();gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(view),FALSE);
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),value.c_str(),-1);
        GtkWidget* scroll=gtk_scrolled_window_new();gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),100);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),view);addField(key,label,scroll);return scroll;
    }
    GtkWidget* number(const std::string& key,const std::string& label,double value,double min,double max,double step,int digits=2) {
        GtkWidget* spin=gtk_spin_button_new_with_range(min,max,step);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin),static_cast<guint>(digits));gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),value);
        addField(key,label,spin);return spin;
    }
    GtkWidget* toggle(const std::string& key,const std::string& label,bool value) {
        GtkWidget* check=gtk_check_button_new_with_label(tr(label).c_str());gtk_check_button_set_active(GTK_CHECK_BUTTON(check),value);gtk_widget_set_hexpand(check,TRUE);
        gtk_widget_set_tooltip_text(check,"Activez ou désactivez cette option.");
        gtk_accessible_update_property(GTK_ACCESSIBLE(check),GTK_ACCESSIBLE_PROPERTY_LABEL,tr(label).c_str(),-1);
        gtk_grid_attach(grid,check,1,row,1,1);fields[key]=check;++row;return check;
    }
    GtkWidget* choose(const std::string& key,const std::string& label,const std::vector<std::string>& choices,int selected=0,bool translateItems=false) {
        GtkStringList* strings=gtk_string_list_new(nullptr);
        for(const auto& item:choices)gtk_string_list_append(strings,translateItems?tr(item).c_str():item.c_str());
        GtkWidget* dropdown=gtk_drop_down_new(G_LIST_MODEL(strings),nullptr);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),choices.empty()?GTK_INVALID_LIST_POSITION:static_cast<guint>(std::clamp(selected,0,static_cast<int>(choices.size())-1)));
        addField(key,label,dropdown);return dropdown;
    }
    std::string getText(const std::string& key) const {
        if(const auto it=textBuffers.find(key);it!=textBuffers.end()){
            GtkTextIter start,end;gtk_text_buffer_get_bounds(it->second,&start,&end);
            gchar* value=gtk_text_buffer_get_text(it->second,&start,&end,FALSE);
            std::string result=value?value:"";g_free(value);return result;
        }
        return gtk_editable_get_text(GTK_EDITABLE(fields.at(key)));
    }
    double getNumber(const std::string& key) const { return gtk_spin_button_get_value(GTK_SPIN_BUTTON(fields.at(key))); }
    bool getToggle(const std::string& key) const { return gtk_check_button_get_active(GTK_CHECK_BUTTON(fields.at(key))); }
    int getIndex(const std::string& key) const { const guint value=gtk_drop_down_get_selected(GTK_DROP_DOWN(fields.at(key)));return value==GTK_INVALID_LIST_POSITION?-1:static_cast<int>(value); }
    void setIndex(const std::string& key,int value) { gtk_drop_down_set_selected(GTK_DROP_DOWN(fields.at(key)),value<0?GTK_INVALID_LIST_POSITION:static_cast<guint>(value)); }
    void setNumber(const std::string& key,double value) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(fields.at(key)),value); }
    std::string getChoice(const std::string& key) const { auto* model=gtk_drop_down_get_model(GTK_DROP_DOWN(fields.at(key)));const guint index=gtk_drop_down_get_selected(GTK_DROP_DOWN(fields.at(key)));if(!model||index==GTK_INVALID_LIST_POSITION)return {};auto* item=GTK_STRING_OBJECT(g_list_model_get_item(model,index));if(!item)return {};std::string value=gtk_string_object_get_string(item);g_object_unref(item);return value; }
    void setVisible(const std::string& key,bool visible) { GtkWidget* control=fields.at(key);int left=0,top=0,width=0,height=0;gtk_grid_query_child(grid,control,&left,&top,&width,&height);gtk_widget_set_visible(control,visible);if(GtkWidget* caption=gtk_grid_get_child_at(grid,0,top))gtk_widget_set_visible(caption,visible); }
    void setLabel(const std::string& key,const std::string& value) { GtkWidget* control=fields.at(key);int left=0,top=0,width=0,height=0;gtk_grid_query_child(grid,control,&left,&top,&width,&height);if(GtkWidget* caption=gtk_grid_get_child_at(grid,0,top))gtk_label_set_text(GTK_LABEL(caption),value.c_str()); }
    void setPlaceholder(const std::string& key,const std::string& value) { gtk_entry_set_placeholder_text(GTK_ENTRY(fields.at(key)),value.c_str()); }
    void present() { applyTooltips(GTK_WIDGET(window));gtk_window_present(window); }

    GtkWindow* window{};
    GtkWidget* save{};
    GtkWidget* cancel{};
private:
    void addField(const std::string& key,const std::string& label,GtkWidget* control) {
        GtkWidget* caption=gtk_label_new(tr(label).c_str());gtk_widget_add_css_class(caption,"field-label");gtk_label_set_xalign(GTK_LABEL(caption),0.0f);
        gtk_widget_set_tooltip_text(control,fieldTooltip(label,control).c_str());
        gtk_accessible_update_property(GTK_ACCESSIBLE(control),GTK_ACCESSIBLE_PROPERTY_LABEL,tr(label).c_str(),-1);
        gtk_grid_attach(grid,caption,0,row,1,1);gtk_grid_attach(grid,control,1,row,1,1);gtk_widget_set_hexpand(control,TRUE);fields[key]=control;++row;
    }
    GtkGrid* grid{};
    int row{};
    std::unordered_map<std::string,GtkWidget*> fields;
    std::unordered_map<std::string,GtkTextBuffer*> textBuffers;
};

void finishForm(const std::shared_ptr<FormDialog>& form) {
    gtk_window_destroy(form->window);
}

struct AccentPalette { std::string primary, hover, soft, softText, onAccent; };

AccentPalette accentPalette(AccentColor accent, bool dark) {
    switch (accent) {
        case AccentColor::Blue:
            return dark ? AccentPalette{"#4d8cf5","#6ca2ff","#172b47","#9dc2ff","#ffffff"} : AccentPalette{"#2563c8","#1d50a8","#e8f0fb","#1a4b9c","#ffffff"};
        case AccentColor::Violet:
            return dark ? AccentPalette{"#9d6bff","#b48cff","#2a2145","#d3c2ff","#ffffff"} : AccentPalette{"#7c3aed","#6429cc","#f0e9fe","#5b21b6","#ffffff"};
        case AccentColor::Amber:
            return dark ? AccentPalette{"#e8a63a","#f2bc61","#3a2a10","#f6d392","#2b1b00"} : AccentPalette{"#c97a0a","#a86208","#fdf3e2","#8a4f07","#ffffff"};
        case AccentColor::Rose:
            return dark ? AccentPalette{"#f06393","#ff86ad","#3d1727","#ffb3cc","#ffffff"} : AccentPalette{"#d6336c","#b02a58","#fdeaf1","#a11a4a","#ffffff"};
        default:
            return dark ? AccentPalette{"#2ec4a0","#4ed8b7","#123a33","#7fe3c7","#06251d"} : AccentPalette{"#0f9d7a","#0b7f63","#e6f6f1","#0a6b55","#ffffff"};
    }
}

bool systemThemeIsDark() {
    GtkSettings* settings=gtk_settings_get_default();
    if(settings){
        gchar* theme=nullptr;
        g_object_get(settings,"gtk-theme-name",&theme,nullptr);
        const std::string name=theme?theme:"";
        g_free(theme);
        std::string lowered=name;
        std::transform(lowered.begin(),lowered.end(),lowered.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(lowered.find("dark")!=std::string::npos)return true;
    }
    GSettingsSchemaSource* source=g_settings_schema_source_get_default();
    if(source&&g_settings_schema_source_lookup(source,"org.gnome.desktop.interface",TRUE)){
        if(GSettings* interface=g_settings_new("org.gnome.desktop.interface")){
            gchar* scheme=g_settings_get_string(interface,"color-scheme");
            const bool dark=scheme&&std::string(scheme)=="prefer-dark";
            g_free(scheme);
            g_object_unref(interface);
            if(dark)return true;
        }
    }
    return false;
}

std::string pageTitleFor(int page) {
    static const char* titles[]={"Vue d'ensemble","Articles","Mouvements de stock","Commandes et flux logistiques","Ventes et clients","Entrepôts et emplacements","Fournisseurs","Rapports et méthodes","Utilisateurs et permissions","Réglages de l'application"};
    return page>=0&&page<10?titles[page]:"";
}
}

App::App(GtkApplication* application,const std::filesystem::path& databasePath)
    : application_(application),database_(std::make_unique<Database>(databasePath)) {
    loadPreferences(*database_);
}

void App::activate() {
    if(window_) { gtk_window_present(GTK_WINDOW(window_));return; }
    window_=gtk_application_window_new(application_);
    gtk_window_set_title(GTK_WINDOW(window_),tr("Gestion des stocks et logistique").c_str());
    gtk_window_set_default_size(GTK_WINDOW(window_),1440,920);
    gtk_widget_set_size_request(window_,1060,720);
    buildHeaderBar();
    installStyle();
    if(database_->userCount()==0)showSetup();else showLogin();
    gtk_window_present(GTK_WINDOW(window_));
}

void App::installStyle() {
    applyStyle();
}

void App::buildHeaderBar() {
    GtkWidget* header=gtk_header_bar_new();
    gtk_widget_add_css_class(header,"app-headerbar");
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header),TRUE);
    GtkWidget* brand=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);
    gtk_widget_set_margin_start(brand,8);
    GtkWidget* mark=gtk_label_new("GS");
    gtk_widget_add_css_class(mark,"brand-mark");
    gtk_widget_set_valign(mark,GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(brand),mark);
    GtkWidget* titles=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
    gtk_widget_set_valign(titles,GTK_ALIGN_CENTER);
    headerTitle_=gtk_label_new(tr("Gestion des stocks").c_str());
    gtk_widget_add_css_class(headerTitle_,"header-title");
    gtk_label_set_xalign(GTK_LABEL(headerTitle_),0);
    headerSubtitle_=gtk_label_new(tr("Stocks · logistique · données locales").c_str());
    gtk_widget_add_css_class(headerSubtitle_,"header-subtitle");
    gtk_label_set_xalign(GTK_LABEL(headerSubtitle_),0);
    gtk_box_append(GTK_BOX(titles),headerTitle_);
    gtk_box_append(GTK_BOX(titles),headerSubtitle_);
    gtk_box_append(GTK_BOX(brand),titles);
    gtk_header_bar_pack_start(GTK_HEADER_BAR(header),brand);

    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
    gtk_widget_set_valign(actions,GTK_ALIGN_CENTER);
    themeButton_=gtk_button_new_with_label("☾");
    gtk_widget_add_css_class(themeButton_,"icon-button");
    gtk_widget_set_tooltip_text(themeButton_,tr("Changer de thème").c_str());
    gtk_box_append(GTK_BOX(actions),themeButton_);
    settingsButton_=gtk_button_new_with_label("⚙");
    gtk_widget_add_css_class(settingsButton_,"icon-button");
    gtk_widget_set_tooltip_text(settingsButton_,tr("Réglages de l'application").c_str());
    gtk_widget_set_visible(settingsButton_,FALSE);
    gtk_box_append(GTK_BOX(actions),settingsButton_);
    gtk_header_bar_pack_end(GTK_HEADER_BAR(header),actions);
    gtk_window_set_titlebar(GTK_WINDOW(window_),header);

    connectClicked(themeButton_,[this]{
        const bool makeDark=!darkThemeActive();
        preferences().theme=makeDark?ThemeMode::Dark:ThemeMode::Light;
        savePreferences(*database_);
        applyStyle();
    });
    connectClicked(settingsButton_,[this]{if(session_.id)navigate(Page::Parametres);});
    if(GtkSettings* settings=gtk_settings_get_default()){
        g_signal_connect(settings,"notify::gtk-theme-name",G_CALLBACK(+[](GObject*,GParamSpec*,gpointer data){
            auto* app=static_cast<App*>(data);
            if(preferences().theme==ThemeMode::System)app->applyStyle();
        }),this);
    }
}

void App::refreshShell() {
    if(sidebarUser_)gtk_label_set_text(GTK_LABEL(sidebarUser_),session_.fullName.c_str());
    if(headerSubtitle_)gtk_label_set_text(GTK_LABEL(headerSubtitle_),tr(pageTitleFor(static_cast<int>(currentPage_))).c_str());
    if(settingsButton_)gtk_widget_set_visible(settingsButton_,session_.id!=0);
}

void App::showToast(const std::string& text,bool error) {
    showMessage(text,error);
}

bool App::darkThemeActive() const {
    if(preferences().theme==ThemeMode::Dark)return true;
    if(preferences().theme==ThemeMode::Light)return false;
    return systemThemeIsDark();
}

int App::pageSize() const {
    return std::clamp(preferences().pageSize,10,500);
}

void App::applyStyle() {
    const bool dark=darkThemeActive();
    const bool compact=preferences().compact;
    AccentPalette accent=accentPalette(preferences().accent,dark);
    std::string bg=dark?"#15171a":"#f2f3f5";
    std::string surface=dark?"#1e2126":"#ffffff";
    std::string surfaceAlt=dark?"#24282e":"#f6f7f9";
    std::string surfaceRaised=dark?"#262a30":"#ffffff";
    std::string border=dark?"#343941":"#dfe3e8";
    std::string borderStrong=dark?"#474e58":"#cbd2da";
    std::string text=dark?"#e7e9ed":"#20242a";
    std::string textStrong=dark?"#f8f9fb":"#111418";
    std::string muted=dark?"#a2a9b4":"#68717c";
    std::string faint=dark?"#7e8692":"#8b939e";
    std::string sidebarTop=dark?"#191c21":"#20242a";
    std::string sidebarBottom=dark?"#111317":"#14171b";
    std::string sidebarText="#e2e5e9";
    std::string sidebarMuted=dark?"#9aa1ac":"#a3a9b2";
    std::string sidebarHover=dark?"#2a2f36":"#2d3239";
    std::string sidebarBorder=dark?"#2e333a":"#111317";
    std::string userBlockBg=dark?"#21252b":"#2a2e35";
    std::string userBlockBorder=dark?"#343a42":"#3a4048";
    std::string headerbar=dark?"#1a1d21":"#ffffff";
    std::string tableHeader=dark?"#262b32":"#eef1f4";
    std::string tableHeaderText=dark?"#c6ccd5":"#3b4652";
    std::string rowAlt=dark?"#1b1e23":"#f8f9fb";
    std::string dangerText=dark?"#ff9a9e":"#a63236";
    std::string dangerSoft=dark?"#3a1d21":"#fbeaea";
    std::string dangerBorder=dark?"#5a2a2e":"#f2d2d2";
    std::string dangerHover=dark?"#4a2227":"#f9dede";
    std::string warnText=dark?"#f3c87a":"#8d520a";
    std::string warnSoft=dark?"#3a2a10":"#fdf3e2";
    std::string okText=dark?"#7fe3c7":"#0f6b55";
    std::string okSoft=dark?"#123a33":"#e7f5ef";
    std::string fieldLabel=dark?"#cdd3db":"#37414c";
    std::string panelTitle=dark?"#f8f9fb":"#202a34";
    std::string secondaryText=dark?"#d6dbe2":"#333c46";
    std::string secondaryHover=dark?"#2d3239":"#e7ebef";
    std::string tooltipBg=dark?"#2d323a":"#24384a";
    auto overrideColor=[&](const std::string& value,std::string& target){if(!value.empty())target=value;};
    overrideColor(preferences().colors.background,bg);
    overrideColor(preferences().colors.surface,surface);
    overrideColor(preferences().colors.surfaceAlt,surfaceAlt);
    overrideColor(preferences().colors.text,text);
    overrideColor(preferences().colors.textStrong,textStrong);
    overrideColor(preferences().colors.muted,muted);
    overrideColor(preferences().colors.border,border);
    overrideColor(preferences().colors.sidebarTop,sidebarTop);
    overrideColor(preferences().colors.sidebarBottom,sidebarBottom);
    if(!preferences().colors.danger.empty()){dangerText=preferences().colors.danger;}
    if(!preferences().colors.accent.empty()){
        accent.primary=preferences().colors.accent;
        GdkRGBA parsed{};
        if(gdk_rgba_parse(&parsed,accent.primary.c_str())){
            const double luminance=0.2126*parsed.red+0.7152*parsed.green+0.0722*parsed.blue;
            accent.onAccent=luminance>0.62?"#101214":"#ffffff";
            auto mix=[&](double amount){GdkRGBA c=parsed;c.red=std::clamp(c.red+(parsed.red>0.5?-amount:amount),0.0,1.0);c.green=std::clamp(c.green+(parsed.green>0.5?-amount:amount),0.0,1.0);c.blue=std::clamp(c.blue+(parsed.blue>0.5?-amount:amount),0.0,1.0);char* hex=gdk_rgba_to_string(&c);std::string s=hex?hex:accent.primary;g_free(hex);return s;};
            accent.hover=mix(0.12);
        }
    }
    const std::string shadow=dark?"0 8px 28px rgba(0,0,0,.32)":"0 10px 28px rgba(29,48,70,.09)";
    const double fontScale=std::clamp(preferences().fontScale,85,130)/100.0;
    const std::string baseFont=std::to_string((compact?12.0:12.8)*fontScale)+"px";
    const std::string pad=compact?"12px":"16px";
    const std::string rowPad=compact?"7px 7px":"9px 8px";
    const std::string pageTitleSize=compact?"23px":"27px";
    const std::string metricPad=compact?"13px":"16px";

    std::string css=R"CSS(
window { background:@bg@; color:@text@; font-family:"Inter","Segoe UI","Noto Sans",sans-serif; font-size:@baseFont@; }
.app-root { background:@bg@; }
headerbar, .app-headerbar { background:@headerbar@; color:@text@; border-bottom:1px solid @border@; box-shadow:0 1px 0 rgba(0,0,0,.03); min-height:46px; padding:2px 8px; }
headerbar button, .app-headerbar button { background:transparent; color:@text@; border:1px solid transparent; border-radius:8px; padding:5px 8px; box-shadow:none; }
headerbar button:hover, .app-headerbar button:hover { background:@surfaceAlt@; border-color:@border@; }
.header-title { color:@textStrong@; font-weight:750; font-size:14px; }
.header-subtitle { color:@muted@; font-size:10.5px; }
.brand-mark { color:@accent@; font-size:11px; font-weight:800; letter-spacing:1.2px; }
.brand-title { color:#ffffff; font-size:18px; font-weight:760; letter-spacing:-.25px; }
.brand-sub { color:@sidebarMuted@; font-size:10px; letter-spacing:1px; }
.sidebar { background:linear-gradient(180deg,@sidebarTop@,@sidebarBottom@); color:@sidebarText@; padding:18px 12px; border-right:1px solid @sidebarBorder@; }
.sidebar-user-block { background:@userBlockBg@; border:1px solid @userBlockBorder@; border-radius:12px; padding:10px; }
.sidebar-user { color:#ffffff; font-weight:650; }
.sidebar-meta { color:@sidebarMuted@; font-size:11px; }
.nav-section { color:@sidebarMuted@; font-size:9.5px; font-weight:750; letter-spacing:1.1px; margin:12px 10px 5px; }
.nav-button { background:transparent; color:@sidebarText@; border:1px solid transparent; border-radius:9px; padding:8px 10px; font-weight:580; box-shadow:none; }
.nav-button:hover { background:@sidebarHover@; color:#ffffff; }
.nav-button.active { background:@accent@; color:@accentOn@; border-color:@accentHover@; box-shadow:0 4px 12px rgba(0,0,0,.15); }
.workspace-eyebrow { color:@muted@; font-size:10px; font-weight:750; letter-spacing:1.1px; }
.page-title { font-size:@pageTitleSize@; font-weight:760; color:@textStrong@; letter-spacing:-.45px; }
.dialog-title { font-size:20px; font-weight:750; color:@textStrong@; }
.page-subtitle,.muted { color:@muted@; font-size:12px; }
.field-label { color:@fieldLabel@; font-weight:650; font-size:12px; }
.panel { background:@surface@; border:1px solid @border@; border-radius:14px; padding:@pad@; box-shadow:@shadow@; }
.panel-title { font-size:14px; font-weight:720; color:@panelTitle@; padding-bottom:8px; }
.metric-card { background:@surfaceRaised@; border:1px solid @border@; border-radius:14px; padding:@metricPad@; box-shadow:0 4px 14px rgba(29,48,70,.05); }
.metric-card:hover { border-color:@accent@; }
.metric-label { color:@muted@; font-size:11.5px; font-weight:620; }
.metric-value { color:@textStrong@; font-size:24px; font-weight:760; }
.metric-detail { color:@faint@; font-size:11px; }
.metric-warn .metric-value { color:@warnText@; }
.metric-danger .metric-value { color:@dangerText@; }
.primary-button { background:@accent@; color:@accentOn@; border:1px solid @accent@; border-radius:9px; padding:9px 15px; font-weight:700; }
.primary-button:hover { background:@accentHover@; border-color:@accentHover@; color:@accentOn@; }
.secondary-button { background:@surfaceAlt@; color:@secondaryText@; border:1px solid @borderStrong@; border-radius:9px; padding:8px 13px; font-weight:650; }
.secondary-button:hover { background:@secondaryHover@; color:@textStrong@; }
.icon-button { background:transparent; color:@text@; border:1px solid transparent; border-radius:8px; padding:5px 8px; font-size:15px; font-weight:700; }
.icon-button:hover { background:@surfaceAlt@; border-color:@border@; }
.danger-button { background:@dangerSoft@; color:@dangerText@; border:1px solid @dangerBorder@; border-radius:8px; padding:7px 10px; font-weight:650; }
.danger-button:hover { background:@dangerHover@; }
button:focus-visible, entry:focus-visible, spinbutton:focus-visible, textview:focus-visible, dropdown:focus-visible { outline:2px solid @accent@; outline-offset:2px; }
.table-header { color:@tableHeaderText@; background:@tableHeader@; font-size:11px; font-weight:720; padding:9px 7px; border-bottom:1px solid @borderStrong@; }
.table-cell { color:@text@; font-size:12px; padding:@rowPad@; }
.row-line { border-bottom:1px solid @border@; }
.row-alt { background:@rowAlt@; }
.pill { background:@okSoft@; color:@okText@; border-radius:999px; padding:4px 8px; font-size:10px; font-weight:700; }
.pill-warn { background:@warnSoft@; color:@warnText@; }
.pill-danger { background:@dangerSoft@; color:@dangerText@; }
entry, spinbutton, dropdown > button, button.flat { background:@surfaceAlt@; color:@text@; border:1px solid @borderStrong@; border-radius:8px; min-height:36px; }
entry:focus, spinbutton:focus, dropdown > button:focus { border-color:@accent@; }
textview, textview text { background:transparent; color:@text@; }
.description-editor { background:@surfaceAlt@; border:1px solid @borderStrong@; border-radius:10px; }
.description-editor-text { background:transparent; color:@text@; }
.login-page { background:linear-gradient(160deg,@sidebarTop@,@sidebarBottom@); }
.login-card { background:@surface@; border:1px solid @border@; border-radius:18px; padding:30px; box-shadow:0 24px 70px rgba(10,27,38,.22); }
.login-heading { font-size:26px; font-weight:750; color:@textStrong@; }
.login-card entry { min-height:38px; }
.empty-title { color:@textStrong@; font-size:15px; font-weight:700; }
.empty-icon { color:@accent@; font-size:28px; font-weight:800; }
.status-bar { color:@faint@; font-size:10px; padding:6px 16px; border-top:1px solid @border@; background:@surface@; }
.pagination-bar { background:@surfaceAlt@; border-top:1px solid @border@; padding:8px 5px 2px; }
.toast { background:@surfaceRaised@; border:1px solid @border@; border-left:5px solid @accent@; border-radius:12px; padding:13px 15px; box-shadow:0 16px 40px rgba(10,27,38,.28); }
.toast-error { border-left-color:@dangerText@; }
.toast-title { color:@textStrong@; font-size:13px; font-weight:750; }
.toast-body { color:@muted@; font-size:12px; }
.toast-icon { background:@okSoft@; color:@okText@; border-radius:999px; font-size:12px; font-weight:800; padding:3px 7px; }
.divider { background:@border@; min-height:1px; }
.settings-section { background:@surface@; border:1px solid @border@; border-radius:14px; padding:@pad@; box-shadow:@shadow@; }
.settings-title { color:@textStrong@; font-size:15px; font-weight:750; }
.settings-hint { color:@muted@; font-size:11.5px; }
.settings-row { padding:8px 0; border-bottom:1px solid @border@; }
.settings-row:last-child { border-bottom:0; }
.settings-label { color:@text@; font-weight:650; font-size:12.5px; }
.success-text { color:@okText@; font-weight:650; }
.warn-text { color:@warnText@; font-weight:650; }
.danger-text { color:@dangerText@; font-weight:650; }
.code { font-family:"SFMono-Regular",Consolas,monospace; font-size:11px; color:@muted@; }
scrollbar { background:transparent; }
scrollbar slider { background:@borderStrong@; border-radius:999px; min-width:8px; min-height:8px; }
tooltip { background:@tooltipBg@; color:#ffffff; border-radius:8px; padding:6px 8px; }
popover, popover > contents { background:@surfaceRaised@; color:@text@; border:1px solid @border@; border-radius:10px; }
checkbutton { color:@text@; }
selection { background:@accent@; color:#ffffff; }
.print-preview { background:@surfaceAlt@; border:1px dashed @borderStrong@; border-radius:10px; padding:14px; }
.swatch { min-width:22px; min-height:22px; border-radius:999px; border:2px solid #ffffff; box-shadow:0 0 0 1px @borderStrong@; }
)CSS";

    const auto replaceAll=[&css](const std::string& key,const std::string& value){
        const std::string token="@"+key+"@";
        std::size_t position=0;
        while((position=css.find(token,position))!=std::string::npos){
            css.replace(position,token.size(),value);
            position+=value.size();
        }
    };
    replaceAll("bg",bg);replaceAll("surfaceAlt",surfaceAlt);replaceAll("surfaceRaised",surfaceRaised);replaceAll("surface",surface);
    replaceAll("borderStrong",borderStrong);replaceAll("border",border);
    replaceAll("textStrong",textStrong);replaceAll("text",text);
    replaceAll("muted",muted);replaceAll("faint",faint);
    replaceAll("sidebarTop",sidebarTop);replaceAll("sidebarBottom",sidebarBottom);replaceAll("sidebarText",sidebarText);replaceAll("sidebarMuted",sidebarMuted);replaceAll("sidebarHover",sidebarHover);replaceAll("sidebarBorder",sidebarBorder);
    replaceAll("userBlockBg",userBlockBg);replaceAll("userBlockBorder",userBlockBorder);
    replaceAll("headerbar",headerbar);replaceAll("tableHeaderText",tableHeaderText);replaceAll("tableHeader",tableHeader);replaceAll("rowAlt",rowAlt);
    replaceAll("dangerText",dangerText);replaceAll("dangerSoft",dangerSoft);replaceAll("dangerBorder",dangerBorder);replaceAll("dangerHover",dangerHover);
    replaceAll("warnText",warnText);replaceAll("warnSoft",warnSoft);replaceAll("okText",okText);replaceAll("okSoft",okSoft);
    replaceAll("fieldLabel",fieldLabel);replaceAll("panelTitle",panelTitle);replaceAll("secondaryText",secondaryText);replaceAll("secondaryHover",secondaryHover);replaceAll("tooltipBg",tooltipBg);
    replaceAll("accentHover",accent.hover);replaceAll("accentOn",accent.onAccent);replaceAll("accent",accent.primary);
    replaceAll("shadow",shadow);replaceAll("baseFont",baseFont);replaceAll("rowPad",rowPad);replaceAll("pageTitleSize",pageTitleSize);replaceAll("metricPad",metricPad);replaceAll("pad",pad);

    if(styleProvider_){
        gtk_style_context_remove_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(styleProvider_));
        g_object_unref(styleProvider_);
        styleProvider_=nullptr;
    }
    styleProvider_=gtk_css_provider_new();
    gtk_css_provider_load_from_string(styleProvider_,css.c_str());
    gtk_style_context_add_provider_for_display(gdk_display_get_default(),GTK_STYLE_PROVIDER(styleProvider_),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if(themeButton_){
        gtk_button_set_label(GTK_BUTTON(themeButton_),dark?"☀":"☾");
        gtk_widget_set_tooltip_text(themeButton_,dark?tr("Thème clair").c_str():tr("Thème sombre").c_str());
    }
}

GtkWidget* App::makeLabel(const std::string& text,const std::string& css) {
    GtkWidget* label=gtk_label_new(tr(text).c_str());
    if(!css.empty())addClasses(label,css);
    gtk_label_set_xalign(GTK_LABEL(label),0.0f);gtk_label_set_wrap(GTK_LABEL(label),TRUE);return label;
}

GtkWidget* App::makeButton(const std::string& text,const std::string& css) {
    GtkWidget* button=gtk_button_new_with_label(tr(text).c_str());
    if(!css.empty())addClasses(button,css);
    gtk_widget_set_tooltip_text(button,actionTooltip(tr(text)).c_str());
    return button;
}

GtkWidget* App::makePanel(const std::string& title) {
    GtkWidget* frame=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);gtk_widget_add_css_class(frame,"panel");
    if(!title.empty())append(frame,makeLabel(title,"panel-title"));
    return frame;
}

GtkWidget* App::makeMetric(const std::string& label,const std::string& value,const std::string& detail,const std::string& tone) {
    GtkWidget* card=gtk_box_new(GTK_ORIENTATION_VERTICAL,5);gtk_widget_add_css_class(card,"metric-card");if(!tone.empty())gtk_widget_add_css_class(card,tone.c_str());
    append(card,makeLabel(label,"metric-label"));append(card,makeLabel(value,"metric-value"));append(card,makeLabel(detail,"metric-detail"));
    return card;
}

GtkWidget* App::makeTable(const std::vector<std::string>& headers,const std::vector<std::vector<std::string>>& rows) {
    GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),12);gtk_grid_set_row_spacing(GTK_GRID(grid),2);
    for(std::size_t col=0;col<headers.size();++col){GtkWidget* cell=makeLabel(headers[col],"table-header");gtk_widget_set_hexpand(cell,TRUE);gtk_grid_attach(GTK_GRID(grid),cell,static_cast<int>(col),0,1,1);}
    int row=1;
    for(const auto& values:rows){
        for(std::size_t col=0;col<headers.size();++col){GtkWidget* cell=makeLabel(col<values.size()?values[col]:"","table-cell");gtk_widget_add_css_class(cell,"row-line");if(row%2==0)gtk_widget_add_css_class(cell,"row-alt");gtk_widget_set_hexpand(cell,TRUE);gtk_grid_attach(GTK_GRID(grid),cell,static_cast<int>(col),row,1,1);}++row;
    }
    return wrapScrollable(grid,std::max(640,static_cast<int>(headers.size())*130));
}

GtkWidget* App::wrapScrollable(GtkWidget* child,int minimumWidth) {
    GtkWidget* scroll=gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),160);
    gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(scroll),520);
    gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),TRUE);
    gtk_widget_set_hexpand(scroll,TRUE);
    if(minimumWidth>0)gtk_widget_set_size_request(child,minimumWidth,-1);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),child);
    return scroll;
}

void App::append(GtkWidget* parent,GtkWidget* child){gtk_box_append(GTK_BOX(parent),child);}
void App::clear(GtkWidget* box){while(GtkWidget* child=gtk_widget_get_first_child(box))gtk_box_remove(GTK_BOX(box),child);}

void App::addSectionTitle(const std::string& title,const std::string& subtitle) {
    GtkWidget* header=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);append(header,makeLabel(title,"page-title"));if(!subtitle.empty())append(header,makeLabel(subtitle,"page-subtitle"));append(pageContainer_,header);
}

void App::showMessage(const std::string& text,bool error) {
    if(error){
        GtkWidget* actions=nullptr;
        GtkWidget* dialog=themedDialog(GTK_WINDOW(window_),"Erreur",text,"!","pill pill-danger",&actions);
        GtkWidget* ok=gtk_button_new_with_label(tr("Fermer").c_str());
        gtk_widget_add_css_class(ok,"primary-button");
        gtk_box_append(GTK_BOX(actions),ok);
        connectClicked(ok,[dialog]{gtk_window_destroy(GTK_WINDOW(dialog));});
        applyTooltips(dialog);
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }
    if(!toastLayer_){
        GtkWidget* actions=nullptr;
        GtkWidget* dialog=themedDialog(GTK_WINDOW(window_),"Information",text,"i","pill",&actions);
        GtkWidget* ok=gtk_button_new_with_label(tr("Fermer").c_str());
        gtk_widget_add_css_class(ok,"primary-button");
        gtk_box_append(GTK_BOX(actions),ok);
        connectClicked(ok,[dialog]{gtk_window_destroy(GTK_WINDOW(dialog));});
        applyTooltips(dialog);
        gtk_window_present(GTK_WINDOW(dialog));
        return;
    }
    GtkWidget* card=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,11);
    gtk_widget_add_css_class(card,"toast");
    GtkWidget* icon=gtk_label_new("✓");
    gtk_widget_add_css_class(icon,"toast-icon");
    gtk_widget_set_valign(icon,GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card),icon);
    GtkWidget* content=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_widget_set_hexpand(content,TRUE);
    GtkWidget* heading=gtk_label_new(tr("Enregistré").c_str());
    gtk_widget_add_css_class(heading,"toast-title");
    gtk_label_set_xalign(GTK_LABEL(heading),0);
    gtk_box_append(GTK_BOX(content),heading);
    GtkWidget* body=gtk_label_new(tr(text).c_str());
    gtk_widget_add_css_class(body,"toast-body");
    gtk_label_set_xalign(GTK_LABEL(body),0);
    gtk_label_set_wrap(GTK_LABEL(body),TRUE);
    gtk_label_set_max_width_chars(GTK_LABEL(body),58);
    gtk_box_append(GTK_BOX(content),body);
    gtk_box_append(GTK_BOX(card),content);
    GtkWidget* close=gtk_button_new_with_label("×");
    gtk_widget_add_css_class(close,"icon-button");
    gtk_widget_set_valign(close,GTK_ALIGN_START);
    gtk_box_append(GTK_BOX(card),close);
    GtkWidget* revealer=gtk_revealer_new();
    gtk_revealer_set_transition_type(GTK_REVEALER(revealer),GTK_REVEALER_TRANSITION_TYPE_SLIDE_LEFT);
    gtk_revealer_set_transition_duration(GTK_REVEALER(revealer),180);
    gtk_revealer_set_child(GTK_REVEALER(revealer),card);
    gtk_box_append(GTK_BOX(toastLayer_),revealer);
    gtk_revealer_set_reveal_child(GTK_REVEALER(revealer),TRUE);
    connectClicked(close,[revealer]{if(gtk_widget_get_parent(revealer))gtk_box_remove(GTK_BOX(gtk_widget_get_parent(revealer)),revealer);});
    g_object_ref(revealer);
    g_timeout_add_seconds_full(G_PRIORITY_DEFAULT,6,+[](gpointer data)->gboolean{
        GtkWidget* view=GTK_WIDGET(data);
        if(gtk_widget_get_parent(view))gtk_box_remove(GTK_BOX(gtk_widget_get_parent(view)),view);
        return G_SOURCE_REMOVE;
    },revealer,g_object_unref);
}

void App::showDocument(const std::filesystem::path& path) {
    if(!preferences().autoOpenDocuments){
        showMessage(tr("Le document a été créé ici :")+"\n"+pathToUtf8(path)+"\n\n"+tr("Ouvrez-le pour l'imprimer."));
        if(statusLabel_)gtk_label_set_text(GTK_LABEL(statusLabel_),(tr("Document prêt : ")+pathToUtf8(path.filename())).c_str());
        return;
    }
    if(!openLocalDocument(path)){
        showMessage(tr("Le document a été créé ici :")+"\n"+pathToUtf8(path)+"\n\n"+tr("Ouvrez-le pour l'imprimer."));
        return;
    }
    if(statusLabel_)gtk_label_set_text(GTK_LABEL(statusLabel_),(tr("Document prêt : ")+pathToUtf8(path.filename())).c_str());
}

void App::addEmptyState(GtkWidget* parent,const std::string& title,const std::string& detail) {
    GtkWidget* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_widget_set_margin_top(box,18);gtk_widget_set_margin_bottom(box,18);
    append(box,makeLabel(title,"empty-title"));append(box,makeLabel(detail,"muted"));append(parent,box);
}

void App::showSetup() {
    GtkWidget* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_add_css_class(outer,"login-page");gtk_widget_set_halign(outer,GTK_ALIGN_CENTER);gtk_widget_set_valign(outer,GTK_ALIGN_CENTER);gtk_widget_set_hexpand(outer,TRUE);gtk_widget_set_vexpand(outer,TRUE);
    GtkWidget* card=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);gtk_widget_add_css_class(card,"login-card");gtk_widget_set_size_request(card,440,-1);
    append(card,makeLabel("GESTION DES STOCKS · LOGISTIQUE","brand-mark"));append(card,makeLabel("Bienvenue","login-heading"));
    append(card,makeLabel("Créez votre compte administrateur pour démarrer. Vos données resteront sur cet ordinateur.","muted"));
    GtkWidget* name=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(name),tr("Nom complet").c_str());gtk_widget_set_tooltip_text(name,tr("Nom affiché de l'administrateur.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(name),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Nom complet").c_str(),-1);append(card,name);
    GtkWidget* username=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(username),tr("Identifiant").c_str());gtk_widget_set_tooltip_text(username,tr("Identifiant de connexion, 64 caractères maximum.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(username),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Identifiant").c_str(),-1);append(card,username);
    GtkWidget* password=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(password),FALSE);gtk_entry_set_placeholder_text(GTK_ENTRY(password),tr("Mot de passe (8 caractères minimum)").c_str());gtk_widget_set_tooltip_text(password,tr("Mot de passe d'au moins 8 caractères. La saisie est masquée.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(password),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Mot de passe").c_str(),-1);append(card,password);
    GtkWidget* confirm=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(confirm),FALSE);gtk_entry_set_placeholder_text(GTK_ENTRY(confirm),tr("Confirmer le mot de passe").c_str());gtk_widget_set_tooltip_text(confirm,tr("Saisissez à nouveau le mot de passe. La saisie est masquée.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(confirm),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Confirmer le mot de passe").c_str(),-1);append(card,confirm);
    GtkWidget* create=makeButton("Créer mon compte administrateur","primary-button");append(card,create);append(outer,card);toastLayer_=nullptr;gtk_window_set_child(GTK_WINDOW(window_),outer);applyTooltips(GTK_WIDGET(window_));
    if(settingsButton_)gtk_widget_set_visible(settingsButton_,FALSE);
    if(headerSubtitle_)gtk_label_set_text(GTK_LABEL(headerSubtitle_),tr("Première configuration").c_str());
    connectClicked(create,[this,name,username,password,confirm]{
        try{
            const std::string n=trim(gtk_editable_get_text(GTK_EDITABLE(name))),u=trim(gtk_editable_get_text(GTK_EDITABLE(username)));
            const std::string p=gtk_editable_get_text(GTK_EDITABLE(password)),c=gtk_editable_get_text(GTK_EDITABLE(confirm));
            if(n.empty()||u.empty())throw std::runtime_error("Indiquez votre nom et un identifiant.");
            if(!passwordMeetsMinimum(p))throw std::runtime_error("Choisissez un mot de passe valide d'au moins 8 caractères.");
            if(p!=c)throw std::runtime_error("Les mots de passe ne correspondent pas.");
            database_->createFirstAdmin(n,u,p);auto user=database_->authenticate(u,p);if(!user)throw std::runtime_error("Le compte a été créé mais la connexion a échoué.");
            session_=*user;if(preferences().rememberLastUser){preferences().lastUser=user->username;savePreferences(*database_);}
            idleCall([this]{showMain();});
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
}

void App::showLogin(const std::string& error) {
    GtkWidget* outer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_add_css_class(outer,"login-page");gtk_widget_set_halign(outer,GTK_ALIGN_CENTER);gtk_widget_set_valign(outer,GTK_ALIGN_CENTER);gtk_widget_set_hexpand(outer,TRUE);gtk_widget_set_vexpand(outer,TRUE);
    GtkWidget* card=gtk_box_new(GTK_ORIENTATION_VERTICAL,14);gtk_widget_add_css_class(card,"login-card");gtk_widget_set_size_request(card,420,-1);
    append(card,makeLabel("GESTION DES STOCKS · LOGISTIQUE","brand-mark"));append(card,makeLabel("Connexion","login-heading"));append(card,makeLabel("Connectez-vous pour accéder à votre espace de travail.","muted"));
    GtkWidget* username=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(username),tr("Identifiant").c_str());gtk_widget_set_tooltip_text(username,tr("Identifiant du compte utilisé pour vous connecter.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(username),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Identifiant").c_str(),-1);
    if(preferences().rememberLastUser&&!preferences().lastUser.empty())gtk_editable_set_text(GTK_EDITABLE(username),preferences().lastUser.c_str());
    append(card,username);
    GtkWidget* password=gtk_entry_new();gtk_entry_set_visibility(GTK_ENTRY(password),FALSE);gtk_entry_set_placeholder_text(GTK_ENTRY(password),tr("Mot de passe").c_str());gtk_widget_set_tooltip_text(password,tr("Mot de passe du compte. La saisie est masquée.").c_str());gtk_accessible_update_property(GTK_ACCESSIBLE(password),GTK_ACCESSIBLE_PROPERTY_LABEL,tr("Mot de passe").c_str(),-1);append(card,password);
    GtkWidget* errorLabel=makeLabel(error,"muted");if(!error.empty()){gtk_widget_add_css_class(errorLabel,"pill-danger");append(card,errorLabel);}
    GtkWidget* login=makeButton("Se connecter","primary-button");append(card,login);append(outer,card);toastLayer_=nullptr;gtk_window_set_child(GTK_WINDOW(window_),outer);applyTooltips(GTK_WIDGET(window_));
    if(settingsButton_)gtk_widget_set_visible(settingsButton_,FALSE);
    if(headerSubtitle_)gtk_label_set_text(GTK_LABEL(headerSubtitle_),tr("Connexion sécurisée").c_str());
    auto attempt=[this,username,password]{const auto u=trim(gtk_editable_get_text(GTK_EDITABLE(username)));const auto p=gtk_editable_get_text(GTK_EDITABLE(password));auto user=database_->authenticate(u,p);if(!user){showLogin("Identifiant ou mot de passe incorrect.");return;}session_=*user;if(preferences().rememberLastUser){preferences().lastUser=user->username;savePreferences(*database_);}idleCall([this]{showMain();});};
    connectClicked(login,attempt);
    connectActivate(password,attempt);
    if(preferences().rememberLastUser&&!preferences().lastUser.empty())gtk_widget_grab_focus(password);else gtk_widget_grab_focus(username);
}

void App::showMain() {
    GtkWidget* root=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_add_css_class(root,"app-root");
    GtkWidget* sidebar=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_widget_add_css_class(sidebar,"sidebar");gtk_widget_set_size_request(sidebar,300,-1);gtk_widget_set_hexpand(sidebar,FALSE);
    GtkWidget* brand=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_widget_set_margin_top(brand,7);gtk_widget_set_margin_bottom(brand,20);gtk_widget_set_margin_start(brand,8);
    append(brand,makeLabel("Gestion des stocks","brand-title"));append(brand,makeLabel("STOCKS  ·  LOGISTIQUE","brand-sub"));append(sidebar,brand);
    navBox_=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);navItems_.clear();
    auto addNav=[this](GtkWidget* parent,Page page,const std::string& label,bool visible){if(!visible)return;GtkWidget* button=gtk_button_new();gtk_widget_add_css_class(button,"nav-button");GtkWidget* text=gtk_label_new(tr(label).c_str());gtk_label_set_xalign(GTK_LABEL(text),0.0f);gtk_label_set_ellipsize(GTK_LABEL(text),PANGO_ELLIPSIZE_END);gtk_label_set_max_width_chars(GTK_LABEL(text),22);gtk_widget_set_hexpand(text,TRUE);gtk_button_set_child(GTK_BUTTON(button),text);gtk_widget_set_halign(button,GTK_ALIGN_FILL);append(parent,button);navItems_.push_back({page,button});connectClicked(button,[this,page]{navigate(page);});};
    append(navBox_,makeLabel("PILOTAGE","nav-section"));
    addNav(navBox_,Page::Dashboard,"▦  Vue d'ensemble",true);
    append(navBox_,makeLabel("OPÉRATIONS","nav-section"));
    addNav(navBox_,Page::Articles,"▤  Articles",canViewStock());
    addNav(navBox_,Page::Mouvements,"⇄  Mouvements",session_.canManageMovements||(session_.canPrint&&canViewStock()));
    addNav(navBox_,Page::Commandes,"↻  Réapprovisionnement",session_.canManageMovements||canViewStock());
    addNav(navBox_,Page::Ventes,"▤  Ventes et clients",canViewStock()||session_.canPrint);
    addNav(navBox_,Page::Entrepots,"⌂  Entrepôts",canViewStock()||session_.canManageWarehouses);
    addNav(navBox_,Page::Fournisseurs,"◎  Fournisseurs",canViewStock()||session_.canManageSuppliers);
    append(navBox_,makeLabel("ANALYSE ET ADMINISTRATION","nav-section"));
    addNav(navBox_,Page::Rapports,"▧  Rapports et méthodes",canViewStock());
    if(session_.admin||session_.canManageUsers)addNav(navBox_,Page::Utilisateurs,"♙  Utilisateurs",true);
    addNav(navBox_,Page::Parametres,"⚙  Réglages",true);
    append(sidebar,navBox_);
    GtkWidget* spacer=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_set_vexpand(spacer,TRUE);append(sidebar,spacer);
    GtkWidget* user=gtk_box_new(GTK_ORIENTATION_VERTICAL,5);gtk_widget_add_css_class(user,"sidebar-user-block");gtk_widget_set_margin_start(user,3);gtk_widget_set_margin_end(user,3);gtk_widget_set_margin_bottom(user,4);
    sidebarUser_=makeLabel(session_.fullName,"sidebar-user");append(user,sidebarUser_);append(user,makeLabel(session_.admin?"Administrateur":"Profil de travail","sidebar-meta"));
    GtkWidget* logoutButton=makeButton("Déconnexion","nav-button");gtk_widget_set_halign(logoutButton,GTK_ALIGN_FILL);append(user,logoutButton);connectClicked(logoutButton,[this]{logout();});append(sidebar,user);
    append(root,sidebar);

    GtkWidget* right=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_set_hexpand(right,TRUE);gtk_widget_set_vexpand(right,TRUE);
    GtkWidget* top=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_margin_start(top,21);gtk_widget_set_margin_end(top,21);gtk_widget_set_margin_top(top,16);gtk_widget_set_margin_bottom(top,10);
    GtkWidget* appLabel=makeLabel("ESPACE DE GESTION","workspace-eyebrow");gtk_widget_set_hexpand(appLabel,TRUE);append(top,appLabel);
    append(top,makeLabel(tr("Connecté : ")+session_.fullName,"muted"));append(right,top);
    GtkWidget* scroll=gtk_scrolled_window_new();gtk_widget_set_hexpand(scroll,TRUE);gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
    pageContainer_=gtk_box_new(GTK_ORIENTATION_VERTICAL,18);gtk_widget_set_margin_start(pageContainer_,20);gtk_widget_set_margin_end(pageContainer_,20);gtk_widget_set_margin_top(pageContainer_,7);gtk_widget_set_margin_bottom(pageContainer_,20);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),pageContainer_);append(right,scroll);
    statusLabel_=makeLabel(tr("Données locales · ")+pathToUtf8(database_->path()),"status-bar");gtk_label_set_ellipsize(GTK_LABEL(statusLabel_),PANGO_ELLIPSIZE_MIDDLE);append(right,statusLabel_);
    append(root,right);
    GtkWidget* overlay=gtk_overlay_new();
    gtk_overlay_set_child(GTK_OVERLAY(overlay),root);
    toastLayer_=gtk_box_new(GTK_ORIENTATION_VERTICAL,8);
    gtk_widget_add_css_class(toastLayer_,"toast-layer");
    gtk_widget_set_halign(toastLayer_,GTK_ALIGN_END);
    gtk_widget_set_valign(toastLayer_,GTK_ALIGN_START);
    gtk_widget_set_margin_top(toastLayer_,62);
    gtk_widget_set_margin_end(toastLayer_,22);
    gtk_widget_set_size_request(toastLayer_,350,-1);
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay),toastLayer_);
    gtk_window_set_child(GTK_WINDOW(window_),overlay);
    if(settingsButton_)gtk_widget_set_visible(settingsButton_,TRUE);
    refreshShell();
    const int startup=std::clamp(preferences().startupPage,0,9);
    navigate(static_cast<Page>(startup));
}

void App::logout(){session_=User{};if(settingsButton_)gtk_widget_set_visible(settingsButton_,FALSE);idleCall([this]{showLogin();});}

bool App::canViewStock() const{return session_.admin||session_.canViewStock||session_.canManageArticles||session_.canManageMovements;}

void App::navigate(Page page) {
    const bool permitted=page==Page::Dashboard||
        (page==Page::Articles&&canViewStock())||
        (page==Page::Mouvements&&(session_.canManageMovements||(session_.canPrint&&canViewStock())))||
        (page==Page::Commandes&&(session_.canManageMovements||canViewStock()))||
        (page==Page::Ventes&&(canViewStock()||session_.canPrint))||
        (page==Page::Entrepots&&(canViewStock()||session_.canManageWarehouses))||
        (page==Page::Fournisseurs&&(canViewStock()||session_.canManageSuppliers))||
        (page==Page::Rapports&&canViewStock())||
        (page==Page::Utilisateurs&&(session_.admin||session_.canManageUsers))||
        (page==Page::Parametres&&session_.id!=0);
    if(!permitted){showMessage("Votre profil ne dispose pas de l'autorisation nécessaire.",true);return;}
    currentPage_=page;clear(pageContainer_);
    for(const auto& item:navItems_){if(item.page==page)gtk_widget_add_css_class(item.button,"active");else gtk_widget_remove_css_class(item.button,"active");}
    if(headerSubtitle_)gtk_label_set_text(GTK_LABEL(headerSubtitle_),tr(pageTitleFor(static_cast<int>(page))).c_str());
    switch(page){case Page::Dashboard:buildDashboard();break;case Page::Articles:buildProducts();break;case Page::Mouvements:buildMovements();break;case Page::Commandes:buildOrders();break;case Page::Ventes:buildSales();break;case Page::Entrepots:buildWarehouses();break;case Page::Fournisseurs:buildSuppliers();break;case Page::Rapports:buildReports();break;case Page::Utilisateurs:buildUsers();break;case Page::Parametres:buildSettings();break;}
    applyTooltips(GTK_WIDGET(window_));
}

void App::buildDashboard() {
    addSectionTitle("Vue d'ensemble","Suivez les niveaux de stock, les alertes et les opérations récentes.");
    if(!canViewStock()){
        GtkWidget* panel=makePanel("Accès limité");addEmptyState(panel,"Votre profil ne consulte pas les niveaux de stock","Vous pouvez utiliser les opérations et documents autorisés depuis le menu latéral.");append(pageContainer_,panel);
        if(session_.canManageMovements){GtkWidget* move=makeButton("Enregistrer un mouvement","primary-button");gtk_widget_set_halign(move,GTK_ALIGN_START);append(pageContainer_,move);connectClicked(move,[this]{openMovementForm();});}return;
    }
    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_halign(actions,GTK_ALIGN_START);
    if(session_.canManageArticles){GtkWidget* add=makeButton("＋  Nouvel article","primary-button");append(actions,add);connectClicked(add,[this]{openProductForm();});}
    if(session_.canManageMovements){GtkWidget* move=makeButton("⇄  Enregistrer un mouvement","secondary-button");append(actions,move);connectClicked(move,[this]{openMovementForm();});}
    append(pageContainer_,actions);

    const int itemCount=database_->productCount();const auto suggestions=database_->reorderSuggestions();const auto warehouses=database_->warehouses();
    const double volume=database_->totalVolume();double capacity=0;for(const auto& w:warehouses)capacity+=w.capacityM3;
    GtkWidget* metrics=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
    append(metrics,makeMetric("Articles actifs",std::to_string(itemCount),"Références du catalogue"));
    const int zeroCount=database_->zeroStockCount();append(metrics,makeMetric("Ruptures",std::to_string(zeroCount),"Aucun stock conforme et non échu",zeroCount?"metric-danger":""));
    append(metrics,makeMetric("À réapprovisionner",std::to_string(suggestions.size()),"Seuil commande / sécurité",suggestions.empty()?"":"metric-warn"));
    const int overCount=database_->overStockCount();append(metrics,makeMetric("Au-dessus du maximum",std::to_string(overCount),"À vérifier dans les emplacements",overCount?"metric-warn":""));
    if(session_.admin||session_.canViewCosts)append(metrics,makeMetric("Valeur en stock",formatMoney(database_->totalStockValue()),"Coût estimé des lots disponibles"));
    const int expiringCount=database_->expiringLotCount(preferences().dlcAlertDays);append(metrics,makeMetric("À péremption",std::to_string(expiringCount),"Lots échus ou à échéance · "+std::to_string(preferences().dlcAlertDays)+" j",expiringCount?"metric-warn":""));
    append(metrics,makeMetric("Occupation entrepôts",formatNumber(volume,2)+" m³",capacity>0?formatNumber(volume/capacity*100,1)+" % de capacité déclarée":"Capacité non renseignée"));
    for(GtkWidget* child=gtk_widget_get_first_child(metrics);child;child=gtk_widget_get_next_sibling(child))gtk_widget_set_hexpand(child,TRUE);
    append(pageContainer_,metrics);

    GtkWidget* middle=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,14);gtk_box_set_homogeneous(GTK_BOX(middle),TRUE);
    GtkWidget* alertPanel=makePanel("Articles à surveiller");
    if(suggestions.empty())addEmptyState(alertPanel,"Aucune alerte de commande","Les niveaux actuels couvrent les seuils renseignés.");
    for(std::size_t i=0;i<std::min<std::size_t>(suggestions.size(),6);++i){
        const auto& s=suggestions[i];GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_add_css_class(row,"row-line");
        GtkWidget* nameBox=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_widget_set_hexpand(nameBox,TRUE);append(nameBox,makeLabel(s.product.name,"field-label"));append(nameBox,makeLabel(s.product.sku+" · disponible "+formatNumber(s.product.onHand,2)+" · engagé "+formatNumber(s.customerCommitments,2)+" · projeté "+formatNumber(s.projectedQty,2)+" / seuil "+formatNumber(s.reorderPoint,2),"muted"));append(row,nameBox);
        append(row,makeLabel(s.capacityLimited?"Capacité disponible insuffisante":"+"+formatNumber(s.suggestedQty,2)+" "+s.product.unit,"pill pill-warn"));
        if(session_.canManageMovements&&!s.capacityLimited&&s.suggestedQty>0){GtkWidget* order=makeButton("Commander","secondary-button");append(row,order);connectClicked(order,[this,id=s.product.id,q=s.suggestedQty]{openOrderForm(id,q);});}
        append(alertPanel,row);
    }
    append(middle,alertPanel);
    GtkWidget* expiryPanel=makePanel(tr("Lots échus / échéance · ")+std::to_string(preferences().dlcAlertDays)+tr(" jours"));const auto expiry=database_->expiringLots(preferences().dlcAlertDays);
    if(expiry.empty())addEmptyState(expiryPanel,"Rien à signaler",tr("Aucun lot périssable n'est échu ou n'arrive à échéance dans les ")+std::to_string(preferences().dlcAlertDays)+tr(" prochains jours."));
    else {
        std::vector<std::vector<std::string>> rows;
        for(const auto& r:expiry)rows.push_back({r[1],r[3],formatDate(r[4]),r[5]+" "+r[6],r[7]});
        append(expiryPanel,makeTable({"Article","Lot","Échéance","Quantité","Qualité"},rows));
    }
    append(middle,expiryPanel);append(pageContainer_,middle);

    GtkWidget* recent=makePanel("Dernières opérations");const auto logs=database_->movements(8);
    if(logs.empty())addEmptyState(recent,"Aucun mouvement","Les réceptions et sorties apparaîtront ici.");
    else {
        std::vector<std::vector<std::string>> rows;
        for(const auto& m:logs)rows.push_back({formatDate(m.createdAt),typeLabel(m.type),m.sku+" · "+m.product,(m.type=="OUT"||m.type=="ADJUST_OUT"||m.type=="WRITE_OFF"||m.type=="TRANSFER_OUT"?"−":"+")+formatNumber(m.quantity,2),m.warehouse,m.user});
        append(recent,makeTable({"Date et heure","Type","Article","Qté","Entrepôt","Utilisateur"},rows));
    }
    append(pageContainer_,recent);
    if(session_.admin||session_.canViewCosts) {
        GtkWidget* cost=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);append(cost,makeMetric("Coût annuel de possession estimé",formatMoney(database_->totalHoldingCost()),"Valeur en stock × taux de possession déclaré",""));gtk_widget_set_hexpand(gtk_widget_get_first_child(cost),TRUE);append(pageContainer_,cost);
    }
}

void App::buildProducts(){renderProducts(productFilter_);}

void App::renderProducts(const std::string& filter) {
    clear(pageContainer_);addSectionTitle("Articles","Catalogue, seuils, lots et emplacement des marchandises.");
    GtkWidget* tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    GtkWidget* search=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(search),tr("Rechercher une référence, un article ou une catégorie…").c_str());gtk_editable_set_text(GTK_EDITABLE(search),filter.c_str());gtk_widget_set_hexpand(search,TRUE);append(tools,search);
    GtkWidget* searchButton=makeButton("Rechercher","secondary-button");append(tools,searchButton);
    connectClicked(searchButton,[this,search]{productFilter_=trim(gtk_editable_get_text(GTK_EDITABLE(search)));productPage_=0;renderProducts(productFilter_);});
    connectActivate(search,[this,search]{productFilter_=trim(gtk_editable_get_text(GTK_EDITABLE(search)));productPage_=0;renderProducts(productFilter_);});
    if(session_.canManageArticles){GtkWidget* add=makeButton("＋  Ajouter un article","primary-button");append(tools,add);connectClicked(add,[this]{openProductForm();});}
    append(pageContainer_,tools);
    const int total=database_->productCount(filter);if(total==0)productPage_=0;else productPage_=std::min(productPage_,(total-1)/pageSize());const auto items=database_->productsPage(filter,pageSize(),productPage_*pageSize());const bool costs=session_.admin||session_.canViewCosts;
    GtkWidget* panel=makePanel(tr("Catalogue · ")+std::to_string(total)+tr(" article(s)"));
    std::vector<std::string> headers={"Référence / désignation","Catégorie","Stock disponible","Min. / sécurité / max."};if(costs){headers.push_back("Coût unitaire");headers.push_back("Prix de vente");}headers.push_back("Dates");headers.push_back("Actions");
    GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),12);gtk_grid_set_row_spacing(GTK_GRID(grid),3);
    for(std::size_t col=0;col<headers.size();++col){GtkWidget* h=makeLabel(headers[col],"table-header");gtk_widget_set_hexpand(h,TRUE);gtk_grid_attach(GTK_GRID(grid),h,static_cast<int>(col),0,1,1);}
    int rowIndex=1;
    for(const auto& p:items){
        int col=0;GtkWidget* details=makeLabel(p.sku+"\n"+p.name+(p.barcode.empty()?"":"\nCode-barres · "+p.barcode),"table-cell");gtk_widget_set_hexpand(details,TRUE);gtk_grid_attach(GTK_GRID(grid),details,col++,rowIndex,1,1);
        GtkWidget* category=makeLabel(p.category.empty()?"—":p.category,"table-cell");gtk_grid_attach(GTK_GRID(grid),category,col++,rowIndex,1,1);
        const std::string stockTone=p.onHand<=0?"pill pill-danger":(p.onHand<=p.minStock?"pill pill-warn":"table-cell");
        GtkWidget* stock=makeLabel(tr("Disponible · ")+formatNumber(p.onHand,2)+" "+p.unit+"\n"+tr("Projeté · ")+formatNumber(p.projectedStock,2)+" "+p.unit,stockTone);gtk_grid_attach(GTK_GRID(grid),stock,col++,rowIndex,1,1);
        GtkWidget* levels=makeLabel(formatNumber(p.minStock,2)+" / "+formatNumber(p.safetyStock,2)+" / "+(p.maxStock>0?formatNumber(p.maxStock,2):"∞"),"table-cell");gtk_grid_attach(GTK_GRID(grid),levels,col++,rowIndex,1,1);
        if(costs){GtkWidget* cost=makeLabel(formatMoney(p.unitCost),"table-cell");gtk_grid_attach(GTK_GRID(grid),cost,col++,rowIndex,1,1);GtkWidget* price=makeLabel(formatMoney(p.salePrice),"table-cell");gtk_grid_attach(GTK_GRID(grid),price,col++,rowIndex,1,1);}
        GtkWidget* dates=makeLabel(tr("Créé ")+formatDate(p.createdAt)+"\n"+tr("Modifié ")+formatDate(p.updatedAt),"muted");gtk_grid_attach(GTK_GRID(grid),dates,col++,rowIndex,1,1);
        GtkWidget* buttons=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
        GtkWidget* detailsButton=makeButton("Fiche","secondary-button");append(buttons,detailsButton);connectClicked(detailsButton,[this,p]{showProductDetails(p);});
        if(session_.canPrint){GtkWidget* label=makeButton("Étiquette","secondary-button");append(buttons,label);connectClicked(label,[this,p]{showDocument(makeBarcodeLabels({{p.name,p.sku,p.barcode,p.salePrice}}));});}
        if(session_.canManageMovements){GtkWidget* movement=makeButton("Mouvement","secondary-button");append(buttons,movement);connectClicked(movement,[this,id=p.id]{openMovementForm(id);});}
        if(session_.canManageArticles){GtkWidget* edit=makeButton("Modifier","secondary-button");append(buttons,edit);connectClicked(edit,[this,p]{openProductForm(&p);});GtkWidget* archive=makeButton("Archiver","danger-button");append(buttons,archive);connectClicked(archive,[this,id=p.id,name=p.name]{confirm(GTK_WINDOW(window_),"Archiver l'article","Archiver « "+name+" » ? L'historique restera consultable.",[this,id]{try{database_->archiveProduct(id,session_.id);navigate(Page::Articles);}catch(const std::exception& e){showMessage(e.what(),true);}});});}
        gtk_grid_attach(GTK_GRID(grid),buttons,col,rowIndex++,1,1);
    }
    if(items.empty())addEmptyState(panel,filter.empty()?"Le catalogue est vide":"Aucun résultat","Ajoutez un article ou modifiez votre recherche.");else {append(panel,wrapScrollable(grid,980));append(panel,paginationBar(productPage_,total,pageSize(),[this](int page){productPage_=page;renderProducts(productFilter_);}));}
    append(pageContainer_,panel);
}

void App::openProductForm(const Product* current) {
    const Product existing=current?*current:Product{};
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),current?"Modifier un article":"Ajouter un article","Renseignez les repères de stock et les données utiles au réapprovisionnement.");
    form->text("sku","Référence / SKU",existing.sku,false,"Ex. MAT-0001");
    form->text("barcode","Code-barres (facultatif)",existing.barcode,false,"Code interne, EAN ou GTIN · lecteur USB compatible");
    form->text("name","Désignation",existing.name,false,"Nom clair pour les équipes");
    form->text("category","Catégorie",existing.category,false,"Ex. Matières premières");
    form->text("unit","Unité",existing.unit.empty()?"unité":existing.unit,false,"unité, kg, litre…");
    form->multiline("description","Description / consignes",existing.description,170);
    const std::vector<std::string> stockTypes={"Stock courant","Production","Hors production · MRO","Projet","Usure / réutilisable","Transbordement","Récupération","Cyclique","Sécurité","Alerte","Saisonnier","Transit","Anticipation / spéculatif","Inactif / dormant","Tampon / découplage","Optimal"};
    const std::vector<std::string> flowPolicies={"Flux tirés · demande réelle","Flux poussés · prévision","Flux tendus · JIT","Flux synchrones"};
    auto findChoice=[](const std::vector<std::string>& choices,const std::string& value){const auto it=std::find(choices.begin(),choices.end(),value);return it==choices.end()?0:static_cast<int>(std::distance(choices.begin(),it));};
    form->choose("stock_type","Typologie du stock",stockTypes,findChoice(stockTypes,existing.stockType),true);
    form->choose("flow_policy","Politique de flux",flowPolicies,findChoice(flowPolicies,existing.flowPolicy),true);
    form->number("min","Stock minimum / point de commande",existing.minStock,0,100000000,0.1,2);
    form->number("safety","Stock de sécurité",existing.safetyStock,0,100000000,0.1,2);
    form->number("max","Stock maximal (0 = sans plafond)",existing.maxStock,0,100000000,0.1,2);
    form->number("lead","Délai propre à l'article (jours, 0 = fournisseur)",existing.leadDays,0,3650,1,0);
    form->number("coverage","Couverture cible après délai (jours)",existing.coverageDays,0,3650,1,0);
    form->number("sale_price","Prix de vente unitaire (DA)",existing.salePrice,0,1000000000,1,2);
    const bool maySeeCosts=session_.admin||session_.canViewCosts;if(maySeeCosts){form->number("cost","Coût d'achat unitaire (DA)",existing.unitCost,0,1000000000,1,2);form->number("holding","Taux annuel de possession (%)",existing.holdingRate,0,100,0.5,1);form->number("order_cost","Frais fixes par commande (DA)",existing.orderCost,0,1000000000,10,2);form->number("shortage_cost","Impact estimé d'une unité en rupture (DA)",existing.shortageCost,0,1000000000,10,2);}
    form->number("volume","Volume par unité (m³)",existing.volumeM3,0,1000000,0.001,3);
    form->toggle("perishable","Article périssable (suivi des dates d'échéance)",existing.perishable);
    auto save=std::function<void(FormDialog&)>([this,existing,maySeeCosts,stockTypes,flowPolicies](FormDialog& f){
        Product p=existing;p.sku=trim(f.getText("sku"));p.barcode=trim(f.getText("barcode"));p.name=trim(f.getText("name"));p.category=trim(f.getText("category"));p.unit=trim(f.getText("unit"));p.description=trim(f.getText("description"));p.stockType=stockTypes.at(static_cast<std::size_t>(std::max(0,f.getIndex("stock_type"))));p.flowPolicy=flowPolicies.at(static_cast<std::size_t>(std::max(0,f.getIndex("flow_policy"))));
        p.minStock=f.getNumber("min");p.safetyStock=f.getNumber("safety");p.maxStock=f.getNumber("max");p.leadDays=static_cast<int>(f.getNumber("lead"));p.coverageDays=static_cast<int>(f.getNumber("coverage"));p.salePrice=f.getNumber("sale_price");if(maySeeCosts){p.unitCost=f.getNumber("cost");p.holdingRate=f.getNumber("holding");p.orderCost=f.getNumber("order_cost");p.shortageCost=f.getNumber("shortage_cost");}p.volumeM3=f.getNumber("volume");p.perishable=f.getToggle("perishable");
        if(p.sku.empty()||p.name.empty())throw std::runtime_error("La référence et la désignation sont obligatoires.");
        if(p.maxStock>0&&p.maxStock<p.minStock)throw std::runtime_error("Le stock maximal doit être supérieur ou égal au stock minimum.");
        if(p.safetyStock>p.maxStock&&p.maxStock>0)throw std::runtime_error("Le stock de sécurité ne peut pas dépasser le stock maximal.");
        database_->saveProduct(p,session_.id);gtk_window_destroy(f.window);navigate(Page::Articles);showMessage(existing.id?"Article mis à jour.":"Article ajouté au catalogue.");
    });
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form,save]{try{save(*form);}catch(const std::exception& e){showMessage(e.what(),true);}});
    form->present();
}

void App::showProductDetails(const Product& p) {
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Fiche article · "+p.sku,"Informations de stock et de traçabilité.");
    form->display("name","Désignation",p.name);form->display("category","Catégorie / unité",p.category+" · "+p.unit);
    form->display("available","Stock conforme disponible",formatNumber(p.onHand,2)+" "+p.unit);form->display("net","Stock engagé · projeté",formatNumber(p.customerCommitments,2)+" engagé(s) · "+formatNumber(p.projectedStock,2)+" projeté(s) après commandes en cours");form->display("levels","Minimum · sécurité · maximum",formatNumber(p.minStock,2)+" · "+formatNumber(p.safetyStock,2)+" · "+(p.maxStock>0?formatNumber(p.maxStock,2):"sans plafond"));
    form->display("logistics","Typologie · flux",p.stockType+" · "+p.flowPolicy);if(!p.barcode.empty())form->display("barcode","Code-barres",p.barcode);
    form->display("lead","Délai et couverture cible",std::to_string(p.leadDays)+" jours de délai · "+std::to_string(p.coverageDays)+" jours de couverture après le délai");if(session_.admin||session_.canViewCosts){form->display("cost","Coût d'achat / vente",formatMoney(p.unitCost)+" · prix de vente "+formatMoney(p.salePrice));form->display("holding","Possession annuelle",formatNumber(p.holdingRate,1)+" % / an");form->display("ordering","Frais de commande / risque de rupture",formatMoney(p.orderCost)+" par commande · "+formatMoney(p.shortageCost)+" par unité en rupture");}
    form->display("volume","Volume physique estimé",formatNumber(p.onHand*p.volumeM3,3)+" m³");form->display("dates","Créé / modifié",formatDate(p.createdAt)+" · "+formatDate(p.updatedAt));
    form->displayMultiline("description","Description",p.description.empty()?"—":p.description);
    const auto lots=database_->productLots(p.id);std::ostringstream lotText;for(const auto& lot:lots)lotText<<lot[2]<<" · "<<lot[0]<<(lot[1].empty()?"":" / "+lot[1])<<" · "<<formatDate(lot[3])<<" · "<<lot[5]<<" "<<lot[6]<<" · "<<lot[8]<<(lot[4].empty()?"":" · échéance "+formatDate(lot[4]))<<(lot[9].empty()?"":" · "+lot[9])<<"\n";form->displayMultiline("lots","Lots en stock (ordre FIFO)",lotText.str().empty()?"Aucun lot en stock":lotText.str());
    auto cancel=std::function<void()>([form]{finishForm(form);});connectClicked(form->cancel,cancel);connectClicked(form->save,cancel);gtk_button_set_label(GTK_BUTTON(form->save),tr("Fermer").c_str());
    form->present();
}

void App::buildMovements() {
    addSectionTitle("Mouvements de stock","Chaque entrée et sortie est associée à un utilisateur, une date, un lot et une référence.");
    GtkWidget* tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    if(session_.canManageMovements){GtkWidget* add=makeButton("＋  Nouveau mouvement","primary-button");append(tools,add);connectClicked(add,[this]{openMovementForm();});}
    if(session_.canPrint){GtkWidget* print=makeButton("Imprimer le journal","secondary-button");append(tools,print);connectClicked(print,[this]{printMovements();});GtkWidget* csv=makeButton("Exporter CSV","secondary-button");append(tools,csv);connectClicked(csv,[this]{exportMovements();});}
    append(pageContainer_,tools);
    const int movementTotal=database_->movementCount();if(movementTotal>0&&movementPage_*pageSize()>=movementTotal)movementPage_=(movementTotal-1)/pageSize();const auto records=database_->movements(pageSize(),movementPage_*pageSize());GtkWidget* panel=makePanel(tr("Historique · ")+std::to_string(movementTotal)+tr(" opération(s)"));
    if(records.empty())addEmptyState(panel,"Aucun mouvement enregistré","Les réceptions, les sorties et les corrections apparaîtront ici.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),11);gtk_grid_set_row_spacing(GTK_GRID(grid),4);
        const std::vector<std::string> headers={"Date et heure","Type","Article","Quantité","Entrepôt / emplacement","Lot / qualité","Référence","Utilisateur","Document"};
        for(std::size_t c=0;c<headers.size();++c){GtkWidget* h=makeLabel(headers[c],"table-header");gtk_widget_set_hexpand(h,TRUE);gtk_grid_attach(GTK_GRID(grid),h,static_cast<int>(c),0,1,1);}
        int row=1;
        for(const auto& m:records){int c=0;
            gtk_grid_attach(GTK_GRID(grid),makeLabel(formatDate(m.createdAt),"table-cell"),c++,row,1,1);
            const bool out=m.type=="OUT"||m.type=="ADJUST_OUT"||m.type=="WRITE_OFF"||m.type=="TRANSFER_OUT";gtk_grid_attach(GTK_GRID(grid),makeLabel(typeLabel(m.type),out?"pill pill-warn":"pill"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(m.sku+"\n"+m.product,"table-cell"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(std::string(out?"−":"+")+formatNumber(m.quantity,2),"table-cell"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(m.warehouse+(m.location.empty()?"":"\n"+m.location),"table-cell"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(m.lotCode+(m.qualityStatus.empty()?"":"\n"+m.qualityStatus),"muted"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(m.reference,"muted"),c++,row,1,1);
            gtk_grid_attach(GTK_GRID(grid),makeLabel(m.user,"table-cell"),c++,row,1,1);
            if(session_.canPrint){GtkWidget* receipt=makeButton("Bon","secondary-button");gtk_grid_attach(GTK_GRID(grid),receipt,c,row,1,1);connectClicked(receipt,[this,id=m.id]{showMovementReceipt(id);});}
            ++row;
        }
        append(panel,wrapScrollable(grid,1080));
    }
    if(!records.empty())append(panel,paginationBar(movementPage_,movementTotal,pageSize(),[this](int page){movementPage_=page;navigate(Page::Mouvements);}));
    append(pageContainer_,panel);
}

void App::openMovementForm(int productId) {
    const auto items=database_->products();const auto stores=database_->warehouses();
    if(items.empty()){showMessage("Ajoutez d'abord un article au catalogue.");return;}
    if(stores.empty()){showMessage("Créez d'abord un entrepôt.");return;}
    std::vector<std::string> productNames,warehouseNames;
    int productIndex=0,warehouseIndex=0;
    for(std::size_t i=0;i<items.size();++i){productNames.push_back(items[i].sku+" — "+items[i].name);if(items[i].id==productId)productIndex=static_cast<int>(i);}
    for(const auto& w:stores)warehouseNames.push_back(w.name);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Enregistrer un mouvement","Les sorties prélèvent d'abord les lots les plus anciens (PEPS / FIFO) dans l'entrepôt choisi.");
    GtkWidget* product=form->choose("product","Article",productNames,productIndex);
    GtkWidget* scan=form->text("scan","Scanner un code-barres / saisir une référence",{},false,"Scannez puis validez avec Entrée");
    connectActivate(scan,[this,form,items,scan]{const std::string code=trim(form->getText("scan"));if(code.empty())return;const auto found=std::find_if(items.begin(),items.end(),[&](const Product& p){return g_ascii_strcasecmp(p.barcode.c_str(),code.c_str())==0||g_ascii_strcasecmp(p.sku.c_str(),code.c_str())==0;});if(found==items.end()){showMessage("Aucun article ne correspond à ce code-barres ou à cette référence.",true);gtk_widget_grab_focus(scan);return;}form->setIndex("product",static_cast<int>(std::distance(items.begin(),found)));gtk_editable_set_text(GTK_EDITABLE(scan),"");});
    GtkWidget* operation=form->choose("type","Type d'opération",{"Réception / entrée","Sortie · FIFO","Correction positive","Correction négative","Déclassement / destruction","Transfert entre entrepôts","Retour client / récupération"},0,true);
    form->choose("warehouse","Entrepôt",warehouseNames,warehouseIndex);
    form->choose("destination","Entrepôt de destination",warehouseNames,warehouseIndex);
    form->number("quantity","Quantité",1,0.01,100000000,1,2);
    const bool maySeeCosts=session_.admin||session_.canViewCosts;if(maySeeCosts){form->number("cost","Coût unitaire (DA) à la réception",productId?items[productIndex].unitCost:0,0,1000000000,1,2);connectNotify(product,"notify::selected",[form,items]{const int index=form->getIndex("product");if(index>=0&&index<static_cast<int>(items.size()))form->setNumber("cost",items[static_cast<std::size_t>(index)].unitCost);});}
    form->text("lot","Code du lot",{},false,"Lot fournisseur (facultatif à la réception)");
    form->text("expiry","Date de péremption (AAAA-MM-JJ)",{},false,"Requise pour les articles périssables");
    form->choose("quality","Contrôle qualité à la réception",{"À contrôler","CONFORME","BLOQUÉ"},0);
    form->text("quality_note","Note de contrôle",{},false,"Motif obligatoire si le lot est bloqué");
    form->text("location","Emplacement",{},false,"Ex. A-03-02 / zone de stockage");
    form->text("reference","Référence",{},false,"Bon, facture, commande…");
    form->text("reason","Motif / commentaire",{},false,"Obligatoire pour un déclassement ou une destruction");
    auto updateInboundFields=[form,operation,maySeeCosts]{const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(operation));const bool inbound=selected==0||selected==2||selected==6;const bool writeOff=selected==4;const bool transfer=selected==5;if(maySeeCosts)form->setVisible("cost",inbound);form->setVisible("destination",transfer);form->setLabel("warehouse",transfer?"Entrepôt source":"Entrepôt");form->setLabel("location",transfer?"Emplacement de destination":"Emplacement");form->setPlaceholder("lot",writeOff?"Saisissez exactement le code du lot à déclasser":"Lot fournisseur (facultatif à la réception)");for(const auto* key:{"expiry","quality","quality_note"})form->setVisible(key,inbound);form->setVisible("location",inbound||transfer);form->setPlaceholder("location",transfer?"Emplacement d'arrivée":"Ex. A-03-02 / zone de stockage");};
    connectNotify(operation,"notify::selected",updateInboundFields);updateInboundFields();
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form,items,stores,maySeeCosts]{
        try{
            const int pi=form->getIndex("product"),ti=form->getIndex("type"),wi=form->getIndex("warehouse"),di=form->getIndex("destination");
            if(pi<0||wi<0)throw std::runtime_error("Sélectionnez un article et un entrepôt.");
            const auto& p=items.at(static_cast<std::size_t>(pi));
            const std::string expiry=trim(form->getText("expiry"));
            const int warehouseId=stores.at(static_cast<std::size_t>(wi)).id;std::string reference=trim(form->getText("reference")),reason=trim(form->getText("reason"));if(reference.empty())reference=preferences().bmmPrefix+"-"+std::to_string(g_get_real_time()/1000);
            if(ti==5){if(di<0)throw std::runtime_error("Sélectionnez un entrepôt de destination.");const int destinationId=stores.at(static_cast<std::size_t>(di)).id;database_->transferStock(p.id,form->getNumber("quantity"),warehouseId,destinationId,trim(form->getText("location")),reference,reason,session_.id);const auto warning=physicalConstraintWarning(*database_,p,destinationId);gtk_window_destroy(form->window);navigate(Page::Mouvements);showMessage(warning.empty()?"Transfert enregistré. Les lots, leurs dates, coûts et statuts qualité ont été conservés.":"Transfert enregistré.\n"+warning,!warning.empty());return;}
            const std::vector<std::string> codes={"IN","OUT","ADJUST_IN","ADJUST_OUT","WRITE_OFF","","RETURN"};database_->addMovement(p.id,codes.at(static_cast<std::size_t>(ti)),form->getNumber("quantity"),maySeeCosts?form->getNumber("cost"):p.unitCost,warehouseId,trim(form->getText("location")),trim(form->getText("lot")),expiry,form->getChoice("quality"),trim(form->getText("quality_note")),reference,reason,session_.id);
            const auto warning=physicalConstraintWarning(*database_,p,warehouseId);gtk_window_destroy(form->window);navigate(Page::Mouvements);showMessage(warning.empty()?"Mouvement enregistré. Les lots non conformes ou échus restent exclus des sorties FIFO.":"Mouvement enregistré.\n"+warning,!warning.empty());
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
    form->present();
}

void App::showMovementReceipt(int movementId) {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer de bons.",true);return;}
    const auto movement=database_->movementById(movementId);
    if(!movement){showMessage("Mouvement introuvable.",true);return;}
    const auto& m=*movement;const bool costs=session_.admin||session_.canViewCosts;std::vector<std::string> headers={"N°","Date et heure","Opération","Réf. article","Désignation","Entrepôt","Fournisseur","Emplacement","Lot","Échéance","Qualité","Note qualité","Quantité"};if(costs)headers.push_back("Coût unitaire");headers.push_back("Référence document");headers.push_back("Motif");headers.push_back("Opérateur");
    std::vector<std::vector<std::string>> rows;for(const auto& line:database_->movementLines(m.id)){std::vector<std::string> row={std::to_string(m.id),formatDate(m.createdAt),typeLabel(m.type),m.sku,m.product,m.warehouse,line[6],line[1],line[0],formatDate(line[2]),line[3],line[7],formatNumber(std::stod(line[4]),2)};if(costs)row.push_back(formatMoney(std::stod(line[5])));row.push_back(m.reference);row.push_back(m.reason);row.push_back(m.user);rows.push_back(std::move(row));}
    if(rows.empty()){std::vector<std::string> row={std::to_string(m.id),formatDate(m.createdAt),typeLabel(m.type),m.sku,m.product,m.warehouse,"—",m.location,m.lotCode,formatDate(m.expiry),m.qualityStatus,"",formatNumber(m.quantity,2)};if(costs)row.push_back(formatMoney(m.unitCost));row.push_back(m.reference);row.push_back(m.reason);row.push_back(m.user);rows.push_back(std::move(row));}
    showDocument(makePrintableReport("Bon de mouvement n° "+std::to_string(m.id),"Justificatif d'entrée, de sortie ou de déclassement · "+formatDate(m.createdAt),headers,rows,"Les sorties détaillent chaque lot prélevé dans l'ordre PEPS (premier entré, premier sorti). Les déclassements indiquent le motif et le lot retiré du stock."));
}

void App::buildOrders() {
    addSectionTitle("Commandes et flux logistiques","Suivez les demandes clients, les expéditions, les approvisionnements et le stock projeté.");
    const int customerTotal=database_->customerOrderCount();if(customerTotal==0)customerOrderPage_=0;else customerOrderPage_=std::min(customerOrderPage_,(customerTotal-1)/pageSize());const auto customerOrders=database_->customerOrders(pageSize(),customerOrderPage_*pageSize());
    GtkWidget* customerPanel=makePanel(tr("Commandes clients · stock réservé · ")+std::to_string(customerTotal));
    GtkWidget* customerTools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);if(session_.canManageMovements){GtkWidget* addCustomer=makeButton("＋  Nouvelle commande client","primary-button");append(customerTools,addCustomer);connectClicked(addCustomer,[this]{openCustomerOrderForm();});}append(customerPanel,customerTools);
    append(customerPanel,makeLabel("Les commandes ouvertes déduisent les quantités promises du stock physique pour calculer le stock net/projeté. La livraison enregistre une sortie FIFO et libère la réservation au fur et à mesure.","muted"));
    if(customerOrders.empty())addEmptyState(customerPanel,"Aucune commande client","Les commandes en attente apparaîtront ici.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),12);gtk_grid_set_row_spacing(GTK_GRID(grid),4);const std::vector<std::string> headers={"Référence / client","Article","Demandé","Expédié","Reliquat","Demandée pour","État","Actions"};for(std::size_t c=0;c<headers.size();++c){GtkWidget* h=makeLabel(headers[c],"table-header");gtk_widget_set_hexpand(h,TRUE);gtk_grid_attach(GTK_GRID(grid),h,static_cast<int>(c),0,1,1);}
        int row=1;for(const auto& o:customerOrders){int c=0;gtk_grid_attach(GTK_GRID(grid),makeLabel(o.reference+"\n"+o.customer,"field-label"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(o.sku+" · "+o.product,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(o.quantity,2)+" "+o.unit,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(o.shippedQuantity,2)+" "+o.unit,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(std::max(0.0,o.quantity-o.shippedQuantity),2)+" "+o.unit,"pill pill-warn"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(o.requestedDate.empty()?"—":formatDate(o.requestedDate),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(customerOrderStatusLabel(o.status),o.status=="LIVRÉE"?"pill":"pill pill-warn"),c++,row,1,1);GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);if(session_.canPrint){GtkWidget* receipt=makeButton("Bon","secondary-button");append(actions,receipt);connectClicked(receipt,[this,o]{const double remaining=std::max(0.0,o.quantity-o.shippedQuantity);const std::vector<std::string> headers={"Référence client","Client / destination","Référence article","Désignation","Quantité demandée","Expédiée","Reliquat","Date souhaitée","État","Consigne","Préparée par","Créée le"};const std::vector<std::string> values={o.reference,o.customer,o.sku,o.product,formatNumber(o.quantity,2)+" "+o.unit,formatNumber(o.shippedQuantity,2)+" "+o.unit,formatNumber(remaining,2)+" "+o.unit,o.requestedDate.empty()?"À convenir":formatDate(o.requestedDate),customerOrderStatusLabel(o.status),o.note,o.createdBy,formatDate(o.createdAt)};showDocument(makePrintableReport("Bon de commande client "+o.reference,"Préparation et suivi d'expédition · "+customerOrderStatusLabel(o.status),headers,{values},"Vérifiez la désignation et les quantités, prélevez les lots conformes selon PEPS/FIFO, puis consignez l'expédition dans le journal des mouvements."));});}if(session_.canManageMovements&&(o.status=="OUVERTE"||o.status=="PARTIELLE")){GtkWidget* ship=makeButton("Expédier","primary-button");append(actions,ship);connectClicked(ship,[this,o]{openShipCustomerOrderForm(o);});GtkWidget* cancel=makeButton("Annuler","danger-button");append(actions,cancel);connectClicked(cancel,[this,o]{confirm(GTK_WINDOW(window_),"Annuler la commande client","Libérer la quantité réservée de "+o.reference+" ?",[this,o]{try{database_->cancelCustomerOrder(o.id,session_.id);navigate(Page::Commandes);showMessage("Commande annulée. La réservation de stock a été libérée.");}catch(const std::exception& e){showMessage(e.what(),true);}});});}gtk_grid_attach(GTK_GRID(grid),actions,c,row,1,1);++row;}
        append(customerPanel,wrapScrollable(grid,1000));append(customerPanel,paginationBar(customerOrderPage_,customerTotal,pageSize(),[this](int page){customerOrderPage_=page;navigate(Page::Commandes);}));
    }
    append(pageContainer_,customerPanel);
    const auto suggestions=database_->reorderSuggestions();const bool maySeeCosts=session_.admin||session_.canViewCosts;
    GtkWidget* panel=makePanel(tr("Réapprovisionnement · stock projeté · ")+std::to_string(suggestions.size()));
    if(!suggestions.empty()&&session_.canPrint){GtkWidget* print=makeButton("Imprimer les propositions","secondary-button");append(panel,print);connectClicked(print,[this]{printReorder();});}
    if(suggestions.empty())addEmptyState(panel,"Pas de commande à proposer","Les quantités disponibles sont au-dessus des points de commande calculés.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),12);gtk_grid_set_row_spacing(GTK_GRID(grid),4);
        std::vector<std::string> headers={"Article","Disponible","En commande","Commandes clients","Projeté","Demande / jour","Délai","Couverture","Seuil","Quantité conseillée","Fournisseur"};if(maySeeCosts){headers.push_back("Lot économique indicatif");headers.push_back("Commande / an");headers.push_back("Possession / an");headers.push_back("Rupture estimée");}headers.push_back("Action");
        for(std::size_t c=0;c<headers.size();++c)gtk_grid_attach(GTK_GRID(grid),makeLabel(headers[c],"table-header"),static_cast<int>(c),0,1,1);
        int row=1;for(const auto& s:suggestions){int c=0;gtk_grid_attach(GTK_GRID(grid),makeLabel(s.product.sku+"\n"+s.product.name,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.product.onHand,2)+" "+s.product.unit,"pill pill-warn"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.pendingQty,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.customerCommitments,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.projectedQty,2)+" "+s.product.unit,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.dailyDemand,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(std::to_string(s.leadDays)+" j","table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(std::to_string(s.product.coverageDays)+" j","table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.reorderPoint,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(s.suggestedQty,2)+" "+s.product.unit,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.supplier.empty()?"À choisir":s.supplier,"table-cell"),c++,row,1,1);if(maySeeCosts){gtk_grid_attach(GTK_GRID(grid),makeLabel(s.economicLot>0?formatNumber(s.economicLot,2)+" "+s.product.unit:"—","table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.annualOrderCost>0?formatMoney(s.annualOrderCost):"—","table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.annualHoldingCost>0?formatMoney(s.annualHoldingCost):"—","table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.shortageExposure>0?formatMoney(s.shortageExposure):"—","table-cell"),c++,row,1,1);}
            if(session_.canManageMovements&&!s.capacityLimited&&s.suggestedQty>0){GtkWidget* create=makeButton("Créer commande","primary-button");gtk_grid_attach(GTK_GRID(grid),create,c,row,1,1);connectClicked(create,[this,id=s.product.id,q=s.suggestedQty]{openOrderForm(id,q);});}else if(s.capacityLimited)gtk_grid_attach(GTK_GRID(grid),makeLabel("Capacité à libérer","pill pill-danger"),c,row,1,1);
            ++row;
        }
        append(panel,wrapScrollable(grid,1260));
    }
    append(pageContainer_,panel);
    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);if(session_.canManageMovements){GtkWidget* add=makeButton("＋  Nouvelle commande","primary-button");append(actions,add);connectClicked(add,[this]{openOrderForm();});}if(session_.canPrint){GtkWidget* print=makeButton("Imprimer le journal","secondary-button");append(actions,print);connectClicked(print,[this]{const bool costs=session_.admin||session_.canViewCosts;const auto orders=database_->purchaseOrders();std::vector<std::string> headers={"Référence","État","Fournisseur","SKU","Article","Qté commandée","Qté reçue"};if(costs){headers.push_back("Coût unitaire");headers.push_back("Total");}headers.push_back("Créée le");headers.push_back("Créée par");std::vector<std::vector<std::string>> rows;for(const auto& o:orders){std::vector<std::string> r={o.reference,orderStatusLabel(o.status),o.supplier,o.sku,o.product,formatNumber(o.quantity,2),formatNumber(o.receivedQuantity,2)};if(costs){r.push_back(formatMoney(o.unitCost));r.push_back(formatMoney(o.total));}r.push_back(formatDate(o.createdAt));r.push_back(o.createdBy);rows.push_back(std::move(r));}showDocument(makePrintableReport("Commandes fournisseurs","Suivi des demandes et réceptions",headers,rows));});}append(pageContainer_,actions);
    const int orderTotal=database_->purchaseOrderCount();if(orderTotal==0)supplierOrderPage_=0;else supplierOrderPage_=std::min(supplierOrderPage_,(orderTotal-1)/pageSize());const auto orders=database_->purchaseOrdersPage(pageSize(),supplierOrderPage_*pageSize());GtkWidget* orderPanel=makePanel(tr("Commandes fournisseurs · ")+std::to_string(orderTotal));
    if(orders.empty())addEmptyState(orderPanel,"Aucune commande enregistrée","Créez une commande depuis une proposition JIT ou ajoutez-la manuellement.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),10);gtk_grid_set_row_spacing(GTK_GRID(grid),4);
        const bool costs=session_.admin||session_.canViewCosts;std::vector<std::string> headers={"Référence","Fournisseur","Article","Commandée","Reçue","État","Créée le"};if(costs){headers.push_back("Coût unitaire");headers.push_back("Total");}headers.push_back("Actions");
        for(std::size_t c=0;c<headers.size();++c)gtk_grid_attach(GTK_GRID(grid),makeLabel(headers[c],"table-header"),static_cast<int>(c),0,1,1);
        int row=1;for(const auto& order:orders){int c=0;gtk_grid_attach(GTK_GRID(grid),makeLabel(order.reference,"field-label"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(order.supplier,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(order.sku+" · "+order.product,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(order.quantity,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(order.receivedQuantity,2),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(orderStatusLabel(order.status),order.status=="REÇUE"?"pill":"pill pill-warn"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatDate(order.createdAt),"table-cell"),c++,row,1,1);if(costs){gtk_grid_attach(GTK_GRID(grid),makeLabel(formatMoney(order.unitCost),"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatMoney(order.total),"table-cell"),c++,row,1,1);}
            GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);if(session_.canPrint){GtkWidget* print=makeButton("Bon","secondary-button");append(actions,print);connectClicked(print,[this,order]{const bool canSeeCosts=session_.admin||session_.canViewCosts;std::vector<std::string> receiptHeaders={"Référence commande","Fournisseur","SKU","Désignation","Qté commandée","Qté déjà reçue","État"};std::vector<std::string> values={order.reference,order.supplier,order.sku,order.product,formatNumber(order.quantity,2),formatNumber(order.receivedQuantity,2),orderStatusLabel(order.status)};if(canSeeCosts){receiptHeaders.push_back("Coût unitaire");receiptHeaders.push_back("Total");values.push_back(formatMoney(order.unitCost));values.push_back(formatMoney(order.total));}receiptHeaders.push_back("Date");receiptHeaders.push_back("Préparée par");values.push_back(formatDate(order.createdAt));values.push_back(order.createdBy);showDocument(makePrintableReport("Bon de commande "+order.reference,"Commande fournisseur · statut : "+orderStatusLabel(order.status),receiptHeaders,{values},"À chaque réception, renseignez le lot, la date de péremption, la quantité réellement reçue et le contrôle qualité."));});}
            if(session_.canManageMovements&&order.status=="BROUILLON"){GtkWidget* sent=makeButton("Envoyer","secondary-button");append(actions,sent);connectClicked(sent,[this,id=order.id]{try{database_->markPurchaseOrderSent(id,session_.id);navigate(Page::Commandes);}catch(const std::exception& e){showMessage(e.what(),true);}});}
            if(session_.canManageMovements&&(order.status=="ENVOYÉE"||order.status=="PARTIELLE")){GtkWidget* receive=makeButton(order.status=="PARTIELLE"?"Reliquat":"Réceptionner","primary-button");append(actions,receive);connectClicked(receive,[this,order]{openReceiveOrderForm(order);});}
            gtk_grid_attach(GTK_GRID(grid),actions,c,row,1,1);++row;
        }
        append(orderPanel,wrapScrollable(grid,1080));append(orderPanel,paginationBar(supplierOrderPage_,orderTotal,pageSize(),[this](int page){supplierOrderPage_=page;navigate(Page::Commandes);}));
    }
    append(pageContainer_,orderPanel);
}

void App::openCustomerOrderForm(int productId,double quantity) {
    const auto items=database_->products();if(items.empty()){showMessage("Ajoutez d'abord un article au catalogue.");return;}
    std::vector<std::string> productNames;int selected=0;for(std::size_t i=0;i<items.size();++i){productNames.push_back(items[i].sku+" — "+items[i].name);if(items[i].id==productId)selected=static_cast<int>(i);}
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Nouvelle commande client","Cette commande réserve la quantité demandée pour le calcul du stock net. Une expédition la transforme en sortie FIFO traçable.");
    form->text("customer","Client / destination");form->choose("product","Article",productNames,selected);form->number("quantity","Quantité demandée",quantity>0?quantity:1,0.01,100000000,1,2);form->text("date","Date souhaitée (AAAA-MM-JJ)",{},false,"Facultative");form->text("note","Consigne / référence client",{},false,"Bon client, projet, instructions…");
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,items]{try{const int index=form->getIndex("product");if(index<0)throw std::runtime_error("Sélectionnez un article.");const int id=database_->createCustomerOrder(trim(form->getText("customer")),items.at(static_cast<std::size_t>(index)).id,form->getNumber("quantity"),trim(form->getText("date")),trim(form->getText("note")),session_.id);gtk_window_destroy(form->window);customerOrderPage_=0;navigate(Page::Commandes);showMessage("Commande client CL-"+std::string(6-std::min<std::size_t>(6,std::to_string(id).size()),'0')+std::to_string(id)+" enregistrée. Le stock projeté et le réapprovisionnement ont été actualisés.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::openShipCustomerOrderForm(const CustomerOrder& order) {
    const auto stores=database_->warehouses();if(stores.empty()){showMessage("Créez un entrepôt avant de préparer une expédition.");return;}
    std::vector<std::string> names;for(const auto& w:stores)names.push_back(w.name);const double remaining=std::max(0.0,order.quantity-order.shippedQuantity);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Expédier "+order.reference,order.customer+" · "+order.sku+" · reste à expédier : "+formatNumber(remaining,2)+" "+order.unit+". Le prélèvement PEPS ne prend que les lots conformes et non échus.");
    form->choose("warehouse","Entrepôt de prélèvement",names);form->number("quantity","Quantité à expédier",remaining,0.01,remaining,0.01,2);form->text("location","Zone / quai de préparation",{},false,"Ex. Quai d'expédition");
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,stores,order]{try{const int index=form->getIndex("warehouse");if(index<0)throw std::runtime_error("Sélectionnez l'entrepôt de prélèvement.");const int batch=database_->shipCustomerOrder(order.id,form->getNumber("quantity"),stores.at(static_cast<std::size_t>(index)).id,trim(form->getText("location")),session_.id);gtk_window_destroy(form->window);navigate(Page::Commandes);showMessage("Expédition enregistrée dans le mouvement n°"+std::to_string(batch)+". La réservation a été réduite et le bon est disponible dans le journal des mouvements.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::openOrderForm(int productId,double quantity) {
    const auto items=database_->products();const auto vendors=database_->suppliers();if(items.empty()){showMessage("Ajoutez d'abord des articles.");return;}
    std::vector<std::string> productNames={};int pi=0;for(std::size_t i=0;i<items.size();++i){productNames.push_back(items[i].sku+" — "+items[i].name);if(items[i].id==productId)pi=static_cast<int>(i);}
    std::vector<std::string> vendorNames={"— Aucun fournisseur associé —"};for(const auto& s:vendors)vendorNames.push_back(s.name);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Nouvelle commande fournisseur","La quantité proposée respecte le niveau cible et le stock maximal configuré pour l'article.");
    GtkWidget* product=form->choose("product","Article",productNames,pi);form->choose("supplier","Fournisseur",vendorNames,0);form->number("quantity","Quantité à commander",quantity>0?quantity:1,0.01,100000000,1,2);const bool maySeeCosts=session_.admin||session_.canViewCosts;if(maySeeCosts){form->number("cost","Coût unitaire estimé (DA)",items[pi].unitCost,0,1000000000,1,2);connectNotify(product,"notify::selected",[form,items]{const int index=form->getIndex("product");if(index>=0&&index<static_cast<int>(items.size()))form->setNumber("cost",items[static_cast<std::size_t>(index)].unitCost);});}
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form,items,vendors,maySeeCosts]{try{const int pi=form->getIndex("product"),si=form->getIndex("supplier");if(pi<0)throw std::runtime_error("Sélectionnez un article.");const int supplierId=si>0?vendors.at(static_cast<std::size_t>(si-1)).id:0;const auto& product=items.at(static_cast<std::size_t>(pi));const int id=database_->createPurchaseOrder(product.id,supplierId,form->getNumber("quantity"),maySeeCosts?form->getNumber("cost"):product.unitCost,session_.id);gtk_window_destroy(form->window);navigate(Page::Commandes);showMessage("Commande CMD-"+std::to_string(id)+" créée. Imprimez le bon puis marquez-la envoyée.");}catch(const std::exception& e){showMessage(e.what(),true);}});
    form->present();
}

void App::openReceiveOrderForm(const PurchaseOrder& order) {
    const auto stores=database_->warehouses();std::vector<std::string> names;for(const auto& w:stores)names.push_back(w.name);if(stores.empty()){showMessage("Créez un entrepôt avant de réceptionner la commande.");return;}
    const auto products=database_->products();const auto productIt=std::find_if(products.begin(),products.end(),[&](const Product& p){return p.sku==order.sku;});if(productIt==products.end()){showMessage("L'article de cette commande n'est plus disponible.",true);return;}const Product product=*productIt;const double remaining=std::max(0.0,order.quantity-order.receivedQuantity);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Réception de la commande "+order.reference,"Reste à recevoir : "+formatNumber(remaining,2)+" "+product.unit+". Chaque livraison crée un lot avec sa propre date et son contrôle qualité.");form->choose("warehouse","Entrepôt de réception",names);form->number("quantity","Quantité reçue maintenant",remaining,0.01,remaining,0.01,2);form->text("lot","Code du lot fournisseur",{},false,"Facultatif");form->text("expiry","Date de péremption (AAAA-MM-JJ)",{},false,"À renseigner pour un produit périssable");form->text("location","Emplacement de rangement",{},false,"Ex. A-03-02");form->choose("quality","Contrôle qualité",{"À contrôler","CONFORME","BLOQUÉ"},0);form->text("quality_note","Note de contrôle",{},false,"Motif obligatoire si le lot est bloqué");
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,stores,order,product]{try{const int wi=form->getIndex("warehouse");if(wi<0)throw std::runtime_error("Sélectionnez l'entrepôt de réception.");const int warehouseId=stores.at(static_cast<std::size_t>(wi)).id;const auto expiry=trim(form->getText("expiry"));database_->receivePurchaseOrder(order.id,form->getNumber("quantity"),warehouseId,trim(form->getText("location")),trim(form->getText("lot")),expiry,form->getChoice("quality"),trim(form->getText("quality_note")),session_.id);const auto warning=physicalConstraintWarning(*database_,product,warehouseId);gtk_window_destroy(form->window);navigate(Page::Commandes);showMessage(warning.empty()?"Réception enregistrée. Les lots conformes sont disponibles; les autres restent en contrôle.":"Réception enregistrée.\n"+warning,!warning.empty());}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}


void App::buildSales() {
    addSectionTitle("Ventes et clients","Créez devis, bons de livraison, ventes comptoir, factures et retours. Les prix client sont proposés automatiquement.");
    const auto customers=database_->customers();
    const auto allDocuments=database_->salesDocuments(100000);
    double revenue=0;int invoiceCount=0;int draftCount=0;
    for(const auto& document:allDocuments){
        if(document.type=="INVOICE"||document.type=="COUNTER"){revenue+=document.total;++invoiceCount;}
        if(document.type=="PROFORMA")++draftCount;
    }
    GtkWidget* metrics=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,12);
    append(metrics,makeMetric("Chiffre d'affaires",formatMoney(revenue),std::to_string(invoiceCount)+" facture(s) et ventes comptoir"));
    append(metrics,makeMetric("Documents de vente",std::to_string(allDocuments.size()),"Toutes périodes confondues"));
    append(metrics,makeMetric("Clients actifs",std::to_string(customers.size()),"Fiches clients disponibles"));
    append(metrics,makeMetric("Proformas ouvertes",std::to_string(draftCount),"À transformer en livraison ou facture"));
    for(GtkWidget* child=gtk_widget_get_first_child(metrics);child;child=gtk_widget_get_next_sibling(child))gtk_widget_set_hexpand(child,TRUE);
    append(pageContainer_,metrics);

    GtkWidget* tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_halign(tools,GTK_ALIGN_START);
    GtkWidget* counter=makeButton("＋  Vente comptoir","primary-button");append(tools,counter);connectClicked(counter,[this]{openSalesForm("COUNTER");});
    for(const auto& [type,label]:std::vector<std::pair<std::string,std::string>>{{"INVOICE","＋  Facture"},{"BL","＋  Bon de livraison"},{"PROFORMA","＋  Proforma"},{"RETURN","＋  Retour client"}}){
        GtkWidget* button=makeButton(label,"secondary-button");append(tools,button);connectClicked(button,[this,type=type]{openSalesForm(type);});
    }
    GtkWidget* newCustomer=makeButton("＋  Nouveau client","secondary-button");append(tools,newCustomer);connectClicked(newCustomer,[this]{openCustomerForm();});
    append(pageContainer_,tools);

    GtkWidget* customerPanel=makePanel(tr("Clients · ")+std::to_string(customers.size()));
    GtkWidget* customerGrid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(customerGrid),12);gtk_grid_set_row_spacing(GTK_GRID(customerGrid),3);
    const std::vector<std::string> customerHeaders={"Code","Client","Contact","Téléphone","Remise","Conditions","Actions"};
    for(std::size_t i=0;i<customerHeaders.size();++i){GtkWidget* header=makeLabel(customerHeaders[i],"table-header");gtk_widget_set_hexpand(header,TRUE);gtk_grid_attach(GTK_GRID(customerGrid),header,static_cast<int>(i),0,1,1);}
    int customerRow=1;
    for(const auto& c:customers){
        int col=0;
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(c.code.empty()?"—":c.code,"code"),col++,customerRow,1,1);
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(c.name+(c.email.empty()?"":"\n"+c.email),"table-cell"),col++,customerRow,1,1);
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(c.contact.empty()?"—":c.contact,"table-cell"),col++,customerRow,1,1);
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(c.phone.empty()?"—":c.phone,"table-cell"),col++,customerRow,1,1);
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(formatNumber(c.discountRate,1)+" %","table-cell"),col++,customerRow,1,1);
        gtk_grid_attach(GTK_GRID(customerGrid),makeLabel(c.paymentTerms.empty()?"—":c.paymentTerms,"muted"),col++,customerRow,1,1);
        GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
        GtkWidget* edit=makeButton("Modifier","secondary-button");append(actions,edit);connectClicked(edit,[this,c]{openCustomerForm(&c);});
        GtkWidget* prices=makeButton("Tarifs","secondary-button");append(actions,prices);connectClicked(prices,[this,c]{openCustomerPricesForm(c);});
        gtk_grid_attach(GTK_GRID(customerGrid),actions,col,customerRow++,1,1);
    }
    if(customers.empty())addEmptyState(customerPanel,"Aucun client","Ajoutez une fiche client pour proposer des tarifs et suivre les documents.");
    else append(customerPanel,wrapScrollable(customerGrid,980));
    append(pageContainer_,customerPanel);

    GtkWidget* documentsPanel=makePanel(tr("Documents de vente · ")+std::to_string(database_->salesDocumentCount(salesFilter_)));
    GtkWidget* filters=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    const std::vector<std::string> filterLabels={"Tous","Proforma","Bons de livraison","Ventes comptoir","Factures","Retours"};
    const std::vector<std::string> filterKeys={"","PROFORMA","BL","COUNTER","INVOICE","RETURN"};
    GtkStringList* filterModel=gtk_string_list_new(nullptr);
    for(const auto& label:filterLabels)gtk_string_list_append(filterModel,tr(label).c_str());
    GtkWidget* filterDrop=gtk_drop_down_new(G_LIST_MODEL(filterModel),nullptr);
    int filterIndex=0;for(std::size_t i=0;i<filterKeys.size();++i)if(filterKeys[i]==salesFilter_)filterIndex=static_cast<int>(i);
    gtk_drop_down_set_selected(GTK_DROP_DOWN(filterDrop),static_cast<guint>(filterIndex));
    gtk_widget_set_size_request(filterDrop,210,-1);
    append(filters,filterDrop);append(documentsPanel,filters);
    connectNotify(filterDrop,"notify::selected",[this,filterKeys,filterDrop]{
        const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(filterDrop));
        salesFilter_=selected<filterKeys.size()?filterKeys[selected]:"";
        salesPage_=0;idleCall([this]{navigate(Page::Ventes);});
    });
    const int total=database_->salesDocumentCount(salesFilter_);if(total==0)salesPage_=0;else salesPage_=std::min(salesPage_,(total-1)/pageSize());
    const auto docs=database_->salesDocuments(pageSize(),salesPage_*pageSize(),salesFilter_);
    GtkWidget* documentGrid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(documentGrid),12);gtk_grid_set_row_spacing(GTK_GRID(documentGrid),3);
    const std::vector<std::string> documentHeaders={"Référence","Type","Client","Date","Lignes","Total","État","Actions"};
    for(std::size_t i=0;i<documentHeaders.size();++i){GtkWidget* header=makeLabel(documentHeaders[i],"table-header");gtk_widget_set_hexpand(header,TRUE);gtk_grid_attach(GTK_GRID(documentGrid),header,static_cast<int>(i),0,1,1);}
    int documentRow=1;
    for(const auto& document:docs){
        int col=0;
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(document.reference,"code"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(salesTypeLabel(document.type),"table-cell"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(document.customerName,"table-cell"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(formatDate(document.documentDate),"table-cell"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(std::to_string(document.lineCount),"table-cell"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(formatMoney(document.total),"table-cell"),col++,documentRow,1,1);
        gtk_grid_attach(GTK_GRID(documentGrid),makeLabel(salesStatusLabel(document.status),document.status=="VALIDÉ"?"pill":"pill pill-warn"),col++,documentRow,1,1);
        GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
        if(session_.canPrint){GtkWidget* print=makeButton("Imprimer","secondary-button");append(actions,print);connectClicked(print,[this,id=document.id]{printSalesDocument(id);});}
        if(document.type=="BL"){GtkWidget* invoice=makeButton("Facturer","primary-button");append(actions,invoice);connectClicked(invoice,[this,id=document.id]{try{const int invoiceId=database_->invoiceFromDelivery(id,session_.id);navigate(Page::Ventes);showMessage("Facture créée depuis le bon de livraison.");if(preferences().autoOpenDocuments)printSalesDocument(invoiceId);}catch(const std::exception& e){showMessage(e.what(),true);}});}
        gtk_grid_attach(GTK_GRID(documentGrid),actions,col,documentRow++,1,1);
    }
    if(docs.empty())addEmptyState(documentsPanel,"Aucun document de vente","Créez une vente comptoir, un bon de livraison ou une facture.");
    else {append(documentsPanel,wrapScrollable(documentGrid,1040));append(documentsPanel,paginationBar(salesPage_,total,pageSize(),[this](int page){salesPage_=page;navigate(Page::Ventes);}));}
    append(pageContainer_,documentsPanel);
}

void App::openCustomerForm(const Customer* current) {
    const Customer existing=current?*current:Customer{};
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),existing.id?"Modifier un client":"Ajouter un client","Les coordonnées, conditions de paiement et remises servent aux documents de vente et aux tarifs personnalisés.");
    form->text("code","Code client",existing.code,false,"Généré automatiquement si vide");
    form->text("name","Nom / raison sociale",existing.name);
    form->text("contact","Contact",existing.contact);
    form->text("phone","Téléphone",existing.phone);
    form->text("email","Courriel",existing.email);
    form->text("address","Adresse",existing.address);
    form->text("tax_id","Identifiant fiscal",existing.taxId);
    form->number("discount","Remise habituelle (%)",existing.discountRate,0,100,0.5,1);
    form->text("terms","Conditions de paiement",existing.paymentTerms,false,"Ex. 30 jours fin de mois");
    form->multiline("notes","Notes",existing.notes,100);
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form,existing]{
        try{
            Customer customer=existing;
            customer.code=trim(form->getText("code"));customer.name=trim(form->getText("name"));customer.contact=trim(form->getText("contact"));
            customer.phone=trim(form->getText("phone"));customer.email=trim(form->getText("email"));customer.address=trim(form->getText("address"));
            customer.taxId=trim(form->getText("tax_id"));customer.paymentTerms=trim(form->getText("terms"));customer.notes=trim(form->getText("notes"));
            customer.discountRate=form->getNumber("discount");
            database_->saveCustomer(customer,session_.id);
            gtk_window_destroy(form->window);
            navigate(Page::Ventes);
            showMessage(existing.id?"Client mis à jour.":"Client ajouté.");
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
    form->present();
}

void App::openCustomerPricesForm(const Customer& customer) {
    const auto products=database_->products();
    if(products.empty()){showMessage("Ajoutez d'abord un article au catalogue.");return;}
    std::vector<std::string> productNames;
    for(const auto& product:products)productNames.push_back(product.sku+" — "+product.name);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Tarifs client · "+customer.name,"Un prix spécifique remplace le prix de vente catalogue. Mettez 0 pour supprimer le tarif personnalisé.");
    GtkWidget* productDrop=form->choose("product","Article",productNames,0);
    const double initialPrice=database_->customerPrice(customer.id,products[0].id,products[0].salePrice);
    form->number("price","Prix de vente spécifique (DA)",initialPrice,0,1000000000,1,2);
    connectNotify(productDrop,"notify::selected",[this,form,products,customer]{
        const int index=form->getIndex("product");
        if(index<0||static_cast<std::size_t>(index)>=products.size())return;
        const auto& product=products[static_cast<std::size_t>(index)];
        form->setNumber("price",database_->customerPrice(customer.id,product.id,product.salePrice));
    });
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form,products,customer]{
        try{
            const int index=form->getIndex("product");
            if(index<0)throw std::runtime_error("Sélectionnez un article.");
            const auto& product=products.at(static_cast<std::size_t>(index));
            database_->setCustomerPrice(customer.id,product.id,form->getNumber("price"),session_.id);
            gtk_window_destroy(form->window);
            navigate(Page::Ventes);
            showMessage("Tarif client enregistré.");
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
    form->present();
}

void App::openSalesForm(const std::string& type) {
    const auto products=database_->products();
    const auto warehouses=database_->warehouses();
    const auto customers=database_->customers();
    if(products.empty()){showMessage("Ajoutez d'abord un article au catalogue.");return;}
    if(warehouses.empty()){showMessage("Créez d'abord un entrepôt.");return;}
    GtkWidget* dialog=gtk_window_new();
    gtk_window_set_title(GTK_WINDOW(dialog),tr("Nouveau document de vente").c_str());
    gtk_window_set_default_size(GTK_WINDOW(dialog),960,800);
    gtk_window_set_modal(GTK_WINDOW(dialog),TRUE);
    gtk_window_set_transient_for(GTK_WINDOW(dialog),GTK_WINDOW(window_));
    GtkWidget* root=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_widget_set_margin_start(root,22);gtk_widget_set_margin_end(root,22);gtk_widget_set_margin_top(root,18);gtk_widget_set_margin_bottom(root,18);
    GtkWidget* heading=makeLabel(tr("Nouveau document · ")+salesTypeLabel(type),"dialog-title");append(root,heading);
    append(root,makeLabel("Sélectionnez le client et l'entrepôt, puis ajoutez les lignes. Les prix client et remises sont proposés automatiquement.","muted"));
    GtkWidget* scroll=gtk_scrolled_window_new();gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
    GtkWidget* content=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);gtk_widget_set_margin_end(content,8);gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),content);append(root,scroll);
    GtkWidget* grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),9);gtk_grid_set_column_spacing(GTK_GRID(grid),16);
    int gridRow=0;
    auto addField=[&](const std::string& label,GtkWidget* control){GtkWidget* caption=makeLabel(label,"field-label");gtk_grid_attach(GTK_GRID(grid),caption,0,gridRow,1,1);gtk_widget_set_hexpand(control,TRUE);gtk_grid_attach(GTK_GRID(grid),control,1,gridRow,1,1);++gridRow;};
    GtkStringList* customerModel=gtk_string_list_new(nullptr);gtk_string_list_append(customerModel,tr("— Aucun client —").c_str());
    for(const auto& customer:customers)gtk_string_list_append(customerModel,(customer.code.empty()?customer.name:customer.code+" · "+customer.name).c_str());
    GtkWidget* customerDrop=gtk_drop_down_new(G_LIST_MODEL(customerModel),nullptr);
    addField("Client",customerDrop);
    GtkStringList* warehouseModel=gtk_string_list_new(nullptr);for(const auto& warehouse:warehouses)gtk_string_list_append(warehouseModel,warehouse.name.c_str());
    GtkWidget* warehouseDrop=gtk_drop_down_new(G_LIST_MODEL(warehouseModel),nullptr);
    addField("Entrepôt",warehouseDrop);
    GtkWidget* dateEntry=gtk_entry_new();gtk_editable_set_text(GTK_EDITABLE(dateEntry),database_->databaseTime().substr(0,10).c_str());
    addField("Date du document (AAAA-MM-JJ)",dateEntry);
    GtkWidget* taxSpin=gtk_spin_button_new_with_range(0,100,0.5);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(taxSpin),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(taxSpin),preferences().defaultTaxRate);
    addField("Taux de TVA (%)",taxSpin);
    GtkWidget* discountSpin=gtk_spin_button_new_with_range(0,100,0.5);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(discountSpin),2);
    addField("Remise globale (%)",discountSpin);
    GtkWidget* noteEntry=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(noteEntry),tr("Référence client, projet, instructions…").c_str());
    addField("Note",noteEntry);
    GtkWidget* expiryEntry=nullptr;
    if(type=="RETURN"){expiryEntry=gtk_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(expiryEntry),tr("AAAA-MM-JJ · obligatoire pour un article périssable").c_str());addField("Date de péremption du retour",expiryEntry);}
    append(content,grid);
    append(content,makeLabel("Lignes du document","settings-title"));
    GtkWidget* lineTools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    GtkStringList* productModel=gtk_string_list_new(nullptr);for(const auto& product:products)gtk_string_list_append(productModel,(product.sku+" — "+product.name).c_str());
    GtkWidget* productDrop=gtk_drop_down_new(G_LIST_MODEL(productModel),nullptr);gtk_widget_set_hexpand(productDrop,TRUE);append(lineTools,productDrop);
    GtkWidget* quantitySpin=gtk_spin_button_new_with_range(0.01,100000000,1);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(quantitySpin),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(quantitySpin),1);gtk_widget_set_size_request(quantitySpin,92,-1);append(lineTools,quantitySpin);
    GtkWidget* priceSpin=gtk_spin_button_new_with_range(0,1000000000,1);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(priceSpin),2);gtk_spin_button_set_value(GTK_SPIN_BUTTON(priceSpin),products[0].salePrice);gtk_widget_set_size_request(priceSpin,118,-1);append(lineTools,priceSpin);
    GtkWidget* lineDiscountSpin=gtk_spin_button_new_with_range(0,100,0.5);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(lineDiscountSpin),1);gtk_widget_set_size_request(lineDiscountSpin,80,-1);append(lineTools,lineDiscountSpin);
    GtkWidget* addLine=makeButton("＋ Ajouter","secondary-button");append(lineTools,addLine);
    append(content,lineTools);
    append(content,makeLabel("Article · quantité · prix unitaire · remise %","settings-hint"));
    GtkWidget* linesBox=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);append(content,linesBox);
    GtkWidget* totalLabel=makeLabel("Total HT : 0,00","panel-title");append(content,totalLabel);
    struct Draft { int productId{}; std::string label; double quantity{}; double price{}; double discount{}; GtkWidget* row{}; };
    auto drafts=std::make_shared<std::vector<Draft>>();
    auto recalc=[drafts,totalLabel,taxSpin]{
        double subtotal=0;for(const auto& draft:*drafts)subtotal+=draft.quantity*draft.price*(1-draft.discount/100.0);
        const double tax=subtotal*gtk_spin_button_get_value(GTK_SPIN_BUTTON(taxSpin))/100.0;
        const std::string text=tr("Total HT")+" : "+formatMoney(subtotal)+"    "+tr("TVA")+" : "+formatMoney(tax)+"    "+tr("Total TTC")+" : "+formatMoney(subtotal+tax);
        gtk_label_set_text(GTK_LABEL(totalLabel),text.c_str());
    };
    auto customerId=[customerDrop,customers]{const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(customerDrop));return selected==0||selected>customers.size()?0:customers[selected-1].id;};
    auto customerDiscount=[customerDrop,customers]{const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(customerDrop));return selected==0||selected>customers.size()?0.0:customers[selected-1].discountRate;};
    connectNotify(productDrop,"notify::selected",[this,productDrop,products,priceSpin,lineDiscountSpin,customerId,customerDiscount]{
        const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(productDrop));if(selected>=products.size())return;
        const auto& product=products[selected];
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(priceSpin),database_->customerPrice(customerId(),product.id,product.salePrice));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(lineDiscountSpin),customerDiscount());
    });
    connectNotify(customerDrop,"notify::selected",[this,customerDrop,productDrop,products,priceSpin,lineDiscountSpin,customerId,customerDiscount]{
        const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(productDrop));if(selected>=products.size())return;
        const auto& product=products[selected];
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(priceSpin),database_->customerPrice(customerId(),product.id,product.salePrice));
        gtk_spin_button_set_value(GTK_SPIN_BUTTON(lineDiscountSpin),customerDiscount());
    });
    connectNotify(taxSpin,"notify::value",[recalc]{recalc();});
    if(!customers.empty())gtk_drop_down_set_selected(GTK_DROP_DOWN(customerDrop),1);
    connectClicked(addLine,[this,productDrop,products,quantitySpin,priceSpin,lineDiscountSpin,linesBox,drafts,recalc]{
        const guint selected=gtk_drop_down_get_selected(GTK_DROP_DOWN(productDrop));
        if(selected>=products.size()){showMessage("Sélectionnez un article.",true);return;}
        const auto& product=products[selected];
        Draft draft;
        draft.productId=product.id;draft.label=product.sku+" — "+product.name;
        draft.quantity=gtk_spin_button_get_value(GTK_SPIN_BUTTON(quantitySpin));
        draft.price=gtk_spin_button_get_value(GTK_SPIN_BUTTON(priceSpin));
        draft.discount=gtk_spin_button_get_value(GTK_SPIN_BUTTON(lineDiscountSpin));
        if(draft.quantity<=0)return;
        GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_add_css_class(row,"settings-row");
        GtkWidget* info=makeLabel(draft.label+"  ·  "+formatNumber(draft.quantity,2)+" × "+formatMoney(draft.price)+"  ·  remise "+formatNumber(draft.discount,1)+" %","settings-label");
        gtk_widget_set_hexpand(info,TRUE);append(row,info);
        GtkWidget* lineTotal=makeLabel(formatMoney(draft.quantity*draft.price*(1-draft.discount/100.0)),"panel-title");append(row,lineTotal);
        GtkWidget* remove=makeButton("Retirer","danger-button");append(row,remove);
        draft.row=row;drafts->push_back(draft);
        gtk_box_append(GTK_BOX(linesBox),row);
        connectClicked(remove,[linesBox,drafts,row,recalc]{
            auto& values=*drafts;values.erase(std::remove_if(values.begin(),values.end(),[row](const Draft& draft){return draft.row==row;}),values.end());
            gtk_box_remove(GTK_BOX(linesBox),row);recalc();
        });
        recalc();
    });
    GtkWidget* footer=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);gtk_widget_set_halign(footer,GTK_ALIGN_END);
    GtkWidget* cancel=makeButton("Annuler","secondary-button");append(footer,cancel);
    GtkWidget* save=makeButton("Enregistrer le document","primary-button");append(footer,save);
    append(root,footer);
    connectClicked(cancel,[dialog]{gtk_window_destroy(GTK_WINDOW(dialog));});
    connectClicked(save,[this,dialog,type,customerDrop,customers,warehouseDrop,warehouses,dateEntry,taxSpin,discountSpin,noteEntry,expiryEntry,drafts]{
        try{
            if(drafts->empty())throw std::runtime_error("Ajoutez au moins une ligne au document.");
            const guint customerIndex=gtk_drop_down_get_selected(GTK_DROP_DOWN(customerDrop));
            const int customerId=customerIndex==0||customerIndex>customers.size()?0:customers[customerIndex-1].id;
            const guint warehouseIndex=gtk_drop_down_get_selected(GTK_DROP_DOWN(warehouseDrop));
            if(warehouseIndex>=warehouses.size())throw std::runtime_error("Sélectionnez un entrepôt.");
            std::vector<SalesLineInput> lines;
            for(const auto& draft:*drafts)lines.push_back({draft.productId,draft.quantity,draft.price,draft.discount,gtk_spin_button_get_value(GTK_SPIN_BUTTON(taxSpin)),expiryEntry?trim(gtk_editable_get_text(GTK_EDITABLE(expiryEntry))):""});
            const int id=database_->createSalesDocument(type,customerId,"",warehouses[warehouseIndex].id,trim(gtk_editable_get_text(GTK_EDITABLE(dateEntry))),trim(gtk_editable_get_text(GTK_EDITABLE(noteEntry))),gtk_spin_button_get_value(GTK_SPIN_BUTTON(taxSpin)),gtk_spin_button_get_value(GTK_SPIN_BUTTON(discountSpin)),lines,0,session_.id);
            gtk_window_destroy(GTK_WINDOW(dialog));
            const auto created=database_->salesDocument(id);
            navigate(Page::Ventes);
            showMessage("Document "+created.reference+" créé.");
            if(preferences().autoOpenDocuments)printSalesDocument(id);
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
    gtk_window_set_child(GTK_WINDOW(dialog),root);
    applyTooltips(dialog);
    gtk_window_present(GTK_WINDOW(dialog));
}

void App::printSalesDocument(int documentId) {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer de documents de vente.",true);return;}
    const auto document=database_->salesDocument(documentId);
    const auto lines=database_->salesLines(documentId);
    ReportDocument report;
    report.title=salesTypeLabel(document.type)+" "+document.reference;
    report.subtitle=document.customerName+" · "+formatDate(document.documentDate)+(document.warehouse.empty()?"":" · "+document.warehouse);
    report.category="DOCUMENT DE VENTE";
    report.barcodeValue=document.reference;
    report.operatorName=document.createdBy;
    report.headers={"Référence","Article","Quantité","Prix unitaire","Remise","Total ligne"};
    for(const auto& line:lines){
        report.rows.push_back({line.sku,line.name,formatNumber(line.quantity,2)+" "+line.unit,formatMoney(line.unitPrice),formatNumber(line.discountRate,1)+" %",formatMoney(line.lineTotal)});
    }
    report.details={{"Client",document.customerName},{"Type",salesTypeLabel(document.type)},{"Date",formatDate(document.documentDate)},{"Entrepôt",document.warehouse.empty()?"—":document.warehouse},{"État",salesStatusLabel(document.status)}};
    report.highlights={{"Total HT",formatMoney(document.subtotal-document.discountTotal)},{"TVA",formatMoney(document.taxTotal)},{"Total TTC",formatMoney(document.total)}};
    report.footnote=document.note.empty()?"Document de vente généré par Gestion des stocks.":document.note;
    showDocument(makePrintableReport(report));
}

void App::printBarcodeLabels() {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer d'étiquettes.",true);return;}
    const auto products=database_->products();
    if(products.empty()){showMessage("Le catalogue est vide.",true);return;}
    std::vector<BarcodeLabel> labels;
    for(const auto& product:products)labels.push_back({product.name,product.sku,product.barcode,product.salePrice});
    showDocument(makeBarcodeLabels(labels));
}

void App::buildWarehouses() {
    addSectionTitle("Entrepôts et emplacements","Suivez la capacité en volume et indiquez une zone ou une case sur chaque réception.");
    if(session_.canManageWarehouses){GtkWidget* add=makeButton("＋  Ajouter un entrepôt","primary-button");gtk_widget_set_halign(add,GTK_ALIGN_START);append(pageContainer_,add);connectClicked(add,[this]{openWarehouseForm();});}
    const auto stores=database_->warehouses();const int total=static_cast<int>(stores.size());if(total==0)warehousePage_=0;else warehousePage_=std::min(warehousePage_,(total-1)/pageSize());GtkWidget* panel=makePanel(tr("Sites de stockage · ")+std::to_string(total));
    if(stores.empty())addEmptyState(panel,"Aucun entrepôt","Créez un site pour organiser les articles.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),12);gtk_grid_set_row_spacing(GTK_GRID(grid),5);const std::vector<std::string> headers={"Entrepôt","Adresse / site","Occupation estimée","Utilisation","Actions"};for(std::size_t c=0;c<headers.size();++c)gtk_grid_attach(GTK_GRID(grid),makeLabel(headers[c],"table-header"),static_cast<int>(c),0,1,1);
        const auto first=static_cast<std::size_t>(warehousePage_*pageSize()),last=std::min(stores.size(),first+static_cast<std::size_t>(pageSize()));int row=1;for(std::size_t i=first;i<last;++i){const auto& store=stores[i];int c=0;gtk_grid_attach(GTK_GRID(grid),makeLabel(store.name,"field-label"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(store.address.empty()?"—":store.address,"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(formatNumber(store.usedM3,3)+" m³ / "+(store.capacityM3>0?formatNumber(store.capacityM3,2)+" m³":"sans plafond"),"table-cell"),c++,row,1,1);const double percent=store.capacityM3>0?store.usedM3/store.capacityM3*100.0:0;const std::string usage=store.capacityM3>0?formatNumber(percent,1)+" %":"À définir";gtk_grid_attach(GTK_GRID(grid),makeLabel(usage,store.capacityM3>0&&percent>=90?"pill pill-warn":"table-cell"),c++,row,1,1);if(session_.canManageWarehouses){GtkWidget* edit=makeButton("Modifier","secondary-button");gtk_grid_attach(GTK_GRID(grid),edit,c,row,1,1);connectClicked(edit,[this,store]{openWarehouseForm(&store);});}++row;}
        append(panel,wrapScrollable(grid,820));append(panel,paginationBar(warehousePage_,total,pageSize(),[this](int page){warehousePage_=page;navigate(Page::Entrepots);}));
    }
    append(pageContainer_,panel);
}

void App::openWarehouseForm(const Warehouse* current) {
    const Warehouse existing=current?*current:Warehouse{};auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),current?"Modifier un entrepôt":"Ajouter un entrepôt","Les capacités servent à visualiser l'occupation. Laissez la capacité à zéro si elle n'est pas connue.");
    form->text("name","Nom de l'entrepôt",existing.name,false,"Ex. Dépôt principal");form->text("address","Adresse / site",existing.address);form->number("capacity","Capacité en volume (m³)",existing.capacityM3,0,100000000,1,2);
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,existing]{try{Warehouse w=existing;w.name=trim(form->getText("name"));w.address=trim(form->getText("address"));w.capacityM3=form->getNumber("capacity");if(w.name.empty())throw std::runtime_error("Le nom de l'entrepôt est obligatoire.");if(existing.id&&w.capacityM3>0&&w.capacityM3<existing.usedM3)throw std::runtime_error("La capacité ne peut pas être inférieure au volume déjà occupé.");database_->saveWarehouse(w,session_.id);gtk_window_destroy(form->window);navigate(Page::Entrepots);showMessage("Entrepôt enregistré.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::buildSuppliers() {
    addSectionTitle("Fournisseurs","Conservez les coordonnées et délais utiles aux achats et au calcul des stocks de sécurité.");
    if(session_.canManageSuppliers){GtkWidget* add=makeButton("＋  Ajouter un fournisseur","primary-button");gtk_widget_set_halign(add,GTK_ALIGN_START);append(pageContainer_,add);connectClicked(add,[this]{openSupplierForm();});}
    const auto vendors=database_->suppliers();const int total=static_cast<int>(vendors.size());if(total==0)supplierPage_=0;else supplierPage_=std::min(supplierPage_,(total-1)/pageSize());GtkWidget* panel=makePanel(tr("Répertoire · ")+std::to_string(total)+tr(" fournisseur(s)"));
    if(vendors.empty())addEmptyState(panel,"Aucun fournisseur","Ajoutez des contacts afin de préparer les commandes plus facilement.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),14);gtk_grid_set_row_spacing(GTK_GRID(grid),6);
        const std::vector<std::string> headers={"Fournisseur","Contact","Téléphone","Courriel","Délais (admin · prép. · transport · contrôle)","Total","Actions"};for(std::size_t i=0;i<headers.size();++i)gtk_grid_attach(GTK_GRID(grid),makeLabel(headers[i],"table-header"),static_cast<int>(i),0,1,1);
        int row=1;const auto first=static_cast<std::size_t>(supplierPage_*pageSize()),last=std::min(vendors.size(),first+static_cast<std::size_t>(pageSize()));for(std::size_t i=first;i<last;++i){const auto& s=vendors[i];gtk_grid_attach(GTK_GRID(grid),makeLabel(s.name,"field-label"),0,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.contact,"table-cell"),1,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.phone,"table-cell"),2,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(s.email,"table-cell"),3,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(std::to_string(s.administrativeDays)+" · "+std::to_string(s.preparationDays)+" · "+std::to_string(s.transportDays)+" · "+std::to_string(s.inspectionDays),"table-cell"),4,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(std::to_string(s.leadDays)+" jours","table-cell"),5,row,1,1);if(session_.canManageSuppliers){GtkWidget* edit=makeButton("Modifier","secondary-button");gtk_grid_attach(GTK_GRID(grid),edit,6,row,1,1);connectClicked(edit,[this,s]{openSupplierForm(&s);});}++row;}
        append(panel,wrapScrollable(grid,1080));append(panel,paginationBar(supplierPage_,total,pageSize(),[this](int page){supplierPage_=page;navigate(Page::Fournisseurs);}));
    }
    append(pageContainer_,panel);
}

void App::openSupplierForm(const Supplier* current) {
    const Supplier existing=current?*current:Supplier{};auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),current?"Modifier un fournisseur":"Ajouter un fournisseur","Détaillez les jours d'administration, de préparation chez le fournisseur, de transport et de contrôle à réception. Leur somme alimente le délai total de réapprovisionnement.");
    form->text("name","Nom du fournisseur",existing.name);form->text("contact","Personne à contacter",existing.contact);form->text("phone","Téléphone",existing.phone);form->text("email","Courriel",existing.email);form->number("admin_days","Administration / transmission (jours)",existing.administrativeDays,0,3650,1,0);form->number("prep_days","Préparation fournisseur (jours)",existing.preparationDays,0,3650,1,0);form->number("transport_days","Transport et livraison (jours)",existing.transportDays,0,3650,1,0);form->number("inspection_days","Contrôle et mise à disposition (jours)",existing.inspectionDays,0,3650,1,0);
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,existing]{try{Supplier s=existing;s.name=trim(form->getText("name"));s.contact=trim(form->getText("contact"));s.phone=trim(form->getText("phone"));s.email=trim(form->getText("email"));s.administrativeDays=static_cast<int>(form->getNumber("admin_days"));s.preparationDays=static_cast<int>(form->getNumber("prep_days"));s.transportDays=static_cast<int>(form->getNumber("transport_days"));s.inspectionDays=static_cast<int>(form->getNumber("inspection_days"));if(s.name.empty())throw std::runtime_error("Le nom du fournisseur est obligatoire.");database_->saveSupplier(s,session_.id);gtk_window_destroy(form->window);navigate(Page::Fournisseurs);showMessage("Fournisseur enregistré. Le délai total a été recalculé.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::buildUsers() {
    addSectionTitle("Utilisateurs et permissions","L'administrateur contrôle l'accès aux stocks, aux coûts, aux modifications et aux documents.");
    GtkWidget* tools=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);if(session_.admin){GtkWidget* add=makeButton("＋  Ajouter un profil de travail","primary-button");append(tools,add);connectClicked(add,[this]{openUserForm();});GtkWidget* account=makeButton("Modifier mon compte","secondary-button");append(tools,account);connectClicked(account,[this]{openAccountForm();});GtkWidget* backup=makeButton("Créer une sauvegarde","secondary-button");append(tools,backup);connectClicked(backup,[this]{backupDatabase();});GtkWidget* restore=makeButton("Restaurer une sauvegarde","secondary-button");append(tools,restore);connectClicked(restore,[this]{
        GtkFileDialog* chooser=gtk_file_dialog_new();
        gtk_file_dialog_set_title(chooser,tr("Choisir une sauvegarde").c_str());
        gtk_file_dialog_set_modal(chooser,TRUE);
        GtkFileFilter* filter=gtk_file_filter_new();gtk_file_filter_set_name(filter,"Bases Gestion des stocks (*.sqlite3, *.db)");gtk_file_filter_add_pattern(filter,"*.sqlite3");gtk_file_filter_add_pattern(filter,"*.db");
        GListStore* filters=g_list_store_new(GTK_TYPE_FILE_FILTER);g_list_store_append(filters,filter);gtk_file_dialog_set_filters(chooser,G_LIST_MODEL(filters));gtk_file_dialog_set_default_filter(chooser,filter);g_object_unref(filters);g_object_unref(filter);
        auto* callback=new std::function<void(GFile*)>([this](GFile* file){
            gchar* path=g_file_get_path(file);
            if(path){const auto source=pathFromUtf8(path);confirm(GTK_WINDOW(window_),"Confirmer la restauration","La base actuelle sera remplacée par « "+pathToUtf8(source.filename())+" ». Une sauvegarde de sécurité sera créée avant le remplacement.",[this,source]{try{auto stamp=database_->databaseTime();std::replace(stamp.begin(),stamp.end(),':','-');std::replace(stamp.begin(),stamp.end(),' ','_');const auto safety=unusedExportPath("avant-restauration-"+stamp,".sqlite3");database_->backupTo(safety);database_->restoreFrom(source);session_=User{};if(database_->userCount()==0)showSetup();else showLogin();showMessage("Sauvegarde restaurée. Connectez-vous avec les comptes de cette sauvegarde.");}catch(const std::exception& e){showMessage(e.what(),true);}});}
            else showMessage("Impossible de lire le fichier choisi.",true);
            if(path)g_free(path);
        });
        gtk_file_dialog_open(chooser,GTK_WINDOW(window_),nullptr,+[](GObject* source,GAsyncResult* result,gpointer data){
            auto* action=static_cast<std::function<void(GFile*)>*>(data);
            GError* error=nullptr;
            GFile* file=gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source),result,&error);
            if(file)(*action)(file);
            else if(error)g_error_free(error);
            if(file)g_object_unref(file);
            delete action;
            g_object_unref(source);
        },callback);
    });}append(pageContainer_,tools);
    const auto accounts=database_->users();GtkWidget* panel=makePanel(tr("Comptes · ")+std::to_string(accounts.size()));
    GtkWidget* list=gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
    for(const auto& u:accounts){GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,9);gtk_widget_add_css_class(row,"row-line");GtkWidget* info=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_hexpand(info,TRUE);append(info,makeLabel(u.fullName+" · "+tr(u.admin?"Administrateur":"Profil de travail"),"field-label"));append(info,makeLabel(tr("Identifiant : ")+u.username+tr(" · Créé le ")+formatDate(u.createdAt)+tr(" · Dernière connexion : ")+(u.lastLogin.empty()?tr("jamais"):formatDate(u.lastLogin)),"muted"));
        std::string permissions=u.admin?"Accès complet":("Stock "+std::string(u.canViewStock?"✓":"—")+" · Articles "+(u.canManageArticles?"✓":"—")+" · Mouvements "+(u.canManageMovements?"✓":"—")+" · Coûts "+(u.canViewCosts?"✓":"—")+" · Impression "+(u.canPrint?"✓":"—"));append(info,makeLabel(permissions,"muted"));append(row,info);
        if(session_.admin&&!u.admin){GtkWidget* edit=makeButton("Permissions","secondary-button");append(row,edit);connectClicked(edit,[this,u]{openUserForm(&u);});GtkWidget* reset=makeButton("Réinitialiser le mot de passe","secondary-button");append(row,reset);connectClicked(reset,[this,u]{auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Réinitialiser le mot de passe",u.fullName+" · choisissez un nouveau mot de passe d'au moins 8 caractères.");form->text("password","Nouveau mot de passe",{},true);form->text("confirm","Confirmer le mot de passe",{},true);connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,u]{try{const auto p=form->getText("password");if(!passwordMeetsMinimum(p))throw std::runtime_error("Utilisez un mot de passe valide d'au moins 8 caractères.");if(p!=form->getText("confirm"))throw std::runtime_error("Les mots de passe ne correspondent pas.");database_->resetPassword(u.id,p,session_.id);gtk_window_destroy(form->window);showMessage("Mot de passe réinitialisé pour "+u.fullName+".");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();});GtkWidget* active=makeButton(u.active?"Désactiver":"Réactiver",u.active?"danger-button":"secondary-button");append(row,active);connectClicked(active,[this,u]{confirm(GTK_WINDOW(window_),u.active?"Désactiver le compte":"Réactiver le compte",u.active?"Désactiver le compte de "+u.fullName+" ?":"Réactiver le compte de "+u.fullName+" ?",[this,u]{try{database_->setUserActive(u.id,!u.active,session_.id);navigate(Page::Utilisateurs);}catch(const std::exception& e){showMessage(e.what(),true);}});});}
        append(list,row);
    }
    append(panel,list);append(pageContainer_,panel);
    GtkWidget* note=makePanel("Protection des comptes");append(note,makeLabel("Les mots de passe sont stockés sous forme dérivée PBKDF2-SHA-256 avec sel individuel. Un profil de travail ne peut ni modifier les comptes ni accorder des permissions.","muted"));append(pageContainer_,note);
}

void App::buildSettings() {
    addSectionTitle("Réglages de l'application","Personnalisez l'apparence, la langue, les formats, les documents, l'entreprise et les alertes.");
    GtkWidget* stack=gtk_stack_new();
    gtk_stack_set_transition_type(GTK_STACK(stack),GTK_STACK_TRANSITION_TYPE_CROSSFADE);
    gtk_widget_set_vexpand(stack,TRUE);
    GtkWidget* switcher=gtk_stack_switcher_new();
    gtk_stack_switcher_set_stack(GTK_STACK_SWITCHER(switcher),GTK_STACK(stack));
    gtk_widget_add_css_class(switcher,"settings-switcher");
    gtk_widget_set_margin_bottom(switcher,8);
    append(pageContainer_,switcher);
    append(pageContainer_,stack);

    auto section=[&](const std::string& title,const std::string& hint){
        GtkWidget* panel=gtk_box_new(GTK_ORIENTATION_VERTICAL,5);
        gtk_widget_add_css_class(panel,"settings-section");
        append(panel,makeLabel(title,"settings-title"));
        append(panel,makeLabel(hint,"settings-hint"));
        GtkWidget* divider=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);
        gtk_widget_add_css_class(divider,"divider");
        gtk_widget_set_margin_top(divider,6);gtk_widget_set_margin_bottom(divider,4);
        append(panel,divider);
        return panel;
    };
    auto addRow=[&](GtkWidget* parent,const std::string& label,const std::string& hint,GtkWidget* control){
        GtkWidget* row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,14);
        gtk_widget_add_css_class(row,"settings-row");
        GtkWidget* info=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
        gtk_widget_set_hexpand(info,TRUE);
        gtk_widget_set_valign(info,GTK_ALIGN_CENTER);
        append(info,makeLabel(label,"settings-label"));
        if(!hint.empty())append(info,makeLabel(hint,"settings-hint"));
        append(row,info);
        gtk_widget_set_valign(control,GTK_ALIGN_CENTER);
        append(row,control);
        append(parent,row);
    };
    auto combo=[&](const std::vector<std::string>& items,int selected){
        GtkStringList* model=gtk_string_list_new(nullptr);
        for(const auto& item:items){const std::string label=tr(item);gtk_string_list_append(model,label.c_str());}
        GtkWidget* dropdown=gtk_drop_down_new(G_LIST_MODEL(model),nullptr);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dropdown),std::clamp(selected,0,static_cast<int>(items.size())-1));
        gtk_widget_set_size_request(dropdown,220,-1);
        return dropdown;
    };
    auto toggle=[&](GtkWidget* parent,const std::string& label,const std::string& hint,bool value,std::function<void(bool)> changed){
        GtkWidget* switchWidget=gtk_switch_new();
        gtk_switch_set_active(GTK_SWITCH(switchWidget),value);
        connectNotify(switchWidget,"notify::active",[this,switchWidget,changed=std::move(changed)]{
            changed(gtk_switch_get_active(GTK_SWITCH(switchWidget)));
            savePreferences(*database_);
        });
        addRow(parent,label,hint,switchWidget);
    };
    auto textSetting=[&](GtkWidget* parent,const std::string& label,const std::string& hint,const std::string& value,std::function<void(const std::string&)> changed,int width=220){
        GtkWidget* entry=gtk_entry_new();gtk_editable_set_text(GTK_EDITABLE(entry),value.c_str());gtk_widget_set_size_request(entry,width,-1);
        connectNotify(entry,"notify::text",[this,entry,changed=std::move(changed)]{
            changed(trim(gtk_editable_get_text(GTK_EDITABLE(entry))));
            savePreferences(*database_);
        });
        addRow(parent,label,hint,entry);
    };
    auto numberSetting=[&](GtkWidget* parent,const std::string& label,const std::string& hint,double value,double min,double max,double step,int digits,std::function<void(double)> changed){
        GtkWidget* spin=gtk_spin_button_new_with_range(min,max,step);gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin),static_cast<guint>(digits));gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin),value);
        gtk_widget_set_size_request(spin,120,-1);
        connectNotify(spin,"notify::value",[this,spin,changed=std::move(changed)]{
            changed(gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)));
            savePreferences(*database_);
        });
        addRow(parent,label,hint,spin);
    };
    auto page=[&](const std::string& name){
        GtkWidget* box=gtk_box_new(GTK_ORIENTATION_VERTICAL,12);
        GtkWidget* scroll=gtk_scrolled_window_new();gtk_widget_set_vexpand(scroll,TRUE);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);
        gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll),box);
        gtk_stack_add_titled(GTK_STACK(stack),scroll,name.c_str(),tr(name).c_str());
        return box;
    };

    GtkWidget* general=page("Général");
    GtkWidget* generalSection=section("Langue et formats","Ces réglages s'appliquent aux écrans et aux nouveaux documents.");
    GtkWidget* language=combo({"Français","English","Espagnol","Allemand"},static_cast<int>(preferences().language));
    connectNotify(language,"notify::selected",[this,language]{
        const auto selected=static_cast<Language>(gtk_drop_down_get_selected(GTK_DROP_DOWN(language)));
        if(selected==preferences().language)return;
        preferences().language=selected;
        setLanguage(selected);
        savePreferences(*database_);
        idleCall([this]{showMain();navigate(Page::Parametres);});
    });
    addRow(generalSection,"Langue de l'interface","Le français reste la langue de référence des données.",language);
    const std::vector<int> startupKeys={0,1,4,7};
    int startupIndex=0;for(std::size_t i=0;i<startupKeys.size();++i)if(startupKeys[i]==preferences().startupPage)startupIndex=static_cast<int>(i);
    GtkWidget* startup=combo({"Vue d'ensemble","Articles","Ventes et clients","Rapports et méthodes"},startupIndex);
    connectNotify(startup,"notify::selected",[this,startupKeys,startup]{
        const auto index=gtk_drop_down_get_selected(GTK_DROP_DOWN(startup));preferences().startupPage=startupKeys.at(std::min<std::size_t>(index,startupKeys.size()-1));savePreferences(*database_);
    });
    addRow(generalSection,"Page d'accueil","Écran ouvert après la connexion.",startup);
    GtkWidget* pageSizeSpin=gtk_spin_button_new_with_range(10,500,10);gtk_spin_button_set_value(GTK_SPIN_BUTTON(pageSizeSpin),preferences().pageSize);gtk_widget_set_size_request(pageSizeSpin,120,-1);
    connectNotify(pageSizeSpin,"notify::value",[this,pageSizeSpin]{preferences().pageSize=static_cast<int>(gtk_spin_button_get_value(GTK_SPIN_BUTTON(pageSizeSpin)));savePreferences(*database_);});
    addRow(generalSection,"Lignes par page","Nombre d'articles, mouvements, clients ou documents affichés.",pageSizeSpin);
    GtkWidget* dateFormat=combo({"JJ/MM/AAAA","AAAA-MM-JJ"},preferences().dateFormat=="iso"?1:0);
    connectNotify(dateFormat,"notify::selected",[this,dateFormat]{preferences().dateFormat=gtk_drop_down_get_selected(GTK_DROP_DOWN(dateFormat))==1?"iso":"fr";savePreferences(*database_);});
    addRow(generalSection,"Format des dates","Affichage des dates dans les listes et documents.",dateFormat);
    GtkWidget* currency=gtk_entry_new();gtk_editable_set_text(GTK_EDITABLE(currency),preferences().currency.c_str());gtk_entry_set_max_length(GTK_ENTRY(currency),8);gtk_widget_set_size_request(currency,220,-1);
    connectNotify(currency,"notify::text",[this,currency]{const std::string value=trim(gtk_editable_get_text(GTK_EDITABLE(currency)));if(value.empty())return;preferences().currency=value;savePreferences(*database_);});
    addRow(generalSection,"Devise","Symbole ou code affiché après les montants (DA, €, USD…).",currency);
    append(general,generalSection);
    GtkWidget* behavior=section("Connexion et confirmations","Comportement de l'application au quotidien.");
    toggle(behavior,"Mémoriser le dernier utilisateur","Le prochain écran de connexion ne demandera plus que le mot de passe.",preferences().rememberLastUser,[this](bool value){preferences().rememberLastUser=value;if(!value)preferences().lastUser.clear();});
    toggle(behavior,"Confirmer les actions destructives","Demande une confirmation avant archivage, restauration ou suppression.",preferences().confirmDestructive,[this](bool value){preferences().confirmDestructive=value;});
    append(general,behavior);

    GtkWidget* appearance=page("Apparence");
    GtkWidget* appearanceSection=section("Thème et interface","Adaptez l'interface à votre environnement de travail.");
    GtkWidget* theme=combo({"Système","Clair","Sombre"},static_cast<int>(preferences().theme));
    connectNotify(theme,"notify::selected",[this,theme]{preferences().theme=static_cast<ThemeMode>(gtk_drop_down_get_selected(GTK_DROP_DOWN(theme)));savePreferences(*database_);applyStyle();});
    addRow(appearanceSection,"Thème","Suit le système ou force un mode clair / sombre.",theme);
    GtkWidget* accent=combo({"Turquoise","Bleu","Violet","Ambre","Rose"},static_cast<int>(preferences().accent));
    connectNotify(accent,"notify::selected",[this,accent]{preferences().accent=static_cast<AccentColor>(gtk_drop_down_get_selected(GTK_DROP_DOWN(accent)));savePreferences(*database_);applyStyle();});
    addRow(appearanceSection,"Couleur d'accent","Boutons principaux, sélections et navigation active.",accent);
    toggle(appearanceSection,"Affichage compact","Réduit les marges et la hauteur des lignes.",preferences().compact,[this](bool value){preferences().compact=value;applyStyle();});
    numberSetting(appearanceSection,"Taille du texte (%)","Ajuste la taille de base de l'interface.",preferences().fontScale,85,130,5,0,[this](double value){preferences().fontScale=static_cast<int>(value);applyStyle();});
    append(appearance,appearanceSection);

    GtkWidget* colorsSection=section("Couleurs personnalisées","Chaque couleur peut être choisie librement. « Auto » rétablit la palette du thème.");
    const bool dark=darkThemeActive();
    auto colorRow=[&](const std::string& label,std::string ColorOverrides::*member,const std::string& fallback){
        GtkColorDialog* colorDialog=gtk_color_dialog_new();
        gtk_color_dialog_set_title(colorDialog,tr("Choisir une couleur").c_str());
        gtk_color_dialog_set_modal(colorDialog,TRUE);
        gtk_color_dialog_set_with_alpha(colorDialog,FALSE);
        GtkWidget* picker=gtk_color_dialog_button_new(colorDialog);
        gtk_widget_set_size_request(picker,62,32);
        const std::string initial=((preferences().colors).*member).empty()?fallback:(preferences().colors).*member;
        GdkRGBA color{};
        if(gdk_rgba_parse(&color,initial.c_str()))gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(picker),&color);
        auto guard=std::make_shared<bool>(false);
        connectNotify(picker,"notify::rgba",[this,member,picker,guard]{
            if(*guard)return;
            const GdkRGBA* rgba=gtk_color_dialog_button_get_rgba(GTK_COLOR_DIALOG_BUTTON(picker));
            if(!rgba)return;
            char* value=gdk_rgba_to_string(rgba);
            (preferences().colors).*member=value?value:"";
            g_free(value);
            savePreferences(*database_);
            applyStyle();
        });
        GtkWidget* autoButton=makeButton("Auto","secondary-button");
        connectClicked(autoButton,[this,member,picker,guard,fallback]{
            *guard=true;
            ((preferences().colors).*member).clear();
            GdkRGBA defaultValue{};if(gdk_rgba_parse(&defaultValue,fallback.c_str()))gtk_color_dialog_button_set_rgba(GTK_COLOR_DIALOG_BUTTON(picker),&defaultValue);
            *guard=false;
            savePreferences(*database_);
            applyStyle();
        });
        GtkWidget* controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);append(controls,picker);append(controls,autoButton);
        addRow(colorsSection,label,"Auto = couleur du thème.",controls);
    };
    const std::string accentDefault=accentPalette(preferences().accent,dark).primary;
    colorRow("Fond de l'application",&ColorOverrides::background,dark?"#15171a":"#f2f3f5");
    colorRow("Surfaces / panneaux",&ColorOverrides::surface,dark?"#1e2126":"#ffffff");
    colorRow("Surfaces secondaires",&ColorOverrides::surfaceAlt,dark?"#24282e":"#f6f7f9");
    colorRow("Texte principal",&ColorOverrides::text,dark?"#e7e9ed":"#20242a");
    colorRow("Texte fort",&ColorOverrides::textStrong,dark?"#f8f9fb":"#111418");
    colorRow("Texte discret",&ColorOverrides::muted,dark?"#a2a9b4":"#68717c");
    colorRow("Bordures",&ColorOverrides::border,dark?"#343941":"#dfe3e8");
    colorRow("Accent",&ColorOverrides::accent,accentDefault);
    colorRow("Barre latérale · haut",&ColorOverrides::sidebarTop,dark?"#191c21":"#20242a");
    colorRow("Barre latérale · bas",&ColorOverrides::sidebarBottom,dark?"#111317":"#14171b");
    colorRow("Alertes / danger",&ColorOverrides::danger,dark?"#ff9a9e":"#a63236");
    GtkWidget* resetColors=makeButton("Réinitialiser toutes les couleurs","danger-button");gtk_widget_set_halign(resetColors,GTK_ALIGN_START);
    connectClicked(resetColors,[this]{preferences().colors.clear();savePreferences(*database_);applyStyle();idleCall([this]{navigate(Page::Parametres);});showMessage("Couleurs réinitialisées.");});
    append(colorsSection,resetColors);
    append(appearance,colorsSection);

    GtkWidget* documents=page("Documents");
    GtkWidget* printSection=section("Impression","Valeurs proposées par défaut lors de la génération des rapports.");
    const int paperIndex=preferences().paperSize=="A4"?1:(preferences().paperSize=="A3"?2:0);
    GtkWidget* paper=combo({"Automatique","A4","A3"},paperIndex);
    connectNotify(paper,"notify::selected",[this,paper]{const int index=gtk_drop_down_get_selected(GTK_DROP_DOWN(paper));preferences().paperSize=index==1?"A4":(index==2?"A3":"auto");savePreferences(*database_);});
    addRow(printSection,"Format du papier","Laisse le document choisir selon le nombre de colonnes.",paper);
    const int orientationIndex=preferences().orientation=="portrait"?1:(preferences().orientation=="landscape"?2:0);
    GtkWidget* orientation=combo({"Automatique","Portrait","Paysage"},orientationIndex);
    connectNotify(orientation,"notify::selected",[this,orientation]{const int index=gtk_drop_down_get_selected(GTK_DROP_DOWN(orientation));preferences().orientation=index==1?"portrait":(index==2?"landscape":"auto");savePreferences(*database_);});
    addRow(printSection,"Orientation","Choisit le sens de la page à l'impression.",orientation);
    toggle(printSection,"Blocs de signature","Ajoute les zones préparé par / vérifié par.",preferences().signatures,[this](bool value){preferences().signatures=value;});
    toggle(printSection,"Ouvrir les documents créés","Lance le navigateur après la génération d'un rapport.",preferences().autoOpenDocuments,[this](bool value){preferences().autoOpenDocuments=value;});
    append(documents,printSection);
    GtkWidget* prefixes=section("Numérotation des documents","Préfixes utilisés pour les références automatiques.");
    textSetting(prefixes,"Préfixe facture","Ex. FAC",preferences().invoicePrefix,[this](const std::string& value){if(!value.empty())preferences().invoicePrefix=value;},120);
    textSetting(prefixes,"Préfixe proforma","Ex. PRO",preferences().proformaPrefix,[this](const std::string& value){if(!value.empty())preferences().proformaPrefix=value;},120);
    textSetting(prefixes,"Préfixe bon de livraison","Ex. BL",preferences().deliveryPrefix,[this](const std::string& value){if(!value.empty())preferences().deliveryPrefix=value;},120);
    textSetting(prefixes,"Préfixe vente comptoir","Ex. CPT",preferences().counterPrefix,[this](const std::string& value){if(!value.empty())preferences().counterPrefix=value;},120);
    textSetting(prefixes,"Préfixe retour client","Ex. RET",preferences().returnPrefix,[this](const std::string& value){if(!value.empty())preferences().returnPrefix=value;},120);
    append(documents,prefixes);
    GtkWidget* labelSection=section("Étiquettes articles","Planche A4 d'étiquettes avec code-barres Code 128, nom, référence et prix.");
    numberSetting(labelSection,"Largeur d'une étiquette (mm)","Entre 20 et 150 mm.",preferences().labelWidthMm,20,150,1,0,[this](double value){preferences().labelWidthMm=static_cast<int>(value);});
    numberSetting(labelSection,"Hauteur d'une étiquette (mm)","Entre 12 et 120 mm.",preferences().labelHeightMm,12,120,1,0,[this](double value){preferences().labelHeightMm=static_cast<int>(value);});
    toggle(labelSection,"Afficher le nom de l'article","Nom sur chaque étiquette.",preferences().labelShowName,[this](bool value){preferences().labelShowName=value;});
    toggle(labelSection,"Afficher le prix","Prix de vente sur chaque étiquette.",preferences().labelShowPrice,[this](bool value){preferences().labelShowPrice=value;});
    GtkWidget* printLabels=makeButton("Imprimer les étiquettes","secondary-button");gtk_widget_set_halign(printLabels,GTK_ALIGN_START);append(labelSection,printLabels);connectClicked(printLabels,[this]{printBarcodeLabels();});
    append(documents,labelSection);

    GtkWidget* salesSettings=page("Ventes et stock");
    GtkWidget* salesSection=section("Ventes","Valeurs par défaut des nouveaux documents.");
    numberSetting(salesSection,"Taux de TVA par défaut (%)","Appliqué aux nouvelles lignes de vente.",preferences().defaultTaxRate,0,100,0.5,2,[this](double value){preferences().defaultTaxRate=value;});
    append(salesSettings,salesSection);
    GtkWidget* alerts=section("Alertes de stock","Seuils utilisés par le tableau de bord et les rapports.");
    numberSetting(alerts,"Alerte péremption (jours)","Signale les lots arrivant à échéance dans cette fenêtre.",preferences().dlcAlertDays,1,3650,1,0,[this](double value){preferences().dlcAlertDays=static_cast<int>(value);});
    numberSetting(alerts,"Stock mort après (mois)","Un article sans sortie depuis cette durée est signalé comme dormant.",preferences().deadStockMonths,1,120,1,0,[this](double value){preferences().deadStockMonths=static_cast<int>(value);});
    textSetting(alerts,"Préfixe BMM","Bon de mouvement manuel.",preferences().bmmPrefix,[this](const std::string& value){if(!value.empty())preferences().bmmPrefix=value;},120);
    append(salesSettings,alerts);

    GtkWidget* company=page("Entreprise");
    GtkWidget* companySection=section("Identité de l'entreprise","Ces informations apparaissent sur les documents imprimés.");
    textSetting(companySection,"Nom de l'entreprise","Raison sociale affichée en en-tête.",preferences().companyName,[this](const std::string& value){if(!value.empty())preferences().companyName=value;},320);
    textSetting(companySection,"Adresse","Adresse postale.",preferences().companyAddress,[this](const std::string& value){preferences().companyAddress=value;},320);
    textSetting(companySection,"Téléphone","Numéro de contact.",preferences().companyPhone,[this](const std::string& value){preferences().companyPhone=value;},220);
    textSetting(companySection,"Courriel","Adresse électronique.",preferences().companyEmail,[this](const std::string& value){preferences().companyEmail=value;},220);
    textSetting(companySection,"Identifiant fiscal","Numéro fiscal ou registre de commerce.",preferences().companyTaxId,[this](const std::string& value){preferences().companyTaxId=value;},220);
    textSetting(companySection,"Pied de page des documents","Mention légale ou coordonnées bancaires.",preferences().documentFooter,[this](const std::string& value){preferences().documentFooter=value;},320);
    append(company,companySection);

    GtkWidget* data=page("Données");
    GtkWidget* dataSection=section("Données et application","Les informations restent stockées localement sur cet ordinateur.");
    GtkWidget* dbPath=makeLabel(pathToUtf8(database_->path()),"code");gtk_label_set_selectable(GTK_LABEL(dbPath),TRUE);gtk_label_set_ellipsize(GTK_LABEL(dbPath),PANGO_ELLIPSIZE_MIDDLE);
    addRow(dataSection,"Base de données","Sauvegardez régulièrement ce fichier.",dbPath);
    GtkWidget* exportFolder=makeButton("Ouvrir le dossier des exports","secondary-button");connectClicked(exportFolder,[this]{if(!openLocalDocument(exportsDirectory()))showMessage("Impossible d'ouvrir le dossier des exports.",true);});
    addRow(dataSection,"Dossier des exports","Documents HTML, CSV et étiquettes générés.",exportFolder);
    GtkWidget* versionText=makeLabel("GTK "+std::to_string(gtk_get_major_version())+"."+std::to_string(gtk_get_minor_version())+"."+std::to_string(gtk_get_micro_version())+" · SQLite "+std::string(sqlite3_libversion()),"code");
    addRow(dataSection,"Environnement","Version des bibliothèques utilisées.",versionText);
    GtkWidget* actions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);
    if(session_.admin){GtkWidget* backup=makeButton("Créer une sauvegarde","secondary-button");append(actions,backup);connectClicked(backup,[this]{backupDatabase();});}
    GtkWidget* reset=makeButton("Réinitialiser les réglages","danger-button");append(actions,reset);
    connectClicked(reset,[this]{
        confirm(GTK_WINDOW(window_),tr("Réinitialiser les réglages"),tr("Revenir aux valeurs par défaut ? Les données, comptes et documents ne seront pas modifiés."),[this]{
            preferences()=Preferences{};setLanguage(preferences().language);savePreferences(*database_);applyStyle();
            idleCall([this]{showMain();navigate(Page::Parametres);showMessage("Réglages réinitialisés.");});
        });
    });
    append(dataSection,actions);append(data,dataSection);
    GtkWidget* note=makePanel("À propos de l'impression");
    append(note,makeLabel("Les documents sont générés en HTML autonome puis ouverts dans votre navigateur. Utilisez Imprimer ou Enregistrer au format PDF depuis la boîte d'impression. Les documents restent sur fond blanc pour préserver les couleurs et l'encre.","muted"));
    append(data,note);
}

void App::openAccountForm() {
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Modifier mon compte administrateur","Changez l'identifiant utilisé à la connexion et, si nécessaire, le mot de passe. Laissez le mot de passe vide pour le conserver.");
    form->text("username","Identifiant de connexion",session_.username,false,"Utilisé pour vous connecter · 64 caractères maximum");
    form->text("name","Nom complet",session_.fullName);
    form->text("password","Nouveau mot de passe",{},true,"Laisser vide pour conserver le mot de passe actuel");
    form->text("confirm","Confirmer le nouveau mot de passe",{},true);
    connectClicked(form->cancel,[form]{finishForm(form);});
    connectClicked(form->save,[this,form]{
        try{
            const auto username=trim(form->getText("username"));
            const auto name=trim(form->getText("name"));
            const auto password=form->getText("password");
            const auto confirmation=form->getText("confirm");
            if(username.empty())throw std::runtime_error("Indiquez l'identifiant de connexion.");
            if(name.empty())throw std::runtime_error("Indiquez le nom complet.");
            if(password.empty()&&!confirmation.empty())throw std::runtime_error("Saisissez le nouveau mot de passe ou videz sa confirmation.");
            if(!password.empty()&&password!=confirmation)throw std::runtime_error("Les mots de passe ne correspondent pas.");
            User updated=session_;updated.username=username;updated.fullName=name;
            database_->updateAdminAccount(updated,password,session_.id);
            session_.username=username;session_.fullName=name;
            if(preferences().rememberLastUser){preferences().lastUser=username;savePreferences(*database_);}
            gtk_window_destroy(form->window);navigate(Page::Utilisateurs);
            showMessage(password.empty()?"Compte administrateur mis à jour.":"Compte administrateur mis à jour. Le nouveau mot de passe est actif.");
        }catch(const std::exception& e){showMessage(e.what(),true);}
    });
    form->present();
}

void App::openUserForm(const User* current) {
    const User existing=current?*current:User{};auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),current?"Droits du profil":"Créer un profil de travail","Choisissez les actions que cette personne peut réaliser dans l'application.");
    form->text("name","Nom complet",existing.fullName);if(!current){form->text("username","Identifiant",{},false,"Utilisé à la connexion");form->text("password","Mot de passe (8 caractères minimum)",{},true);form->text("confirm","Confirmer le mot de passe",{},true);}
    form->toggle("stock","Consulter le stock et les articles",existing.canViewStock);form->toggle("articles","Créer et modifier les articles",existing.canManageArticles);form->toggle("moves","Enregistrer les entrées et sorties",existing.canManageMovements);form->toggle("warehouses","Gérer les entrepôts",existing.canManageWarehouses);form->toggle("suppliers","Gérer les fournisseurs",existing.canManageSuppliers);form->toggle("costs","Consulter les coûts et valeurs",existing.canViewCosts);form->toggle("print","Imprimer et exporter les documents",existing.canPrint);if(current)form->toggle("active","Compte actif",existing.active);
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,existing,current]{try{User u=existing;u.fullName=trim(form->getText("name"));u.canViewStock=form->getToggle("stock");u.canManageArticles=form->getToggle("articles");u.canManageMovements=form->getToggle("moves");u.canManageWarehouses=form->getToggle("warehouses");u.canManageSuppliers=form->getToggle("suppliers");u.canViewCosts=form->getToggle("costs");u.canPrint=form->getToggle("print");if(u.fullName.empty())throw std::runtime_error("Indiquez le nom de la personne.");if(current){u.active=form->getToggle("active");database_->updateWorker(u,session_.id);}else{u.username=trim(form->getText("username"));const auto password=form->getText("password");if(u.username.empty())throw std::runtime_error("Choisissez un identifiant.");if(!passwordMeetsMinimum(password))throw std::runtime_error("Utilisez un mot de passe valide d'au moins 8 caractères.");if(password!=form->getText("confirm"))throw std::runtime_error("Les mots de passe ne correspondent pas.");database_->createWorker(u,password,session_.id);}gtk_window_destroy(form->window);navigate(Page::Utilisateurs);showMessage(current?"Permissions mises à jour.":"Profil créé. La personne peut maintenant se connecter.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::backupDatabase() {
    try{auto stamp=database_->databaseTime();std::replace(stamp.begin(),stamp.end(),':','-');std::replace(stamp.begin(),stamp.end(),' ','_');const auto path=unusedExportPath("sauvegarde-gestion-stock-"+stamp,".sqlite3");database_->backupTo(path);showMessage("Sauvegarde créée :\n"+pathToUtf8(path));}
    catch(const std::exception& e){showMessage(e.what(),true);}
}

void App::buildReports() {
    addSectionTitle("Rapports et méthodes","Inventaires imprimables, analyse ABC, règles PEPS/FIFO, échéances et recommandations JIT.");
    if(!canViewStock()){GtkWidget* panel=makePanel("Accès aux rapports limité");addEmptyState(panel,"Votre profil ne consulte pas les stocks","Demandez à l'administrateur d'activer la permission de consultation des stocks si ces documents sont nécessaires.");append(pageContainer_,panel);return;}
    GtkWidget* inventory=makePanel("Inventaire physique");append(inventory,makeLabel("État du stock par article, entrepôt, emplacement et lot. Le document prévoit des colonnes de comptage et de signature.","muted"));
    GtkWidget* inventoryActions=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,8);if(session_.canPrint){GtkWidget* print=makeButton("Imprimer l'inventaire","primary-button");append(inventoryActions,print);connectClicked(print,[this]{printInventory();});GtkWidget* csv=makeButton("Exporter pour Excel","secondary-button");append(inventoryActions,csv);connectClicked(csv,[this]{exportInventory();});}append(inventory,inventoryActions);append(pageContainer_,inventory);
    if(session_.admin||session_.canViewCosts){
        const auto pmp=database_->pmpValuation();
        double totalValue=0;for(const auto& row:pmp)totalValue+=row.value;
        GtkWidget* pmpPanel=makePanel("Valorisation PMP · prix moyen pondéré");
        append(pmpPanel,makeLabel("Le PMP est calculé à partir des quantités et coûts réels des lots encore en stock. Il compare la valeur moyenne du stock au coût unitaire de la fiche article.","muted"));
        append(pmpPanel,makeMetric("Valeur totale au PMP",formatMoney(totalValue),"Somme des lots disponibles"));
        std::vector<std::vector<std::string>> pmpRows;
        for(const auto& row:pmp)if(row.quantity>0)pmpRows.push_back({row.sku,row.name,formatNumber(row.quantity,2)+" "+row.unit,formatMoney(row.value),formatMoney(row.pmp),formatMoney(row.unitCost),formatMoney(row.variance)});
        if(pmpRows.empty())append(pmpPanel,makeLabel("Aucun stock disponible à valoriser.","muted"));
        else append(pmpPanel,makeTable({"Référence","Article","Quantité","Valeur stock","PMP","Coût fiche","Écart"},pmpRows));
        if(session_.canPrint){GtkWidget* print=makeButton("Imprimer la valorisation PMP","secondary-button");gtk_widget_set_halign(print,GTK_ALIGN_START);append(pmpPanel,print);connectClicked(print,[this,pmp,totalValue]{std::vector<std::vector<std::string>> rows;for(const auto& row:pmp)if(row.quantity>0)rows.push_back({row.sku,row.name,formatNumber(row.quantity,2),formatMoney(row.value),formatMoney(row.pmp),formatMoney(row.unitCost)});showDocument(makePrintableReport("Valorisation PMP du stock","Valorisation au prix moyen pondéré · valeur totale "+formatMoney(totalValue),{"Référence","Article","Quantité","Valeur stock","PMP","Coût fiche"},rows,"Le PMP est recalculé à partir des quantités et coûts des lots disponibles."));});}
        append(pageContainer_,pmpPanel);
    }

    const auto deadStock=database_->deadStockRows(preferences().deadStockMonths);
    const bool maySeeCosts=session_.admin||session_.canViewCosts;
    double deadValue=0;for(const auto& row:deadStock)deadValue+=row.value;
    GtkWidget* deadPanel=makePanel(tr("Stock dormant / mort · ")+std::to_string(preferences().deadStockMonths)+tr(" mois"));
    append(deadPanel,makeLabel(tr("Articles encore en stock mais sans sortie depuis la période configurée.")+(maySeeCosts?" "+tr("Valeur immobilisée")+" : "+formatMoney(deadValue)+".":""),"muted"));
    if(deadStock.empty())append(deadPanel,makeLabel("Aucun stock dormant détecté.","muted"));
    else {
        std::vector<std::vector<std::string>> rows;
        for(const auto& row:deadStock){std::vector<std::string> cells={row.sku,row.name,row.category.empty()?"—":row.category,formatNumber(row.quantity,2)};if(maySeeCosts)cells.push_back(formatMoney(row.value));cells.push_back(row.lastOut.empty()?"Jamais":formatDate(row.lastOut));cells.push_back(std::to_string(row.daysSinceOut)+" j");rows.push_back(std::move(cells));}
        std::vector<std::string> headers={"Référence","Article","Catégorie","Quantité"};if(maySeeCosts)headers.push_back("Valeur");headers.push_back("Dernière sortie");headers.push_back("Ancienneté");
        append(deadPanel,makeTable(headers,rows));
        if(session_.canPrint){GtkWidget* print=makeButton("Imprimer le stock dormant","secondary-button");gtk_widget_set_halign(print,GTK_ALIGN_START);append(deadPanel,print);connectClicked(print,[this,deadStock,deadValue,maySeeCosts]{std::vector<std::vector<std::string>> rows;for(const auto& row:deadStock){std::vector<std::string> cells={row.sku,row.name,formatNumber(row.quantity,2)};if(maySeeCosts)cells.push_back(formatMoney(row.value));cells.push_back(row.lastOut.empty()?"Jamais":formatDate(row.lastOut));cells.push_back(std::to_string(row.daysSinceOut)+" j");rows.push_back(std::move(cells));}std::vector<std::string> headers={"Référence","Article","Quantité"};if(maySeeCosts)headers.push_back("Valeur");headers.push_back("Dernière sortie");headers.push_back("Ancienneté");std::string subtitle="Aucune sortie depuis "+std::to_string(preferences().deadStockMonths)+" mois";if(maySeeCosts)subtitle+=" · valeur immobilisée "+formatMoney(deadValue);showDocument(makePrintableReport("Stock dormant / mort",subtitle,headers,rows,"Un réexamen du stock dormant peut libérer de l'espace et du capital."));});}
    }
    append(pageContainer_,deadPanel);

    const auto flowRows=database_->flowStats(90);
    GtkWidget* flowPanel=makePanel("Fréquence des mouvements et rotation · 90 jours");
    append(flowPanel,makeLabel("Entrées et sorties valorisées sur 90 jours, stock actuel, couverture et dernière activité. Les articles sans sortie sont marqués dormants.","muted"));
    std::vector<std::vector<std::string>> flowTable;
    for(const auto& row:flowRows)if(row.inbound>0||row.outbound>0||row.currentQuantity>0)flowTable.push_back({row.sku,row.name,formatNumber(row.inbound,2),formatNumber(row.outbound,2),formatNumber(row.currentQuantity,2),row.averageDailyOut>0?formatNumber(row.coverageDays,1)+" j":"∞",row.lastMovement.empty()?"—":formatDate(row.lastMovement),tr(row.status)});
    if(flowTable.empty())append(flowPanel,makeLabel("Aucun mouvement sur la période.","muted"));
    else append(flowPanel,makeTable({"Référence","Article","Entrées","Sorties","Stock","Couverture","Dernier mouvement","État"},flowTable));
    if(session_.canPrint){GtkWidget* print=makeButton("Imprimer les statistiques de flux","secondary-button");gtk_widget_set_halign(print,GTK_ALIGN_START);append(flowPanel,print);connectClicked(print,[this,flowRows]{std::vector<std::vector<std::string>> rows;for(const auto& row:flowRows)if(row.inbound>0||row.outbound>0||row.currentQuantity>0)rows.push_back({row.sku,row.name,formatNumber(row.inbound,2),formatNumber(row.outbound,2),formatNumber(row.currentQuantity,2),row.averageDailyOut>0?formatNumber(row.coverageDays,1)+" j":"∞",row.lastMovement.empty()?"—":formatDate(row.lastMovement),tr(row.status)});showDocument(makePrintableReport("Fréquence des mouvements et rotation","Entrées, sorties et couverture sur 90 jours",{"Référence","Article","Entrées","Sorties","Stock","Couverture","Dernier mouvement","État"},rows,"La couverture estime le nombre de jours de stock au rythme moyen de sortie observé."));});}
    append(pageContainer_,flowPanel);

    GtkWidget* labelsPanel=makePanel("Étiquettes et codes-barres");
    append(labelsPanel,makeLabel("Planche d'étiquettes A4 avec code-barres Code 128, nom, référence et prix. La taille est réglable dans les paramètres.","muted"));
    if(session_.canPrint){GtkWidget* labels=makeButton("Imprimer les étiquettes articles","secondary-button");gtk_widget_set_halign(labels,GTK_ALIGN_START);append(labelsPanel,labels);connectClicked(labels,[this]{printBarcodeLabels();});}
    append(pageContainer_,labelsPanel);


    GtkWidget* fifo=makePanel("PEPS / FIFO · traçabilité des lots");append(fifo,makeLabel("Les sorties consomment automatiquement les plus anciens lots encore disponibles dans l'entrepôt sélectionné. Chaque réception conserve son code de lot, son emplacement, son coût et sa date de péremption.","muted"));append(pageContainer_,fifo);

    GtkWidget* abc=makePanel("Méthode ABC · priorité selon la valeur consommée");append(abc,makeLabel("Classement de toutes les références actives selon la valeur des sorties réelles (365 jours), y compris les articles sans consommation. Les seuils usuels sont environ 60 % de valeur pour A, 90 % pour A+B; la courbe réelle peut franchir un seuil entre deux références.","muted"));
    if(session_.admin||session_.canViewCosts){const auto analysis=database_->abcAnalysis();std::array<int,3> counts{};std::array<double,3> values{};for(const auto& item:analysis){const int band=item.category=='A'?0:(item.category=='B'?1:2);++counts[static_cast<std::size_t>(band)];values[static_cast<std::size_t>(band)]+=item.consumptionValue;}const double total=values[0]+values[1]+values[2];
        GtkWidget* totals=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,10);for(int i=0;i<3;++i){const char label=static_cast<char>('A'+i);const double share=total>0?100*values[static_cast<std::size_t>(i)]/total:0;append(totals,makeMetric("Classe "+std::string(1,label),std::to_string(counts[static_cast<std::size_t>(i)])+" réf.",formatNumber(share,1)+" % de la valeur"));gtk_widget_set_hexpand(gtk_widget_get_last_child(totals),TRUE);}append(abc,totals);
        const int totalRows=static_cast<int>(analysis.size());if(totalRows==0)append(abc,makeLabel("Aucune référence active à classer.","muted"));else{abcPage_=std::min(abcPage_,(totalRows-1)/pageSize());const auto start=static_cast<std::size_t>(abcPage_*pageSize());std::vector<std::vector<std::string>> rows;for(std::size_t i=start;i<std::min(analysis.size(),start+static_cast<std::size_t>(pageSize()));++i){const auto& item=analysis[i];rows.push_back({item.sku,item.name,formatMoney(item.consumptionValue),formatNumber(item.cumulativeShare*100,1)+" %",formatNumber(item.cumulativeArticleShare*100,1)+" %",std::string(1,item.category)});}append(abc,makeTable({"Référence","Article","Valeur consommée","Valeur cumulée","Références cumulées","Classe"},rows));append(abc,paginationBar(abcPage_,totalRows,pageSize(),[this](int page){abcPage_=page;navigate(Page::Rapports);}));}
        if(session_.canPrint){GtkWidget* button=makeButton("Imprimer le classement ABC","secondary-button");gtk_widget_set_halign(button,GTK_ALIGN_START);append(abc,button);connectClicked(button,[this]{printAbc();});}
    }else append(abc,makeLabel("La permission « Consulter les coûts et valeurs » est nécessaire pour afficher l'analyse financière.","muted"));
    append(pageContainer_,abc);

    GtkWidget* jit=makePanel("Flux tirés · tendus · synchrones");append(jit,makeLabel("Le réapprovisionnement tiré suit les sorties réelles des 90 derniers jours. Le flux tendu/JIT réduit l'espace et le capital immobilisé; il rend toutefois l'entreprise plus sensible aux retards et aux transports fréquents. Le flux synchrone rapproche la livraison du moment d'utilisation et exige un délai maîtrisé. Le stock de sécurité absorbe les variations de demande et de livraison.","muted"));append(jit,makeLabel("Le point d'alerte retient le plus élevé entre le minimum défini et le stock de sécurité + demande moyenne × délai. Le stock projeté déduit les commandes client ouvertes et ajoute les approvisionnements confirmés. Chaque article définit son propre horizon de couverture après délai; la proposition respecte le plafond et la capacité entrepôt.","muted"));
    if(session_.canPrint){GtkWidget* print=makeButton("Imprimer les recommandations","secondary-button");gtk_widget_set_halign(print,GTK_ALIGN_START);append(jit,print);connectClicked(print,[this]{printReorder();});}append(pageContainer_,jit);

    GtkWidget* logistics=makePanel("Repères logistiques et typologies");append(logistics,makeLabel("Chaîne logistique : approvisionnement en amont, production / transformation sur le site, puis distribution et service client en aval. Les flux internes comprennent les déplacements, la manutention, le contrôle qualité et les transferts. Une gestion efficace maintient le bon article, à la bonne destination et dans les délais convenus.","muted"));
    append(logistics,makeLabel("Stockage : conservation provisoire, généralement interne et de courte durée. Entreposage : hébergement des marchandises dans un entrepôt dédié, souvent en volumes plus grands et sur une durée plus longue. La capacité et les emplacements de l'application servent à suivre cette occupation.","muted"));
    append(logistics,makeLabel("Typologies disponibles sur les fiches article : production et hors production (MRO / entretien-réparation); projet, usure, transbordement et récupération; stock cyclique, de sécurité, d'alerte, saisonnier, en transit, d'anticipation/spéculatif, dormant/inactif et tampon/découplage. Le stock cyclique couvre l'usage courant; le stock de sécurité protège des incertitudes; le stock d'alerte déclenche l'approvisionnement; les stocks dormant et en transit nécessitent un suivi distinct.","muted"));
    append(logistics,makeLabel("Flux poussés : anticiper à partir de prévisions, avec risque de surstock ou de manque si la demande diffère. Flux tirés : planifier depuis la demande observée, avec moins de stock mais potentiellement plus de délai. JIT / flux tendus : livrer au plus près du besoin, en réduisant le stockage au prix d'une forte dépendance à la fiabilité des fournisseurs. Flux synchrones : livrer au rythme de la production, ce qui limite l'attente mais rend tout imprévu susceptible de bloquer la chaîne.","muted"));append(pageContainer_,logistics);

    GtkWidget* costsPanel=makePanel("Équilibre des coûts et limites du surstock");append(costsPanel,makeLabel("Quatre coûts à arbitrer : achat des articles, passation et réception des commandes, maintien en stock (capital, espace, assurance, manutention, détention) et pénurie (arrêt de production, urgence, commande perdue ou insatisfaction client). Les grandes quantités peuvent réduire le nombre de commandes ou obtenir une remise, mais augmentent le stock moyen, le besoin de financement, l'occupation et les risques de détérioration, obsolescence et désuétude.","muted"));append(costsPanel,makeLabel("L'application suit le coût unitaire, le taux annuel de possession, les budgets de commande, les ruptures et l'occupation. Renseignez des coûts réalistes et tenez compte des contraintes de délai, de capacité, de personnel, de qualité et du coût du système de gestion.","muted"));append(pageContainer_,costsPanel);

    GtkWidget* expiry=makePanel("Qualité et péremption");append(expiry,makeLabel(tr("Les lots périssables peuvent être suivis à la réception avec une date d'échéance. Consultez et imprimez les lots arrivant à échéance dans les ")+std::to_string(preferences().dlcAlertDays)+tr(" prochains jours."),"muted"));
    if(session_.canPrint){GtkWidget* print=makeButton("Imprimer les échéances","secondary-button");gtk_widget_set_halign(print,GTK_ALIGN_START);append(expiry,print);connectClicked(print,[this]{printExpiring();});}append(pageContainer_,expiry);

    GtkWidget* quality=makePanel("Contrôle qualité et lots en attente");append(quality,makeLabel("Les sorties FIFO utilisent uniquement les lots conformes et non échus. Les lots en attente, bloqués ou périmés restent visibles dans l'inventaire et l'espace occupé.","muted"));
    const auto lots=database_->qualityLots();const int qualityTotal=static_cast<int>(lots.size());if(qualityTotal==0)qualityLotPage_=0;else qualityLotPage_=std::min(qualityLotPage_,(qualityTotal-1)/pageSize());
    if(lots.empty())addEmptyState(quality,"Aucun lot en stock","Les statuts de contrôle apparaîtront à la réception.");
    else {
        GtkWidget* grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(grid),11);gtk_grid_set_row_spacing(GTK_GRID(grid),4);
        const std::vector<std::string> headers={"Article / lot","Entrepôt / emplacement","Échéance","Quantité","Statut","Note","Action"};for(std::size_t c=0;c<headers.size();++c)gtk_grid_attach(GTK_GRID(grid),makeLabel(headers[c],"table-header"),static_cast<int>(c),0,1,1);
        const auto today=database_->databaseTime().substr(0,10);int row=1;
        const auto first=static_cast<std::size_t>(qualityLotPage_*pageSize()),last=std::min(lots.size(),first+static_cast<std::size_t>(pageSize()));for(std::size_t i=first;i<last;++i){const auto& lot=lots[i];int c=0;gtk_grid_attach(GTK_GRID(grid),makeLabel(lot[1]+"\n"+lot[2]+" · "+lot[5],"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(lot[3]+(lot[4].empty()?"":"\n"+lot[4]),"table-cell"),c++,row,1,1);const bool expired=!lot[7].empty()&&lot[7]<today;gtk_grid_attach(GTK_GRID(grid),makeLabel(expired?"PÉRIMÉ":(lot[7].empty()?"—":formatDate(lot[7])),expired?"pill pill-danger":"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(lot[8]+" "+lot[9],"table-cell"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(lot[10],lot[10]=="CONFORME"&&!expired?"pill":"pill pill-warn"),c++,row,1,1);gtk_grid_attach(GTK_GRID(grid),makeLabel(lot[11],"muted"),c++,row,1,1);if(session_.canManageMovements){GtkWidget* control=makeButton("Contrôler","secondary-button");gtk_grid_attach(GTK_GRID(grid),control,c,row,1,1);connectClicked(control,[this,id=std::stoi(lot[0]),status=lot[10],note=lot[11]]{openLotQualityForm(id,status,note);});}++row;}
        append(quality,wrapScrollable(grid,1040));append(quality,paginationBar(qualityLotPage_,qualityTotal,pageSize(),[this](int page){qualityLotPage_=page;navigate(Page::Rapports);}));
    }
    append(pageContainer_,quality);

    if(session_.admin){GtkWidget* audit=makePanel("Journal d'audit");append(audit,makeLabel("Les créations, modifications, archivages, opérations de stock et actions de commande sont horodatés.","muted"));std::vector<std::vector<std::string>> rows;for(const auto& r:database_->auditLog(20))rows.push_back({formatDate(r[0]),r[1],r[2],r[3],r[4]});if(rows.empty())addEmptyState(audit,"Aucune action enregistrée","Le journal apparaîtra dès les premières opérations.");else append(audit,makeTable({"Date","Utilisateur","Action","Objet","Détail"},rows));append(pageContainer_,audit);}
}

void App::openLotQualityForm(int lotId,const std::string& currentStatus,const std::string& currentNote) {
    const std::vector<std::string> statuses={"À contrôler","CONFORME","BLOQUÉ"};int selected=0;for(std::size_t i=0;i<statuses.size();++i)if(statuses[i]==currentStatus)selected=static_cast<int>(i);
    auto form=std::make_shared<FormDialog>(GTK_WINDOW(window_),"Contrôler un lot","Un lot conforme et non échu peut être prélevé. Les lots bloqués restent traçables mais ne sont jamais distribués.");form->choose("status","Décision qualité",statuses,selected);form->text("note","Note / justification",currentNote,false,"Résultat, anomalie, certificat…");
    connectClicked(form->cancel,[form]{finishForm(form);});connectClicked(form->save,[this,form,lotId]{try{database_->setLotQuality(lotId,form->getChoice("status"),trim(form->getText("note")),session_.id);gtk_window_destroy(form->window);navigate(Page::Rapports);showMessage("Statut qualité du lot mis à jour.");}catch(const std::exception& e){showMessage(e.what(),true);}});form->present();
}

void App::printInventory() {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer l'inventaire.",true);return;}
    if(!canViewStock()){showMessage("Votre profil ne peut pas consulter les niveaux de stock.",true);return;}
    const bool costs=session_.admin||session_.canViewCosts;auto rows=database_->inventoryRows(costs);
    auto headers=std::vector<std::string>{"Référence","Article","Typologie de stock","Politique de flux","Entrepôt","Emplacement","Lot","Entré le","Échéance","Quantité","Unité","Statut qualité","Note de contrôle"};if(costs)headers.push_back("Coût unitaire (DA)");headers.push_back("Compté");headers.push_back("Écart");
    for(auto& row:rows){if(costs)row.back()=formatNumber(std::stod(row.back()),2)+" DA";row.push_back("");row.push_back("");}
    showDocument(makePrintableReport("Feuille d'inventaire physique","Stock théorique par article, politique de flux, entrepôt, emplacement et lot · édité le "+formatDate(database_->databaseTime()),headers,rows,"Comptez physiquement chaque ligne, y compris les articles sans lot théorique. Inscrivez l'écart constaté, le motif de correction et les signatures des personnes ayant compté et vérifié."));
}

void App::printMovements() {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer le journal.",true);return;}
    if(!canViewStock()){showMessage("Votre profil ne peut pas consulter les mouvements de stock.",true);return;}
    const bool costs=session_.admin||session_.canViewCosts;std::vector<std::vector<std::string>> rows;
    for(const auto& m:database_->movements(1000000)){std::vector<std::string> r={formatDate(m.createdAt),typeLabel(m.type),m.sku,m.product,(m.type=="OUT"||m.type=="ADJUST_OUT"||m.type=="WRITE_OFF"||m.type=="TRANSFER_OUT"?"−":"+")+formatNumber(m.quantity,2),m.warehouse,m.location,m.lotCode,m.qualityStatus,m.expiry,m.reference,m.reason,m.user};if(costs)r.push_back(formatMoney(m.unitCost));rows.push_back(std::move(r));}
    std::vector<std::string> headers={"Date","Opération","Référence","Article","Qté","Entrepôt","Emplacement","Lot","Qualité","Échéance","Document","Motif","Opérateur"};if(costs)headers.push_back("Coût unitaire");
    showDocument(makePrintableReport("Journal des mouvements","Entrées, sorties, corrections et déclassements · édité le "+formatDate(database_->databaseTime()),headers,rows,"Les sorties sont affectées aux plus anciens lots conformes et non échus (PEPS / FIFO). Les déclassements retirent explicitement les lots bloqués, périmés ou détériorés du stock physique."));
}

void App::printReorder() {
    if(!session_.canPrint){showMessage("Votre profil ne peut pas imprimer les propositions.",true);return;}
    if(!canViewStock()){showMessage("Votre profil ne peut pas consulter les propositions de réapprovisionnement.",true);return;}
    const bool costs=session_.admin||session_.canViewCosts;std::vector<std::string> headers={"Référence","Article","Stock conforme","En commande","Réservé client","Stock projeté","Demande moyenne / j","Délai (j)","Couverture cible (j)","Point de commande","Qté proposée","Stock cible","Unité","Fournisseur","Capacité"};if(costs){headers.insert(headers.end(),{"Lot économique indicatif","Passation annuelle estimée","Possession annuelle estimée","Exposition de rupture estimée","Budget proposé"});}std::vector<std::vector<std::string>> rows;for(const auto& s:database_->reorderSuggestions()){std::vector<std::string> r={s.product.sku,s.product.name,formatNumber(s.product.onHand,2),formatNumber(s.pendingQty,2),formatNumber(s.customerCommitments,2),formatNumber(s.projectedQty,2),formatNumber(s.dailyDemand,3),std::to_string(s.leadDays),std::to_string(s.product.coverageDays),formatNumber(s.reorderPoint,2),formatNumber(s.suggestedQty,2),formatNumber(s.targetStock,2),s.product.unit,s.supplier.empty()?"À renseigner":s.supplier,s.capacityLimited?"À libérer avant commande":"Disponible"};if(costs){r.push_back(s.economicLot>0?formatNumber(s.economicLot,2)+" "+s.product.unit:"—");r.push_back(s.annualOrderCost>0?formatMoney(s.annualOrderCost):"—");r.push_back(s.annualHoldingCost>0?formatMoney(s.annualHoldingCost):"—");r.push_back(s.shortageExposure>0?formatMoney(s.shortageExposure):"—");r.push_back(formatMoney(s.product.unitCost*s.suggestedQty));}rows.push_back(std::move(r));}
    showDocument(makePrintableReport("Propositions de réapprovisionnement","Stock projeté après approvisionnements et commandes clients · demande observée sur 90 jours · édité le "+formatDate(database_->databaseTime()),headers,rows,"Le stock projeté = stock conforme disponible + commandes fournisseurs en cours − reliquats des commandes clients. Le point d'alerte est le maximum du minimum paramétré et du stock de sécurité augmenté de la demande moyenne pendant le délai. L'horizon de couverture après délai est réglé par article; la quantité proposée respecte le stock maximal et la capacité connue. Le lot économique de Wilson est un repère indicatif fondé sur une demande et des coûts stables; il ne remplace pas la proposition ajustée aux commandes, au stock maximal, aux délais et à la capacité. Les flux tendus réduisent le stockage mais rendent les retards plus sensibles."));
}

void App::printAbc() {
    if(!session_.canPrint||(!session_.admin&&!session_.canViewCosts)){showMessage("Votre profil ne peut pas imprimer l'analyse ABC.",true);return;}
    std::vector<std::vector<std::string>> rows;for(const auto& a:database_->abcAnalysis())rows.push_back({a.sku,a.name,formatMoney(a.consumptionValue),formatNumber(a.cumulativeShare*100,1)+" %",formatNumber(a.cumulativeArticleShare*100,1)+" %",std::string(1,a.category)});
    showDocument(makePrintableReport("Analyse ABC des consommations","Sorties valorisées sur les 365 derniers jours · édité le "+formatDate(database_->databaseTime()),{"Référence","Article","Valeur consommée (DA)","Valeur cumulée","Références cumulées","Classe"},rows,"Repères usuels : la classe A concentre environ 60 % de la valeur, B mène le cumul vers 90 %, puis C regroupe le solde. Les seuils réels varient selon la courbe de consommation et la classification de chaque article; surveillez les références A de façon prioritaire."));
}

void App::printExpiring() {
    if(!session_.canPrint||!canViewStock()){showMessage("Votre profil ne peut pas imprimer le suivi des échéances.",true);return;}
    const auto lots=database_->expiringLots(30);std::vector<std::vector<std::string>> rows;for(const auto& r:lots)rows.push_back(r);
    showDocument(makePrintableReport("Suivi des échéances et lots échus","Lots échus et dates prévues dans les 30 prochains jours · édité le "+formatDate(database_->databaseTime()),{"Référence","Article","Entrepôt","Lot","Échéance","Quantité","Unité","Qualité"},rows,"Les lots dont la date d'échéance est passée sont signalés et indisponibles pour les sorties FIFO. Isolez les lots concernés et enregistrez un déclassement avec son motif si une destruction ou une sortie physique est nécessaire."));
}

void App::exportInventory() {
    if(!session_.canPrint||!canViewStock()){showMessage("Votre profil ne peut pas exporter l'inventaire.",true);return;}
    const bool costs=session_.admin||session_.canViewCosts;auto rows=database_->inventoryRows(costs);auto headers=std::vector<std::string>{"Référence","Article","Typologie de stock","Politique de flux","Entrepôt","Emplacement","Lot","Entré le","Échéance","Quantité","Unité","Statut qualité","Note de contrôle"};if(costs)headers.push_back("Coût unitaire (DA)");
    showMessage("Fichier CSV créé :\n"+pathToUtf8(exportCsv("inventaire",headers,rows)));
}

void App::exportMovements() {
    if(!session_.canPrint||!canViewStock()){showMessage("Votre profil ne peut pas exporter les mouvements.",true);return;}
    const bool costs=session_.admin||session_.canViewCosts;std::vector<std::vector<std::string>> rows;for(const auto& m:database_->movements(1000000)){std::vector<std::string> r={m.createdAt,typeLabel(m.type),m.sku,m.product,csvNumber(m.quantity),m.warehouse,m.location,m.lotCode,m.qualityStatus,m.expiry,m.reference,m.reason,m.user};if(costs)r.push_back(csvNumber(m.unitCost));rows.push_back(std::move(r));}
    auto headers=std::vector<std::string>{"Date et heure","Opération","Référence","Article","Quantité","Entrepôt","Emplacement","Lot","Qualité","Échéance","Document","Motif","Opérateur"};if(costs)headers.push_back("Coût unitaire");
    showMessage("Fichier CSV créé :\n"+pathToUtf8(exportCsv("mouvements",headers,rows)));
}
