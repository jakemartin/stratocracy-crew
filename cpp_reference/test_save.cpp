// Test Engineer's gate for §4.11 row 10, PART (a) — Save & replay format.
//
// WHAT THIS SUITE DOES NOT COVER, stated up front because a suite that quietly omits
// an ID reads as a complete pass and this one is DELIBERATELY PARTIAL. §4.11 splits
// row 10 into three parts; this build is part (a), which has no dependencies and on
// which T-SAVE-04 closes ALONE. Six of the row's seven acceptance IDs do not run here
// and each is printed by name with its reason at the end of the run, never folded
// into the tally.
//
// Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. Nothing in §3's table
// moves on this build, and the nine verified rows stand.
//
// THE CHECK RECOMPUTES; IT DOES NOT DELEGATE. Every expected refusal is constructed
// here from §4.10's own field list, and the fixtures are built by MUTATING ONE FIELD
// of a known-good save rather than by asking the module what it thinks a mismatch is.
// A check that asked `checkHeader` to tell it which fields matter would agree with
// any answer, correct or not -- which is the entire content of T-SAVE-04.
//
// Links Hex.cpp for the odd-r conversion and NOTHING ELSE. That empty dependency set
// is §4.11's claim about part (a), and the link set is where it is checked.
#include "Hex.h"
#include "Save.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace strat;

static int g_total = 0;
static int g_fail  = 0;

static void check(const char* name, bool cond) {
    ++g_total;
    if (cond) { std::printf("PASS  %s\n", name); }
    else      { std::printf("FAIL  %s\n", name); ++g_fail; }
}

static bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// A known-good save, built field by field from §4.10's layout table. Two commands so
// the log is non-trivial and the round trip has something to preserve.
static Save goodSave() {
    Save s;
    s.formatVersion = kFormatVersion;
    s.rulesCommit   = "d837fc8";
    s.dataHash      = "1122334455667788";
    s.scenarioId    = "ferrum_crossing";
    s.scenarioHash  = "99aabbccddeeff00";
    s.seed          = 0;
    s.stateHash     = "0f0e0d0c0b0a0908";
    s.hasResult     = false;

    SaveCommand mv;
    mv.turn = 1; mv.side = 0; mv.kind = SaveCommandKind::Move;
    mv.unitId = 3; mv.hex = offsetToAxial(4, 2); mv.hasUnit = true; mv.hasHex = true;
    s.commandLog.push_back(mv);

    SaveCommand et;
    et.turn = 1; et.side = 0; et.kind = SaveCommandKind::EndTurn;
    s.commandLog.push_back(et);
    return s;
}

static SaveHeaderExpectation expectationFor(const Save& s) {
    SaveHeaderExpectation e;
    e.expectedVersion = s.formatVersion;
    e.rulesCommit     = s.rulesCommit;
    e.dataHash        = s.dataHash;
    e.scenarioHash    = s.scenarioHash;
    return e;
}

// A save whose fields are all DIFFERENT from goodSave()'s, so "untouched" is a claim
// with teeth: if a refused load writes through, every field it wrote is visibly wrong.
static Save sentinelSave() {
    Save s;
    s.formatVersion = 4242;
    s.rulesCommit   = "SENTINEL-COMMIT";
    s.dataHash      = "SENTINEL-DATA";
    s.scenarioId    = "SENTINEL-SCENARIO";
    s.scenarioHash  = "SENTINEL-HASH";
    s.seed          = 0;
    s.stateHash     = "SENTINEL-STATE";
    s.result        = "SENTINEL-RESULT";
    s.hasResult     = true;
    return s;
}

static bool sameAsSentinel(const Save& s) {
    const Save ref = sentinelSave();
    return s.formatVersion == ref.formatVersion &&
           s.rulesCommit   == ref.rulesCommit   &&
           s.dataHash      == ref.dataHash      &&
           s.scenarioId    == ref.scenarioId    &&
           s.scenarioHash  == ref.scenarioHash  &&
           s.seed          == ref.seed          &&
           s.stateHash     == ref.stateHash     &&
           s.result        == ref.result        &&
           s.hasResult     == ref.hasResult     &&
           s.commandLog.empty();
}

