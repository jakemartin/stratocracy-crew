// Stratocracy — self-play log producer, §4.11 row 10 part (c). See Balance.h.
#include "Balance.h"

namespace strat {

AiState aiViewOf(const GameState& g, const std::vector<UnitDef>& ud,
                 const std::vector<TerrainDef>& td, const std::vector<int>& buildlist) {
    AiState s;
    s.bounds        = g.bounds;
    s.terrain       = g.terrain;
    s.unitDefs      = ud;
    s.terrainDefs   = td;
    s.economy       = g.economy;
    s.turn          = g.turn;
    s.builtThisTurn = g.turn.builtThisTurn;
    s.buildlist     = buildlist;
    for (std::size_t i = 0; i < g.units.size(); ++i) {
        const GameUnit& u = g.units[i];
        AiUnit a;
        a.id       = u.id;
        a.side     = u.side;
        a.defIndex = u.defIndex;
        a.hex      = u.hex;
        a.hp       = u.hp;
        a.isFlag   = isFlagUnit(g, u);
        s.units.push_back(a);
    }
    return s;
}

bool aiCommandToSave(const GameState& g, const AiCommand& a, int turn, int side,
                     SaveCommand& out) {
    out = SaveCommand();
    out.turn = turn;
    out.side = side;
    switch (a.kind) {
    case AiCommandKind::Move:
        out.kind    = SaveCommandKind::Move;
        out.unitId  = a.unitId;
        out.hex     = a.hex;
        out.hasUnit = true;
        out.hasHex  = true;
        return true;
    case AiCommandKind::Attack: {
        const GameUnit* target = findGameUnit(g, a.targetId);
        if (target == nullptr) return false;
        out.kind    = SaveCommandKind::Attack;
        out.unitId  = a.unitId;
        out.hex     = target->hex;
        out.hasUnit = true;
        out.hasHex  = true;
        return true;
    }
    case AiCommandKind::Build:
        out.kind    = SaveCommandKind::Build;
        out.unitId  = a.defIndex;      // Build names the unit BUILT (Save.h)
        out.hex     = a.hex;
        out.hasUnit = true;
        out.hasHex  = true;
        return true;
    case AiCommandKind::EndTurn:
        out.kind    = SaveCommandKind::EndTurn;
        return true;
    }
    return false;
}

SelfPlayResult playSelfPlay(GameState g, const RulesTables& t,
                            const std::vector<UnitDef>& ud,
                            const std::vector<TerrainDef>& td,
                            const std::vector<int>& buildlist,
                            int maxCommands) {
    SelfPlayResult r;
    for (int issued = 0; issued < maxCommands; ++issued) {
        if (!g.turn.running) {
            r.stop = SelfPlayStop::Ended;
            break;
        }
        // Both tags are read BEFORE the command is applied: `EndTurn` advances the turn
        // and the side, and the entry belongs to the turn that ended, not to the one
        // that opens.
        const int turn = g.turn.turnNumber;
        const int side = g.turn.activeSide;

        const AiState  view = aiViewOf(g, ud, td, buildlist);
        const AiCommand a   = nextCommand(view, side);

        SaveCommand sc;
        if (!aiCommandToSave(g, a, turn, side, sc)) {
            r.stop   = SelfPlayStop::Untranslatable;
            // ASCII only in a string literal: the section mark lives in comments, never
            // in bytes a reason string carries across two compilers' codepages.
            r.reason = std::string("no GDD 4.9 spelling for ") + commandKindName(a.kind);
            break;
        }

        const ReplayResult rr = applyCommand(g, sc, t);
        if (!rr.ok) {
            r.stop   = SelfPlayStop::Refused;
            r.reason = rr.reason;
            break;
        }
        // ONLY AFTER ACCEPTANCE. A log that records what was proposed rather than what
        // was applied replays to a different state than the match it came from, which is
        // exactly the drift T-SAVE-07 exists to refuse.
        r.commandLog.push_back(sc);
    }
    r.final  = g;
    r.result = g.turn.result;
    return r;
}

Save selfPlaySave(const SelfPlayResult& r, const SaveHeaderExpectation& expect,
                  const std::string& scenarioId) {
    Save s;
    s.formatVersion = kFormatVersion;
    s.rulesCommit   = expect.rulesCommit;
    s.dataHash      = expect.dataHash;
    s.scenarioId    = scenarioId;
    s.scenarioHash  = expect.scenarioHash;
    s.seed          = 0;
    s.commandLog    = r.commandLog;
    s.stateHash     = canonicalStateHash(r.final);
    if (r.stop == SelfPlayStop::Ended && r.result.tier != ResultTier::InProgress) {
        s.result    = tierName(r.result.tier);
        s.hasResult = true;
    } else {
        s.result.clear();
        s.hasResult = false;
    }
    return s;
}

} // namespace strat
