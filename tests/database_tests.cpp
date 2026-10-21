#include "barcode.hpp"
#include "database.hpp"
#include "i18n.hpp"
#include "path_utils.hpp"
#include "preferences.hpp"
#include "reports.hpp"
#include "security.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
int checks=0;
int failures=0;

void check(bool condition,const std::string& message) {
    ++checks;
    if(!condition){++failures;std::cerr<<"ÉCHEC : "<<message<<'\n';}
}

template<class Function>
void mustThrow(Function&& function,const std::string& message) {
    bool threw=false;
    try{function();}catch(const std::exception&){threw=true;}
    check(threw,message);
}

std::filesystem::path makeRoot() {
    const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
    auto root=std::filesystem::temp_directory_path()/("gestion-stock-tests-"+std::to_string(stamp));
    std::filesystem::create_directories(root);
    return root;
}

int countType(const std::vector<Movement>& records,const std::string& type) {
    int result=0;for(const auto& row:records)if(row.type==type)++result;return result;
}
}

int main() {
    const auto root=makeRoot();
    try {
        const auto config=root/"config";std::filesystem::create_directories(config);
        const auto documents=root/"Documents";
        g_setenv("XDG_CONFIG_HOME",config.string().c_str(),TRUE);
        std::ofstream(config/"user-dirs.dirs")<<"XDG_DOCUMENTS_DIR=\""<<documents.string()<<"\"\n";

        const std::string unicodePath="Données locales";
        check(pathToUtf8(pathFromUtf8(unicodePath))==unicodePath,"préserve les chemins UTF-8 entre les sélecteurs GTK, SQLite et le système de fichiers");
        check(!passwordMeetsMinimum("1234567"),"refuse les mots de passe de moins de huit caractères");
        check(passwordMeetsMinimum("12345678"),"accepte un mot de passe ASCII d'au moins huit caractères");
        check(passwordMeetsMinimum("éééééééé"),"compte correctement les caractères UTF-8 du mot de passe");
        const auto passwordHash=hashPassword("mot-de-passe-test");
        check(passwordHash.find("pbkdf2-sha256$120000$")==0,"stocke la version et le nombre d'itérations du dérivé");
        check(verifyPassword("mot-de-passe-test",passwordHash),"vérifie le mot de passe correct");
        check(!verifyPassword("mauvais-mot-de-passe",passwordHash),"rejette un mot de passe incorrect");
        check(!verifyPassword("mot-de-passe-test","pbkdf2-sha256$120000$00zz$00"),"rejette un hash mal formé");

        {
            const auto activeDatabase=root/pathFromUtf8("Données locales")/"gestion.sqlite3";
            Database database(activeDatabase);
            check(database.userCount()==0,"initialise une base sans compte");
            const auto stores=database.warehouses();
            check(stores.size()==1&&stores[0].name=="Entrepôt principal","crée l'entrepôt par défaut");

            mustThrow([&]{database.createFirstAdmin("Admin","admin","court");},"refuse un mot de passe administrateur trop court");
            database.createFirstAdmin("Administratrice","admin","mot-de-passe-admin");
            const auto adminLogin=database.authenticate("ADMIN","mot-de-passe-admin");
            check(adminLogin&&adminLogin->admin,"authentifie l'administrateur sans tenir compte de la casse de l'identifiant");
            check(!database.authenticate("admin","faux-mot-de-passe"),"refuse un mauvais mot de passe");
            const auto admin=database.users().front();

            User worker;worker.username="magasinier";worker.fullName="Équipe magasin";
            check(!worker.canViewStock&&!worker.canManageMovements&&!worker.canPrint,"les droits par défaut d'un nouveau profil sont refusés");
            database.createWorker(worker,"mot-de-passe-worker",admin.id);
            mustThrow([&]{User duplicate=worker;duplicate.username="MAGASINIER";database.createWorker(duplicate,"mot-de-passe-worker",admin.id);},"refuse un identifiant de profil existant sans message SQLite brut");
            auto users=database.users();
            const auto workerIt=std::find_if(users.begin(),users.end(),[](const User& user){return user.username=="magasinier";});
            check(workerIt!=users.end()&&!workerIt->admin&&!workerIt->canViewStock&&!workerIt->canManageMovements,"crée un profil de travail avec des droits restreints");
            const int workerId=workerIt==users.end()?0:workerIt->id;
            if(workerId){
                User changed=*workerIt;changed.canViewStock=true;changed.canManageMovements=true;database.updateWorker(changed,admin.id);
                database.resetPassword(workerId,"nouveau-mot-de-passe",admin.id);
                check(database.authenticate("magasinier","nouveau-mot-de-passe").has_value(),"réinitialise le mot de passe du profil");
                database.setUserActive(workerId,false,admin.id);
                check(!database.authenticate("magasinier","nouveau-mot-de-passe"),"bloque la connexion d'un profil désactivé");
                database.setUserActive(workerId,true,admin.id);
                check(database.authenticate("magasinier","nouveau-mot-de-passe").has_value(),"réactive le profil de travail");
            }

            User adminAccount=admin;adminAccount.username="proprietaire";adminAccount.fullName="Propriétaire du stock";
            database.updateAdminAccount(adminAccount,"nouveau-mot-de-passe-admin",admin.id);
            check(!database.authenticate("admin","mot-de-passe-admin").has_value(),"refuse l'ancien identifiant après modification du compte administrateur");
            const auto renamedAdmin=database.authenticate("PROPRIETAIRE","nouveau-mot-de-passe-admin");
            check(renamedAdmin&&renamedAdmin->admin&&renamedAdmin->fullName=="Propriétaire du stock","modifie l'identifiant, le nom et le mot de passe de l'administrateur");
            adminAccount.fullName="Propriétaire renommé";
            database.updateAdminAccount(adminAccount,"",admin.id);
            check(database.authenticate("proprietaire","nouveau-mot-de-passe-admin").has_value(),"conserve le mot de passe quand le champ est laissé vide");
            mustThrow([&]{User duplicate=adminAccount;duplicate.username="MAGASINIER";database.updateAdminAccount(duplicate,"",admin.id);},"refuse un identifiant d'administrateur déjà utilisé sans tenir compte de la casse");
            mustThrow([&]{User shortPassword=adminAccount;database.updateAdminAccount(shortPassword,"court",admin.id);},"refuse un mot de passe administrateur trop court");
            if(workerId)mustThrow([&]{database.updateAdminAccount(adminAccount,"",workerId);},"interdit à un profil de travail de modifier le compte administrateur");

            Supplier supplier;supplier.name="Fournisseur Nord";supplier.administrativeDays=1;supplier.preparationDays=2;supplier.transportDays=2;supplier.inspectionDays=1;database.saveSupplier(supplier,admin.id);
            mustThrow([&]{Supplier duplicate=supplier;duplicate.id=0;duplicate.name="fournisseur nord";database.saveSupplier(duplicate,admin.id);},"refuse un fournisseur en double indépendamment de la casse");
            const auto suppliers=database.suppliers();
            check(suppliers.size()==1&&suppliers[0].leadDays==6&&suppliers[0].administrativeDays==1&&suppliers[0].preparationDays==2&&suppliers[0].transportDays==2&&suppliers[0].inspectionDays==1,"enregistre et additionne les étapes administratives, fournisseur, transport et contrôle");

            Product product;product.sku="MAT-001";product.name="Matière première";product.category="Production";product.unit="kg";
            product.barcode="EAN-MAT-001";product.stockType="Production";product.flowPolicy="Flux tirés · demande réelle";product.minStock=5;product.safetyStock=2;product.maxStock=100;product.leadDays=6;product.coverageDays=14;product.unitCost=12.5;product.holdingRate=18;product.orderCost=40;product.volumeM3=0.02;product.perishable=true;
            database.saveProduct(product,admin.id);product=database.products().front();
            check(product.id>0&&product.barcode=="EAN-MAT-001"&&product.stockType=="Production"&&product.flowPolicy=="Flux tirés · demande réelle"&&product.coverageDays==14,"crée un article et conserve son code-barres, sa typologie, sa politique de flux et son horizon de couverture");
            const auto noConsumptionAbc=database.abcAnalysis();check(noConsumptionAbc.size()==1&&noConsumptionAbc.front().category=='C'&&noConsumptionAbc.front().cumulativeShare==0,"classe en C les articles lorsque la valeur consommée est nulle");
            mustThrow([&]{Product invalid=product;invalid.id=0;invalid.sku="COVER-BAD";invalid.coverageDays=-1;database.saveProduct(invalid,admin.id);},"refuse un horizon de couverture négatif");
            mustThrow([&]{Product duplicate=product;duplicate.id=0;database.saveProduct(duplicate,admin.id);},"refuse une référence SKU en double");
            mustThrow([&]{Product duplicate=product;duplicate.id=0;duplicate.sku="MAT-002";database.saveProduct(duplicate,admin.id);},"refuse un code-barres déjà attribué à un autre article");
            mustThrow([&]{Product invalid=product;invalid.id=0;invalid.unitCost=std::numeric_limits<double>::quiet_NaN();database.saveProduct(invalid,admin.id);},"refuse un coût NaN");

            mustThrow([&]{database.addMovement(product.id,"IN",2,12.5,stores[0].id,"","LOT-MISSING","","À contrôler","","REF","",admin.id);},"exige une échéance pour la réception d'un article périssable");
            mustThrow([&]{database.addMovement(product.id,"IN",2,12.5,stores[0].id,"","LOT-BAD","2026-02-30","À contrôler","","REF","",admin.id);},"refuse une date calendaire impossible");
            mustThrow([&]{database.addMovement(product.id,"INCONNU",2,12.5,stores[0].id,"","LOT-BAD","2030-01-01","À contrôler","","REF","",admin.id);},"refuse un type de mouvement inconnu");
            mustThrow([&]{database.addMovement(product.id,"IN",1,12.5,stores[0].id,"","LOT-BLOCKED","2030-01-01","BLOQUÉ","","REF","",admin.id);},"exige un motif explicite à la réception d'un lot bloqué");

            Warehouse limited;limited.name="Zone à capacité limitée";limited.capacityM3=1.1;database.saveWarehouse(limited,admin.id);
            mustThrow([&]{Warehouse duplicate=limited;duplicate.id=0;duplicate.name="zone à capacité limitée";database.saveWarehouse(duplicate,admin.id);},"refuse un entrepôt en double indépendamment de la casse");
            const auto limitedRows=database.warehouses();const auto limitedIt=std::find_if(limitedRows.begin(),limitedRows.end(),[](const Warehouse& w){return w.name=="Zone à capacité limitée";});
            check(limitedIt!=limitedRows.end(),"crée un entrepôt avec une capacité mesurable");
            Product bulky;bulky.sku="VOL-001";bulky.name="Article volumineux";bulky.unit="palette";bulky.volumeM3=0.5;database.saveProduct(bulky,admin.id);
            const auto bulkyRows=database.products();const auto bulkyIt=std::find_if(bulkyRows.begin(),bulkyRows.end(),[](const Product& p){return p.sku=="VOL-001";});
            check(bulkyIt!=bulkyRows.end(),"ajoute un article destiné au test de capacité");
            if(bulkyIt!=bulkyRows.end()&&limitedIt!=limitedRows.end()){
                bulky=*bulkyIt;const auto limitedStore=*limitedIt;database.addMovement(bulky.id,"IN",2,4,limitedStore.id,"L-01","PALETTE-01","","CONFORME","","CAP-1","",admin.id);
                Product enlarged=bulky;enlarged.volumeM3=1.1;mustThrow([&]{database.saveProduct(enlarged,admin.id);},"empêche une modification de volume de dépasser la capacité physique de l'entrepôt");
                Warehouse tooSmall=limitedStore;tooSmall.capacityM3=0.4;mustThrow([&]{database.saveWarehouse(tooSmall,admin.id);},"empêche de réduire la capacité sous le volume réellement occupé");
                const auto afterCapacityCheck=database.warehouses();const auto afterLimited=std::find_if(afterCapacityCheck.begin(),afterCapacityCheck.end(),[&](const Warehouse& w){return w.id==limitedStore.id;});
                check(afterLimited!=afterCapacityCheck.end()&&std::abs(afterLimited->usedM3-1.0)<1e-9,"calcule l'occupation par unité et conserve la capacité après un refus");
                const int cleanOutbound=database.addMovement(bulky.id,"OUT",1,0,limitedStore.id,"","SAISIE-IGNORÉE","2035-02-20","","","CAP-2","Sortie test",admin.id);
                const auto cleanRows=database.movements();const auto cleanIt=std::find_if(cleanRows.begin(),cleanRows.end(),[cleanOutbound](const Movement& m){return m.id==cleanOutbound;});
                check(cleanIt!=cleanRows.end()&&cleanIt->lotCode=="PALETTE-01"&&cleanIt->expiry.empty(),"n'enregistre pas les anciennes valeurs d'entrée cachées dans une sortie");
                const auto foundMovement=database.movementById(cleanOutbound);
                check(foundMovement&&foundMovement->id==cleanOutbound&&foundMovement->lotCode=="PALETTE-01","retrouve directement un mouvement par son identifiant");
                check(!database.movementById(-1),"renvoie une absence pour un identifiant de mouvement inconnu");
                database.setAppSetting("test.preference","valeur-locale");
                check(database.appSetting("test.preference")=="valeur-locale"&&database.appSetting("test.absente","défaut")=="défaut","enregistre et relit les réglages persistants de l'application");
                Warehouse tiny;tiny.name="Quarantaine exiguë";tiny.capacityM3=0.1;database.saveWarehouse(tiny,admin.id);const auto warehousesAfterTiny=database.warehouses();const auto tinyIt=std::find_if(warehousesAfterTiny.begin(),warehousesAfterTiny.end(),[](const Warehouse& w){return w.name=="Quarantaine exiguë";});
                if(tinyIt!=warehousesAfterTiny.end()){
                    const auto beforeTransfer=database.movements().size();mustThrow([&]{database.transferStock(bulky.id,0.4,limitedStore.id,tinyIt->id,"Q-01","","Test capacité",admin.id);},"refuse un transfert qui dépasserait la capacité de destination");check(database.movements().size()==beforeTransfer&&std::abs(database.physicalStock(bulky.id)-1)<1e-9,"annule intégralement un transfert refusé pour capacité insuffisante");
                    const auto transfer=database.transferStock(bulky.id,0.4,limitedStore.id,stores[0].id,"A-02","","Réorganisation",admin.id);const auto outLines=database.movementLines(transfer.first),inLines=database.movementLines(transfer.second);check(outLines.size()==1&&inLines.size()==1&&outLines[0][0]=="PALETTE-01"&&inLines[0][0]=="PALETTE-01"&&std::abs(std::stod(inLines[0][4])-0.4)<1e-9&&inLines[0][3]=="CONFORME"&&std::abs(std::stod(inLines[0][5])-4.0)<1e-9,"transfère les lots entre entrepôts en préservant code, quantité, qualité et coût FIFO");
                    const auto afterTransfer=database.warehouses();const auto sourceAfter=std::find_if(afterTransfer.begin(),afterTransfer.end(),[&](const Warehouse& w){return w.id==limitedStore.id;});check(sourceAfter!=afterTransfer.end()&&std::abs(sourceAfter->usedM3-0.3)<1e-9,"met à jour l'occupation de l'entrepôt après transfert interne");
                }
            }

            const int firstLot=database.addMovement(product.id,"IN",5,12.5,stores[0].id,"A-01","LOT-A","2030-01-01","CONFORME","Contrôlé","REC-1","",admin.id);
            (void)firstLot;
            database.addMovement(product.id,"IN",3,14.0,stores[0].id,"A-02","LOT-B","2031-01-01","CONFORME","","REC-2","",admin.id);
            check(std::abs(database.product(product.id).onHand-8)<1e-9,"compte les lots conformes et non échus comme disponibles");
            const int outbound=database.addMovement(product.id,"OUT",6,0,stores[0].id,"","","","","","SORT-1","Vente",admin.id);
            const auto fifoLines=database.movementLines(outbound);
            check(fifoLines.size()==2&&fifoLines[0][0]=="LOT-A"&&std::abs(std::stod(fifoLines[0][4])-5)<1e-9&&std::abs(std::stod(fifoLines[1][4])-1)<1e-9,"prélève les lots les plus anciens en premier avec une ventilation exacte");
            const auto movementRows=database.movements();const auto outboundRow=std::find_if(movementRows.begin(),movementRows.end(),[outbound](const Movement& m){return m.id==outbound;});
            check(outboundRow!=movementRows.end()&&std::abs(outboundRow->unitCost-12.75)<1e-9,"calcule le coût moyen réel des lots prélevés pour le journal");
            const auto suggestions=database.reorderSuggestions();const auto suggestion=std::find_if(suggestions.begin(),suggestions.end(),[&](const ReorderSuggestion& s){return s.product.id==product.id;});
            check(suggestion!=suggestions.end()&&std::abs(suggestion->dailyDemand-6.0/90.0)<1e-9&&std::abs(suggestion->reorderPoint-5)<1e-9&&std::abs(suggestion->suggestedQty-3)<1e-9,"calcule le point de commande et le réapprovisionnement JIT sur la demande récente");
            check(suggestion!=suggestions.end()&&suggestion->economicLot>0&&std::abs(suggestion->annualOrderCost-suggestion->annualHoldingCost)<1e-6,"calcule le lot économique de Wilson et équilibre les coûts annuels de commande et de possession");
            mustThrow([&]{database.createPurchaseOrder(product.id,suppliers[0].id,99,13,admin.id);},"refuse une commande dépassant le stock maximal configuré");
            const auto movementCount=database.movements().size();
            mustThrow([&]{database.addMovement(product.id,"OUT",5,0,stores[0].id,"","","","","","SORT-2","",admin.id);},"bloque une sortie supérieure au stock disponible");
            check(database.movements().size()==movementCount,"annule intégralement une sortie impossible");

            const int pendingQuality=database.addMovement(product.id,"IN",4,12.5,stores[0].id,"Q-01","LOT-Q","2030-01-01","BLOQUÉ","À tester","REC-3","",admin.id);
            check(pendingQuality>0,"journalise l'entrée d'un lot bloqué sans le rendre distribuable");
            const auto qualityRows=database.qualityLots();
            const auto qualityIt=std::find_if(qualityRows.begin(),qualityRows.end(),[](const auto& row){return row[5]=="LOT-Q";});
            check(qualityIt!=qualityRows.end(),"rend les lots en attente visibles pour le contrôle qualité");
            const auto lotId=qualityIt==qualityRows.end()?0:std::stoi((*qualityIt)[0]);
            const auto blockedReceipt=database.movementLines(pendingQuality);
            check(blockedReceipt.size()==1&&blockedReceipt[0][3]=="BLOQUÉ"&&blockedReceipt[0][7]=="À tester","conserve sur le bon de réception le statut et la note qualité d'origine");
            check(std::abs(database.product(product.id).onHand-2)<1e-9,"exclut les lots bloqués du stock disponible");
            if(lotId)mustThrow([&]{database.setLotQuality(lotId,"BLOQUÉ","",admin.id);},"exige un motif lorsqu'un contrôle qualité bloque un lot");
            if(lotId)mustThrow([&]{database.setLotQuality(lotId,"CONFORME","",admin.id);},"exige une note pour lever un blocage qualité");
            if(lotId){database.setLotQuality(lotId,"CONFORME","Contrôle effectué",admin.id);check(database.product(product.id).onHand>=5,"rend disponible un lot après contrôle conforme");}
            mustThrow([&]{database.addMovement(product.id,"WRITE_OFF",1,0,stores[0].id,"","LOT-Q","","","","DECL-1","",admin.id);},"exige un motif de déclassement");
            mustThrow([&]{database.addMovement(product.id,"WRITE_OFF",1,0,stores[0].id,"","","","","","DECL-1","Casse",admin.id);},"exige le code du lot à déclasser");
            const int writeOff=database.addMovement(product.id,"WRITE_OFF",1,0,stores[0].id,"","LOT-Q","","","","DECL-1","Casse constatée",admin.id);
            const auto writeOffLines=database.movementLines(writeOff);
            check(writeOffLines.size()==1&&writeOffLines[0][0]=="LOT-Q"&&std::abs(std::stod(writeOffLines[0][4])-1)<1e-9,"déclasse uniquement la quantité demandée du lot explicitement sélectionné");
            check(countType(database.movements(),"WRITE_OFF")==1,"conserve le déclassement dans le journal des mouvements");
            database.addMovement(product.id,"IN",2,12.5,stores[0].id,"Q-02","LOT-EXPIRE","2020-01-01","À contrôler","","REC-4","",admin.id);
            const auto expiring=database.expiringLots(30);check(std::any_of(expiring.begin(),expiring.end(),[](const auto& row){return row[3]=="LOT-EXPIRE";}),"signale également les lots déjà périmés dans le suivi des échéances");
            const auto lotsAfterExpired=database.qualityLots();
            const auto expiredIt=std::find_if(lotsAfterExpired.begin(),lotsAfterExpired.end(),[](const auto& row){return row[5]=="LOT-EXPIRE";});
            if(expiredIt!=lotsAfterExpired.end())mustThrow([&]{database.setLotQuality(std::stoi((*expiredIt)[0]),"CONFORME","",admin.id);},"empêche de déclarer conforme un lot périmé");

            const int orderId=database.createPurchaseOrder(product.id,suppliers[0].id,10,13.25,admin.id);
            auto orders=database.purchaseOrders();
            auto orderIt=std::find_if(orders.begin(),orders.end(),[orderId](const PurchaseOrder& o){return o.id==orderId;});
            check(orderIt!=orders.end()&&orderIt->status=="BROUILLON","crée une commande fournisseur en brouillon");
            check(std::abs(database.product(product.id).pendingInbound)<1e-9,"n'inclut pas les commandes en brouillon dans le stock en commande");
            check(database.purchaseOrderCount()==1&&database.purchaseOrdersPage(1,0).size()==1&&database.purchaseOrdersPage(1,1).empty(),"compte et pagine les commandes fournisseurs sans charger toute la liste");
            mustThrow([&]{database.receivePurchaseOrder(orderId,2,stores[0].id,"","ORDER-LOT","2030-01-01","CONFORME","",admin.id);},"exige l'envoi avant la réception d'une commande");
            database.markPurchaseOrderSent(orderId,admin.id);
            mustThrow([&]{database.markPurchaseOrderSent(orderId,admin.id);},"refuse de journaliser une deuxième fois l'envoi d'une commande");
            const int partial=database.receivePurchaseOrder(orderId,4,stores[0].id,"B-01","ORDER-LOT-A","2030-01-01","À contrôler","Livraison partielle",admin.id);
            (void)partial;
            orders=database.purchaseOrders();orderIt=std::find_if(orders.begin(),orders.end(),[orderId](const PurchaseOrder& o){return o.id==orderId;});
            check(orderIt!=orders.end()&&orderIt->status=="PARTIELLE"&&std::abs(orderIt->receivedQuantity-4)<1e-9,"suit une réception partielle et son reliquat");
            mustThrow([&]{database.cancelPurchaseOrder(orderId,admin.id);},"refuse d'annuler une commande déjà partiellement reçue");
            mustThrow([&]{database.receivePurchaseOrder(orderId,7,stores[0].id,"","ORDER-LOT-OVER","2030-01-01","CONFORME","",admin.id);},"refuse de recevoir plus que le reliquat commandé");
            database.receivePurchaseOrder(orderId,6,stores[0].id,"B-02","ORDER-LOT-B","2031-01-01","CONFORME","",admin.id);
            orders=database.purchaseOrders();orderIt=std::find_if(orders.begin(),orders.end(),[orderId](const PurchaseOrder& o){return o.id==orderId;});
            check(orderIt!=orders.end()&&orderIt->status=="REÇUE"&&std::abs(orderIt->receivedQuantity-10)<1e-9,"termine la commande après plusieurs réceptions");
            mustThrow([&]{database.cancelPurchaseOrder(orderId,admin.id);},"refuse d'annuler une commande déjà reçue");
            const int cancelledId=database.createPurchaseOrder(product.id,suppliers[0].id,1,13.25,admin.id);
            database.cancelPurchaseOrder(cancelledId,admin.id);
            orders=database.purchaseOrders();orderIt=std::find_if(orders.begin(),orders.end(),[cancelledId](const PurchaseOrder& o){return o.id==cancelledId;});
            check(orderIt!=orders.end()&&orderIt->status=="ANNULÉE","marque une commande non réceptionnée comme annulée");
            check(std::abs(database.product(product.id).pendingInbound)<1e-9,"retire une commande annulée du stock projeté");
            mustThrow([&]{database.cancelPurchaseOrder(cancelledId,admin.id);},"refuse d'annuler une commande qui l'est déjà");

            Product supplierLead;supplierLead.sku="LEAD-001";supplierLead.name="Article au délai fournisseur";supplierLead.unit="pièce";supplierLead.minStock=2;database.saveProduct(supplierLead,admin.id);const auto leadProducts=database.products();const auto leadIt=std::find_if(leadProducts.begin(),leadProducts.end(),[](const Product& p){return p.sku=="LEAD-001";});if(leadIt!=leadProducts.end()){database.createPurchaseOrder(leadIt->id,suppliers[0].id,1,5,admin.id);const auto leadSuggestions=database.reorderSuggestions();const auto leadSuggestion=std::find_if(leadSuggestions.begin(),leadSuggestions.end(),[&](const ReorderSuggestion& s){return s.product.id==leadIt->id;});check(leadSuggestion!=leadSuggestions.end()&&leadSuggestion->leadDays==6,"utilise la somme des étapes fournisseur si aucun délai particulier n'est défini sur l'article");}

            Product sales;sales.sku="CLIENT-001";sales.name="Article réservé client";sales.unit="pièce";sales.minStock=5;sales.safetyStock=1;sales.leadDays=3;sales.maxStock=100;sales.unitCost=4;sales.holdingRate=25;sales.orderCost=40;sales.shortageCost=50;sales.barcode="SCAN-001";sales.stockType="Hors production · MRO";sales.flowPolicy="Flux tendus · JIT";database.saveProduct(sales,admin.id);
            auto salesRows=database.products();const auto savedSales=std::find_if(salesRows.begin(),salesRows.end(),[](const Product& p){return p.sku=="CLIENT-001";});
            check(savedSales!=salesRows.end(),"crée un article logistique pour les commandes clients");
            if(savedSales!=salesRows.end()){
                sales=*savedSales;database.addMovement(sales.id,"IN",10,4,stores[0].id,"S-01","SALES-LOT","","CONFORME","","REC-SALES","",admin.id);
                mustThrow([&]{database.createCustomerOrder("Client test",sales.id,2,"2026-02-30","",admin.id);},"refuse une date souhaitée impossible sur une commande client");
                const int customerOrderId=database.createCustomerOrder("Client test",sales.id,8,"2026-11-01","Commande urgente",admin.id);
                auto projected=database.product(sales.id);check(std::abs(projected.onHand-10)<1e-9&&std::abs(projected.customerCommitments-8)<1e-9&&std::abs(projected.projectedStock-2)<1e-9,"calcule le stock net en déduisant les commandes clients non expédiées");
                const auto proposal=database.reorderSuggestions();const auto saleProposal=std::find_if(proposal.begin(),proposal.end(),[&](const ReorderSuggestion& row){return row.product.id==sales.id;});check(saleProposal!=proposal.end()&&std::abs(saleProposal->projectedQty-2)<1e-9&&std::abs(saleProposal->suggestedQty-3)<1e-9,"déclenche le réapprovisionnement sur le stock net projeté et non le seul stock physique");check(saleProposal!=proposal.end()&&saleProposal->economicLot==0,"n'affiche pas de lot économique sans consommation observée");
                const auto salesMovementCount=database.movements().size();mustThrow([&]{database.shipCustomerOrder(customerOrderId,11,stores[0].id,"",admin.id);},"refuse une expédition client supérieure au reliquat demandé");check(database.movements().size()==salesMovementCount&&std::abs(database.product(sales.id).onHand-10)<1e-9,"annule intégralement une expédition client refusée");
                database.shipCustomerOrder(customerOrderId,3,stores[0].id,"Quai 1",admin.id);auto clientRows=database.customerOrders();auto clientOrder=std::find_if(clientRows.begin(),clientRows.end(),[customerOrderId](const CustomerOrder& o){return o.id==customerOrderId;});check(clientOrder!=clientRows.end()&&clientOrder->status=="PARTIELLE"&&std::abs(clientOrder->shippedQuantity-3)<1e-9,"suit une livraison client partielle et son reliquat");
                database.shipCustomerOrder(customerOrderId,5,stores[0].id,"Quai 1",admin.id);clientRows=database.customerOrders();clientOrder=std::find_if(clientRows.begin(),clientRows.end(),[customerOrderId](const CustomerOrder& o){return o.id==customerOrderId;});check(clientOrder!=clientRows.end()&&clientOrder->status=="LIVRÉE"&&std::abs(database.product(sales.id).onHand-2)<1e-9&&std::abs(database.product(sales.id).customerCommitments)<1e-9,"termine la commande client par sorties FIFO et libère sa réservation");
                const int cancelled=database.createCustomerOrder("Client test",sales.id,1,"","Annulation",admin.id);database.cancelCustomerOrder(cancelled,admin.id);check(std::abs(database.product(sales.id).customerCommitments)<1e-9,"libère le stock projeté lors de l'annulation d'une commande client");
                database.addMovement(sales.id,"RETURN",1,4,stores[0].id,"Retours","RETOUR-01","","À contrôler","Retour client à inspecter","RET-1","Retour client",admin.id);check(countType(database.movements(),"RETURN")==1&&std::abs(database.product(sales.id).onHand-2)<1e-9,"journalise un retour client comme lot séparé en attente de contrôle qualité");
                const int backOrder=database.createCustomerOrder("Client en attente",sales.id,4,"","Rupture à quantifier",admin.id);const auto exposure=database.reorderSuggestions();const auto exposed=std::find_if(exposure.begin(),exposure.end(),[&](const ReorderSuggestion& s){return s.product.id==sales.id;});check(exposed!=exposure.end()&&exposed->shortageExposure>0&&std::abs(exposed->shortageExposure-100)<1e-9,"estime l'exposition aux coûts de pénurie renseignés quand les commandes dépassent le stock projeté");database.cancelCustomerOrder(backOrder,admin.id);
            }

            const auto inventory=database.inventoryRows(false);
            const auto productionRow=std::find_if(inventory.begin(),inventory.end(),[](const auto& row){return !row.empty()&&row[0]=="MAT-001";});check(productionRow!=inventory.end()&&productionRow->size()==13&&(*productionRow)[2]=="Production"&&(*productionRow)[3]=="Flux tirés · demande réelle","produit un inventaire détaillé avec typologie et politique de flux sans divulguer les coûts");
            Product zero;zero.sku="MAT-VIDE";zero.name="Article sans stock";zero.unit="unité";database.saveProduct(zero,admin.id);
            const auto inventoryWithZero=database.inventoryRows(false);
            check(std::any_of(inventoryWithZero.begin(),inventoryWithZero.end(),[](const auto& row){return row[0]=="MAT-VIDE"&&row[9]=="0.000000";}),"inclut les articles sans stock dans la feuille d'inventaire physique");
            const auto abcRows=database.abcAnalysis();check(abcRows.size()>=2,"calcule la classification ABC et inclut les articles sans sortie");check(!abcRows.empty()&&abcRows.back().category=='C'&&abcRows.back().cumulativeArticleShare>0.99,"classe en C les références à faible consommation et calcule le cumul du nombre de références");
            check(database.productCount()>=3&&database.productsPage("MAT-",1,0).size()==1,"compte et pagine les résultats d'articles sans charger toute la table dans l'interface");
            check(database.movementCount()==static_cast<int>(database.movements(1000000).size())&&!database.movements(1,0).empty(),"compte les mouvements et lit une page de résultats bornée");
            Product capped;capped.sku="MAX-001";capped.name="Article au-dessus du plafond";capped.unit="unité";capped.maxStock=1;database.saveProduct(capped,admin.id);
            const auto cappedProducts=database.products();const auto cappedIt=std::find_if(cappedProducts.begin(),cappedProducts.end(),[](const Product& p){return p.sku=="MAX-001";});
            check(cappedIt!=cappedProducts.end(),"ajoute un article avec un stock maximal configuré");
            if(cappedIt!=cappedProducts.end()){
                capped=*cappedIt;database.addMovement(capped.id,"IN",2,1,stores[0].id,"","MAX-LOT","","CONFORME","","REC-MAX","Réception physique excédentaire",admin.id);
                check(std::abs(database.physicalStock(capped.id)-2)<1e-9&&database.overStockCount()>=1,"conserve la quantité réelle et signale un dépassement du stock maximal");
                mustThrow([&]{database.createPurchaseOrder(capped.id,suppliers[0].id,0.1,1,admin.id);},"bloque un réapprovisionnement qui aggraverait un dépassement de stock maximal");
            }
            auto capacityStores=database.warehouses();const auto mainStore=std::find_if(capacityStores.begin(),capacityStores.end(),[&](const Warehouse& w){return w.id==stores[0].id;});if(mainStore!=capacityStores.end()){Warehouse cappedMain=*mainStore;cappedMain.capacityM3=std::max(0.01,cappedMain.usedM3);database.saveWarehouse(cappedMain,admin.id);}
            Product needsSpace;needsSpace.sku="SPACE-001";needsSpace.name="Article nécessitant de la place";needsSpace.unit="palette";needsSpace.minStock=2;needsSpace.volumeM3=1;database.saveProduct(needsSpace,admin.id);const auto spaceProducts=database.products();const auto spaceIt=std::find_if(spaceProducts.begin(),spaceProducts.end(),[](const Product& p){return p.sku=="SPACE-001";});if(spaceIt!=spaceProducts.end()){const auto spaceSuggestions=database.reorderSuggestions();const auto spaceSuggestion=std::find_if(spaceSuggestions.begin(),spaceSuggestions.end(),[&](const ReorderSuggestion& s){return s.product.id==spaceIt->id;});check(spaceSuggestion!=spaceSuggestions.end()&&spaceSuggestion->capacityLimited&&spaceSuggestion->suggestedQty<2,"borne le réapprovisionnement selon le volume libre connu et signale le manque d'espace");}
            check(!database.auditLog(200).empty(),"journalise les changements de compte et les opérations de stock");
            Product coverage;coverage.sku="COVER-001";coverage.name="Article à couverture réglable";coverage.unit="unité";coverage.minStock=1;coverage.leadDays=1;coverage.coverageDays=10;database.saveProduct(coverage,admin.id);const auto coverageProducts=database.products();const auto coverageIt=std::find_if(coverageProducts.begin(),coverageProducts.end(),[](const Product& p){return p.sku=="COVER-001";});if(coverageIt!=coverageProducts.end()){coverage=*coverageIt;database.addMovement(coverage.id,"IN",20,2,stores[0].id,"C-01","COVER-LOT","","CONFORME","","COVER-IN","",admin.id);database.addMovement(coverage.id,"OUT",20,0,stores[0].id,"","","","","","COVER-OUT","Consommation test",admin.id);auto coverageSuggestions=database.reorderSuggestions();auto coverageSuggestion=std::find_if(coverageSuggestions.begin(),coverageSuggestions.end(),[&](const ReorderSuggestion& s){return s.product.id==coverage.id;});const double dailyCoverage=20.0/90.0;check(coverageSuggestion!=coverageSuggestions.end()&&std::abs(coverageSuggestion->targetStock-dailyCoverage*11)<1e-9,"calcule la cible sur l'horizon de couverture propre à l'article");coverage.coverageDays=40;database.saveProduct(coverage,admin.id);coverageSuggestions=database.reorderSuggestions();coverageSuggestion=std::find_if(coverageSuggestions.begin(),coverageSuggestions.end(),[&](const ReorderSuggestion& s){return s.product.id==coverage.id;});check(coverageSuggestion!=coverageSuggestions.end()&&std::abs(coverageSuggestion->targetStock-dailyCoverage*41)<1e-9,"recalcule la cible après modification de l'horizon de couverture");}

            Customer client;client.name="Client Test";client.contact="Service achats";client.discountRate=5;client.paymentTerms="30 jours";database.saveCustomer(client,admin.id);
            const auto clientRows=database.customers("client test");
            check(clientRows.size()==1&&clientRows[0].code=="CLI-0001"&&clientRows[0].discountRate==5&&clientRows[0].active,"crée un client, génère son code et le retrouve par recherche");
            const int clientId=clientRows.empty()?0:clientRows[0].id;
            check(clientId>0&&database.customer(clientId).paymentTerms=="30 jours","relit la fiche client complète");
            mustThrow([&]{Customer duplicate=client;duplicate.id=0;database.saveCustomer(duplicate,admin.id);},"refuse deux clients portant le même nom sans tenir compte de la casse");
            mustThrow([&]{Customer invalid=client;invalid.id=0;invalid.name="Remise invalide";invalid.discountRate=120;database.saveCustomer(invalid,admin.id);},"refuse une remise client supérieure à 100 %");
            Product sale;sale.sku="SALE-001";sale.name="Article de vente";sale.unit="pièce";sale.unitCost=10;sale.salePrice=15;sale.perishable=true;database.saveProduct(sale,admin.id);
            {const auto saleRows=database.products();const auto saleIt=std::find_if(saleRows.begin(),saleRows.end(),[](const Product& p){return p.sku=="SALE-001";});check(saleIt!=saleRows.end(),"retrouve l'article de vente créé");if(saleIt!=saleRows.end())sale=*saleIt;}
            check(sale.salePrice==15,"conserve le prix de vente catalogue de l'article");
            database.addMovement(sale.id,"IN",20,10,stores[0].id,"V-01","SALE-LOT","2030-01-01","CONFORME","","SALE-IN","",admin.id);
            check(database.customerPrice(clientId,sale.id,sale.salePrice)==15,"utilise le prix catalogue lorsqu'aucun tarif client n'existe");
            database.setCustomerPrice(clientId,sale.id,12,admin.id);
            check(database.customerPrice(clientId,sale.id,sale.salePrice)==12,"applique le tarif négocié propre au client");
            database.setCustomerPrice(clientId,sale.id,0,admin.id);
            check(database.customerPrice(clientId,sale.id,sale.salePrice)==15,"supprime le tarif client lorsque le prix est remis à zéro");
            database.setCustomerPrice(clientId,sale.id,12,admin.id);

            const int proforma=database.createSalesDocument("PROFORMA",clientId,"",stores[0].id,"2026-03-01","Devis client",19,0,{{sale.id,3,12,5,19,""}},0,admin.id);
            const auto proformaDoc=database.salesDocument(proforma);
            check(proformaDoc.reference.rfind("PRO-20260301-",0)==0&&proformaDoc.status=="BROUILLON"&&proformaDoc.lineCount==1,"génère une proforma avec référence, statut brouillon et lignes");
            check(std::abs(proformaDoc.subtotal-36)<1e-9&&std::abs(proformaDoc.discountTotal-1.8)<1e-9&&std::abs(proformaDoc.taxTotal-6.498)<1e-9&&std::abs(proformaDoc.total-40.698)<1e-9,"calcule sous-total, remise, TVA et total d'un document de vente");
            check(std::abs(database.physicalStock(sale.id)-20)<1e-9,"ne modifie pas le stock pour une proforma");

            const auto saleLine=database.salesLines(proforma);
            check(saleLine.size()==1&&saleLine[0].sku=="SALE-001"&&std::abs(saleLine[0].unitCost-10)<1e-9&&saleLine[0].expiry.empty(),"conserve le coût réel et les détails de ligne du document");
            const int delivery=database.createSalesDocument("BL",clientId,"",stores[0].id,"2026-03-02","",19,0,{{sale.id,2,12,0,19,""}},0,admin.id);
            check(database.salesDocument(delivery).status=="LIVRÉ"&&std::abs(database.physicalStock(sale.id)-18)<1e-9&&countType(database.movements(),"OUT")>=1,"décrémente le stock à la validation d'un bon de livraison");
            const int deliveryInvoice=database.invoiceFromDelivery(delivery,admin.id);
            const auto invoiceDoc=database.salesDocument(deliveryInvoice);
            check(invoiceDoc.type=="INVOICE"&&invoiceDoc.sourceDocumentId==delivery&&invoiceDoc.status=="VALIDÉ"&&invoiceDoc.customerId==clientId&&std::abs(database.physicalStock(sale.id)-18)<1e-9,"facture un bon de livraison sans sortir une seconde fois le stock");
            mustThrow([&]{database.invoiceFromDelivery(proforma,admin.id);},"refuse de facturer un document qui n'est pas un bon de livraison");
            mustThrow([&]{database.createSalesDocument("INCONNU",clientId,"",stores[0].id,"","",0,0,{{sale.id,1,12,0,0,""}},0,admin.id);},"refuse un type de document de vente inconnu");
            mustThrow([&]{database.createSalesDocument("BL",clientId,"",stores[0].id,"","",0,0,{{sale.id,0,12,0,0,""}},0,admin.id);},"refuse une ligne de vente à quantité nulle");
            mustThrow([&]{database.createSalesDocument("RETURN",clientId,"",stores[0].id,"","",0,0,{{sale.id,1,12,0,0,""}},0,admin.id);},"exige une échéance pour le retour d'un article périssable");
            const int counter=database.createSalesDocument("COUNTER",clientId,"",stores[0].id,"2026-03-03","Vente comptoir",19,0,{{sale.id,1,15,0,19,""}},0,admin.id);
            check(database.salesDocument(counter).status=="VALIDÉ"&&std::abs(database.physicalStock(sale.id)-17)<1e-9,"valide une vente comptoir et sort le stock");
            const int directInvoice=database.createSalesDocument("INVOICE",clientId,"",stores[0].id,"2026-03-04","",19,0,{{sale.id,1,15,0,19,""}},0,admin.id);
            check(database.salesDocument(directInvoice).status=="VALIDÉ"&&std::abs(database.physicalStock(sale.id)-16)<1e-9,"valide une facture directe et sort le stock");
            const int returned=database.createSalesDocument("RETURN",clientId,"",stores[0].id,"2026-03-05","Retour",19,0,{{sale.id,1,12,0,19,"2030-01-01"}},0,admin.id);
            const auto returnLines=database.salesLines(returned);
            check(database.salesDocument(returned).status=="VALIDÉ"&&std::abs(database.physicalStock(sale.id)-17)<1e-9&&returnLines.size()==1&&returnLines[0].expiry=="2030-01-01","enregistre un retour client dans un lot séparé en attente de contrôle");
            check(database.salesDocumentCount("BL")==1&&database.salesDocuments(10,0,"BL").size()==1,"filtre et compte les documents de vente par type");
            database.setCustomerActive(clientId,false,admin.id);
            check(database.customers().size()==0,"masque les clients archivés de la liste active");
            mustThrow([&]{database.createSalesDocument("BL",clientId,"",stores[0].id,"","",0,0,{{sale.id,1,12,0,0,""}},0,admin.id);},"refuse un document pour un client archivé");
            database.setCustomerActive(clientId,true,admin.id);
            check(database.customers().size()==1,"réactive un client archivé");

            Product dormant;dormant.sku="DEAD-001";dormant.name="Article dormant";dormant.unit="unité";dormant.category="Divers";dormant.unitCost=4;database.saveProduct(dormant,admin.id);
            {const auto dormantRows=database.products();const auto dormantIt=std::find_if(dormantRows.begin(),dormantRows.end(),[](const Product& p){return p.sku=="DEAD-001";});check(dormantIt!=dormantRows.end(),"retrouve l'article dormant créé");if(dormantIt!=dormantRows.end())dormant=*dormantIt;}database.addMovement(dormant.id,"IN",5,4,stores[0].id,"","DEAD-LOT","","CONFORME","","DEAD-IN","",admin.id);
            const auto dead=database.deadStockRows(1);
            check(std::any_of(dead.begin(),dead.end(),[&](const DeadStockRow& row){return row.sku=="DEAD-001"&&row.quantity==5&&std::abs(row.value-20)<1e-9&&row.lastOut.empty();}),"signale le stock sans sortie comme dormant avec sa valeur");
            const auto flows=database.flowStats(90);
            const auto flow=std::find_if(flows.begin(),flows.end(),[&](const FlowStatRow& row){return row.sku=="SALE-001";});
            check(flow!=flows.end()&&std::abs(flow->inbound-21)<1e-9&&std::abs(flow->outbound-4)<1e-9&&std::abs(flow->currentQuantity-17)<1e-9&&std::abs(flow->averageDailyOut-4.0/90.0)<1e-9,"calcule les entrées, sorties, stock et rotation sur la période");
            const auto deadFlow=std::find_if(flows.begin(),flows.end(),[&](const FlowStatRow& row){return row.sku=="DEAD-001";});
            check(deadFlow!=flows.end()&&deadFlow->status=="DORMANT","marque un article sans sortie comme dormant dans les statistiques de flux");
            const auto pmp=database.pmpValuation();
            const auto salePmp=std::find_if(pmp.begin(),pmp.end(),[&](const PmpRow& row){return row.sku=="SALE-001";});
            check(salePmp!=pmp.end()&&std::abs(salePmp->quantity-17)<1e-9&&std::abs(salePmp->value-170)<1e-9&&std::abs(salePmp->pmp-10)<1e-9&&std::abs(salePmp->variance)<1e-9,"valorise le stock au PMP et compare au coût de la fiche");
            check(database.expiringLotCount(3650)>=1,"compte les lots à échéance dans la fenêtre demandée");
            const int anonymousCounter=database.createSalesDocument("COUNTER",0,"",stores[0].id,"2026-03-06","Vente sans compte client",19,0,{{sale.id,1,15,0,19,""}},0,admin.id);
            check(database.salesDocument(anonymousCounter).customerName=="Client de passage"&&std::abs(database.physicalStock(sale.id)-16)<1e-9,"autorise une vente comptoir sans client identifié");
            mustThrow([&]{database.createSalesDocument("INVOICE",0,"",stores[0].id,"2026-03-06","",19,0,{{sale.id,1,15,0,19,""}},0,admin.id);},"exige toujours un client pour une facture");

            const std::string barcode=barcodeSvg("SALE-001",48,true);
            check(barcode.find("<svg")==0&&barcode.find("aria-label=\"SALE-001\"")!=std::string::npos&&barcode.find(">SALE-001</text>")!=std::string::npos,"génère un code-barres Code 128 SVG lisible avec son libellé");
            check(barcodeSvg("").empty(),"renvoie un SVG vide pour une valeur de code-barres vide");
            mustThrow([&]{barcodeSvg("é");},"refuse les caractères non ASCII dans un code-barres Code 128B");

            const auto backup=root/"backup.sqlite3";database.backupTo(backup);
            mustThrow([&]{database.backupTo(activeDatabase);},"refuse d'écraser la base active lors d'une sauvegarde");
            Product extra;extra.sku="RESTORE-TEST";extra.name="À retirer après restauration";extra.unit="unité";database.saveProduct(extra,admin.id);
            database.restoreFrom(backup);
            const auto restoredProducts=database.products();
            check(std::none_of(restoredProducts.begin(),restoredProducts.end(),[](const Product& p){return p.sku=="RESTORE-TEST";}),"restaure une sauvegarde cohérente");
            mustThrow([&]{database.restoreFrom(activeDatabase);},"refuse la restauration depuis la base active elle-même");
            const auto malformed=root/"invalid.sqlite3";std::ofstream(malformed)<<"pas une base SQLite";
            const int preservedUsers=database.userCount();
            mustThrow([&]{database.restoreFrom(malformed);},"refuse une sauvegarde qui n'est pas une base de l'application");
            check(database.userCount()==preservedUsers&&database.products().size()>=2,"préserve la base actuelle lorsqu'une sauvegarde invalide est choisie");
        }

        {
            const auto legacyPath=root/"legacy.sqlite3";sqlite3* legacyRaw=nullptr;check(sqlite3_open(legacyPath.string().c_str(),&legacyRaw)==SQLITE_OK,"ouvre la base de test créée avec l'ancien schéma");
            const char* legacySql="CREATE TABLE products(id INTEGER PRIMARY KEY,sku TEXT NOT NULL UNIQUE COLLATE NOCASE,name TEXT NOT NULL,category TEXT NOT NULL DEFAULT '',unit TEXT NOT NULL DEFAULT 'unité',description TEXT NOT NULL DEFAULT '',min_stock REAL NOT NULL DEFAULT 0,safety_stock REAL NOT NULL DEFAULT 0,max_stock REAL NOT NULL DEFAULT 0,lead_days INTEGER NOT NULL DEFAULT 0,unit_cost REAL NOT NULL DEFAULT 0,holding_rate REAL NOT NULL DEFAULT 0,volume_m3 REAL NOT NULL DEFAULT 0,perishable INTEGER NOT NULL DEFAULT 0,active INTEGER NOT NULL DEFAULT 1,created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));INSERT INTO products(sku,name,unit) VALUES('LEGACY-001','Article historique','pièce');CREATE TABLE suppliers(id INTEGER PRIMARY KEY,name TEXT NOT NULL UNIQUE COLLATE NOCASE,contact TEXT NOT NULL DEFAULT '',phone TEXT NOT NULL DEFAULT '',email TEXT NOT NULL DEFAULT '',lead_days INTEGER NOT NULL DEFAULT 0,created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));INSERT INTO suppliers(name,lead_days) VALUES('Fournisseur historique',6);";
            char* legacyError=nullptr;if(legacyRaw){const int legacyResult=sqlite3_exec(legacyRaw,legacySql,nullptr,nullptr,&legacyError);check(legacyResult==SQLITE_OK,"prépare une base antérieure dépourvue des nouveaux champs de logistique");if(legacyError)sqlite3_free(legacyError);sqlite3_close(legacyRaw);legacyRaw=nullptr;}
            {Database migrated(legacyPath);const auto migratedProducts=migrated.products();const auto migratedSuppliers=migrated.suppliers();check(migratedProducts.size()==1&&migratedProducts[0].sku=="LEGACY-001"&&migratedProducts[0].barcode.empty()&&migratedProducts[0].coverageDays==30,"migre une ancienne base sans perdre son article et applique les valeurs logistiques par défaut");check(migratedSuppliers.size()==1&&migratedSuppliers[0].leadDays==6&&migratedSuppliers[0].preparationDays==6,"migre l'ancien délai fournisseur global sans en perdre la durée");}
        }

        Database preferenceDatabase(root/"preferences.sqlite3");
        preferenceDatabase.setAppSetting("ui.theme","dark");preferenceDatabase.setAppSetting("ui.accent","violet");preferenceDatabase.setAppSetting("ui.language","en");preferenceDatabase.setAppSetting("ui.page_size","120");preferenceDatabase.setAppSetting("ui.currency","EUR");preferenceDatabase.setAppSetting("ui.date_format","iso");preferenceDatabase.setAppSetting("print.signatures","0");
        preferenceDatabase.setAppSetting("ui.remember_last_user","1");preferenceDatabase.setAppSetting("ui.last_user","admin");preferenceDatabase.setAppSetting("ui.startup_page","4");
        preferenceDatabase.setAppSetting("ui.font_scale","115");preferenceDatabase.setAppSetting("ui.color.background","#123456");preferenceDatabase.setAppSetting("company.name","Entreprise Test");
        preferenceDatabase.setAppSetting("sales.invoice_prefix","FAC");preferenceDatabase.setAppSetting("inventory.dlc_days","45");preferenceDatabase.setAppSetting("labels.width_mm","60");
        loadPreferences(preferenceDatabase);
        check(preferences().theme==ThemeMode::Dark&&preferences().accent==AccentColor::Violet&&preferences().language==Language::English&&preferences().pageSize==120&&preferences().currency=="EUR"&&preferences().dateFormat=="iso"&&!preferences().signatures,"charge les préférences d'apparence, de langue et d'impression depuis la base");
        check(preferences().rememberLastUser&&preferences().lastUser=="admin"&&preferences().startupPage==4&&preferences().fontScale==115&&preferences().colors.background=="#123456"&&preferences().companyName=="Entreprise Test"&&preferences().invoicePrefix=="FAC"&&preferences().dlcAlertDays==45&&preferences().labelWidthMm==60,"charge les préférences de session, d'entreprise, de ventes, d'alertes et de couleurs");
        preferenceDatabase.setAppSetting("ui.page_size","9999");loadPreferences(preferenceDatabase);
        check(preferences().pageSize==500,"borne la pagination à 500 lignes");
        preferenceDatabase.setAppSetting("ui.page_size","1");loadPreferences(preferenceDatabase);
        check(preferences().pageSize==10,"borne la pagination à 10 lignes minimum");
        preferences().pageSize=240;preferences().theme=ThemeMode::Light;savePreferences(preferenceDatabase);loadPreferences(preferenceDatabase);
        check(preferences().pageSize==240&&preferences().theme==ThemeMode::Light,"enregistre puis restaure les préférences modifiées");
        preferenceDatabase.setAppSetting("ui.font_scale","5");loadPreferences(preferenceDatabase);check(preferences().fontScale==85,"borne la taille du texte à 85 % minimum");
        preferenceDatabase.setAppSetting("ui.font_scale","500");loadPreferences(preferenceDatabase);check(preferences().fontScale==130,"borne la taille du texte à 130 % maximum");
        preferenceDatabase.setAppSetting("ui.color.background","pas-une-couleur");loadPreferences(preferenceDatabase);check(preferences().colors.background.empty(),"ignore une couleur de thème invalide");
        check(languageKey(Language::Spanish)=="es"&&languageFromKey("de")==Language::German&&languageFromKey("inconnu")==Language::French,"convertit les langues depuis et vers leurs clés persistées");
        setLanguage(Language::English);check(tr("Vente comptoir")=="Counter sale"&&tr("Validé")=="Validated"&&tr("TVA")=="VAT"&&tr("Enregistrer le document")=="Save document","traduit les types, statuts et libellés des documents de vente en anglais");
        setLanguage(Language::Spanish);check(tr("Facture")=="Factura"&&tr("Livré")=="Entregado"&&tr("Retirer")=="Quitar","traduit les libellés des documents de vente en espagnol");
        setLanguage(Language::German);check(tr("Bon de livraison")=="Lieferschein"&&tr("Annulé")=="Storniert"&&tr("＋ Ajouter")=="＋ Hinzufügen","traduit les libellés des documents de vente en allemand");
        preferences()=Preferences{};setLanguage(Language::French);savePreferences(preferenceDatabase);

        preferences().companyEmail="contact@test.example";
        const auto report=makePrintableReport("Inventaire <test>","Détail & contrôle",{"SKU","Désignation"},{{"A-1","Pièce & produit"}},"Vérifier <les> quantités");
        const auto secondReport=makePrintableReport("Document de contrôle","Deuxième export",{"Valeur"},{{"OK"}});
        const std::vector<std::string> wideHeaders={"1","2","3","4","5","6","7","8","9","10","11","12","13","14"};const auto wideReport=makePrintableReport("Rapport étendu","Table détaillée",wideHeaders,{{"1","2","3","4","5","6","7","8","9","10","11","12","13","14"}});std::ifstream wideFile(wideReport);const std::string wideHtml((std::istreambuf_iterator<char>(wideFile)),{});
        check(report!=secondReport,"attribue un nom distinct aux impressions créées rapidement");
        std::ifstream reportFile(report);const std::string html((std::istreambuf_iterator<char>(reportFile)),{});
        check(html.find("Inventaire &lt;test&gt;")!=std::string::npos&&html.find("Vérifier &lt;les&gt; quantités")!=std::string::npos,"échappe le contenu HTML et ajoute les notes de contrôle");
        check(html.find("contact@test.example")!=std::string::npos,"affiche le courriel de l'entreprise dans les documents imprimés");
        check(html.find("<thead>")!=std::string::npos&&html.find("signatures")!=std::string::npos&&html.find("Page ")!=std::string::npos,"génère les en-têtes, signatures et indications de pagination imprimables");
        check(html.find("@page{size:A4 portrait;")!=std::string::npos&&wideHtml.find("@page{size:A3 landscape;")!=std::string::npos&&wideHtml.find("font-size:8px")!=std::string::npos,"adapte les rapports larges au format A3 avec une densité lisible");
        check(csvEscape("=HYPERLINK(\"https://example.test\")").find("'=HYPERLINK")!=std::string::npos,"neutralise les cellules CSV susceptibles d'être interprétées comme une formule");
        check(csvEscape(" \t=1+1").find("\"' \t=1+1\"")==0,"neutralise également les formules CSV précédées d'espaces");
        check(csvEscape("-12.50")=="\"-12.50\"","conserve les nombres négatifs comme données CSV");
        check(htmlEscape("<tag & \"x\">")=="&lt;tag &amp; &quot;x&quot;&gt;","échappe le balisage HTML");
        std::vector<BarcodeLabel> labels;labels.push_back({"Article & test","SKU-1","123456",12.5});labels.push_back({"Deuxième","SKU-2","",4.0});const auto labelFile=makeBarcodeLabels(labels);std::ifstream labelStream(labelFile);const std::string labelHtml((std::istreambuf_iterator<char>(labelStream)),{});check(labelHtml.find("Article &amp; test")!=std::string::npos&&labelHtml.find("<svg")!=std::string::npos&&labelHtml.find("123456")!=std::string::npos,"génère une planche d'étiquettes avec code-barres et contenu échappé");check(!labels.empty()&&labels[0].price==12.5,"conserve les prix des étiquettes");
    } catch(const std::exception& error) {
        ++failures;std::cerr<<"ERREUR DE TEST : "<<error.what()<<'\n';
    }
    std::filesystem::remove_all(root);
    std::cout<<checks<<" vérifications, "<<failures<<" échec(s).\n";
    return failures?1:0;
}
