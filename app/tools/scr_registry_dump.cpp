// Prints what each colossus registry can run (scr_registry_dump), the coverage report's input
// (S13 design D13): `registry <name>` then its lines, for knight and kirin.
#include <cstdio>

#include "scr_kirin.h"
#include "scr_library.h"
#include "scr_predicates.h"

int main() {
    std::printf("registry knight\n%s", scr_registry_dump(scr_registry_knight()).c_str());
    std::printf("registry kirin\n%s", scr_registry_dump(scr_registry_kirin()).c_str());
    return 0;
}
