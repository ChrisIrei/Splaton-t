// Splaton't server — local SQLite account database
#pragma once
#include "shared/defs.h"
#include <cstdint>
#include <string>

struct sqlite3;

struct Account {
    int64_t id = 0;
    std::string name;
    u8 weapon = W_SPLATTERSHOT;
    u8 skin = 0;
    u8 sub = SUB_SPLAT_BOMB;
    u32 kills = 0, deaths = 0, wins = 0, losses = 0, matches = 0, paint = 0;
};

struct DB {
    sqlite3* db = nullptr;

    bool open(const std::string& path, std::string& err);
    void close();

    // creates the account (salted sha256) and fills out on success
    bool registerAccount(const std::string& name, const std::string& pass, Account& out, std::string& err);
    bool login(const std::string& name, const std::string& pass, Account& out, std::string& err);
    void setLoadout(int64_t id, u8 weapon, u8 skin, u8 sub);
    void addMatchStats(int64_t id, int kills, int deaths, int paint, bool won, bool lost);
};