int main() {
    // ---------------------------------------------------------------------------
    // The control: a matching header must LOAD. Without it every mismatch fixture
    // below would pass against a loader that refuses everything.
    // ---------------------------------------------------------------------------
    {
        const Save good = goodSave();
        const std::string text = serializeSave(good);
        Save out = sentinelSave();
        const SaveLoadResult r = loadSave(text, "control", expectationFor(good), out);
        bool ok = r.ok && r.failedId.empty() && r.reason.empty();
        if (ok) {
            ok = out.formatVersion == good.formatVersion &&
                 out.rulesCommit == good.rulesCommit &&
                 out.dataHash == good.dataHash &&
                 out.scenarioId == good.scenarioId &&
                 out.scenarioHash == good.scenarioHash &&
                 out.seed == 0 && out.stateHash == good.stateHash &&
                 out.hasResult == false &&
                 out.commandLog.size() == 2;
        }
        if (ok) {
            const SaveCommand& c = out.commandLog[0];
            int col = 0, row = 0;
            axialToOffset(c.hex, col, row);
            ok = c.kind == SaveCommandKind::Move && c.turn == 1 && c.side == 0 &&
                 c.unitId == 3 && col == 4 && row == 2 &&
                 out.commandLog[1].kind == SaveCommandKind::EndTurn;
        }
        check("T-SAVE-04 control matching-header-loads", ok);
    }

    // ---------------------------------------------------------------------------
    // T-SAVE-04, the four mismatch fixtures — one per field §4.10's Version policy
    // enumerates. Each asserts THREE things, because the invariant states three:
    // refused, refused WITH A REASON that names the field, and the caller's state
    // UNTOUCHED. A loader that refuses without a reason, or that fills the caller's
    // object before validating, fails here.
    // ---------------------------------------------------------------------------
    struct Fixture {
        const char* name;
        const char* field;
        int         version;      // applied to the EXPECTATION, not the file
        const char* rulesCommit;
        const char* dataHash;
        const char* scenarioHash;
    };
    const Fixture fixtures[] = {
        {"T-SAVE-04 (a) formatVersion-mismatch-refused", "formatVersion",
         kFormatVersion + 1, "d837fc8", "1122334455667788", "99aabbccddeeff00"},
        {"T-SAVE-04 (b) rulesCommit-mismatch-refused", "rulesCommit",
         kFormatVersion, "0000000", "1122334455667788", "99aabbccddeeff00"},
        {"T-SAVE-04 (c) dataHash-mismatch-refused", "dataHash",
         kFormatVersion, "d837fc8", "ffffffffffffffff", "99aabbccddeeff00"},
        {"T-SAVE-04 (d) scenarioHash-mismatch-refused", "scenarioHash",
         kFormatVersion, "d837fc8", "1122334455667788", "eeeeeeeeeeeeeeee"},
    };

    for (const Fixture& f : fixtures) {
        const Save good = goodSave();
        const std::string text = serializeSave(good);

        SaveHeaderExpectation e;
        e.expectedVersion = f.version;
        e.rulesCommit     = f.rulesCommit;
        e.dataHash        = f.dataHash;
        e.scenarioHash    = f.scenarioHash;

        Save out = sentinelSave();
        const SaveLoadResult r = loadSave(text, "mismatch", e, out);

        bool ok = !r.ok;                                   // refused
        if (ok) ok = (r.failedId == "T-SAVE-04");          // ...as a HEADER refusal
        if (ok) ok = !r.reason.empty() &&                  // ...with a reason
                     contains(r.reason, f.field);          // ...that names the field
        if (ok) ok = sameAsSentinel(out);                  // ...state untouched
        check(f.name, ok);
    }

    // A fifth, and it is the one reading 2 is about: `scenarioId` disagreeing is NOT a
    // refusal. §4.10's Version policy enumerates four fields and T-SAVE-04's
    // parenthetical repeats the same four; making this a fifth trigger would be a rule
    // the GDD does not have.
    {
        Save good = goodSave();
        const SaveHeaderExpectation e = expectationFor(good);
        good.scenarioId = "some_other_map";
        const std::string text = serializeSave(good);
        Save out;
        const SaveLoadResult r = loadSave(text, "scenarioId", e, out);
        check("T-SAVE-04 (e) scenarioId-is-not-a-refusal-trigger",
              r.ok && out.scenarioId == "some_other_map");
    }

    // Two fields disagreeing at once still refuses, and names the FIRST in §4.10's
    // table order — so the reason is deterministic rather than whichever check ran.
    {
        const Save good = goodSave();
        const std::string text = serializeSave(good);
        SaveHeaderExpectation e = expectationFor(good);
        e.rulesCommit = "0000000";
        e.dataHash    = "ffffffffffffffff";
        Save out = sentinelSave();
        const SaveLoadResult r = loadSave(text, "two-fields", e, out);
        check("T-SAVE-04 (f) first-disagreement-in-table-order-is-reported",
              !r.ok && r.failedId == "T-SAVE-04" &&
              contains(r.reason, "rulesCommit") && !contains(r.reason, "dataHash mismatch") &&
              sameAsSentinel(out));
    }

    // checkHeader on an already-parsed save, with no file involved. §4.10's policy is
    // about the header, not about parsing, and the two must be separable.
    {
        const Save good = goodSave();
        SaveHeaderExpectation e = expectationFor(good);
        e.scenarioHash = "eeeeeeeeeeeeeeee";
        const SaveLoadResult r = checkHeader(good, e);
        const SaveLoadResult ok = checkHeader(good, expectationFor(good));
        check("T-SAVE-04 (g) header-check-is-separable-from-the-parse",
              !r.ok && r.failedId == "T-SAVE-04" && contains(r.reason, "scenarioHash") &&
              ok.ok);
    }

    // ---------------------------------------------------------------------------
    // GATE-SAVE-PARSE — the strict parser. Not a §4.7 stub ID and it mints no
    // acceptance ID: the GATE-SCN-PARSE / GATE-AI-SMOKE / GATE-CAP-PARTIAL
    // precedent. It gates a file format, not a rules system, so it moves no §4.5
    // count and flips no ledger row.
    //
    // Every fixture below is the SERIALIZED good save with one surgical edit, so a
    // refusal cannot be blamed on the rest of the document being malformed.
    // ---------------------------------------------------------------------------
    const std::string base = serializeSave(goodSave());

    struct ParseCase {
        const char* name;
        const char* find;
        const char* replace;
    };
    const ParseCase cases[] = {
        {"unknown-key",        "\"seed\": 0",            "\"seed\": 0,\n  \"extra\": 1"},
        {"duplicate-key",      "\"seed\": 0",            "\"seed\": 0,\n  \"seed\": 0"},
        {"trailing-comma",     "\"result\": null\n}",    "\"result\": null,\n}"},
        {"null-outside-result","\"stateHash\": ",        "\"stateHash\": null, \"unused\": "},
        {"non-integer-number", "\"seed\": 0",            "\"seed\": 0.5"},
        {"u-escape",           "\"ferrum_crossing\"",    "\"ferrum\\u005fcrossing\""},
        {"non-zero-seed",      "\"seed\": 0",            "\"seed\": 7"},
        {"unknown-command",    "\"kind\": \"Move\"",     "\"kind\": \"Teleport\""},
        {"foreign-field",      "\"destHex\":",           "\"targetHex\":"},
        {"bad-side",           "\"side\": 0, \"kind\": \"Move\"", "\"side\": 5, \"kind\": \"Move\""},
    };

    for (const ParseCase& c : cases) {
        std::string text = base;
        const std::size_t at = text.find(c.find);
        bool ok = false;
        std::string why;
        if (at == std::string::npos) {
            why = "the fixture's anchor is not in the serialized save";
        } else {
            text.replace(at, std::string(c.find).size(), c.replace);
            Save out = sentinelSave();
            const SaveLoadResult r = parseSave(text, "parse", out);
            ok = !r.ok && r.failedId == "GATE-SAVE-PARSE" && !r.reason.empty() &&
                 sameAsSentinel(out);
            if (!ok) why = r.ok ? "accepted" : (r.failedId + ": " + r.reason);
        }
        std::string label = std::string("GATE-SAVE-PARSE ") + c.name;
        check(label.c_str(), ok);
        if (!ok && !why.empty()) std::printf("      why: %s\n", why.c_str());
    }

    // Missing key, trailing content and a raw control character need no anchor.
    {
        std::string text = base;
        const std::size_t at = text.find("  \"stateHash\"");
        bool ok = false;
        if (at != std::string::npos) {
            const std::size_t end = text.find('\n', at);
            text.erase(at, end - at + 1);
            Save out = sentinelSave();
            const SaveLoadResult r = parseSave(text, "parse", out);
            ok = !r.ok && r.failedId == "GATE-SAVE-PARSE" &&
                 contains(r.reason, "stateHash") && sameAsSentinel(out);
        }
        check("GATE-SAVE-PARSE missing-key", ok);
    }
    {
        Save out = sentinelSave();
        const SaveLoadResult r = parseSave(base + "{}", "parse", out);
        check("GATE-SAVE-PARSE trailing-content",
              !r.ok && r.failedId == "GATE-SAVE-PARSE" && sameAsSentinel(out));
    }
    {
        std::string text = base;
        const std::size_t at = text.find("ferrum_crossing");
        bool ok = false;
        if (at != std::string::npos) {
            text[at + 6] = '\n';                       // a raw newline inside a string
            Save out = sentinelSave();
            const SaveLoadResult r = parseSave(text, "parse", out);
            ok = !r.ok && r.failedId == "GATE-SAVE-PARSE" && sameAsSentinel(out);
        }
        check("GATE-SAVE-PARSE raw-control-character", ok);
    }

    // `result` as a STRING is legal — the other half of reading 3. Without this, a
    // parser that refused every `result` would pass every fixture above.
    {
        std::string text = base;
        const std::size_t at = text.find("\"result\": null");
        bool ok = false;
        if (at != std::string::npos) {
            text.replace(at, std::string("\"result\": null").size(),
                         "\"result\": \"Decisive\"");
            Save out;
            const SaveLoadResult r = parseSave(text, "parse", out);
            ok = r.ok && out.hasResult && out.result == "Decisive";
        }
        check("GATE-SAVE-PARSE result-may-be-a-string", ok);
    }

    // Round trip: serialize -> parse -> serialize is byte-identical. §4.10 fixes the
    // field order, and a writer that drifted from the table would show up here.
    {
        const Save good = goodSave();
        const std::string once = serializeSave(good);
        Save back;
        const SaveLoadResult r = parseSave(once, "roundtrip", back);
        const std::string twice = r.ok ? serializeSave(back) : std::string();
        check("GATE-SAVE-PARSE serialize-parse-serialize-is-stable", r.ok && once == twice);
    }

    // An empty command log is legal — a save taken before the first command. §4.10's
    // mid-match policy makes every PREFIX of a log a save, and the empty prefix is one.
    {
        Save good = goodSave();
        good.commandLog.clear();
        const std::string text = serializeSave(good);
        Save back;
        const SaveLoadResult r = parseSave(text, "empty-log", back);
        check("GATE-SAVE-PARSE empty-command-log-is-legal",
              r.ok && back.commandLog.empty() && serializeSave(back) == text);
    }

    // Every §4.9 command kind survives a round trip with its own field names. This is
    // the schema's vocabulary check: five kinds, no more, each with what §4.9 names it.
    {
        Save good = goodSave();
        good.commandLog.clear();
        const SaveCommandKind kinds[] = {
            SaveCommandKind::Move, SaveCommandKind::Attack, SaveCommandKind::Build,
            SaveCommandKind::Capture, SaveCommandKind::EndTurn };
        for (SaveCommandKind k : kinds) {
            SaveCommand c;
            c.turn = 2; c.side = 1; c.kind = k;
            if (k != SaveCommandKind::EndTurn) { c.unitId = 7; c.hasUnit = true; }
            if (k == SaveCommandKind::Move || k == SaveCommandKind::Attack ||
                k == SaveCommandKind::Build) { c.hex = offsetToAxial(2, 3); c.hasHex = true; }
            good.commandLog.push_back(c);
        }
        const std::string text = serializeSave(good);
        Save back;
        const SaveLoadResult r = parseSave(text, "kinds", back);
        bool ok = r.ok && back.commandLog.size() == 5;
        if (ok)
            for (std::size_t n = 0; n < 5; ++n)
                if (back.commandLog[n].kind != kinds[n]) { ok = false; break; }
        if (ok) ok = contains(text, "\"destHex\"") && contains(text, "\"targetHex\"") &&
                     contains(text, "\"factoryHex\"") && contains(text, "\"unitId\"");
        check("GATE-SAVE-PARSE every-4.9-command-kind-round-trips", ok);
    }

    // --- what did NOT run ------------------------------------------------------------
    std::printf("\n");
    std::printf("NOT RUN HERE  T-SAVE-01, T-SAVE-02, T-SAVE-03 and T-SAVE-05. All four need\n");
    std::printf("         the HEADLESS REPLAYER and the canonical state hash, which are part\n");
    std::printf("         (b); part (a) applies no command and defines no hash, so none of\n");
    std::printf("         them has a subject in THIS suite. They are not outstanding: part\n");
    std::printf("         (b) has since landed and closes all four in its own runner, which\n");
    std::printf("         runs beside this one. In particular T-SAVE-03 is still NOT covered\n");
    std::printf("         by the empty-log case above -- the parser accepting every prefix\n");
    std::printf("         as a DOCUMENT is not every prefix being a LOADABLE SAVE.\n");
    std::printf("NOT RUN  T-SAVE-06 stateHash stability across the headless and in-engine\n");
    std::printf("         builds. Marked † in §4.11, asserted jointly with T-INT-02, and no\n");
    std::printf("         in-editor Automation harness exists. Its OTHER blocker -- the\n");
    std::printf("         canonical state hash being unbuilt -- was removed by part (b).\n");
    std::printf("NOT RUN  T-SAVE-07 harness compatibility (a Balance Analyst self-play log\n");
    std::printf("         validates and replays as a save). Needs row 6's self-play output.\n");
    std::printf("         Part (c), week 4.\n");
    std::printf("\n");
    std::printf("Row 10 is a PROPOSED ledger row (§4.11) and has none to flip. T-SAVE-04 is\n");
    std::printf("the one acceptance ID this build closes.\n");

    std::printf("\n%d/%d passed\n", g_total - g_fail, g_total);
    return g_fail == 0 ? 0 : 1;
}
