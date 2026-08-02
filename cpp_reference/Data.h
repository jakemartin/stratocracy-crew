// Stratocracy — headless data tables (GDD §4.8, §4.7 Stub 2, §4.11 row 2).
// Zero engine dependencies. Pure parse: a missing column or unparseable value is a
// HARD LOAD FAILURE, never a silent default.
#pragma once

#include <string>
#include <vector>

#include "Combat.h"   // strat::UnitType — declared ONCE, order pinned (addendum Part A)

namespace strat {

// data/units.csv — exactly four rows (§2.4). The flag unit is NOT a row: §2.4 makes
// it "a designated Tank", so flag status is a scenario placement field (Stub 7).
// `MoveClass` is reserved on Q2 and is neither required nor read (data_spec.md).
struct UnitDef {
    std::string id;
    int hpMax    = 0;
    int move     = 0;
    int atk      = 0;
    int def      = 0;
    int rangeMin = 0;
    int rangeMax = 0;
    int costFame = 0;
    UnitType type = UnitType::Infantry;
    bool canCapture = false;
};

// data/terrain.csv — exactly seven rows (§2.3).
struct TerrainDef {
    std::string id;
    int  moveCost   = 0;      // 0 == impassable (§4.8 sentinel)
    int  defensePct = 0;      // SIGNED — Bridge is -10 (§2.3)
    bool passLand   = false;
    bool passAir    = false;
    bool passSea    = false;
    bool capturable = false;
    int  incomeFame = 0;      // Factory 100, Town 25, else 0 (§2.7)
    bool isSpawnPoint  = false;
    bool isRepairPoint = false;
};

// The four unit types in the pinned order (Infantry, Tank, Artillery, Recon).
constexpr int UNIT_TYPE_COUNT = 4;

// Every loader returns false and fills `err` on ANY defect. `out` is left untouched
// on failure — a caller can never end up with a half-parsed table.
bool loadUnits(const std::string& path, std::vector<UnitDef>& out, std::string& err);
bool loadTerrain(const std::string& path, std::vector<TerrainDef>& out, std::string& err);

// 4x4, row = attacker type, column = defender type, indexed by the pinned order.
// Header row and first column must both name the types IN that order, or it is a
// hard failure rather than a silently transposed matrix.
bool loadEffectiveness(const std::string& path, double out[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT],
                       std::string& err);

// Parses one of the four pinned type names. False on anything else.
bool parseUnitType(const std::string& name, UnitType& out);
const char* unitTypeName(UnitType t);

const UnitDef*    findUnit(const std::vector<UnitDef>& units, const std::string& id);
const TerrainDef* findTerrain(const std::vector<TerrainDef>& terrain, const std::string& id);

} // namespace strat
