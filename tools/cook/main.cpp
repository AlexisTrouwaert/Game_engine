// The engine's cooking tool for a game without data tables (milestone 8, part 6): see moteur/cook.hpp.
// A game with tables builds its own, a main() that calls moteur::cook_main() with its tables.
//
//   moteur_cook --sources DIR... --manifest FILE --output DIR [--shaders DIR] [--force] [--orphans]

#include "moteur/cook.hpp"

int main(int argc, char* argv[]) {
    return moteur::cook_main(argc, argv);
}
