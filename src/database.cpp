#include "database.hpp"
#include "path_utils.hpp"
#include "security.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_map>

namespace {
class Statement {
public:
    Statement(sqlite3* db, const std::string& sql) : db_(db) {
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt_, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(stmt_); }
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    void text(int index, const std::string& value) {
        if(value.size()>static_cast<std::size_t>(std::numeric_limits<int>::max()))throw std::runtime_error("Le texte dépasse la taille maximale autorisée.");
        check(sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT));
    }
    void integer(int index, int value) { check(sqlite3_bind_int(stmt_, index, value)); }
    void real(int index, double value) { check(sqlite3_bind_double(stmt_, index, value)); }
    void null(int index) { check(sqlite3_bind_null(stmt_, index)); }
    bool row() {
        const int rc = sqlite3_step(stmt_);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        throw std::runtime_error(sqlite3_errmsg(db_));
    }
    void done() {
        if (sqlite3_step(stmt_) != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
    }
    int i(int col) const { return sqlite3_column_int(stmt_, col); }
    double d(int col) const { return sqlite3_column_double(stmt_, col); }
    std::string s(int col) const {
        const auto* value = sqlite3_column_text(stmt_, col);
        return value ? reinterpret_cast<const char*>(value) : "";
    }
private:
    void check(int rc) { if(rc!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(db_)); }
    sqlite3* db_{};
    sqlite3_stmt* stmt_{};
};

class Transaction {
public:
    explicit Transaction(sqlite3* db):db_(db),owns_(sqlite3_get_autocommit(db)!=0) {
        if(owns_)execute("BEGIN IMMEDIATE");
    }
    ~Transaction() { if(owns_&&!finished_)sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr); }
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    void commit() { if(owns_)execute("COMMIT");finished_=true; }
private:
    void execute(const char* sql) {
        char* error=nullptr;
        if(sqlite3_exec(db_,sql,nullptr,nullptr,&error)!=SQLITE_OK){std::string message=error?error:sqlite3_errmsg(db_);sqlite3_free(error);throw std::runtime_error(message);}
    }
    sqlite3* db_{};
    bool owns_{};
    bool finished_{};
};

bool validIsoDate(const std::string& value) {
    if(value.size()!=10||value[4]!='-'||value[7]!='-')return false;
    for(std::size_t i=0;i<value.size();++i)if(i!=4&&i!=7&&!std::isdigit(static_cast<unsigned char>(value[i])))return false;
    const int y=std::stoi(value.substr(0,4)),m=std::stoi(value.substr(5,2)),d=std::stoi(value.substr(8,2));
    return y>=1&&std::chrono::year_month_day{std::chrono::year{y},std::chrono::month{static_cast<unsigned>(m)},std::chrono::day{static_cast<unsigned>(d)}}.ok();
}

void validateOptionalDate(const std::string& value) {
    if(!value.empty()&&!validIsoDate(value))throw std::runtime_error("La date doit être valide au format AAAA-MM-JJ.");
}

bool finiteNonNegative(double value) { return std::isfinite(value)&&value>=0; }

std::string trimmed(std::string value) {
    const auto first=value.find_first_not_of(" \t\n\r");
    if(first==std::string::npos)return {};
    const auto last=value.find_last_not_of(" \t\n\r");
    return value.substr(first,last-first+1);
}

bool blank(const std::string& value) { return trimmed(value).empty(); }

int completeBackup(sqlite3_backup* backup) {
    int result=SQLITE_OK;unsigned int retries=0;
    do{
        result=sqlite3_backup_step(backup,-1);
        if(result==SQLITE_BUSY||result==SQLITE_LOCKED){if(++retries>100)return result;sqlite3_sleep(50);}
        else if(result==SQLITE_OK&&++retries>1000)return result;
    }while(result==SQLITE_OK||result==SQLITE_BUSY||result==SQLITE_LOCKED);
    return result;
}

User readUser(Statement& q, int start = 0) {
    User u;
    u.id = q.i(start + 0); u.username = q.s(start + 1); u.fullName = q.s(start + 2);
    u.passwordHash = q.s(start + 3); u.admin = q.i(start + 4) != 0;
    u.canViewStock = q.i(start + 5) != 0; u.canManageArticles = q.i(start + 6) != 0;
    u.canManageMovements = q.i(start + 7) != 0; u.canManageWarehouses = q.i(start + 8) != 0;
    u.canManageSuppliers = q.i(start + 9) != 0; u.canViewCosts = q.i(start + 10) != 0;
    u.canPrint = q.i(start + 11) != 0; u.canManageUsers = q.i(start + 12) != 0;
    u.active = q.i(start + 13) != 0; u.createdAt = q.s(start + 14); u.lastLogin = q.s(start + 15);
    return u;
}

Product readProduct(Statement& q) {
    Product p;
    p.id = q.i(0); p.sku = q.s(1); p.name = q.s(2); p.category = q.s(3); p.unit = q.s(4);
    p.description = q.s(5); p.minStock = q.d(6); p.safetyStock = q.d(7); p.maxStock = q.d(8);
    p.leadDays = q.i(9); p.unitCost = q.d(10); p.holdingRate = q.d(11); p.volumeM3 = q.d(12);
    p.perishable = q.i(13) != 0; p.onHand = q.d(14); p.createdAt = q.s(15); p.updatedAt = q.s(16);
    p.barcode = q.s(17); p.stockType = q.s(18); p.flowPolicy = q.s(19);p.coverageDays=q.i(20);
    p.customerCommitments=q.d(21);p.pendingInbound=q.d(22);p.projectedStock=p.onHand+p.pendingInbound-p.customerCommitments;
    p.orderCost=q.d(23);p.shortageCost=q.d(24);p.salePrice=q.d(25);
    return p;
}

constexpr const char* userColumns = "id,username,full_name,password_hash,is_admin,can_view_stock,can_manage_articles,can_manage_movements,can_manage_warehouses,can_manage_suppliers,can_view_costs,can_print,can_manage_users,active,created_at,last_login";
constexpr const char* productColumns = "p.id,p.sku,p.name,p.category,p.unit,p.description,p.min_stock,p.safety_stock,p.max_stock,p.lead_days,p.unit_cost,p.holding_rate,p.volume_m3,p.perishable,COALESCE(SUM(CASE WHEN l.quality_status='CONFORME' AND (l.expiry='' OR date(l.expiry)>=date('now','localtime')) THEN l.quantity ELSE 0 END),0),p.created_at,p.updated_at,p.barcode,p.stock_type,p.flow_policy,p.coverage_days,COALESCE((SELECT SUM(o.quantity-o.shipped_quantity) FROM customer_orders o WHERE o.product_id=p.id AND o.status IN ('OUVERTE','PARTIELLE')),0),COALESCE((SELECT SUM(o.quantity-o.received_quantity) FROM purchase_orders o WHERE o.product_id=p.id AND o.status IN ('BROUILLON','ENVOYÉE','PARTIELLE')),0),p.order_cost,p.shortage_cost,p.sale_price";
}

