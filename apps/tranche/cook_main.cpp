// The slice's cooking tool (milestone 8, part 6): moteur::cook_main() with the slice's data tables,
// which it compiles to their binary form. Run by the build (target tranche_package, cmake/Package.cmake).

#include "moteur/cook.hpp"
#include "moteur/data_table.hpp"

#include "sandbox_data.hpp"

int main(int argc, char* argv[]) {
    return moteur::cook_main(argc, argv, [](const std::string& root, const std::string& directory, moteur::DataIssues& issues) {
        moteur::DataTables tables;
        register_sandbox_tables(tables);
        return tables.compile(root, directory, issues);
    });
}
