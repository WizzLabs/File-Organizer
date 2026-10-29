// Automated tests for the File Organizer core logic (no GUI needed).
// Build and run:
//   g++ -std=c++17 -Isrc tests/test_core.cpp -o test_core
//   ./test_core          (Windows: test_core.exe)
#include "organizer_core.h"

#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <set>

using namespace organizer;

static int g_failures = 0;
#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::cout << "FAIL line " << __LINE__ << ": " #cond "\n";            \
            g_failures++;                                                        \
        }                                                                        \
    } while (0)

static void writeFile(const fs::path &p, const std::string &text) {
    std::ofstream(p) << text;
}
static std::string readFile(const fs::path &p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    return s;
}

static void testCategoryTable() {
    std::set<std::string> seen;
    for (const Category &c : categoryTable()) {
        for (const std::string &e : c.extensions) {
            CHECK(e == toLower(e));                  // lowercase
            CHECK(e.find('.') == std::string::npos); // no dot
            CHECK(seen.insert(e).second);            // no extension in two categories
        }
    }
    CHECK(categoryNames().back() == "Others");
}

static void testCategories() {
    CHECK(categoryForExtension("exe") == "Applications");
    CHECK(categoryForExtension("msi") == "Applications");
    CHECK(categoryForExtension("md") == "Documents");
    CHECK(categoryForExtension("markdown") == "Documents");
    CHECK(categoryForExtension("jpg") == "Images");
    CHECK(categoryForExtension("heic") == "Images");
    CHECK(categoryForExtension("cpp") == "Code");
    CHECK(categoryForExtension("epub") == "Ebooks");
    CHECK(categoryForExtension("ttf") == "Fonts");
    CHECK(categoryForExtension("psd") == "Design");
    CHECK(categoryForExtension("iso") == "Disk Images");
    CHECK(categoryForExtension("sqlite") == "Data");
    CHECK(categoryForExtension("gz") == "Archives");
    CHECK(categoryForExtension("xyz") == "Others");
    CHECK(categoryForExtension("") == "Others");
}

static void testScanAndMove() {
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path root = fs::temp_directory_path() / ("fo_test_" + std::to_string(stamp));
    fs::create_directories(root / "sub");
    fs::create_directories(root / "Images");

    writeFile(root / "a.JPG", "new");              // conflicts with Images/a.JPG
    writeFile(root / "Images" / "a.JPG", "old");   // must survive untouched
    writeFile(root / "Setup.EXE", "x");
    writeFile(root / "README.md", "x");
    writeFile(root / "notes.txt", "x");
    writeFile(root / "noextension", "x");
    writeFile(root / "self.exe", "x");             // pretend this is the running program
    writeFile(root / "sub" / "inner.png", "x");    // inside a subfolder: must not move
#ifndef _WIN32
    writeFile(root / ".hidden", "x");
#endif

    ScanResult r = scanDirectory(root, root / "self.exe");
    CHECK(r.error.empty());
    CHECK(r.items.size() == 5);                     // a.JPG, Setup.EXE, README.md, notes.txt, noextension
    CHECK(r.ignored >= 1);                          // self.exe (+ .hidden on Linux)

    std::map<std::string, PlanItem> byName;
    for (const PlanItem &i : r.items) byName[i.source.filename().string()] = i;
    CHECK(byName["a.JPG"].category == "Images");
    CHECK(byName["a.JPG"].status == Status::Conflict);
    CHECK(byName["Setup.EXE"].category == "Applications");
    CHECK(byName["README.md"].category == "Documents");
    CHECK(byName["noextension"].category == "Others");
    CHECK(byName.count("inner.png") == 0);
    CHECK(byName.count("self.exe") == 0);

    for (PlanItem &i : r.items) moveOne(i);
    int moved = 0, skipped = 0;
    for (const PlanItem &i : r.items) {
        if (i.status == Status::Moved) moved++;
        if (i.status == Status::Skipped) skipped++;
    }
    CHECK(moved == 4);
    CHECK(skipped == 1);
    CHECK(readFile(root / "Images" / "a.JPG") == "old");   // never overwritten
    CHECK(fs::exists(root / "a.JPG"));                     // skipped file stays put
    CHECK(fs::exists(root / "Applications" / "Setup.EXE"));
    CHECK(fs::exists(root / "Documents" / "README.md"));
    CHECK(fs::exists(root / "Others" / "noextension"));
    CHECK(fs::exists(root / "sub" / "inner.png"));         // subfolder untouched
    CHECK(fs::exists(root / "self.exe"));                  // program file untouched

    std::error_code ec;
    fs::remove_all(root, ec);
}

int main() {
    testCategoryTable();
    testCategories();
    testScanAndMove();
    if (g_failures == 0) std::cout << "All tests passed.\n";
    else std::cout << g_failures << " check(s) failed.\n";
    return g_failures == 0 ? 0 : 1;
}