Database::Database(const std::filesystem::path& path) : path_(path) {
    std::filesystem::create_directories(path.parent_path());
#ifndef _WIN32
    std::filesystem::permissions(path.parent_path(),std::filesystem::perms::owner_all,std::filesystem::perm_options::replace);
#endif
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    const auto utf8Path=pathToUtf8(path);
    if (sqlite3_open_v2(utf8Path.c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        const std::string message = db_ ? sqlite3_errmsg(db_) : "SQLite ne peut pas ouvrir la base.";
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error(message);
    }
    sqlite3_busy_timeout(db_, 5000);
    initialize();
#ifndef _WIN32
    std::filesystem::permissions(path_,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
#endif
}

Database::~Database() { if (db_) sqlite3_close(db_); }

namespace {
constexpr int maximumRowLimit = 1000000;
}

void Database::exec(const std::string& sql) const {
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw std::runtime_error(message);
    }
}

void Database::initialize() {
    exec("PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL;");
    exec(R"SQL(
CREATE TABLE IF NOT EXISTS users(
 id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE COLLATE NOCASE,
 full_name TEXT NOT NULL, password_hash TEXT NOT NULL, is_admin INTEGER NOT NULL DEFAULT 0,
 can_view_stock INTEGER NOT NULL DEFAULT 1, can_manage_articles INTEGER NOT NULL DEFAULT 0,
 can_manage_movements INTEGER NOT NULL DEFAULT 1, can_manage_warehouses INTEGER NOT NULL DEFAULT 0,
 can_manage_suppliers INTEGER NOT NULL DEFAULT 0, can_view_costs INTEGER NOT NULL DEFAULT 0,
 can_print INTEGER NOT NULL DEFAULT 1, can_manage_users INTEGER NOT NULL DEFAULT 0,
 active INTEGER NOT NULL DEFAULT 1, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
 last_login TEXT NOT NULL DEFAULT '') ;
CREATE TABLE IF NOT EXISTS suppliers(
 id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, contact TEXT NOT NULL DEFAULT '',
 phone TEXT NOT NULL DEFAULT '', email TEXT NOT NULL DEFAULT '', lead_days INTEGER NOT NULL DEFAULT 0,
 administrative_days INTEGER NOT NULL DEFAULT 0, preparation_days INTEGER NOT NULL DEFAULT 0,
 transport_days INTEGER NOT NULL DEFAULT 0, inspection_days INTEGER NOT NULL DEFAULT 0,
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS warehouses(
 id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE COLLATE NOCASE, address TEXT NOT NULL DEFAULT '',
 capacity_m3 REAL NOT NULL DEFAULT 0, created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS products(
 id INTEGER PRIMARY KEY, sku TEXT NOT NULL UNIQUE COLLATE NOCASE, name TEXT NOT NULL,
 category TEXT NOT NULL DEFAULT '', unit TEXT NOT NULL DEFAULT 'unité', description TEXT NOT NULL DEFAULT '',
 min_stock REAL NOT NULL DEFAULT 0, safety_stock REAL NOT NULL DEFAULT 0, max_stock REAL NOT NULL DEFAULT 0,
 lead_days INTEGER NOT NULL DEFAULT 0, unit_cost REAL NOT NULL DEFAULT 0,
 holding_rate REAL NOT NULL DEFAULT 0, volume_m3 REAL NOT NULL DEFAULT 0,
 perishable INTEGER NOT NULL DEFAULT 0, active INTEGER NOT NULL DEFAULT 1,
 barcode TEXT NOT NULL DEFAULT '', stock_type TEXT NOT NULL DEFAULT 'Stock courant',
 flow_policy TEXT NOT NULL DEFAULT 'Flux tirés · demande réelle', coverage_days INTEGER NOT NULL DEFAULT 30,
 order_cost REAL NOT NULL DEFAULT 0, shortage_cost REAL NOT NULL DEFAULT 0,
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
 sale_price REAL NOT NULL DEFAULT 0,
 updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS stock_lots(
 id INTEGER PRIMARY KEY, product_id INTEGER NOT NULL REFERENCES products(id), warehouse_id INTEGER NOT NULL REFERENCES warehouses(id),
 location TEXT NOT NULL DEFAULT '', lot_code TEXT NOT NULL, expiry TEXT NOT NULL DEFAULT '',
 received_at TEXT NOT NULL DEFAULT (datetime('now','localtime')), quantity REAL NOT NULL DEFAULT 0,
 unit_cost REAL NOT NULL DEFAULT 0, supplier_id INTEGER REFERENCES suppliers(id),
 quality_status TEXT NOT NULL DEFAULT 'CONFORME', quality_note TEXT NOT NULL DEFAULT '');
CREATE INDEX IF NOT EXISTS stock_lots_fifo ON stock_lots(product_id,warehouse_id,received_at,id);
CREATE TABLE IF NOT EXISTS movement_batches(
 id INTEGER PRIMARY KEY, type TEXT NOT NULL, product_id INTEGER NOT NULL REFERENCES products(id),
 warehouse_id INTEGER NOT NULL REFERENCES warehouses(id), quantity REAL NOT NULL, unit_cost REAL NOT NULL DEFAULT 0,
 lot_code TEXT NOT NULL DEFAULT '', expiry TEXT NOT NULL DEFAULT '', reference TEXT NOT NULL DEFAULT '',
 reason TEXT NOT NULL DEFAULT '', actor_id INTEGER REFERENCES users(id),
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS movement_lines(
 id INTEGER PRIMARY KEY, batch_id INTEGER NOT NULL REFERENCES movement_batches(id) ON DELETE CASCADE,
 lot_id INTEGER NOT NULL REFERENCES stock_lots(id), quantity REAL NOT NULL, unit_cost REAL NOT NULL DEFAULT 0,
 quality_status_at_movement TEXT NOT NULL DEFAULT '', quality_note_at_movement TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS audit_log(
 id INTEGER PRIMARY KEY, actor_id INTEGER REFERENCES users(id), action TEXT NOT NULL,
 entity TEXT NOT NULL, entity_id INTEGER NOT NULL DEFAULT 0, detail TEXT NOT NULL DEFAULT '',
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS purchase_orders(
 id INTEGER PRIMARY KEY, reference TEXT NOT NULL UNIQUE, product_id INTEGER NOT NULL REFERENCES products(id),
 supplier_id INTEGER REFERENCES suppliers(id), quantity REAL NOT NULL, received_quantity REAL NOT NULL DEFAULT 0,
 unit_cost REAL NOT NULL, status TEXT NOT NULL DEFAULT 'BROUILLON', actor_id INTEGER REFERENCES users(id),
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')), sent_at TEXT NOT NULL DEFAULT '', received_at TEXT NOT NULL DEFAULT '');
CREATE TABLE IF NOT EXISTS customer_orders(
 id INTEGER PRIMARY KEY, reference TEXT NOT NULL UNIQUE, customer TEXT NOT NULL,
 product_id INTEGER NOT NULL REFERENCES products(id), quantity REAL NOT NULL,
 shipped_quantity REAL NOT NULL DEFAULT 0, status TEXT NOT NULL DEFAULT 'OUVERTE',
 requested_date TEXT NOT NULL DEFAULT '', note TEXT NOT NULL DEFAULT '', actor_id INTEGER REFERENCES users(id),
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE INDEX IF NOT EXISTS movement_batches_date ON movement_batches(created_at DESC,id DESC);
CREATE INDEX IF NOT EXISTS movement_batches_product ON movement_batches(product_id,type,created_at);
CREATE TABLE IF NOT EXISTS app_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS customers(
 id INTEGER PRIMARY KEY, code TEXT NOT NULL DEFAULT '', name TEXT NOT NULL UNIQUE COLLATE NOCASE,
 contact TEXT NOT NULL DEFAULT '', phone TEXT NOT NULL DEFAULT '', email TEXT NOT NULL DEFAULT '',
 address TEXT NOT NULL DEFAULT '', tax_id TEXT NOT NULL DEFAULT '', discount_rate REAL NOT NULL DEFAULT 0,
 payment_terms TEXT NOT NULL DEFAULT '', notes TEXT NOT NULL DEFAULT '', active INTEGER NOT NULL DEFAULT 1,
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS customer_prices(
 customer_id INTEGER NOT NULL REFERENCES customers(id) ON DELETE CASCADE,
 product_id INTEGER NOT NULL REFERENCES products(id) ON DELETE CASCADE,
 price REAL NOT NULL, updated_at TEXT NOT NULL DEFAULT (datetime('now','localtime')),
 PRIMARY KEY(customer_id,product_id));
CREATE TABLE IF NOT EXISTS sales_documents(
 id INTEGER PRIMARY KEY, reference TEXT NOT NULL UNIQUE, type TEXT NOT NULL,
 customer_id INTEGER REFERENCES customers(id), customer_name TEXT NOT NULL DEFAULT '',
 status TEXT NOT NULL DEFAULT 'BROUILLON', document_date TEXT NOT NULL DEFAULT (date('now','localtime')),
 due_date TEXT NOT NULL DEFAULT '', warehouse_id INTEGER REFERENCES warehouses(id),
 source_document_id INTEGER REFERENCES sales_documents(id), subtotal REAL NOT NULL DEFAULT 0,
 discount_total REAL NOT NULL DEFAULT 0, tax_rate REAL NOT NULL DEFAULT 0, tax_total REAL NOT NULL DEFAULT 0,
 total REAL NOT NULL DEFAULT 0, note TEXT NOT NULL DEFAULT '', actor_id INTEGER REFERENCES users(id),
 created_at TEXT NOT NULL DEFAULT (datetime('now','localtime')));
CREATE TABLE IF NOT EXISTS sales_lines(
 id INTEGER PRIMARY KEY, document_id INTEGER NOT NULL REFERENCES sales_documents(id) ON DELETE CASCADE,
 product_id INTEGER NOT NULL REFERENCES products(id), quantity REAL NOT NULL, unit_price REAL NOT NULL DEFAULT 0,
 unit_cost REAL NOT NULL DEFAULT 0, discount_rate REAL NOT NULL DEFAULT 0, tax_rate REAL NOT NULL DEFAULT 0,
 line_total REAL NOT NULL DEFAULT 0, expiry TEXT NOT NULL DEFAULT '');
)SQL");
    exec("CREATE INDEX IF NOT EXISTS stock_lots_warehouse ON stock_lots(warehouse_id,product_id,quantity);"
         "CREATE INDEX IF NOT EXISTS movement_lines_batch ON movement_lines(batch_id,lot_id);"
         "CREATE INDEX IF NOT EXISTS purchase_orders_product_status ON purchase_orders(product_id,status,id);");
    auto ensureColumn=[this](const std::string& table,const std::string& column,const std::string& definition){
        Statement columns(db_,"PRAGMA table_info("+table+")");bool found=false;while(columns.row())if(columns.s(1)==column)found=true;
        if(!found)exec("ALTER TABLE "+table+" ADD COLUMN "+column+" "+definition);
    };
    ensureColumn("stock_lots","quality_status","TEXT NOT NULL DEFAULT 'CONFORME'");
    ensureColumn("stock_lots","quality_note","TEXT NOT NULL DEFAULT ''");
    ensureColumn("movement_lines","quality_status_at_movement","TEXT NOT NULL DEFAULT ''");
    ensureColumn("movement_lines","quality_note_at_movement","TEXT NOT NULL DEFAULT ''");
    ensureColumn("products","barcode","TEXT NOT NULL DEFAULT ''");
    ensureColumn("products","stock_type","TEXT NOT NULL DEFAULT 'Stock courant'");
    ensureColumn("products","flow_policy","TEXT NOT NULL DEFAULT 'Flux tirés · demande réelle'");
    ensureColumn("products","coverage_days","INTEGER NOT NULL DEFAULT 30");
    ensureColumn("products","order_cost","REAL NOT NULL DEFAULT 0");
    ensureColumn("products","shortage_cost","REAL NOT NULL DEFAULT 0");
    ensureColumn("products","sale_price","REAL NOT NULL DEFAULT 0");
    ensureColumn("suppliers","administrative_days","INTEGER NOT NULL DEFAULT 0");
    ensureColumn("suppliers","preparation_days","INTEGER NOT NULL DEFAULT 0");
    ensureColumn("suppliers","transport_days","INTEGER NOT NULL DEFAULT 0");
    ensureColumn("suppliers","inspection_days","INTEGER NOT NULL DEFAULT 0");
    exec("UPDATE suppliers SET preparation_days=lead_days WHERE lead_days>0 AND administrative_days=0 AND preparation_days=0 AND transport_days=0 AND inspection_days=0;");
    exec("UPDATE products SET barcode='' WHERE barcode<>'' AND id NOT IN (SELECT MIN(id) FROM products WHERE barcode<>'' GROUP BY barcode);");
    exec("CREATE INDEX IF NOT EXISTS stock_lots_quality ON stock_lots(product_id,quality_status,expiry);"
         "CREATE UNIQUE INDEX IF NOT EXISTS products_barcode_unique ON products(barcode) WHERE barcode<>'';"
         "CREATE INDEX IF NOT EXISTS customer_orders_product_status ON customer_orders(product_id,status,id);"
         "CREATE INDEX IF NOT EXISTS customers_name ON customers(name COLLATE NOCASE);"
         "CREATE INDEX IF NOT EXISTS sales_documents_date ON sales_documents(document_date DESC,id DESC);"
         "CREATE INDEX IF NOT EXISTS sales_documents_customer ON sales_documents(customer_id,document_date DESC);"
         "CREATE INDEX IF NOT EXISTS sales_lines_document ON sales_lines(document_id);");
    Statement q(db_, "SELECT COUNT(*) FROM warehouses");
    if (q.row() && q.i(0) == 0) {
        exec("INSERT INTO warehouses(name,address,capacity_m3) VALUES('Entrepôt principal','',0)");
    }
}

int Database::userCount() const { Statement q(db_, "SELECT COUNT(*) FROM users"); q.row(); return q.i(0); }

std::optional<User> Database::authenticate(const std::string& username, const std::string& password) {
    User user;
    {Statement q(db_, std::string("SELECT ") + userColumns + " FROM users WHERE username=? COLLATE NOCASE AND active=1");q.text(1, username);if (!q.row()) return std::nullopt;user=readUser(q);}
    if (!verifyPassword(password, user.passwordHash)) return std::nullopt;
    Statement update(db_, "UPDATE users SET last_login=datetime('now','localtime') WHERE id=?");
    update.integer(1, user.id); update.done();
    Statement reload(db_, std::string("SELECT ") + userColumns + " FROM users WHERE id=?");
    reload.integer(1, user.id); reload.row();
    return readUser(reload);
}

std::vector<User> Database::users() const {
    std::vector<User> result;
    Statement q(db_, std::string("SELECT ") + userColumns + " FROM users ORDER BY is_admin DESC,full_name COLLATE NOCASE");
    while (q.row()) result.push_back(readUser(q));
    return result;
}

void Database::createFirstAdmin(const std::string& fullName, const std::string& username, const std::string& password) {
    const auto name=trimmed(fullName),login=trimmed(username);
    if(name.empty()||name.size()>200||login.empty()||login.size()>64)throw std::runtime_error("Indiquez un nom et un identifiant valides (64 caractères maximum pour l'identifiant).");
    if(!passwordMeetsMinimum(password))throw std::runtime_error("Le mot de passe doit contenir au moins 8 caractères valides.");
    Transaction transaction(db_);
    if (userCount() != 0) throw std::runtime_error("Un compte existe déjà.");
    Statement q(db_, "INSERT INTO users(username,full_name,password_hash,is_admin,can_view_stock,can_manage_articles,can_manage_movements,can_manage_warehouses,can_manage_suppliers,can_view_costs,can_print,can_manage_users) VALUES(?,?,?,1,1,1,1,1,1,1,1,1)");
    q.text(1, login); q.text(2, name); q.text(3, hashPassword(password)); q.done();
    const int id=static_cast<int>(sqlite3_last_insert_rowid(db_));
    audit(id,"Création","compte",id,"Compte administrateur créé");
    transaction.commit();
}

void Database::createWorker(const User& u, const std::string& password, int actorId) {
    const auto name=trimmed(u.fullName),login=trimmed(u.username);
    if(name.empty()||name.size()>200||login.empty()||login.size()>64)throw std::runtime_error("Indiquez un nom et un identifiant valides (64 caractères maximum pour l'identifiant).");
    if(!passwordMeetsMinimum(password))throw std::runtime_error("Le mot de passe doit contenir au moins 8 caractères valides.");
    Transaction transaction(db_);
    {Statement duplicate(db_,"SELECT 1 FROM users WHERE username=? COLLATE NOCASE");duplicate.text(1,login);if(duplicate.row())throw std::runtime_error("Cet identifiant est déjà utilisé.");}
    Statement q(db_, "INSERT INTO users(username,full_name,password_hash,is_admin,can_view_stock,can_manage_articles,can_manage_movements,can_manage_warehouses,can_manage_suppliers,can_view_costs,can_print,can_manage_users,active) VALUES(?,?,?,0,?,?,?,?,?,?,?,0,1)");
    q.text(1, login); q.text(2, name); q.text(3, hashPassword(password));
    q.integer(4, u.canViewStock); q.integer(5, u.canManageArticles); q.integer(6, u.canManageMovements);
    q.integer(7, u.canManageWarehouses); q.integer(8, u.canManageSuppliers); q.integer(9, u.canViewCosts);
    q.integer(10, u.canPrint); q.done();
    const int id=static_cast<int>(sqlite3_last_insert_rowid(db_));
    audit(actorId,"Création","compte",id,"Profil de travail créé : "+login);
    transaction.commit();
}

void Database::updateWorker(const User& u, int actorId) {
    const auto name=trimmed(u.fullName);
    if(name.empty()||name.size()>200)throw std::runtime_error("Le nom du profil est obligatoire (200 caractères maximum).");
    Transaction transaction(db_);
    Statement q(db_, "UPDATE users SET full_name=?,can_view_stock=?,can_manage_articles=?,can_manage_movements=?,can_manage_warehouses=?,can_manage_suppliers=?,can_view_costs=?,can_print=?,active=? WHERE id=? AND is_admin=0");
    q.text(1, name); q.integer(2, u.canViewStock); q.integer(3, u.canManageArticles); q.integer(4, u.canManageMovements);
    q.integer(5, u.canManageWarehouses); q.integer(6, u.canManageSuppliers); q.integer(7, u.canViewCosts);
    q.integer(8, u.canPrint); q.integer(9, u.active); q.integer(10, u.id); q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce profil de travail n'existe plus ou ne peut pas être modifié.");
    audit(actorId,"Modification des droits","compte",u.id,"Permissions du profil mises à jour");
    transaction.commit();
}

void Database::updateAdminAccount(const User& u, const std::string& newPassword, int actorId) {
    const auto name=trimmed(u.fullName),login=trimmed(u.username);
    if(name.empty()||name.size()>200||login.empty()||login.size()>64)throw std::runtime_error("Indiquez un nom et un identifiant valides (64 caractères maximum pour l'identifiant).");
    if(!newPassword.empty()&&!passwordMeetsMinimum(newPassword))throw std::runtime_error("Le mot de passe doit contenir au moins 8 caractères valides.");
    Transaction transaction(db_);
    {Statement actor(db_,"SELECT is_admin FROM users WHERE id=?");actor.integer(1,actorId);if(!actor.row()||actor.i(0)!=1)throw std::runtime_error("Seul un administrateur peut modifier ce compte.");}
    {Statement target(db_,"SELECT is_admin FROM users WHERE id=?");target.integer(1,u.id);if(!target.row()||target.i(0)!=1)throw std::runtime_error("Ce compte administrateur n'existe plus.");}
    {Statement duplicate(db_,"SELECT 1 FROM users WHERE username=? COLLATE NOCASE AND id<>?");duplicate.text(1,login);duplicate.integer(2,u.id);if(duplicate.row())throw std::runtime_error("Cet identifiant est déjà utilisé.");}
    if(newPassword.empty()){
        Statement q(db_,"UPDATE users SET username=?,full_name=? WHERE id=? AND is_admin=1");q.text(1,login);q.text(2,name);q.integer(3,u.id);q.done();
    }else{
        Statement q(db_,"UPDATE users SET username=?,full_name=?,password_hash=? WHERE id=? AND is_admin=1");q.text(1,login);q.text(2,name);q.text(3,hashPassword(newPassword));q.integer(4,u.id);q.done();
    }
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce compte administrateur n'existe plus.");
    audit(actorId,"Modification du compte","compte",u.id,newPassword.empty()?"Nom et identifiant de l'administrateur mis à jour":"Identifiant et mot de passe de l'administrateur mis à jour");
    transaction.commit();
}

void Database::resetPassword(int userId, const std::string& password, int actorId) {
    if(!passwordMeetsMinimum(password))throw std::runtime_error("Le mot de passe doit contenir au moins 8 caractères valides.");
    Transaction transaction(db_);
    Statement q(db_, "UPDATE users SET password_hash=? WHERE id=? AND is_admin=0"); q.text(1, hashPassword(password)); q.integer(2, userId); q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce profil de travail n'existe plus.");
    audit(actorId,"Réinitialisation du mot de passe","compte",userId,"Mot de passe réinitialisé par un administrateur");
    transaction.commit();
}

void Database::setUserActive(int userId, bool active, int actorId) {
    Transaction transaction(db_);
    Statement q(db_, "UPDATE users SET active=? WHERE id=? AND is_admin=0"); q.integer(1, active); q.integer(2, userId); q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce profil de travail n'existe plus.");
    audit(actorId,active?"Activation":"Désactivation","compte",userId,active?"Profil réactivé":"Profil désactivé");
    transaction.commit();
}

std::vector<Product> Database::products(const std::string& filter) const {
    return productsPage(filter,1000000,0);
}

std::vector<Product> Database::productsPage(const std::string& filter,int limit,int offset) const {
    std::vector<Product> result;
    std::string sql = std::string("SELECT ") + productColumns + " FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.active=1";
    if (!filter.empty()) sql += " AND (p.sku LIKE ? OR p.barcode LIKE ? OR p.name LIKE ? OR p.category LIKE ?)";
    sql += " GROUP BY p.id ORDER BY p.name COLLATE NOCASE LIMIT ? OFFSET ?";
    Statement q(db_, sql);
    int bind=1;if (!filter.empty()) { const auto f = "%" + filter + "%"; q.text(bind++, f); q.text(bind++, f); q.text(bind++, f); q.text(bind++, f); }
    q.integer(bind++,std::clamp(limit,1,maximumRowLimit));q.integer(bind,std::max(0,offset));
    while (q.row()) result.push_back(readProduct(q));
    return result;
}

int Database::productCount(const std::string& filter) const {
    if(filter.empty()){Statement q(db_,"SELECT COUNT(*) FROM products WHERE active=1");q.row();return q.i(0);}
    Statement q(db_,"SELECT COUNT(*) FROM products p WHERE p.active=1 AND (p.sku LIKE ? OR p.barcode LIKE ? OR p.name LIKE ? OR p.category LIKE ?)");const auto f="%"+filter+"%";q.text(1,f);q.text(2,f);q.text(3,f);q.text(4,f);q.row();return q.i(0);
}

Product Database::product(int id) const {
    Statement q(db_, std::string("SELECT ") + productColumns + " FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.id=? GROUP BY p.id");
    q.integer(1, id);
    if (!q.row()) throw std::runtime_error("Article introuvable.");
    return readProduct(q);
}

std::vector<std::vector<std::string>> Database::productLots(int productId) const {
    std::vector<std::vector<std::string>> rows;
    Statement q(db_,"SELECT w.name,l.location,l.lot_code,l.received_at,l.expiry,l.quantity,p.unit,l.unit_cost,l.quality_status,l.quality_note FROM stock_lots l JOIN products p ON p.id=l.product_id JOIN warehouses w ON w.id=l.warehouse_id WHERE l.product_id=? AND l.quantity>0 ORDER BY l.received_at,l.id");q.integer(1,productId);
    while(q.row())rows.push_back({q.s(0),q.s(1),q.s(2),q.s(3),q.s(4),std::to_string(q.d(5)),q.s(6),std::to_string(q.d(7)),q.s(8),q.s(9)});
    return rows;
}

std::vector<std::vector<std::string>> Database::inventoryRows(bool includeCosts) const {
    std::vector<std::vector<std::string>> rows;
    Statement q(db_,"SELECT p.sku,p.name,p.stock_type,p.flow_policy,COALESCE(w.name,''),COALESCE(l.location,''),COALESCE(l.lot_code,''),COALESCE(l.received_at,''),COALESCE(l.expiry,''),COALESCE(l.quantity,0),p.unit,COALESCE(l.quality_status,''),COALESCE(l.quality_note,''),COALESCE(l.unit_cost,p.unit_cost) FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 LEFT JOIN warehouses w ON w.id=l.warehouse_id WHERE p.active=1 ORDER BY p.name COLLATE NOCASE,w.name,l.received_at,l.id");
    while(q.row()) { std::vector<std::string> row={q.s(0),q.s(1),q.s(2),q.s(3),q.s(4),q.s(5),q.s(6),q.s(7),q.s(8),std::to_string(q.d(9)),q.s(10),q.s(11),q.s(12)};if(includeCosts)row.push_back(std::to_string(q.d(13)));rows.push_back(std::move(row)); }
    return rows;
}

std::vector<std::vector<std::string>> Database::qualityLots() const {
    std::vector<std::vector<std::string>> rows;
    Statement q(db_,"SELECT l.id,p.sku,p.name,w.name,l.location,l.lot_code,l.received_at,l.expiry,l.quantity,p.unit,l.quality_status,l.quality_note FROM stock_lots l JOIN products p ON p.id=l.product_id JOIN warehouses w ON w.id=l.warehouse_id WHERE l.quantity>0 ORDER BY CASE l.quality_status WHEN 'BLOQUÉ' THEN 0 WHEN 'À contrôler' THEN 1 ELSE 2 END,l.received_at,l.id");
    while(q.row())rows.push_back({std::to_string(q.i(0)),q.s(1),q.s(2),q.s(3),q.s(4),q.s(5),q.s(6),q.s(7),std::to_string(q.d(8)),q.s(9),q.s(10),q.s(11)});
    return rows;
}

void Database::setLotQuality(int lotId,const std::string& status,const std::string& note,int actorId) {
    if(status!="À contrôler"&&status!="CONFORME"&&status!="BLOQUÉ")throw std::runtime_error("Statut qualité inconnu.");
    if(note.size()>2000)throw std::runtime_error("La note de contrôle dépasse la longueur maximale autorisée.");
    if(status=="BLOQUÉ"&&blank(note))throw std::runtime_error("Ajoutez un motif de blocage qualité pour tracer la décision.");
    Transaction transaction(db_);
    Statement current(db_,"SELECT quality_status,expiry FROM stock_lots WHERE id=? AND quantity>0");current.integer(1,lotId);
    if(!current.row())throw std::runtime_error("Lot introuvable ou sans quantité en stock.");
    const auto previousStatus=current.s(0),expiryDate=current.s(1);
    if(status=="CONFORME"&&!expiryDate.empty()&&expiryDate<databaseTime().substr(0,10))throw std::runtime_error("Un lot périmé ne peut pas être déclaré conforme.");
    if(previousStatus=="BLOQUÉ"&&status=="CONFORME"&&blank(note))throw std::runtime_error("Ajoutez une justification avant de lever le blocage qualité.");
    Statement q(db_,"UPDATE stock_lots SET quality_status=?,quality_note=? WHERE id=? AND quantity>0");q.text(1,status);q.text(2,note);q.integer(3,lotId);q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce lot n'existe plus ou n'a plus de quantité en stock.");
    audit(actorId,"Contrôle qualité","lot",lotId,status+" · "+note);
    transaction.commit();
}

void Database::audit(int actorId, const std::string& action, const std::string& entity, int entityId, const std::string& detail) {
    Statement q(db_, "INSERT INTO audit_log(actor_id,action,entity,entity_id,detail) VALUES(?,?,?,?,?)");
    q.integer(1, actorId); q.text(2, action); q.text(3, entity); q.integer(4, entityId); q.text(5, detail); q.done();
}

void Database::saveProduct(const Product& p, int actorId) {
    Product value=p;value.sku=trimmed(p.sku);value.name=trimmed(p.name);value.category=trimmed(p.category);value.unit=trimmed(p.unit);value.barcode=trimmed(p.barcode);value.stockType=trimmed(p.stockType);value.flowPolicy=trimmed(p.flowPolicy);
    if(value.sku.empty()||value.name.empty())throw std::runtime_error("La référence et la désignation sont obligatoires.");
    const std::vector<std::string> stockTypes={"Stock courant","Production","Hors production · MRO","Projet","Usure / réutilisable","Transbordement","Récupération","Cyclique","Sécurité","Alerte","Saisonnier","Transit","Anticipation / spéculatif","Inactif / dormant","Tampon / découplage","Optimal"};
    const std::vector<std::string> flowPolicies={"Flux tirés · demande réelle","Flux poussés · prévision","Flux tendus · JIT","Flux synchrones"};
    if(value.stockType.empty())value.stockType="Stock courant";
    if(value.flowPolicy.empty())value.flowPolicy="Flux tirés · demande réelle";
    if(std::find(stockTypes.begin(),stockTypes.end(),value.stockType)==stockTypes.end()||std::find(flowPolicies.begin(),flowPolicies.end(),value.flowPolicy)==flowPolicies.end())throw std::runtime_error("Choisissez une typologie de stock et une politique de flux reconnues.");
    if(value.sku.size()>128||value.name.size()>200||value.category.size()>120||value.unit.empty()||value.unit.size()>40||value.description.size()>10000||value.barcode.size()>128)throw std::runtime_error("Vérifiez la longueur de la référence, du code-barres, du nom, de la catégorie, de l'unité ou de la description.");
    if(!finiteNonNegative(value.minStock)||!finiteNonNegative(value.safetyStock)||!finiteNonNegative(value.maxStock)||!finiteNonNegative(value.unitCost)||!finiteNonNegative(value.holdingRate)||!finiteNonNegative(value.orderCost)||!finiteNonNegative(value.shortageCost)||!finiteNonNegative(value.volumeM3)||!finiteNonNegative(value.salePrice)||value.leadDays<0||value.leadDays>3650||value.coverageDays<0||value.coverageDays>3650)throw std::runtime_error("Les niveaux de stock, coûts, volumes et délais doivent être des valeurs positives valides.");
    if(value.maxStock>0&&(value.maxStock<value.minStock||value.safetyStock>value.maxStock))throw std::runtime_error("Le stock maximal doit être supérieur ou égal aux stocks minimum et de sécurité.");
    Transaction transaction(db_);
    Statement duplicate(db_,"SELECT id FROM products WHERE sku=? COLLATE NOCASE AND id<>?");duplicate.text(1,value.sku);duplicate.integer(2,value.id);if(duplicate.row())throw std::runtime_error("Cette référence existe déjà dans le catalogue.");
    if(!value.barcode.empty()){Statement duplicateBarcode(db_,"SELECT id FROM products WHERE barcode=? COLLATE NOCASE AND id<>?");duplicateBarcode.text(1,value.barcode);duplicateBarcode.integer(2,value.id);if(duplicateBarcode.row())throw std::runtime_error("Ce code-barres est déjà attribué à un autre article.");}
    if(value.id>0){double oldVolume=0;bool found=false;{Statement old(db_,"SELECT volume_m3 FROM products WHERE id=? AND active=1");old.integer(1,value.id);if(old.row()){oldVolume=old.d(0);found=true;}}if(found&&value.volumeM3>oldVolume+1e-9){Statement capacity(db_,"SELECT w.name FROM warehouses w LEFT JOIN stock_lots l ON l.warehouse_id=w.id AND l.quantity>0 LEFT JOIN products p ON p.id=l.product_id WHERE w.capacity_m3>0 GROUP BY w.id HAVING COALESCE(SUM(CASE WHEN p.id=? THEN l.quantity*? ELSE l.quantity*p.volume_m3 END),0)>w.capacity_m3+1e-9 LIMIT 1");capacity.integer(1,value.id);capacity.real(2,value.volumeM3);if(capacity.row())throw std::runtime_error("Le volume de cet article dépasserait la capacité de l'entrepôt « "+capacity.s(0)+" ». Réduisez le volume ou ajustez la capacité après vérification.");}}
    int id=value.id;
    if(value.id==0){
        Statement q(db_,"INSERT INTO products(sku,name,category,unit,description,min_stock,safety_stock,max_stock,lead_days,unit_cost,sale_price,holding_rate,volume_m3,perishable,barcode,stock_type,flow_policy,coverage_days,order_cost,shortage_cost) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        q.text(1,value.sku);q.text(2,value.name);q.text(3,value.category);q.text(4,value.unit);q.text(5,value.description);
        q.real(6,value.minStock);q.real(7,value.safetyStock);q.real(8,value.maxStock);q.integer(9,value.leadDays);
        q.real(10,value.unitCost);q.real(11,value.salePrice);q.real(12,value.holdingRate);q.real(13,value.volumeM3);q.integer(14,value.perishable);q.text(15,value.barcode);q.text(16,value.stockType);q.text(17,value.flowPolicy);q.integer(18,value.coverageDays);q.real(19,value.orderCost);q.real(20,value.shortageCost);q.done();
        id=static_cast<int>(sqlite3_last_insert_rowid(db_));audit(actorId,"Création","article",id,value.sku+" — "+value.name);
    }else{
        Statement q(db_,"UPDATE products SET sku=?,name=?,category=?,unit=?,description=?,min_stock=?,safety_stock=?,max_stock=?,lead_days=?,unit_cost=?,sale_price=?,holding_rate=?,volume_m3=?,perishable=?,barcode=?,stock_type=?,flow_policy=?,coverage_days=?,order_cost=?,shortage_cost=?,updated_at=datetime('now','localtime') WHERE id=? AND active=1");
        q.text(1,value.sku);q.text(2,value.name);q.text(3,value.category);q.text(4,value.unit);q.text(5,value.description);
        q.real(6,value.minStock);q.real(7,value.safetyStock);q.real(8,value.maxStock);q.integer(9,value.leadDays);
        q.real(10,value.unitCost);q.real(11,value.salePrice);q.real(12,value.holdingRate);q.real(13,value.volumeM3);q.integer(14,value.perishable);q.text(15,value.barcode);q.text(16,value.stockType);q.text(17,value.flowPolicy);q.integer(18,value.coverageDays);q.real(19,value.orderCost);q.real(20,value.shortageCost);q.integer(21,value.id);q.done();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("Cet article n'existe plus ou est archivé.");
        audit(actorId,"Modification","article",value.id,value.sku+" — "+value.name);
    }
    transaction.commit();
}

void Database::archiveProduct(int productId, int actorId) {
    Transaction transaction(db_);
    double quantity=0;{Statement stock(db_,"SELECT COALESCE(SUM(quantity),0) FROM stock_lots WHERE product_id=? AND quantity>0");stock.integer(1,productId);stock.row();quantity=stock.d(0);}
    if(quantity>0)throw std::runtime_error("Cet article a encore du stock, y compris des lots bloqués. Enregistrez les sorties, corrections ou déclassements nécessaires avant de l'archiver.");
    Statement q(db_,"UPDATE products SET active=0,updated_at=datetime('now','localtime') WHERE id=? AND active=1");
    q.integer(1,productId);q.done();if(sqlite3_changes(db_)!=1)throw std::runtime_error("Cet article n'existe plus ou est déjà archivé.");
    audit(actorId,"Archivage","article",productId,"Article désactivé");transaction.commit();
}

std::vector<Warehouse> Database::warehouses() const {
    std::vector<Warehouse> result;
    Statement q(db_, "SELECT w.id,w.name,w.address,w.capacity_m3,COALESCE(SUM(l.quantity*p.volume_m3),0),w.created_at FROM warehouses w LEFT JOIN stock_lots l ON l.warehouse_id=w.id AND l.quantity>0 LEFT JOIN products p ON p.id=l.product_id GROUP BY w.id ORDER BY w.name COLLATE NOCASE");
    while(q.row()) { Warehouse w; w.id=q.i(0); w.name=q.s(1); w.address=q.s(2); w.capacityM3=q.d(3); w.usedM3=q.d(4); w.createdAt=q.s(5); result.push_back(w); }
    return result;
}

void Database::saveWarehouse(const Warehouse& w, int actorId) {
    const auto name=trimmed(w.name);
    if(name.empty()||name.size()>200||!finiteNonNegative(w.capacityM3))throw std::runtime_error("Indiquez un nom d'entrepôt et une capacité valide.");
    Transaction transaction(db_);
    {Statement duplicate(db_,"SELECT 1 FROM warehouses WHERE name=? COLLATE NOCASE AND id<>?");duplicate.text(1,name);duplicate.integer(2,w.id);if(duplicate.row())throw std::runtime_error("Un entrepôt porte déjà ce nom.");}
    if(w.id>0){Statement used(db_,"SELECT COALESCE(SUM(l.quantity*p.volume_m3),0) FROM stock_lots l JOIN products p ON p.id=l.product_id WHERE l.warehouse_id=? AND l.quantity>0");used.integer(1,w.id);used.row();if(w.capacityM3>0&&w.capacityM3+1e-9<used.d(0))throw std::runtime_error("La capacité ne peut pas être inférieure au volume actuellement occupé.");}
    if (w.id == 0) {
        Statement q(db_,"INSERT INTO warehouses(name,address,capacity_m3) VALUES(?,?,?)");q.text(1,name);q.text(2,trimmed(w.address));q.real(3,w.capacityM3);q.done();
        audit(actorId,"Création","entrepôt",static_cast<int>(sqlite3_last_insert_rowid(db_)),name);
    } else {
        Statement q(db_,"UPDATE warehouses SET name=?,address=?,capacity_m3=? WHERE id=?");q.text(1,name);q.text(2,trimmed(w.address));q.real(3,w.capacityM3);q.integer(4,w.id);q.done();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("Cet entrepôt n'existe plus.");
        audit(actorId,"Modification","entrepôt",w.id,name);
    }
    transaction.commit();
}

std::vector<Supplier> Database::suppliers() const {
    std::vector<Supplier> result; Statement q(db_,"SELECT id,name,contact,phone,email,lead_days,administrative_days,preparation_days,transport_days,inspection_days FROM suppliers ORDER BY name COLLATE NOCASE");
    while(q.row()) { Supplier s; s.id=q.i(0);s.name=q.s(1);s.contact=q.s(2);s.phone=q.s(3);s.email=q.s(4);s.leadDays=q.i(5);s.administrativeDays=q.i(6);s.preparationDays=q.i(7);s.transportDays=q.i(8);s.inspectionDays=q.i(9);result.push_back(s); }
    return result;
}

void Database::saveSupplier(const Supplier& s, int actorId) {
    const auto name=trimmed(s.name);const long long totalDays=static_cast<long long>(s.administrativeDays)+s.preparationDays+s.transportDays+s.inspectionDays;if(name.empty()||name.size()>200||s.administrativeDays<0||s.preparationDays<0||s.transportDays<0||s.inspectionDays<0||totalDays>3650||s.contact.size()>200||s.phone.size()>80||s.email.size()>254)throw std::runtime_error("Vérifiez le nom, les coordonnées et les délais détaillés du fournisseur.");const int leadDays=static_cast<int>(totalDays);
    Transaction transaction(db_);
    {Statement duplicate(db_,"SELECT 1 FROM suppliers WHERE name=? COLLATE NOCASE AND id<>?");duplicate.text(1,name);duplicate.integer(2,s.id);if(duplicate.row())throw std::runtime_error("Un fournisseur porte déjà ce nom.");}
    if(s.id==0){Statement q(db_,"INSERT INTO suppliers(name,contact,phone,email,lead_days,administrative_days,preparation_days,transport_days,inspection_days) VALUES(?,?,?,?,?,?,?,?,?)");q.text(1,name);q.text(2,trimmed(s.contact));q.text(3,trimmed(s.phone));q.text(4,trimmed(s.email));q.integer(5,leadDays);q.integer(6,s.administrativeDays);q.integer(7,s.preparationDays);q.integer(8,s.transportDays);q.integer(9,s.inspectionDays);q.done();audit(actorId,"Création","fournisseur",static_cast<int>(sqlite3_last_insert_rowid(db_)),name+" · "+std::to_string(leadDays)+" jours");}
    else{Statement q(db_,"UPDATE suppliers SET name=?,contact=?,phone=?,email=?,lead_days=?,administrative_days=?,preparation_days=?,transport_days=?,inspection_days=? WHERE id=?");q.text(1,name);q.text(2,trimmed(s.contact));q.text(3,trimmed(s.phone));q.text(4,trimmed(s.email));q.integer(5,leadDays);q.integer(6,s.administrativeDays);q.integer(7,s.preparationDays);q.integer(8,s.transportDays);q.integer(9,s.inspectionDays);q.integer(10,s.id);q.done();if(sqlite3_changes(db_)!=1)throw std::runtime_error("Ce fournisseur n'existe plus.");audit(actorId,"Modification","fournisseur",s.id,name+" · "+std::to_string(leadDays)+" jours");}
    transaction.commit();
}

int Database::addMovement(int productId, const std::string& type, double quantity, double unitCost,
                          int warehouseId, const std::string& location, const std::string& lotCode, const std::string& expiry,
                          const std::string& qualityStatus, const std::string& qualityNote,
                          const std::string& reference, const std::string& reason, int actorId) {
    if (quantity <= 0 || !std::isfinite(quantity)) throw std::runtime_error("La quantité doit être supérieure à zéro.");
    if(!finiteNonNegative(unitCost))throw std::runtime_error("Le coût unitaire doit être une valeur positive valide.");
    if(type!="IN"&&type!="OUT"&&type!="ADJUST_IN"&&type!="ADJUST_OUT"&&type!="WRITE_OFF"&&type!="RETURN")throw std::runtime_error("Type de mouvement inconnu.");
    const bool inbound = type == "IN" || type == "ADJUST_IN" || type == "RETURN";
    const bool writeOff=type=="WRITE_OFF";
    if(inbound)validateOptionalDate(expiry);
    if(writeOff&&blank(reason))throw std::runtime_error("Indiquez le motif du déclassement ou de la destruction.");
    if(writeOff&&blank(lotCode))throw std::runtime_error("Indiquez le code exact du lot à déclasser.");
    if(inbound&&qualityStatus!="À contrôler"&&qualityStatus!="CONFORME"&&qualityStatus!="BLOQUÉ")throw std::runtime_error("Choisissez un statut de contrôle qualité reconnu.");
    Transaction transaction(db_);
    bool perishable=false;
    {Statement product(db_,"SELECT perishable FROM products WHERE id=? AND active=1");product.integer(1,productId);if(!product.row())throw std::runtime_error("Article indisponible ou archivé.");perishable=product.i(0)!=0;}
    if(inbound&&perishable&&expiry.empty())throw std::runtime_error("Une date de péremption est requise pour cet article périssable.");
    const auto normalizedLocation=trimmed(location),normalizedLot=trimmed(lotCode),normalizedQualityNote=trimmed(qualityNote),normalizedReference=trimmed(reference),normalizedReason=trimmed(reason);
    if(normalizedLot.size()>200||normalizedLocation.size()>200||normalizedReference.size()>200||normalizedReason.size()>1000||normalizedQualityNote.size()>2000)throw std::runtime_error("Un champ de traçabilité dépasse la longueur maximale autorisée.");
    if(inbound&&qualityStatus=="BLOQUÉ"&&normalizedQualityNote.empty())throw std::runtime_error("Ajoutez un motif de blocage qualité pour tracer la décision.");
        Statement batch(db_,"INSERT INTO movement_batches(type,product_id,warehouse_id,quantity,unit_cost,lot_code,expiry,reference,reason,actor_id) VALUES(?,?,?,?,?,?,?,?,?,?)");
        batch.text(1,type);batch.integer(2,productId);batch.integer(3,warehouseId);batch.real(4,quantity);batch.real(5,unitCost);
        batch.text(6,(inbound||writeOff)?normalizedLot:"");batch.text(7,inbound?expiry:"");batch.text(8,normalizedReference);batch.text(9,normalizedReason);batch.integer(10,actorId);batch.done();
        const int batchId=static_cast<int>(sqlite3_last_insert_rowid(db_));
        if(inbound) {
            std::string actualLot=normalizedLot;
            if(actualLot.empty()) actualLot="LOT-"+std::to_string(batchId);
            Statement lot(db_,"INSERT INTO stock_lots(product_id,warehouse_id,location,lot_code,expiry,quantity,unit_cost,quality_status,quality_note) VALUES(?,?,?,?,?,?,?,?,?)");
            lot.integer(1,productId);lot.integer(2,warehouseId);lot.text(3,normalizedLocation);lot.text(4,actualLot);lot.text(5,expiry);lot.real(6,quantity);lot.real(7,unitCost);lot.text(8,qualityStatus);lot.text(9,normalizedQualityNote);lot.done();
            const int lotId=static_cast<int>(sqlite3_last_insert_rowid(db_));
            Statement line(db_,"INSERT INTO movement_lines(batch_id,lot_id,quantity,unit_cost,quality_status_at_movement,quality_note_at_movement) VALUES(?,?,?,?,?,?)");
            line.integer(1,batchId);line.integer(2,lotId);line.real(3,quantity);line.real(4,unitCost);line.text(5,qualityStatus);line.text(6,normalizedQualityNote);line.done();
        } else {
            const char* sql=writeOff
                ?"SELECT id,quantity,unit_cost,quality_status,quality_note FROM stock_lots WHERE product_id=? AND warehouse_id=? AND lot_code=? AND quantity>0 ORDER BY received_at ASC,id ASC"
                :"SELECT id,quantity,unit_cost,quality_status,quality_note FROM stock_lots WHERE product_id=? AND warehouse_id=? AND quantity>0 AND quality_status='CONFORME' AND (expiry='' OR date(expiry)>=date('now','localtime')) ORDER BY received_at ASC,id ASC";
            Statement available(db_,sql);
            available.integer(1,productId);available.integer(2,warehouseId);if(writeOff)available.text(3,normalizedLot);
            std::vector<std::tuple<int,double,double,std::string,std::string>> lots;
            double total=0;
            while(available.row()) { lots.emplace_back(available.i(0),available.d(1),available.d(2),available.s(3),available.s(4)); total+=available.d(1); }
            if(total+1e-9<quantity) throw std::runtime_error(writeOff?"Quantité à déclasser supérieure au stock physique de cet entrepôt.":"Stock conforme insuffisant dans cet entrepôt. Disponible : "+std::to_string(total)+". Les lots en attente, bloqués ou échus ne sont pas distribués.");
            double remaining=quantity;
            for(const auto& [lotId,availableQty,lotCost,lotQuality,lotQualityNote]:lots) {
                if(remaining<=1e-9) break;
                const double taken=std::min(remaining,availableQty);
                Statement reduce(db_,"UPDATE stock_lots SET quantity=quantity-? WHERE id=?");reduce.real(1,taken);reduce.integer(2,lotId);reduce.done();
                Statement line(db_,"INSERT INTO movement_lines(batch_id,lot_id,quantity,unit_cost,quality_status_at_movement,quality_note_at_movement) VALUES(?,?,?,?,?,?)");line.integer(1,batchId);line.integer(2,lotId);line.real(3,taken);line.real(4,lotCost);line.text(5,lotQuality);line.text(6,lotQualityNote);line.done();
                remaining-=taken;
            }
        }
        audit(actorId,"Mouvement",inbound?"entrée":(writeOff?"déclassement":"sortie"),batchId,type+" · "+std::to_string(quantity)+(normalizedReason.empty()?"":" · "+normalizedReason));
        transaction.commit();
        return batchId;
}

std::pair<int,int> Database::transferStock(int productId,double quantity,int sourceWarehouseId,int destinationWarehouseId,const std::string& destinationLocation,const std::string& reference,const std::string& note,int actorId) {
    if(!std::isfinite(quantity)||quantity<=0)throw std::runtime_error("La quantité à transférer doit être supérieure à zéro.");
    if(sourceWarehouseId==destinationWarehouseId)throw std::runtime_error("Choisissez deux entrepôts différents pour un transfert.");
    const auto location=trimmed(destinationLocation),cleanNote=trimmed(note),requestedRef=trimmed(reference);
    if(location.size()>200||cleanNote.size()>1000||requestedRef.size()>200)throw std::runtime_error("Un champ du transfert dépasse la longueur maximale autorisée.");
    Transaction transaction(db_);double volume=0;std::string sku;
    {Statement product(db_,"SELECT sku,volume_m3 FROM products WHERE id=? AND active=1");product.integer(1,productId);if(!product.row())throw std::runtime_error("Article indisponible ou archivé.");sku=product.s(0);volume=product.d(1);}
    double capacity=0,used=0;std::string destinationName;
    {Statement dest(db_,"SELECT w.name,w.capacity_m3,COALESCE(SUM(l.quantity*p.volume_m3),0) FROM warehouses w LEFT JOIN stock_lots l ON l.warehouse_id=w.id AND l.quantity>0 LEFT JOIN products p ON p.id=l.product_id WHERE w.id=? GROUP BY w.id");dest.integer(1,destinationWarehouseId);if(!dest.row())throw std::runtime_error("L'entrepôt de destination n'existe plus.");destinationName=dest.s(0);capacity=dest.d(1);used=dest.d(2);}
    std::string sourceName;{Statement source(db_,"SELECT name FROM warehouses WHERE id=?");source.integer(1,sourceWarehouseId);if(!source.row())throw std::runtime_error("L'entrepôt source n'existe plus.");sourceName=source.s(0);}
    if(capacity>0&&used+quantity*volume>capacity+1e-9)throw std::runtime_error("Le transfert dépasserait la capacité de l'entrepôt « "+destinationName+" ». Libérez de l'espace ou choisissez une autre destination.");
    struct LotPart{int id{};double quantity{};double unitCost{};std::string code,expiry,receivedAt,quality,note;int supplierId{};};std::vector<LotPart> lots;double total=0;
    {Statement available(db_,"SELECT id,quantity,unit_cost,lot_code,expiry,received_at,quality_status,quality_note,COALESCE(supplier_id,0) FROM stock_lots WHERE product_id=? AND warehouse_id=? AND quantity>0 ORDER BY received_at,id");available.integer(1,productId);available.integer(2,sourceWarehouseId);while(available.row()){lots.push_back({available.i(0),available.d(1),available.d(2),available.s(3),available.s(4),available.s(5),available.s(6),available.s(7),available.i(8)});total+=available.d(1);}}
    if(total+1e-9<quantity)throw std::runtime_error("Stock physique insuffisant dans l'entrepôt source. Disponible : "+std::to_string(total)+".");
    int expected=1;{Statement next(db_,"SELECT COALESCE(MAX(id),0)+1 FROM movement_batches");if(next.row())expected=next.i(0);}const std::string ref=requestedRef.empty()?"TRF-"+std::to_string(expected):requestedRef;
    auto createBatch=[&](const std::string& type,int warehouseId,const std::string& reason){Statement q(db_,"INSERT INTO movement_batches(type,product_id,warehouse_id,quantity,unit_cost,reference,reason,actor_id) VALUES(?,?,?,?,0,?,?,?)");q.text(1,type);q.integer(2,productId);q.integer(3,warehouseId);q.real(4,quantity);q.text(5,ref);q.text(6,reason);q.integer(7,actorId);q.done();return static_cast<int>(sqlite3_last_insert_rowid(db_));};
    const int outbound=createBatch("TRANSFER_OUT",sourceWarehouseId,"Transfert vers "+destinationName+(cleanNote.empty()?"":" · "+cleanNote));
    const int inbound=createBatch("TRANSFER_IN",destinationWarehouseId,"Transfert depuis "+sourceName+(cleanNote.empty()?"":" · "+cleanNote));
    double remaining=quantity;
    for(const auto& lot:lots){if(remaining<=1e-9)break;const double moved=std::min(remaining,lot.quantity);
        Statement reduce(db_,"UPDATE stock_lots SET quantity=quantity-? WHERE id=? AND quantity>=?");reduce.real(1,moved);reduce.integer(2,lot.id);reduce.real(3,moved);reduce.done();if(sqlite3_changes(db_)!=1)throw std::runtime_error("La quantité du lot source a changé. Rechargez l'inventaire puis réessayez.");
        Statement outLine(db_,"INSERT INTO movement_lines(batch_id,lot_id,quantity,unit_cost,quality_status_at_movement,quality_note_at_movement) VALUES(?,?,?,?,?,?)");outLine.integer(1,outbound);outLine.integer(2,lot.id);outLine.real(3,moved);outLine.real(4,lot.unitCost);outLine.text(5,lot.quality);outLine.text(6,lot.note);outLine.done();
        Statement newLot(db_,"INSERT INTO stock_lots(product_id,warehouse_id,location,lot_code,expiry,received_at,quantity,unit_cost,supplier_id,quality_status,quality_note) VALUES(?,?,?,?,?,?,?,?,?,?,?)");newLot.integer(1,productId);newLot.integer(2,destinationWarehouseId);newLot.text(3,location);newLot.text(4,lot.code);newLot.text(5,lot.expiry);newLot.text(6,lot.receivedAt);newLot.real(7,moved);newLot.real(8,lot.unitCost);if(lot.supplierId>0)newLot.integer(9,lot.supplierId);else newLot.null(9);newLot.text(10,lot.quality);newLot.text(11,lot.note);newLot.done();const int destinationLot=static_cast<int>(sqlite3_last_insert_rowid(db_));
        Statement inLine(db_,"INSERT INTO movement_lines(batch_id,lot_id,quantity,unit_cost,quality_status_at_movement,quality_note_at_movement) VALUES(?,?,?,?,?,?)");inLine.integer(1,inbound);inLine.integer(2,destinationLot);inLine.real(3,moved);inLine.real(4,lot.unitCost);inLine.text(5,lot.quality);inLine.text(6,lot.note);inLine.done();remaining-=moved;
    }
    audit(actorId,"Transfert sortant","mouvement",outbound,sku+" · "+std::to_string(quantity)+" · "+sourceName+" → "+destinationName);audit(actorId,"Transfert entrant","mouvement",inbound,sku+" · "+std::to_string(quantity)+" · "+sourceName+" → "+destinationName);transaction.commit();return {outbound,inbound};
}

std::vector<Movement> Database::movements(int limit,int offset) const {
    std::vector<Movement> result;
    Statement q(db_,"SELECT m.id,m.type,p.sku,p.name,w.name,COALESCE(GROUP_CONCAT(DISTINCT l.location),''),COALESCE(GROUP_CONCAT(DISTINCT COALESCE(NULLIF(ml.quality_status_at_movement,''),l.quality_status)),''),m.quantity,CASE WHEN m.type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT','TRANSFER_IN') THEN COALESCE(SUM(ml.quantity*ml.unit_cost)/NULLIF(SUM(ml.quantity),0),m.unit_cost) ELSE m.unit_cost END,COALESCE(GROUP_CONCAT(DISTINCT l.lot_code),''),COALESCE(NULLIF(GROUP_CONCAT(DISTINCT NULLIF(l.expiry,'')),''),m.expiry),m.reference,m.reason,COALESCE(u.full_name,'—'),m.created_at FROM movement_batches m JOIN products p ON p.id=m.product_id JOIN warehouses w ON w.id=m.warehouse_id LEFT JOIN movement_lines ml ON ml.batch_id=m.id LEFT JOIN stock_lots l ON l.id=ml.lot_id LEFT JOIN users u ON u.id=m.actor_id GROUP BY m.id ORDER BY m.created_at DESC,m.id DESC LIMIT ? OFFSET ?");
    q.integer(1,std::clamp(limit,1,maximumRowLimit));q.integer(2,std::max(0,offset));
    while(q.row()) { Movement m; m.id=q.i(0);m.type=q.s(1);m.sku=q.s(2);m.product=q.s(3);m.warehouse=q.s(4);m.location=q.s(5);m.qualityStatus=q.s(6);m.quantity=q.d(7);m.unitCost=q.d(8);m.lotCode=q.s(9);m.expiry=q.s(10);m.reference=q.s(11);m.reason=q.s(12);m.user=q.s(13);m.createdAt=q.s(14);result.push_back(m); }
    return result;
}

std::optional<Movement> Database::movementById(int movementId) const {
    Statement q(db_,"SELECT m.id,m.type,p.sku,p.name,w.name,COALESCE(GROUP_CONCAT(DISTINCT l.location),''),COALESCE(GROUP_CONCAT(DISTINCT COALESCE(NULLIF(ml.quality_status_at_movement,''),l.quality_status)),''),m.quantity,CASE WHEN m.type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT','TRANSFER_IN') THEN COALESCE(SUM(ml.quantity*ml.unit_cost)/NULLIF(SUM(ml.quantity),0),m.unit_cost) ELSE m.unit_cost END,COALESCE(GROUP_CONCAT(DISTINCT l.lot_code),''),COALESCE(NULLIF(GROUP_CONCAT(DISTINCT NULLIF(l.expiry,'')),''),m.expiry),m.reference,m.reason,COALESCE(u.full_name,'—'),m.created_at FROM movement_batches m JOIN products p ON p.id=m.product_id JOIN warehouses w ON w.id=m.warehouse_id LEFT JOIN movement_lines ml ON ml.batch_id=m.id LEFT JOIN stock_lots l ON l.id=ml.lot_id LEFT JOIN users u ON u.id=m.actor_id WHERE m.id=? GROUP BY m.id");
    q.integer(1,movementId);
    if(!q.row())return std::nullopt;
    Movement m; m.id=q.i(0);m.type=q.s(1);m.sku=q.s(2);m.product=q.s(3);m.warehouse=q.s(4);m.location=q.s(5);m.qualityStatus=q.s(6);m.quantity=q.d(7);m.unitCost=q.d(8);m.lotCode=q.s(9);m.expiry=q.s(10);m.reference=q.s(11);m.reason=q.s(12);m.user=q.s(13);m.createdAt=q.s(14);
    return m;
}

int Database::movementCount() const {Statement q(db_,"SELECT COUNT(*) FROM movement_batches");q.row();return q.i(0);}

std::vector<std::vector<std::string>> Database::movementLines(int batchId) const {
    std::vector<std::vector<std::string>> rows;Statement q(db_,"SELECT l.lot_code,l.location,l.expiry,COALESCE(NULLIF(ml.quality_status_at_movement,''),l.quality_status),ml.quantity,ml.unit_cost,COALESCE(s.name,'—'),COALESCE(ml.quality_note_at_movement,l.quality_note) FROM movement_lines ml JOIN stock_lots l ON l.id=ml.lot_id LEFT JOIN suppliers s ON s.id=l.supplier_id WHERE ml.batch_id=? ORDER BY l.received_at,l.id");q.integer(1,batchId);
    while(q.row())rows.push_back({q.s(0),q.s(1),q.s(2),q.s(3),std::to_string(q.d(4)),std::to_string(q.d(5)),q.s(6),q.s(7)});
    return rows;
}

std::vector<ReorderSuggestion> Database::reorderSuggestions() const {
    std::vector<ReorderSuggestion> result;
    auto items=products();
    const auto stores=warehouses();bool capacityKnown=!stores.empty(),hasUnboundedStore=false;double freeCapacity=0;
    for(const auto& store:stores){if(store.capacityM3<=0){hasUnboundedStore=true;continue;}freeCapacity+=std::max(0.0,store.capacityM3-store.usedM3);}
    capacityKnown=capacityKnown&&!hasUnboundedStore;
    std::unordered_map<int,double> demandByProduct,pendingByProduct,physicalByProduct;
    struct PreferredSupplier { std::string name; int leadDays{}; };
    std::unordered_map<int,PreferredSupplier> supplierByProduct;
    {Statement q(db_,"SELECT product_id,SUM(quantity)/90.0 FROM movement_batches WHERE type IN ('OUT','ADJUST_OUT') AND created_at>=datetime('now','-90 days','localtime') GROUP BY product_id");while(q.row())demandByProduct[q.i(0)]=q.d(1);}
    {Statement q(db_,"SELECT product_id,SUM(quantity-received_quantity) FROM purchase_orders WHERE status IN ('BROUILLON','ENVOYÉE','PARTIELLE') GROUP BY product_id");while(q.row())pendingByProduct[q.i(0)]=q.d(1);}
    {Statement q(db_,"SELECT product_id,SUM(quantity) FROM stock_lots WHERE quantity>0 GROUP BY product_id");while(q.row())physicalByProduct[q.i(0)]=q.d(1);}
    {Statement q(db_,"SELECT o.product_id,COALESCE(s.name,''),COALESCE(s.lead_days,0) FROM purchase_orders o LEFT JOIN suppliers s ON s.id=o.supplier_id WHERE o.id=(SELECT MAX(latest.id) FROM purchase_orders latest WHERE latest.product_id=o.product_id)");while(q.row())supplierByProduct[q.i(0)]={q.s(1),q.i(2)};}
    for(auto& p:items) {
        const double daily=demandByProduct[p.id];
        std::string supplierName; int lead=p.leadDays;
        if(const auto found=supplierByProduct.find(p.id);found!=supplierByProduct.end()){supplierName=found->second.name;if(lead==0)lead=found->second.leadDays;}
        const double pending=pendingByProduct[p.id];
        const double commitments=p.customerCommitments;
        const double point=std::max(p.minStock,p.safetyStock+daily*static_cast<double>(lead));
        const double projected=p.onHand+pending-commitments;
        if(projected>point) continue;
        double target=std::max(point,p.safetyStock+daily*(static_cast<double>(lead)+static_cast<double>(p.coverageDays)));
        if(p.maxStock>0) target=std::min(target,p.maxStock);
        double proposed=std::max(0.0,target-projected);
        if(p.maxStock>0)proposed=std::min(proposed,std::max(0.0,p.maxStock-physicalByProduct[p.id]-pending));
        bool capacityLimited=false;if(capacityKnown&&p.volumeM3>0){const double capacityQty=freeCapacity/p.volumeM3;capacityLimited=capacityQty+1e-9<proposed;proposed=std::min(proposed,capacityQty);}
        if(proposed<=0&&!capacityLimited) continue;
        const double annualDemand=daily*365.0,annualHoldingPerUnit=p.unitCost*p.holdingRate/100.0;double economicLot=0,annualOrderCost=0,annualHoldingCost=0;
        if(annualDemand>0&&p.orderCost>0&&annualHoldingPerUnit>0){economicLot=std::sqrt((2.0*annualDemand*p.orderCost)/annualHoldingPerUnit);if(economicLot>0){annualOrderCost=(annualDemand/economicLot)*p.orderCost;annualHoldingCost=(economicLot/2.0)*annualHoldingPerUnit;}}
        const double shortageExposure=std::max(0.0,-projected)*p.shortageCost;
        result.push_back({p,supplierName,daily,pending,commitments,projected,point,target,proposed,lead,capacityLimited,economicLot,annualOrderCost,annualHoldingCost,shortageExposure});
    }
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.product.onHand<b.product.onHand;});
    return result;
}

std::vector<AbcRow> Database::abcAnalysis() const {
    std::vector<AbcRow> result;
    Statement q(db_,"SELECT p.id,p.sku,p.name,COALESCE(SUM(ml.quantity*ml.unit_cost),0) AS value FROM products p LEFT JOIN movement_batches m ON m.product_id=p.id AND m.type IN ('OUT','ADJUST_OUT') AND m.created_at>=datetime('now','-365 days','localtime') LEFT JOIN movement_lines ml ON ml.batch_id=m.id WHERE p.active=1 GROUP BY p.id ORDER BY value DESC,p.sku");
    double total=0;
    while(q.row()) {
        AbcRow row; row.productId=q.i(0);row.sku=q.s(1);row.name=q.s(2);row.consumptionValue=q.d(3);total+=row.consumptionValue;result.push_back(row);
    }
    double cumulative=0;
    for(std::size_t i=0;i<result.size();++i) { auto& row=result[i];const double previousShare=total>0?cumulative/total:0;cumulative+=row.consumptionValue;row.cumulativeShare=total>0?cumulative/total:0;row.cumulativeArticleShare=result.empty()?0:static_cast<double>(i+1)/static_cast<double>(result.size());row.category=total<=0?'C':(previousShare<0.60?'A':(previousShare<0.90?'B':'C')); }
    return result;
}

std::vector<PurchaseOrder> Database::purchaseOrders() const {
    return purchaseOrdersPage(1000000,0);
}

std::vector<PurchaseOrder> Database::purchaseOrdersPage(int limit,int offset) const {
    std::vector<PurchaseOrder> rows;
    Statement q(db_,"SELECT o.id,o.reference,o.status,COALESCE(s.name,'—'),p.sku,p.name,o.quantity,o.received_quantity,o.unit_cost,o.quantity*o.unit_cost,COALESCE(u.full_name,'—'),o.created_at FROM purchase_orders o JOIN products p ON p.id=o.product_id LEFT JOIN suppliers s ON s.id=o.supplier_id LEFT JOIN users u ON u.id=o.actor_id ORDER BY o.id DESC LIMIT ? OFFSET ?");
    q.integer(1,std::clamp(limit,1,maximumRowLimit));q.integer(2,std::max(0,offset));
    while(q.row()) { PurchaseOrder p;p.id=q.i(0);p.reference=q.s(1);p.status=q.s(2);p.supplier=q.s(3);p.sku=q.s(4);p.product=q.s(5);p.quantity=q.d(6);p.receivedQuantity=q.d(7);p.unitCost=q.d(8);p.total=q.d(9);p.createdBy=q.s(10);p.createdAt=q.s(11);rows.push_back(p); }
    return rows;
}

int Database::purchaseOrderCount() const {Statement q(db_,"SELECT COUNT(*) FROM purchase_orders");q.row();return q.i(0);}

std::vector<CustomerOrder> Database::customerOrders(int limit,int offset) const {
    std::vector<CustomerOrder> rows;
    Statement q(db_,"SELECT o.id,o.reference,o.customer,o.status,p.sku,p.name,p.unit,o.quantity,o.shipped_quantity,o.requested_date,o.note,COALESCE(u.full_name,'—'),o.created_at FROM customer_orders o JOIN products p ON p.id=o.product_id LEFT JOIN users u ON u.id=o.actor_id ORDER BY o.id DESC LIMIT ? OFFSET ?");
    q.integer(1,std::clamp(limit,1,maximumRowLimit));q.integer(2,std::max(0,offset));
    while(q.row()){CustomerOrder o;o.id=q.i(0);o.reference=q.s(1);o.customer=q.s(2);o.status=q.s(3);o.sku=q.s(4);o.product=q.s(5);o.unit=q.s(6);o.quantity=q.d(7);o.shippedQuantity=q.d(8);o.requestedDate=q.s(9);o.note=q.s(10);o.createdBy=q.s(11);o.createdAt=q.s(12);rows.push_back(std::move(o));}
    return rows;
}

int Database::customerOrderCount() const {Statement q(db_,"SELECT COUNT(*) FROM customer_orders");q.row();return q.i(0);}

int Database::createCustomerOrder(const std::string& customer,int productId,double quantity,const std::string& requestedDate,const std::string& note,int actorId) {
    const auto client=trimmed(customer),cleanNote=trimmed(note);validateOptionalDate(requestedDate);
    if(client.empty()||client.size()>200||cleanNote.size()>1000)throw std::runtime_error("Indiquez un client et vérifiez la longueur des champs.");
    if(!std::isfinite(quantity)||quantity<=0)throw std::runtime_error("La quantité demandée doit être supérieure à zéro.");
    Transaction transaction(db_);
    {Statement product(db_,"SELECT 1 FROM products WHERE id=? AND active=1");product.integer(1,productId);if(!product.row())throw std::runtime_error("Article indisponible ou archivé.");}
    int expected=1;{Statement q(db_,"SELECT COALESCE(MAX(id),0)+1 FROM customer_orders");if(q.row())expected=q.i(0);}
    std::ostringstream ref;ref<<"CL-"<<std::setw(6)<<std::setfill('0')<<expected;
    Statement q(db_,"INSERT INTO customer_orders(reference,customer,product_id,quantity,requested_date,note,actor_id) VALUES(?,?,?,?,?,?,?)");q.text(1,ref.str());q.text(2,client);q.integer(3,productId);q.real(4,quantity);q.text(5,requestedDate);q.text(6,cleanNote);q.integer(7,actorId);q.done();
    const int id=static_cast<int>(sqlite3_last_insert_rowid(db_));audit(actorId,"Réservation client","commande client",id,ref.str()+" · "+client+" · "+std::to_string(quantity));transaction.commit();return id;
}

int Database::shipCustomerOrder(int orderId,double quantity,int warehouseId,const std::string& location,int actorId) {
    if(!std::isfinite(quantity)||quantity<=0)throw std::runtime_error("La quantité expédiée doit être supérieure à zéro.");
    Transaction transaction(db_);
    int productId=0;double ordered=0,shipped=0;std::string ref,status;
    {Statement q(db_,"SELECT product_id,quantity,shipped_quantity,reference,status FROM customer_orders WHERE id=?");q.integer(1,orderId);if(!q.row())throw std::runtime_error("Commande client introuvable.");productId=q.i(0);ordered=q.d(1);shipped=q.d(2);ref=q.s(3);status=q.s(4);}
    if(status!="OUVERTE"&&status!="PARTIELLE")throw std::runtime_error("Cette commande client n'est plus à expédier.");
    const double remaining=ordered-shipped;if(quantity>remaining+1e-9)throw std::runtime_error("La quantité dépasse le reliquat de "+std::to_string(remaining)+".");
    const int batchId=addMovement(productId,"OUT",quantity,0,warehouseId,location,"","","","",ref,"Expédition commande client",actorId);
    const double total=shipped+quantity;const bool complete=total>=ordered-1e-9;
    Statement update(db_,"UPDATE customer_orders SET shipped_quantity=?,status=? WHERE id=? AND status IN ('OUVERTE','PARTIELLE')");update.real(1,std::min(total,ordered));update.text(2,complete?"LIVRÉE":"PARTIELLE");update.integer(3,orderId);update.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("L'état de la commande client a changé. Rechargez la liste puis réessayez.");
    audit(actorId,"Expédition","commande client",orderId,ref+" · "+std::to_string(quantity)+" expédiée(s) · "+(complete?"terminée":"partielle"));transaction.commit();return batchId;
}

void Database::cancelCustomerOrder(int orderId,int actorId) {
    Transaction transaction(db_);Statement q(db_,"UPDATE customer_orders SET status='ANNULÉE' WHERE id=? AND status IN ('OUVERTE','PARTIELLE')");q.integer(1,orderId);q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Cette commande client n'existe plus ou n'est plus annulable.");
    audit(actorId,"Annulation","commande client",orderId,"Réservation client libérée");transaction.commit();
}

int Database::createPurchaseOrder(int productId,int supplierId,double quantity,double unitCost,int actorId) {
    if(quantity<=0||!std::isfinite(quantity)) throw std::runtime_error("La quantité commandée doit être une valeur positive valide.");
    if(!finiteNonNegative(unitCost))throw std::runtime_error("Le coût unitaire doit être une valeur positive valide.");
    Transaction transaction(db_);
        double maxStock=0,available=0;{Statement maximum(db_,"SELECT p.max_stock,COALESCE((SELECT SUM(quantity) FROM stock_lots WHERE product_id=p.id AND quantity>0),0),COALESCE((SELECT SUM(quantity-received_quantity) FROM purchase_orders WHERE product_id=p.id AND status IN ('BROUILLON','ENVOYÉE','PARTIELLE')),0) FROM products p WHERE p.id=? AND p.active=1");maximum.integer(1,productId);if(!maximum.row())throw std::runtime_error("Article indisponible.");maxStock=maximum.d(0);available=maximum.d(1)+maximum.d(2);}
        if(maxStock>0&&available+quantity>maxStock+1e-9)throw std::runtime_error("Cette commande dépasserait le stock maximal de l'article (en incluant les commandes en cours).");
        if(supplierId>0){Statement supplier(db_,"SELECT 1 FROM suppliers WHERE id=?");supplier.integer(1,supplierId);if(!supplier.row())throw std::runtime_error("Le fournisseur sélectionné n'existe plus.");}
        int expected=0;{Statement next(db_,"SELECT COALESCE(MAX(id),0)+1 FROM purchase_orders");next.row();expected=next.i(0);}const std::string ref="CMD-"+std::to_string(expected);
        Statement q(db_,"INSERT INTO purchase_orders(reference,product_id,supplier_id,quantity,unit_cost,actor_id) VALUES(?,?,?,?,?,?)");
        q.text(1,ref);q.integer(2,productId);if(supplierId>0)q.integer(3,supplierId);else q.null(3);
        q.real(4,quantity);q.real(5,unitCost);q.integer(6,actorId);q.done();
        const int id=static_cast<int>(sqlite3_last_insert_rowid(db_));
        audit(actorId,"Création","commande",id,ref);transaction.commit();return id;
}

void Database::markPurchaseOrderSent(int orderId,int actorId) {
    Transaction transaction(db_);
    Statement q(db_,"UPDATE purchase_orders SET status='ENVOYÉE',sent_at=datetime('now','localtime') WHERE id=? AND status='BROUILLON'");q.integer(1,orderId);q.done();
    if(sqlite3_changes(db_)!=1)throw std::runtime_error("Cette commande n'existe plus ou n'est plus à l'état brouillon.");
    audit(actorId,"Envoi","commande",orderId,"Commande marquée comme envoyée");
    transaction.commit();
}

int Database::receivePurchaseOrder(int orderId,double quantity,int warehouseId,const std::string& location,const std::string& lotCode,const std::string& expiry,const std::string& qualityStatus,const std::string& qualityNote,int actorId) {
    if(!std::isfinite(quantity)||quantity<=0)throw std::runtime_error("La quantité reçue doit être une valeur positive valide.");
    Transaction transaction(db_);
        int productId=0,supplierId=0;double ordered=0,received=0,cost=0;std::string ref,status;
        {Statement q(db_,"SELECT product_id,quantity,received_quantity,unit_cost,reference,status,COALESCE(supplier_id,0) FROM purchase_orders WHERE id=?");q.integer(1,orderId);if(!q.row())throw std::runtime_error("Commande introuvable.");productId=q.i(0);ordered=q.d(1);received=q.d(2);cost=q.d(3);ref=q.s(4);status=q.s(5);supplierId=q.i(6);}
        if(status!="ENVOYÉE"&&status!="PARTIELLE")throw std::runtime_error("Marquez d'abord la commande comme envoyée avant sa réception.");
        const double remaining=ordered-received;
        if(quantity>remaining+1e-9)throw std::runtime_error("La quantité saisie dépasse le reliquat de "+std::to_string(std::max(0.0,remaining))+".");
        const int batchId=addMovement(productId,"IN",quantity,cost,warehouseId,location,lotCode,expiry,qualityStatus,qualityNote,ref,"Réception de commande",actorId);
        if(supplierId>0){Statement supplier(db_,"UPDATE stock_lots SET supplier_id=? WHERE id IN (SELECT lot_id FROM movement_lines WHERE batch_id=?)");supplier.integer(1,supplierId);supplier.integer(2,batchId);supplier.done();}
        const double newReceived=received+quantity;const bool complete=newReceived>=ordered-1e-9;
        Statement update(db_,"UPDATE purchase_orders SET status=?,received_quantity=?,received_at=CASE WHEN ? THEN datetime('now','localtime') ELSE received_at END WHERE id=? AND status IN ('ENVOYÉE','PARTIELLE')");update.text(1,complete?"REÇUE":"PARTIELLE");update.real(2,std::min(newReceived,ordered));update.integer(3,complete);update.integer(4,orderId);update.done();
        if(sqlite3_changes(db_)!=1)throw std::runtime_error("L'état de cette commande a changé. Rechargez la liste puis réessayez.");
        audit(actorId,"Réception","commande",orderId,ref+" · "+std::to_string(quantity)+" reçue(s) · "+(complete?"complète":"partielle"));transaction.commit();return batchId;
}

std::vector<std::vector<std::string>> Database::expiringLots(int days) const {
    std::vector<std::vector<std::string>> rows;
    Statement q(db_,"SELECT p.sku,p.name,w.name,l.lot_code,l.expiry,l.quantity,p.unit,l.quality_status FROM stock_lots l JOIN products p ON p.id=l.product_id JOIN warehouses w ON w.id=l.warehouse_id WHERE l.quantity>0 AND l.expiry<>'' AND date(l.expiry)<=date('now','localtime','+'||?||' days') ORDER BY l.expiry,l.id");
    q.integer(1,days); while(q.row()) rows.push_back({q.s(0),q.s(1),q.s(2),q.s(3),q.s(4),std::to_string(q.d(5)),q.s(6),q.s(7)}); return rows;
}

double Database::totalStockValue() const { Statement q(db_,"SELECT COALESCE(SUM(quantity*unit_cost),0) FROM stock_lots WHERE quantity>0");q.row();return q.d(0); }
double Database::totalHoldingCost() const { Statement q(db_,"SELECT COALESCE(SUM(l.quantity*l.unit_cost*p.holding_rate/100.0),0) FROM stock_lots l JOIN products p ON p.id=l.product_id WHERE l.quantity>0");q.row();return q.d(0); }
double Database::totalVolume() const { Statement q(db_,"SELECT COALESCE(SUM(l.quantity*p.volume_m3),0) FROM stock_lots l JOIN products p ON p.id=l.product_id WHERE l.quantity>0");q.row();return q.d(0); }
double Database::physicalStock(int productId) const { Statement q(db_,"SELECT COALESCE(SUM(quantity),0) FROM stock_lots WHERE product_id=? AND quantity>0");q.integer(1,productId);q.row();return q.d(0); }
int Database::lowStockCount() const { Statement q(db_,"SELECT COUNT(*) FROM (SELECT p.id,COALESCE(SUM(CASE WHEN l.quality_status='CONFORME' AND (l.expiry='' OR date(l.expiry)>=date('now','localtime')) THEN l.quantity ELSE 0 END),0) stock FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.active=1 GROUP BY p.id HAVING stock<=p.min_stock)");q.row();return q.i(0); }
int Database::zeroStockCount() const { Statement q(db_,"SELECT COUNT(*) FROM (SELECT p.id,COALESCE(SUM(CASE WHEN l.quality_status='CONFORME' AND (l.expiry='' OR date(l.expiry)>=date('now','localtime')) THEN l.quantity ELSE 0 END),0) stock FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.active=1 GROUP BY p.id HAVING stock<=0)");q.row();return q.i(0); }
int Database::overStockCount() const { Statement q(db_,"SELECT COUNT(*) FROM (SELECT p.id,p.max_stock,COALESCE(SUM(l.quantity),0) physical FROM products p LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.active=1 AND p.max_stock>0 GROUP BY p.id HAVING physical>p.max_stock)");q.row();return q.i(0); }

std::vector<std::vector<std::string>> Database::auditLog(int limit) const {
    std::vector<std::vector<std::string>> rows; Statement q(db_,"SELECT a.created_at,COALESCE(u.full_name,'Compte supprimé'),a.action,a.entity,a.detail FROM audit_log a LEFT JOIN users u ON u.id=a.actor_id ORDER BY a.id DESC LIMIT ?");q.integer(1,limit);
    while(q.row()) rows.push_back({q.s(0),q.s(1),q.s(2),q.s(3),q.s(4)});
    return rows;
}

std::string Database::appSetting(const std::string& key, const std::string& fallback) const {
    Statement q(db_,"SELECT value FROM app_meta WHERE key=?");
    q.text(1,key);
    return q.row()?q.s(0):fallback;
}

void Database::setAppSetting(const std::string& key, const std::string& value) {
    if(key.empty()||key.size()>120||value.size()>4000)throw std::runtime_error("Préférence invalide.");
    Statement q(db_,"INSERT INTO app_meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value");
    q.text(1,key);q.text(2,value);q.done();
}

void Database::backupTo(const std::filesystem::path& destination) const {
    if(destination.empty())throw std::runtime_error("Choisissez un emplacement de sauvegarde.");
    std::error_code pathError;
    if(std::filesystem::equivalent(destination,path_,pathError)&&!pathError)throw std::runtime_error("La sauvegarde doit être créée dans un fichier différent de la base active.");
    sqlite3* target=nullptr;
    const auto utf8Destination=pathToUtf8(destination);
    if(sqlite3_open_v2(utf8Destination.c_str(),&target,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,nullptr)!=SQLITE_OK) {
        const std::string msg=target?sqlite3_errmsg(target):"Création impossible"; if(target)sqlite3_close(target); throw std::runtime_error(msg);
    }
    sqlite3_backup* backup=sqlite3_backup_init(target,"main",db_,"main");
    if(!backup) { const std::string msg=sqlite3_errmsg(target);sqlite3_close(target);throw std::runtime_error(msg); }
    const int rc=completeBackup(backup);const int finish=sqlite3_backup_finish(backup);
    if(rc!=SQLITE_DONE||finish!=SQLITE_OK) { const std::string msg=sqlite3_errmsg(target);sqlite3_close(target);throw std::runtime_error(msg); }
    sqlite3_close(target);
#ifndef _WIN32
    std::filesystem::permissions(destination,std::filesystem::perms::owner_read|std::filesystem::perms::owner_write,std::filesystem::perm_options::replace);
#endif
}

void Database::restoreFrom(const std::filesystem::path& source) {
    std::error_code pathError;
    if(std::filesystem::equivalent(source,path_,pathError)&&!pathError)throw std::runtime_error("Choisissez un fichier de sauvegarde différent de la base actuellement utilisée.");
    sqlite3* input=nullptr;const auto utf8Source=pathToUtf8(source);
    if(sqlite3_open_v2(utf8Source.c_str(),&input,SQLITE_OPEN_READONLY|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){const std::string message=input?sqlite3_errmsg(input):"Impossible d'ouvrir la sauvegarde.";if(input)sqlite3_close(input);throw std::runtime_error(message);}
    try {
        Statement schema(input,"SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name IN ('users','products','warehouses','stock_lots')");
        if(!schema.row()||schema.i(0)!=4)throw std::runtime_error("Ce fichier n'est pas une sauvegarde Gestion des stocks valide.");
        {Statement integrity(input,"PRAGMA quick_check");if(!integrity.row()||integrity.s(0)!="ok")throw std::runtime_error("La sauvegarde est endommagée et ne peut pas être restaurée.");}
        {Statement foreignKeys(input,"PRAGMA foreign_key_check");if(foreignKeys.row())throw std::runtime_error("La sauvegarde contient des références incohérentes et ne peut pas être restaurée.");}
    } catch(...) { sqlite3_close(input); throw; }
    sqlite3_backup* backup=sqlite3_backup_init(db_,"main",input,"main");
    if(!backup){const std::string message=sqlite3_errmsg(db_);sqlite3_close(input);throw std::runtime_error(message);}
    const int rc=completeBackup(backup);const int finish=sqlite3_backup_finish(backup);const std::string message=sqlite3_errmsg(db_);sqlite3_close(input);
    if(rc!=SQLITE_DONE||finish!=SQLITE_OK)throw std::runtime_error(message);
    initialize();
}

std::string Database::databaseTime() const { Statement q(db_,"SELECT datetime('now','localtime')");q.row();return q.s(0); }

namespace {
bool validSalesType(const std::string& type) {
    return type == "PROFORMA" || type == "BL" || type == "COUNTER" || type == "INVOICE" || type == "RETURN";
}

std::string salesStatus(const std::string& type) {
    if (type == "PROFORMA") return "BROUILLON";
    if (type == "BL") return "LIVRÉ";
    return "VALIDÉ";
}

std::string referencePrefix(Database& database, const std::string& type) {
    if (type == "PROFORMA") return database.appSetting("sales.proforma_prefix", "PRO");
    if (type == "BL") return database.appSetting("sales.delivery_prefix", "BL");
    if (type == "COUNTER") return database.appSetting("sales.counter_prefix", "CPT");
    if (type == "RETURN") return database.appSetting("sales.return_prefix", "RET");
    return database.appSetting("sales.invoice_prefix", "FAC");
}
}

std::vector<Customer> Database::customers(const std::string& filter) const {
    std::vector<Customer> result;
    const auto search = "%" + trimmed(filter) + "%";
    Statement q(db_, "SELECT id,code,name,contact,phone,email,address,tax_id,discount_rate,payment_terms,notes,active,created_at "
                     "FROM customers WHERE active=1 AND (?='%%' OR name LIKE ? COLLATE NOCASE OR code LIKE ? COLLATE NOCASE OR contact LIKE ? COLLATE NOCASE) "
                     "ORDER BY name COLLATE NOCASE");
    q.text(1, search); q.text(2, search); q.text(3, search); q.text(4, search);
    while (q.row()) {
        Customer c;
        c.id = q.i(0); c.code = q.s(1); c.name = q.s(2); c.contact = q.s(3); c.phone = q.s(4);
        c.email = q.s(5); c.address = q.s(6); c.taxId = q.s(7); c.discountRate = q.d(8);
        c.paymentTerms = q.s(9); c.notes = q.s(10); c.active = q.i(11) != 0; c.createdAt = q.s(12);
        result.push_back(c);
    }
    return result;
}

Customer Database::customer(int customerId) const {
    Statement q(db_, "SELECT id,code,name,contact,phone,email,address,tax_id,discount_rate,payment_terms,notes,active,created_at FROM customers WHERE id=?");
    q.integer(1, customerId);
    if (!q.row()) throw std::runtime_error("Ce client n'existe pas.");
    Customer c;
    c.id = q.i(0); c.code = q.s(1); c.name = q.s(2); c.contact = q.s(3); c.phone = q.s(4);
    c.email = q.s(5); c.address = q.s(6); c.taxId = q.s(7); c.discountRate = q.d(8);
    c.paymentTerms = q.s(9); c.notes = q.s(10); c.active = q.i(11) != 0; c.createdAt = q.s(12);
    return c;
}

void Database::saveCustomer(const Customer& input, int actorId) {
    const auto name = trimmed(input.name), code = trimmed(input.code), contact = trimmed(input.contact);
    const auto phone = trimmed(input.phone), email = trimmed(input.email), address = trimmed(input.address);
    const auto taxId = trimmed(input.taxId), terms = trimmed(input.paymentTerms), notes = trimmed(input.notes);
    if (name.empty() || name.size() > 200) throw std::runtime_error("Le nom du client est obligatoire (200 caractères maximum).");
    if (!finiteNonNegative(input.discountRate) || input.discountRate > 100) throw std::runtime_error("La remise client doit être comprise entre 0 et 100 %.");
    Transaction transaction(db_);
    int id = input.id;
    if (id == 0) {
        Statement q(db_, "INSERT INTO customers(code,name,contact,phone,email,address,tax_id,discount_rate,payment_terms,notes,active) VALUES(?,?,?,?,?,?,?,?,?,?,1)");
        q.text(1, code); q.text(2, name); q.text(3, contact); q.text(4, phone); q.text(5, email);
        q.text(6, address); q.text(7, taxId); q.real(8, input.discountRate); q.text(9, terms); q.text(10, notes); q.done();
        id = static_cast<int>(sqlite3_last_insert_rowid(db_));
        if (code.empty()) {
            std::ostringstream generated;
            generated << "CLI-" << std::setw(4) << std::setfill('0') << id;
            Statement update(db_, "UPDATE customers SET code=? WHERE id=?"); update.text(1, generated.str()); update.integer(2, id); update.done();
        }
        audit(actorId, "Création", "client", id, name);
    } else {
        Statement q(db_, "UPDATE customers SET code=?,name=?,contact=?,phone=?,email=?,address=?,tax_id=?,discount_rate=?,payment_terms=?,notes=?,active=? WHERE id=?");
        q.text(1, code); q.text(2, name); q.text(3, contact); q.text(4, phone); q.text(5, email);
        q.text(6, address); q.text(7, taxId); q.real(8, input.discountRate); q.text(9, terms); q.text(10, notes);
        q.integer(11, input.active ? 1 : 0); q.integer(12, id); q.done();
        if (sqlite3_changes(db_) != 1) throw std::runtime_error("Ce client n'existe plus.");
        audit(actorId, "Modification", "client", id, name);
    }
    transaction.commit();
}

void Database::setCustomerActive(int customerId, bool active, int actorId) {
    Transaction transaction(db_);
    Statement q(db_, "UPDATE customers SET active=? WHERE id=?");
    q.integer(1, active ? 1 : 0); q.integer(2, customerId); q.done();
    if (sqlite3_changes(db_) != 1) throw std::runtime_error("Ce client n'existe plus.");
    audit(actorId, active ? "Réactivation" : "Archivage", "client", customerId, "");
    transaction.commit();
}

double Database::customerPrice(int customerId, int productId, double basePrice) const {
    if (customerId <= 0 || productId <= 0) return basePrice;
    Statement q(db_, "SELECT price FROM customer_prices WHERE customer_id=? AND product_id=?");
    q.integer(1, customerId); q.integer(2, productId);
    return q.row() ? q.d(0) : basePrice;
}

void Database::setCustomerPrice(int customerId, int productId, double price, int actorId) {
    if (customerId <= 0 || productId <= 0) throw std::runtime_error("Sélectionnez un client et un article.");
    if (!finiteNonNegative(price)) throw std::runtime_error("Le prix doit être une valeur positive.");
    Transaction transaction(db_);
    if (price <= 0) {
        Statement q(db_, "DELETE FROM customer_prices WHERE customer_id=? AND product_id=?");
        q.integer(1, customerId); q.integer(2, productId); q.done();
    } else {
        Statement q(db_, "INSERT INTO customer_prices(customer_id,product_id,price) VALUES(?,?,?) ON CONFLICT(customer_id,product_id) DO UPDATE SET price=excluded.price,updated_at=datetime('now','localtime')");
        q.integer(1, customerId); q.integer(2, productId); q.real(3, price); q.done();
    }
    audit(actorId, "Tarif client", "client", customerId, "Article " + std::to_string(productId));
    transaction.commit();
}

std::vector<SalesDocument> Database::salesDocuments(int limit, int offset, const std::string& type) const {
    std::vector<SalesDocument> result;
    Statement q(db_, "SELECT d.id,d.reference,d.type,d.customer_id,d.customer_name,d.status,d.document_date,d.due_date,"
                     "d.warehouse_id,COALESCE(w.name,''),d.source_document_id,d.subtotal,d.discount_total,d.tax_rate,d.tax_total,d.total,"
                     "d.note,COALESCE(u.full_name,'—'),d.created_at,(SELECT COUNT(*) FROM sales_lines l WHERE l.document_id=d.id) "
                     "FROM sales_documents d LEFT JOIN warehouses w ON w.id=d.warehouse_id LEFT JOIN users u ON u.id=d.actor_id "
                     "WHERE (?='' OR d.type=?) ORDER BY d.document_date DESC,d.id DESC LIMIT ? OFFSET ?");
    q.text(1, type); q.text(2, type); q.integer(3, std::clamp(limit, 1, maximumRowLimit)); q.integer(4, std::max(0, offset));
    while (q.row()) {
        SalesDocument d;
        d.id = q.i(0); d.reference = q.s(1); d.type = q.s(2); d.customerId = q.i(3); d.customerName = q.s(4);
        d.status = q.s(5); d.documentDate = q.s(6); d.dueDate = q.s(7); d.warehouseId = q.i(8); d.warehouse = q.s(9);
        d.sourceDocumentId = q.i(10); d.subtotal = q.d(11); d.discountTotal = q.d(12); d.taxRate = q.d(13);
        d.taxTotal = q.d(14); d.total = q.d(15); d.note = q.s(16); d.createdBy = q.s(17); d.createdAt = q.s(18);
        d.lineCount = q.i(19);
        result.push_back(d);
    }
    return result;
}

int Database::salesDocumentCount(const std::string& type) const {
    Statement q(db_, "SELECT COUNT(*) FROM sales_documents WHERE (?='' OR type=?)");
    q.text(1, type); q.text(2, type); q.row(); return q.i(0);
}

SalesDocument Database::salesDocument(int documentId) const {
    Statement q(db_, "SELECT d.id,d.reference,d.type,d.customer_id,d.customer_name,d.status,d.document_date,d.due_date,"
                     "d.warehouse_id,COALESCE(w.name,''),d.source_document_id,d.subtotal,d.discount_total,d.tax_rate,d.tax_total,d.total,"
                     "d.note,COALESCE(u.full_name,'—'),d.created_at,(SELECT COUNT(*) FROM sales_lines l WHERE l.document_id=d.id) "
                     "FROM sales_documents d LEFT JOIN warehouses w ON w.id=d.warehouse_id LEFT JOIN users u ON u.id=d.actor_id WHERE d.id=?");
    q.integer(1, documentId);
    if (!q.row()) throw std::runtime_error("Ce document de vente n'existe pas.");
    SalesDocument d;
    d.id = q.i(0); d.reference = q.s(1); d.type = q.s(2); d.customerId = q.i(3); d.customerName = q.s(4);
    d.status = q.s(5); d.documentDate = q.s(6); d.dueDate = q.s(7); d.warehouseId = q.i(8); d.warehouse = q.s(9);
    d.sourceDocumentId = q.i(10); d.subtotal = q.d(11); d.discountTotal = q.d(12); d.taxRate = q.d(13);
    d.taxTotal = q.d(14); d.total = q.d(15); d.note = q.s(16); d.createdBy = q.s(17); d.createdAt = q.s(18);
    d.lineCount = q.i(19);
    return d;
}

std::vector<SalesLine> Database::salesLines(int documentId) const {
    std::vector<SalesLine> result;
    Statement q(db_, "SELECT l.id,l.document_id,l.product_id,p.sku,p.name,p.unit,l.quantity,l.unit_price,l.unit_cost,l.discount_rate,l.tax_rate,l.line_total,l.expiry "
                     "FROM sales_lines l JOIN products p ON p.id=l.product_id WHERE l.document_id=? ORDER BY l.id");
    q.integer(1, documentId);
    while (q.row()) {
        SalesLine line;
        line.id = q.i(0); line.documentId = q.i(1); line.productId = q.i(2); line.sku = q.s(3); line.name = q.s(4);
        line.unit = q.s(5); line.quantity = q.d(6); line.unitPrice = q.d(7); line.unitCost = q.d(8);
        line.discountRate = q.d(9); line.taxRate = q.d(10); line.lineTotal = q.d(11); line.expiry = q.s(12);
        result.push_back(line);
    }
    return result;
}

int Database::createSalesDocument(const std::string& type, int customerId, const std::string& customerName,
                                  int warehouseId, const std::string& documentDate, const std::string& note,
                                  double taxRate, double discountRate, const std::vector<SalesLineInput>& lines,
                                  int sourceDocumentId, int actorId) {
    if (!validSalesType(type)) throw std::runtime_error("Type de document de vente inconnu.");
    if (lines.empty()) throw std::runtime_error("Ajoutez au moins une ligne au document.");
    if (!std::isfinite(taxRate) || taxRate < 0 || taxRate > 100) throw std::runtime_error("Le taux de TVA doit être compris entre 0 et 100 %.");
    if (!std::isfinite(discountRate) || discountRate < 0 || discountRate > 100) throw std::runtime_error("La remise doit être comprise entre 0 et 100 %.");
    if (warehouseId <= 0) throw std::runtime_error("Sélectionnez un entrepôt.");
    validateOptionalDate(documentDate);
    Transaction transaction(db_);
    std::string clientName = trimmed(customerName);
    if (customerId > 0) {
        Statement c(db_, "SELECT name FROM customers WHERE id=? AND active=1"); c.integer(1, customerId);
        if (!c.row()) throw std::runtime_error("Ce client n'existe plus.");
        if (clientName.empty()) clientName = c.s(0);
    }
    if (clientName.empty() && type == "COUNTER") clientName = "Client de passage";
    if (clientName.empty() || clientName.size() > 200) throw std::runtime_error("Indiquez le nom du client (200 caractères maximum).");
    {Statement w(db_, "SELECT 1 FROM warehouses WHERE id=?"); w.integer(1, warehouseId); if (!w.row()) throw std::runtime_error("Cet entrepôt n'existe plus.");}
    std::string date = documentDate;
    if (date.empty()) {Statement d(db_, "SELECT date('now','localtime')"); d.row(); date = d.s(0);}
    struct CheckedLine { SalesLineInput input; double cost{}; bool perishable{}; };
    std::vector<CheckedLine> checked;
    checked.reserve(lines.size());
    double subtotal = 0, discountTotal = 0;
    for (const auto& input : lines) {
        if (input.productId <= 0) throw std::runtime_error("Sélectionnez un article sur chaque ligne.");
        if (!std::isfinite(input.quantity) || input.quantity <= 0) throw std::runtime_error("Chaque quantité doit être supérieure à zéro.");
        if (!finiteNonNegative(input.unitPrice)) throw std::runtime_error("Chaque prix doit être une valeur positive.");
        if (!finiteNonNegative(input.discountRate) || input.discountRate > 100) throw std::runtime_error("Chaque remise doit être comprise entre 0 et 100 %.");
        if (!finiteNonNegative(input.taxRate) || input.taxRate > 100) throw std::runtime_error("Chaque taux de TVA doit être compris entre 0 et 100 %.");
        validateOptionalDate(input.expiry);
        Statement p(db_, "SELECT unit_cost,perishable FROM products WHERE id=? AND active=1");
        p.integer(1, input.productId);
        if (!p.row()) throw std::runtime_error("Un article du document n'est plus disponible.");
        if (type == "RETURN" && p.i(1) != 0 && input.expiry.empty()) throw std::runtime_error("Une date de péremption est requise pour un retour d'article périssable.");
        checked.push_back({input, p.d(0), p.i(1) != 0});
        const double gross = input.quantity * input.unitPrice;
        subtotal += gross;
        discountTotal += gross * input.discountRate / 100.0;
    }
    const double net = subtotal - discountTotal;
    const double overallDiscount = net * discountRate / 100.0;
    const double taxable = net - overallDiscount;
    const double taxTotal = taxable * taxRate / 100.0;
    const double total = taxable + taxTotal;
    int id = 0;
    {Statement next(db_, "SELECT COALESCE(MAX(id),0)+1 FROM sales_documents"); next.row(); id = next.i(0);}
    std::string compact = date;
    compact.erase(std::remove(compact.begin(), compact.end(), '-'), compact.end());
    std::ostringstream refStream;
    refStream << referencePrefix(*this, type) << '-' << compact << '-' << std::setw(4) << std::setfill('0') << id;
    const std::string reference = refStream.str();
    const std::string status = salesStatus(type);
    {
        Statement q(db_, "INSERT INTO sales_documents(id,reference,type,customer_id,customer_name,status,document_date,warehouse_id,source_document_id,subtotal,discount_total,tax_rate,tax_total,total,note,actor_id) "
                         "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        q.integer(1, id); q.text(2, reference); q.text(3, type);
        if (customerId > 0) q.integer(4, customerId); else q.null(4);
        q.text(5, clientName); q.text(6, status); q.text(7, date); q.integer(8, warehouseId);
        if (sourceDocumentId > 0) q.integer(9, sourceDocumentId); else q.null(9);
        q.real(10, subtotal); q.real(11, discountTotal + overallDiscount); q.real(12, taxRate); q.real(13, taxTotal);
        q.real(14, total); q.text(15, trimmed(note)); q.integer(16, actorId); q.done();
    }
    int lineNumber = 0;
    for (const auto& line : checked) {
        ++lineNumber;
        const double lineTotal = line.input.quantity * line.input.unitPrice * (1.0 - line.input.discountRate / 100.0);
        {Statement q(db_, "INSERT INTO sales_lines(document_id,product_id,quantity,unit_price,unit_cost,discount_rate,tax_rate,line_total,expiry) VALUES(?,?,?,?,?,?,?,?,?)");
         q.integer(1, id); q.integer(2, line.input.productId); q.real(3, line.input.quantity); q.real(4, line.input.unitPrice);
         q.real(5, line.cost); q.real(6, line.input.discountRate); q.real(7, line.input.taxRate); q.real(8, lineTotal);
         q.text(9, line.input.expiry); q.done();}
        if (type == "PROFORMA" || sourceDocumentId > 0) continue;
        if (type == "RETURN") {
            const std::string lot = "RET-" + reference + "-" + std::to_string(lineNumber);
            addMovement(line.input.productId, "RETURN", line.input.quantity, line.cost, warehouseId, "", lot, line.input.expiry,
                        "À contrôler", "Retour client", reference, "Retour client", actorId);
        } else {
            const std::string reason = type == "BL" ? "Bon de livraison" : (type == "COUNTER" ? "Vente comptoir" : "Facture de vente");
            addMovement(line.input.productId, "OUT", line.input.quantity, 0, warehouseId, "", "", "", "", "",
                        reference, reason, actorId);
        }
    }
    audit(actorId, "Document de vente", "vente", id, reference + " · " + clientName);
    transaction.commit();
    return id;
}

int Database::invoiceFromDelivery(int documentId, int actorId) {
    const auto source = salesDocument(documentId);
    if (source.type != "BL") throw std::runtime_error("Seul un bon de livraison peut être facturé.");
    const auto sourceLines = salesLines(documentId);
    if (sourceLines.empty()) throw std::runtime_error("Ce bon de livraison ne contient aucune ligne.");
    std::vector<SalesLineInput> lines;
    lines.reserve(sourceLines.size());
    for (const auto& line : sourceLines) {
        lines.push_back({line.productId, line.quantity, line.unitPrice, line.discountRate, line.taxRate, line.expiry});
    }
    return createSalesDocument("INVOICE", source.customerId, source.customerName, source.warehouseId, "", "Facture issue du " + source.reference,
                               source.taxRate, 0, lines, source.id, actorId);
}

std::vector<DeadStockRow> Database::deadStockRows(int months) const {
    std::vector<DeadStockRow> result;
    const int safeMonths = std::clamp(months, 1, 120);
    Statement q(db_, "SELECT * FROM (SELECT p.id,p.sku,p.name,p.category,"
                     "COALESCE((SELECT SUM(l.quantity) FROM stock_lots l WHERE l.product_id=p.id AND l.quantity>0),0) quantity,"
                     "COALESCE((SELECT SUM(l.quantity*l.unit_cost) FROM stock_lots l WHERE l.product_id=p.id AND l.quantity>0),0) value,"
                     "COALESCE((SELECT MAX(m.created_at) FROM movement_batches m WHERE m.product_id=p.id AND m.type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT')),'') last_out,"
                     "CAST(julianday('now','localtime')-julianday(COALESCE((SELECT MAX(m.created_at) FROM movement_batches m WHERE m.product_id=p.id AND m.type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT')),p.created_at)) AS INTEGER) days "
                     "FROM products p WHERE p.active=1) WHERE quantity>0 AND (last_out='' OR last_out<datetime('now','localtime',?)) ORDER BY value DESC");
    q.text(1, "-" + std::to_string(safeMonths) + " months");
    while (q.row()) {
        DeadStockRow row;
        row.productId = q.i(0); row.sku = q.s(1); row.name = q.s(2); row.category = q.s(3);
        row.quantity = q.d(4); row.value = q.d(5); row.lastOut = q.s(6); row.daysSinceOut = q.i(7);
        result.push_back(row);
    }
    return result;
}

std::vector<FlowStatRow> Database::flowStats(int days) const {
    std::vector<FlowStatRow> result;
    const int safeDays = std::clamp(days, 1, 3650);
    const std::string modifier = "-" + std::to_string(safeDays) + " days";
    Statement q(db_, "WITH flows AS (SELECT product_id,"
                     "SUM(CASE WHEN type IN ('IN','ADJUST_IN','RETURN','TRANSFER_IN') THEN quantity ELSE 0 END) inbound,"
                     "SUM(CASE WHEN type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT') THEN quantity ELSE 0 END) outbound,"
                     "SUM(CASE WHEN type IN ('IN','ADJUST_IN','RETURN','TRANSFER_IN') THEN quantity*unit_cost ELSE 0 END) inbound_value,"
                     "SUM(CASE WHEN type IN ('OUT','ADJUST_OUT','WRITE_OFF','TRANSFER_OUT') THEN quantity*unit_cost ELSE 0 END) outbound_value "
                     "FROM movement_batches WHERE created_at>=datetime('now','localtime',?) GROUP BY product_id),"
                     "stock AS (SELECT product_id,SUM(quantity) quantity FROM stock_lots WHERE quantity>0 GROUP BY product_id),"
                     "last_move AS (SELECT product_id,MAX(created_at) last_at FROM movement_batches GROUP BY product_id) "
                     "SELECT p.id,p.sku,p.name,p.unit,COALESCE(f.inbound,0),COALESCE(f.outbound,0),COALESCE(f.inbound_value,0),"
                     "COALESCE(f.outbound_value,0),COALESCE(s.quantity,0),COALESCE(l.last_at,'') "
                     "FROM products p LEFT JOIN flows f ON f.product_id=p.id LEFT JOIN stock s ON s.product_id=p.id "
                     "LEFT JOIN last_move l ON l.product_id=p.id WHERE p.active=1 ORDER BY COALESCE(f.outbound,0) DESC,p.name COLLATE NOCASE");
    q.text(1, modifier);
    while (q.row()) {
        FlowStatRow row;
        row.productId = q.i(0); row.sku = q.s(1); row.name = q.s(2); row.unit = q.s(3);
        row.inbound = q.d(4); row.outbound = q.d(5); row.inboundValue = q.d(6); row.outboundValue = q.d(7);
        row.currentQuantity = q.d(8); row.lastMovement = q.s(9);
        row.averageDailyOut = row.outbound / static_cast<double>(safeDays);
        row.coverageDays = row.averageDailyOut > 1e-9 ? row.currentQuantity / row.averageDailyOut : (row.currentQuantity > 0 ? 9999 : 0);
        if (row.currentQuantity <= 1e-9) row.status = "RUPTURE";
        else if (row.outbound <= 1e-9) row.status = "DORMANT";
        else if (row.coverageDays < 15) row.status = "ALERTE";
        else row.status = "OK";
        result.push_back(row);
    }
    return result;
}

std::vector<PmpRow> Database::pmpValuation() const {
    std::vector<PmpRow> result;
    Statement q(db_, "SELECT p.id,p.sku,p.name,p.unit,COALESCE(SUM(l.quantity),0) quantity,"
                     "COALESCE(SUM(l.quantity*l.unit_cost),0) value,p.unit_cost FROM products p "
                     "LEFT JOIN stock_lots l ON l.product_id=p.id AND l.quantity>0 WHERE p.active=1 "
                     "GROUP BY p.id ORDER BY p.name COLLATE NOCASE");
    while (q.row()) {
        PmpRow row;
        row.productId = q.i(0); row.sku = q.s(1); row.name = q.s(2); row.unit = q.s(3);
        row.quantity = q.d(4); row.value = q.d(5); row.unitCost = q.d(6);
        row.pmp = row.quantity > 1e-9 ? row.value / row.quantity : 0;
        row.variance = row.pmp - row.unitCost;
        result.push_back(row);
    }
    return result;
}

int Database::expiringLotCount(int days) const {
    Statement q(db_, "SELECT COUNT(*) FROM stock_lots WHERE quantity>0 AND expiry<>'' AND date(expiry)<=date('now','localtime',?)");
    q.text(1, "+" + std::to_string(std::clamp(days, 0, 3650)) + " days");
    q.row(); return q.i(0);
}
