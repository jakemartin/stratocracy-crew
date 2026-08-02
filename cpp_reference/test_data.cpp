// Test Engineer's gate for §4.11 row 2 — Data tables (T-DATA-01..04, 06).
// T-DATA-05 is the in-editor Unreal Automation half and is NOT run here; §4.11 marks
// it † and Q29 holds row 2's ledger flip until it clears in the editor pass.
//
// Reads the canonical CSVs from `data/` — argv[1] overrides the directory, default
// "../data" because the gate compiles and runs inside build/.
#include "Combat.h"
#include "Data.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s\n", name); }
}

// The §2.4 table, restated here so the gate asserts the CSV against the GDD rather
// than against itself.
struct ExpectUnit {
    const char* id; int hp; int move; int atk; int def;
    int rangeMin; int rangeMax; int cost; UnitType type; bool canCapture;
};
static const ExpectUnit kUnits[4] = {
    {"Infantry",  10, 3,  4, 2, 1, 1, 100, UnitType::Infantry,  true },
    {"Tank",      20, 5,  8, 5, 1, 1, 300, UnitType::Tank,      false},
    {"Artillery",  8, 3, 10, 1, 2, 3, 200, UnitType::Artillery, false},
    {"Recon",     12, 7,  5, 3, 1, 1, 150, UnitType::Recon,     false},
};

