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
    // migration: sub-weapon column (added in v2); fails harmlessly if it exists
    sqlite3_exec(db, "ALTER TABLE accounts ADD COLUMN sub INTEGER NOT NULL DEFAULT 0", nullptr, nullptr, nullptr);
    return true;
}

void DB::close() {
    if (db) sqlite3_close(db);
    db = nullptr;
}

static void readAccountRow(sqlite3_stmt* st, int col0, Account& a) {
    a.weapon = (u8)sqlite3_column_int(st, col0);
    a.skin = (u8)sqlite3_column_int(st, col0 + 1);
    a.sub = (u8)sqlite3_column_int(st, col0 + 2);
    a.kills = sqlite3_column_int(st, col0 + 3);
    a.deaths = sqlite3_column_int(st, col0 + 4);
    a.wins = sqlite3_column_int(st, col0 + 5);
    a.losses = sqlite3_column_int(st, col0 + 6);
    a.matches = sqlite3_column_int(st, col0 + 7);
    a.paint = sqlite3_column_int(st, col0 + 8);
    if (a.weapon >= W_COUNT) a.weapon = W_SPLATTERSHOT;
    if (a.skin >= SKIN_COUNT) a.skin = 0;
    if (a.sub >= SUB_COUNT) a.sub = SUB_SPLAT_BOMB;
}

bool DB::registerAccount(const std::string& name, const std::string& pass, Account& out, std::string& err) {
    if (!validName(name)) { err = "Name must be 3-16 letters/digits/underscore"; return false; }
    if (pass.size() < 3 || pass.size() > 64) { err = "Password must be 3-64 characters"; return false; }
    std::string salt = random_hex(16);
    std::string hash = sha256_hex(salt + pass);
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
    sqlite3_prepare_v2(db,
        "SELECT id,name,salt,hash,weapon,skin,sub,kills,deaths,wins,losses,matches,paint "
        "FROM accounts WHERE name=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    bool okRow = sqlite3_step(st) == SQLITE_ROW;
    if (!okRow) {
        sqlite3_finalize(st);
        err = "No such account";
        return false;
    }
    std::string salt = (const char*)sqlite3_column_text(st, 2);
    std::string hash = (const char*)sqlite3_column_text(st, 3);
    if (sha256_hex(salt + pass) != hash) {
        sqlite3_finalize(st);
        err = "Wrong password";
        return false;
    }
    out.id = sqlite3_column_int64(st, 0);
    out.name = (const char*)sqlite3_column_text(st, 1);
    readAccountRow(st, 4, out);
    sqlite3_finalize(st);
    return true;
}

void DB::setLoadout(int64_t id, u8 weapon, u8 skin, u8 sub) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db, "UPDATE accounts SET weapon=?,skin=?,sub=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, weapon);
    sqlite3_bind_int(st, 2, skin);
    sqlite3_bind_int(st, 3, sub);
    sqlite3_bind_int64(st, 4, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void DB::addMatchStats(int64_t id, int kills, int deaths, int paint, bool won, bool lost) {
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db,
        "UPDATE accounts SET kills=kills+?,deaths=deaths+?,paint=paint+?,"
        "wins=wins+?,losses=losses+?,matches=matches+1 WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int(st, 1, kills);
    sqlite3_bind_int(st, 2, deaths);
    sqlite3_bind_int(st, 3, paint);
    sqlite3_bind_int(st, 4, won ? 1 : 0);
    sqlite3_bind_int(st, 5, lost ? 1 : 0);
    sqlite3_bind_int64(st, 6, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}
