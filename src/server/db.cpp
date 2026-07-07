#include "server/db.h"
#include "engine/sha256.h"
#include <sqlite3.h>
#include <cctype>
#include <cstdio>

static const char* SCHEMA = R"sql(
CREATE TABLE IF NOT EXISTS accounts(
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT UNIQUE NOT NULL COLLATE NOCASE,
  salt TEXT NOT NULL,
  hash TEXT NOT NULL,
  created TEXT NOT NULL DEFAULT (datetime('now')),
  weapon INTEGER NOT NULL DEFAULT 0,
  skin INTEGER NOT NULL DEFAULT 0,
  kills INTEGER NOT NULL DEFAULT 0,
  deaths INTEGER NOT NULL DEFAULT 0,
  wins INTEGER NOT NULL DEFAULT 0,
  losses INTEGER NOT NULL DEFAULT 0,
  matches INTEGER NOT NULL DEFAULT 0,
  paint INTEGER NOT NULL DEFAULT 0
);
)sql";

static bool validName(const std::string& s) {
    if (s.size() < 3 || s.size() > 16) return false;
    for (char c : s)
        if (!isalnum((unsigned char)c) && c != '_') return false;
    return true;
}

bool DB::open(const std::string& path, std::string& err) {
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        err = sqlite3_errmsg(db);
        return false;
    }
    char* e = nullptr;
    if (sqlite3_exec(db, SCHEMA, nullptr, nullptr, &e) != SQLITE_OK) {
        err = e ? e : "schema error";
        sqlite3_free(e);
        return false;
    }
    // migrations; each fails harmlessly if the column already exists
    sqlite3_exec(db, "ALTER TABLE accounts ADD COLUMN sub INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE accounts ADD COLUMN hat INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE accounts ADD COLUMN coins INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    sqlite3_exec(db, "ALTER TABLE accounts ADD COLUMN hats INTEGER NOT NULL DEFAULT 1", nullptr, nullptr, nullptr);
    return true;
}

// password hashes: "p2$<iters>$<hex>" = PBKDF2-HMAC-SHA256; bare 64-hex = legacy
// salted sha256 (upgraded in place on the next successful login)
static constexpr int PBKDF2_ITERS = 60000;

static std::string makeHash(const std::string& salt, const std::string& pass) {
    char buf[16];
    snprintf(buf, sizeof buf, "p2$%d$", PBKDF2_ITERS);
    return buf + pbkdf2_hex(pass, salt, PBKDF2_ITERS);
}

static bool verifyHash(const std::string& stored, const std::string& salt, const std::string& pass, bool& legacy) {
    legacy = false;
    if (stored.rfind("p2$", 0) == 0) {
        size_t d = stored.find('$', 3);
        if (d == std::string::npos) return false;
        int iters = atoi(stored.substr(3, d - 3).c_str());
        if (iters < 1000 || iters > 10000000) return false;
        return pbkdf2_hex(pass, salt, iters) == stored.substr(d + 1);
    }
    legacy = true;
    return sha256_hex(salt + pass) == stored;
}

void DB::close() {
    if (db) sqlite3_close(db);
    db = nullptr;
}

static const char* ACC_COLS = "weapon,skin,sub,hat,coins,hats,kills,deaths,wins,losses,matches,paint";

static void readAccountRow(sqlite3_stmt* st, int col0, Account& a) {
    a.weapon = (u8)sqlite3_column_int(st, col0);
    a.skin = (u8)sqlite3_column_int(st, col0 + 1);
    a.sub = (u8)sqlite3_column_int(st, col0 + 2);
    a.hat = (u8)sqlite3_column_int(st, col0 + 3);
    a.coins = (u32)sqlite3_column_int(st, col0 + 4);
    a.hatsOwned = (u16)(sqlite3_column_int(st, col0 + 5) | 1);
    a.kills = sqlite3_column_int(st, col0 + 6);
    a.deaths = sqlite3_column_int(st, col0 + 7);
    a.wins = sqlite3_column_int(st, col0 + 8);
    a.losses = sqlite3_column_int(st, col0 + 9);
    a.matches = sqlite3_column_int(st, col0 + 10);
    a.paint = sqlite3_column_int(st, col0 + 11);
    if (a.weapon >= W_COUNT) a.weapon = W_SPLATTERSHOT;
    if (a.skin >= SKIN_COUNT) a.skin = 0;
    if (a.sub >= SUB_COUNT) a.sub = SUB_SPLAT_BOMB;
    if (a.hat >= HAT_COUNT || !(a.hatsOwned & (1 << a.hat))) a.hat = 0;
}

