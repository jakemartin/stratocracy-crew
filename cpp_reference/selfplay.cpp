// Balance Analyst harness — deterministic AI-vs-AI duels over the combat module.
// For every ordered pair of the 4 unit types, run a 1v1 duel on plains (attacker
// strikes first; defender counters if it survives and the attacker is in range),
// to a 50-round cap. Reports winner and rounds-to-kill. No RNG: fully reproducible.
#include "Combat.h"
#include <cstdio>
#include <string>
#include <vector>

using namespace strat;

struct Named { const char* name; Unit u; };

// Resolve one full duel; returns +1 if A wins, -1 if B wins, 0 if capped. Sets rounds.
static int duel(Unit A, Unit B, int distance, int& rounds) {
    for (rounds = 1; rounds <= 50; ++rounds) {
        // A attacks B
        B.hp -= resolveDamage(A, B, 0);
        if (B.hp <= 0) return +1;
        // B counters if in range
        if (defenderCanCounter(B, distance)) {
            A.hp -= resolveDamage(B, A, 0);
            if (A.hp <= 0) return -1;
        }
    }
    return 0;
}

int main() {
    std::vector<Named> roster = {
        {"Infantry",  {4, 2, 10, 10, 1, 1}},
        {"Tank",      {8, 5, 20, 20, 1, 1}},
        {"Artillery", {10,1, 8,  8,  2, 3}},
        {"Recon",     {5, 3, 12, 12, 1, 1}},
    };

    std::printf("Stratocracy self-play - 1v1 duels on plains (adjacent, distance 1)\n");
    std::printf("%-10s vs %-10s -> %-9s (rounds)\n", "attacker", "defender", "winner");
    std::printf("--------------------------------------------------------\n");

    std::vector<int> wins(roster.size(), 0);
    int games = 0;
    for (size_t i = 0; i < roster.size(); ++i) {
        for (size_t j = 0; j < roster.size(); ++j) {
            if (i == j) continue;
            int rounds = 0;
            int r = duel(roster[i].u, roster[j].u, /*distance=*/1, rounds);
            const char* w = (r > 0) ? roster[i].name : (r < 0) ? roster[j].name : "DRAW/cap";
            if (r > 0) wins[i]++; else if (r < 0) wins[j]++;
            ++games;
            std::printf("%-10s vs %-10s -> %-9s (%d)\n",
                        roster[i].name, roster[j].name, w, rounds);
        }
    }

    std::printf("\nWin tally (as attacker or counter-killer), %d duels:\n", games);
    for (size_t i = 0; i < roster.size(); ++i)
        std::printf("  %-10s %d\n", roster[i].name, wins[i]);
    return 0;
}
