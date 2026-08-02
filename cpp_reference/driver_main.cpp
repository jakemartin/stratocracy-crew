// Stratocracy — the debug-command REPL (§4.4 week 1, "Playable via debug commands").
// Reads commands on stdin, writes results on stdout. No rules live here; this file
// is I/O only and delegates every command to strat::execute.
#include "Driver.h"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::string dataDir = (argc > 1) ? std::string(argv[1]) : std::string("../data");

    strat::Session session;
    std::string err;
    if (!strat::sessionInit(session, dataDir, err)) {
        std::cout << "cannot start: " << err << "\n";
        return 1;                       // §4.8: a bad table is a hard failure
    }

    std::cout << "Stratocracy debug driver — rows 1-4 + Combat/Repair, headless.\n"
              << "No turn loop, no AI, no scenario file: rows 5-8 hold no code.\n"
              << "'help' for commands, 'fixture list' for boards, 'quit' to exit.\n";

    std::string line;
    while (std::getline(std::cin, line)) {
        std::vector<std::string> out;
        const bool keepGoing = strat::execute(session, line, out);
        for (const std::string& l : out) std::cout << l << "\n";
        if (!keepGoing) break;
        std::cout << "> " << std::flush;
    }
    return 0;
}