bool DB::registerAccount(const std::string& name, const std::string& pass, Account& out, std::string& err) {
    if (!validName(name)) { err = "Name must be 3-16 letters/digits/underscore"; return false; }
    if (pass.size() < 3 || pass.size() > 64) { err = "Password must be 3-64 characters"; return false; }
    std::string salt = random_hex(16);
    std::string hash = makeHash(salt, pass);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "INSERT INTO accounts(name,salt,hash) VALUES(?,?,?)", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, hash.c_str(), -1, SQLITE_TRANSIENT);
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        err = (rc == SQLITE_CONSTRAINT) ? "That name is already taken" : "Database error";
        return false;
    }
    out = Account{};
    out.id = sqlite3_last_insert_rowid(db);
    out.name = name;
    return true;
}

bool DB::login(const std::string& name, const std::string& pass, Account& out, std::string& err) {
    sqlite3_stmt* st = nullptr;
    std::string q = std::string("SELECT id,name,salt,hash,") + ACC_COLS + " FROM accounts WHERE name=?";
    sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool okRow = sqlite3_step(st) == SQLITE_ROW;
    if (!okRow) {
        sqlite3_finalize(st);
        err = "No such account";
        return false;
    }
    std::string salt = (const char*)sqlite3_column_text(st, 2);
    std::string hash = (const char*)sqlite3_column_text(st, 3);
    bool legacy = false;
    if (!verifyHash(hash, salt, pass, legacy)) {
        sqlite3_finalize(st);
        err = "Wrong password";
        return false;
    }
    out.id = sqlite3_column_int64(st, 0);
    out.name = (const char*)sqlite3_column_text(st, 1);
    readAccountRow(st, 4, out);
    sqlite3_finalize(st);

    if (legacy) {   // upgrade old sha256 hashes to PBKDF2 in place
        std::string nh = makeHash(salt, pass);
        sqlite3_stmt* up = nullptr;
        sqlite3_prepare_v2(db, "UPDATE accounts SET hash=? WHERE id=?", -1, &up, nullptr);
        sqlite3_bind_text(up, 1, nh.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(up, 2, out.id);
        sqlite3_step(up);
        sqlite3_finalize(up);
    }
    return true;
}

bool DB::loadAccount(int64_t id, Account& out) {
    sqlite3_stmt* st = nullptr;
    std::string q = std::string("SELECT id,name,") + ACC_COLS + " FROM accounts WHERE id=?";
    sqlite3_prepare_v2(db, q.c_str(), -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    bool ok = sqlite3_step(st) == SQLITE_ROW;
    if (ok) {
        out.id = sqlite3_column_int64(st, 0);
        out.name = (const char*)sqlite3_column_text(st, 1);
        readAccountRow(st, 2, out);
    }
    sqlite3_finalize(st);
    return ok;
}

void DB::setLoadout(int64_t id, u8 weapon, u8 skin, u8 sub, u8 hat) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "UPDATE accounts SET weapon=?,skin=?,sub=?,hat=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, weapon);
    sqlite3_bind_int(st, 2, skin);
    sqlite3_bind_int(st, 3, sub);
    sqlite3_bind_int(st, 4, hat);
    sqlite3_bind_int64(st, 5, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void DB::updateCoinsHats(int64_t id, u32 coins, u16 hats) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "UPDATE accounts SET coins=?,hats=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, (int)coins);
    sqlite3_bind_int(st, 2, hats);
    sqlite3_bind_int64(st, 3, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void DB::addMatchStats(int64_t id, int kills, int deaths, int paint, bool won, bool lost, int coins) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE accounts SET kills=kills+?,deaths=deaths+?,paint=paint+?,"
        "wins=wins+?,losses=losses+?,matches=matches+1,coins=coins+? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, kills);
    sqlite3_bind_int(st, 2, deaths);
    sqlite3_bind_int(st, 3, paint);
    sqlite3_bind_int(st, 4, won ? 1 : 0);
    sqlite3_bind_int(st, 5, lost ? 1 : 0);
    sqlite3_bind_int(st, 6, coins);
    sqlite3_bind_int64(st, 7, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}