// The §2.3 table, likewise. DefensePct order: 0, 20, 40, 0, 10, -10, 15.
// IncomeFame per §2.7: Factory 100, Town 25, else 0.
struct ExpectTerrain {
    const char* id; int moveCost; int defensePct;
    bool land; bool air; bool sea; bool capturable;
    int income; bool spawn; bool repair;
};
static const ExpectTerrain kTerrain[7] = {
    {"Plains",    1,   0, true,  true, false, false,   0, false, false},
    {"Woods",     2,  20, true,  true, false, false,   0, false, false},
    {"Mountains", 3,  40, true,  true, false, false,   0, false, false},
    {"Water",     0,   0, false, true, true,  false,   0, false, false},
    {"Town",      1,  10, true,  true, false, true,   25, false, true },
    {"Bridge",    1, -10, true,  true, false, false,   0, false, false},
    {"Factory",   1,  15, true,  true, false, true,  100, true,  true },
};

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? std::string(argv[1]) : std::string("../data");
    const std::string unitsPath   = dir + "/units.csv";
    const std::string terrainPath = dir + "/terrain.csv";
    const std::string effPath     = dir + "/effectiveness.csv";

    std::vector<UnitDef> units;
    std::vector<TerrainDef> terrain;
    double eff[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT];
    std::string err;

    if (!loadUnits(unitsPath, units, err)) {
        std::printf("FAIL  T-DATA-01 load-units (%s)\n", err.c_str());
        std::printf("\n0/1 passed\n");
        return 1;
    }
    if (!loadTerrain(terrainPath, terrain, err)) {
        std::printf("FAIL  T-DATA-02 load-terrain (%s)\n", err.c_str());
        std::printf("\n0/1 passed\n");
        return 1;
    }
    if (!loadEffectiveness(effPath, eff, err)) {
        std::printf("FAIL  T-DATA-06 load-effectiveness (%s)\n", err.c_str());
        std::printf("\n0/1 passed\n");
        return 1;
    }

    // --- T-DATA-01 -------------------------------------------------------------
    bool ok01 = (units.size() == 4);
    for (int i = 0; i < 4 && ok01; ++i) {
        const UnitDef* u = findUnit(units, kUnits[i].id);
        if (u == nullptr) { ok01 = false; break; }
        if (u->hpMax    != kUnits[i].hp)         ok01 = false;
        if (u->move     != kUnits[i].move)       ok01 = false;
        if (u->atk      != kUnits[i].atk)        ok01 = false;
        if (u->def      != kUnits[i].def)        ok01 = false;
        if (u->rangeMin != kUnits[i].rangeMin)   ok01 = false;
        if (u->rangeMax != kUnits[i].rangeMax)   ok01 = false;
        if (u->costFame != kUnits[i].cost)       ok01 = false;
        if (u->type     != kUnits[i].type)       ok01 = false;
        if (u->canCapture != kUnits[i].canCapture) ok01 = false;
    }
    check("T-DATA-01 units-equal-the-2.4-table", ok01);

    // --- T-DATA-02 -------------------------------------------------------------
    bool ok02 = (terrain.size() == 7);
    for (int i = 0; i < 7 && ok02; ++i) {
        const TerrainDef* t = findTerrain(terrain, kTerrain[i].id);
        if (t == nullptr) { ok02 = false; break; }
        if (t->moveCost   != kTerrain[i].moveCost)   ok02 = false;
        if (t->defensePct != kTerrain[i].defensePct) ok02 = false;
        if (t->passLand   != kTerrain[i].land)       ok02 = false;
        if (t->passAir    != kTerrain[i].air)        ok02 = false;
        if (t->passSea    != kTerrain[i].sea)        ok02 = false;
        if (t->capturable != kTerrain[i].capturable) ok02 = false;
        if (t->incomeFame != kTerrain[i].income)     ok02 = false;
        if (t->isSpawnPoint  != kTerrain[i].spawn)   ok02 = false;
        if (t->isRepairPoint != kTerrain[i].repair)  ok02 = false;
    }
    // The two clauses §4.8 calls out by name, asserted explicitly rather than left
    // to the loop above: Bridge's defense is NEGATIVE, and Water is closed to land.
    {
        const TerrainDef* bridge = findTerrain(terrain, "Bridge");
        const TerrainDef* water  = findTerrain(terrain, "Water");
        if (bridge == nullptr || bridge->defensePct >= 0) ok02 = false;
        if (water == nullptr || water->passLand || water->moveCost != 0) ok02 = false;
    }
    check("T-DATA-02 terrain-equals-the-2.3-table", ok02);

    // --- T-DATA-03 -------------------------------------------------------------
    int capturers = 0;
    const UnitDef* theCapturer = nullptr;
    for (const UnitDef& u : units) if (u.canCapture) { ++capturers; theCapturer = &u; }
    check("T-DATA-03 exactly-one-capturer-is-infantry",
          capturers == 1 && theCapturer != nullptr && theCapturer->id == "Infantry");

    // --- T-DATA-04 -------------------------------------------------------------
    bool ok04 = true;
    for (const UnitDef& u : units) {
        if (u.costFame <= 0) ok04 = false;
        if (u.hpMax    <= 0) ok04 = false;
        if (u.rangeMin > u.rangeMax) ok04 = false;
        if (u.rangeMin <= 0) ok04 = false;
    }
    // Move cost and land-passability say the same thing from two directions; a table
    // where they disagree is exactly the silent default §4.8 forbids.
    for (const TerrainDef& t : terrain) {
        if (t.passLand  && t.moveCost <= 0) ok04 = false;
        if (!t.passLand && t.moveCost != 0) ok04 = false;
    }
    check("T-DATA-04 sanity-costs-ranges-hp", ok04);

    // --- T-DATA-06 -------------------------------------------------------------
    // 4x4 in the pinned order, every cell in {0.5, 1.0, 1.5}, and the SHIPPED file
    // all-1.0 — re-asserting T-COMBAT-09 at the data layer. Then the layers are
    // compared: the CSV and the verified code stub must agree on all sixteen pairs,
    // so neither can be retuned alone.
    bool ok06 = true;
    for (int a = 0; a < UNIT_TYPE_COUNT; ++a) {
        for (int d = 0; d < UNIT_TYPE_COUNT; ++d) {
            const double v = eff[a][d];
            if (v != 0.5 && v != 1.0 && v != 1.5) ok06 = false;
            if (v != 1.0) ok06 = false;                       // shipped state
            if (v != effectiveness(static_cast<UnitType>(a),
                                   static_cast<UnitType>(d))) ok06 = false;
        }
    }
    check("T-DATA-06 effectiveness-4x4-pinned-neutral", ok06);

    // --- §4.8 contract: hard fail, never a silent default ----------------------
    // Not a numbered spec ID — §4.8 states the rule in prose, and this asserts it.
    // A table missing a required column, naming an unknown type, carrying a
    // non-numeric value, or listing the types out of order must all be REFUSED.
    bool okHardFail = true;
    {
        const char* kBadUnits =
            "Id,HP,Move,Atk,Def,RangeMin,RangeMax,CostFame,Type\n"
            "Infantry,10,3,4,2,1,1,100,Infantry\n";                 // no CanCapture
        const char* kBadType =
            "Id,HP,Move,Atk,Def,RangeMin,RangeMax,CostFame,Type,CanCapture\n"
            "Infantry,10,3,4,2,1,1,100,Sniper,true\n";              // unknown type
        const char* kBadInt =
            "Id,HP,Move,Atk,Def,RangeMin,RangeMax,CostFame,Type,CanCapture\n"
            "Infantry,ten,3,4,2,1,1,100,Infantry,true\n";           // HP not an int
        const char* kBadEmpty =
            "Id,HP,Move,Atk,Def,RangeMin,RangeMax,CostFame,Type,CanCapture\n"
            "Infantry,,3,4,2,1,1,100,Infantry,true\n";              // empty required cell
        const char* kBadEffOrder =
            "Attacker,Tank,Infantry,Artillery,Recon\n"              // types out of order
            "Infantry,1.0,1.0,1.0,1.0\nTank,1.0,1.0,1.0,1.0\n"
            "Artillery,1.0,1.0,1.0,1.0\nRecon,1.0,1.0,1.0,1.0\n";

        struct Case { const char* file; const char* body; bool isEff; };
        const Case cases[5] = {
            {"_gate_bad_units.csv",  kBadUnits,    false},
            {"_gate_bad_type.csv",   kBadType,     false},
            {"_gate_bad_int.csv",    kBadInt,      false},
            {"_gate_bad_empty.csv",  kBadEmpty,    false},
            {"_gate_bad_eff.csv",    kBadEffOrder, true },
        };
        for (const Case& c : cases) {
            {
                std::ofstream f(c.file);
                if (!f) { okHardFail = false; continue; }
                f << c.body;
            }
            std::string e;
            bool loaded = false;
            if (c.isEff) {
                double tmp[UNIT_TYPE_COUNT][UNIT_TYPE_COUNT];
                loaded = loadEffectiveness(c.file, tmp, e);
            } else {
                std::vector<UnitDef> tmp;
                loaded = loadUnits(c.file, tmp, e);
                if (!loaded && !tmp.empty()) okHardFail = false;   // out untouched on failure
            }
            if (loaded || e.empty()) okHardFail = false;
            std::remove(c.file);
        }
        // A missing file is a failure too, not an empty table.
        std::vector<UnitDef> tmp;
        std::string e;
        if (loadUnits(dir + "/does_not_exist.csv", tmp, e)) okHardFail = false;
    }
    check("GATE-DATA-HARDFAIL no-silent-defaults (4.8 contract)", okHardFail);

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
