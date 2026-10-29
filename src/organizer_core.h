// organizer_core.h
// File Organizer - core logic (no GUI code in this file).
// This is the same logic as the original command-line version:
// scan one folder, decide a category from the extension, never overwrite.
#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace organizer {

// Lowercase a string (used for case-insensitive extension matching)
inline std::string toLower(std::string text) {
    for (char &c : text) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return text;
}

inline bool contains(const std::vector<std::string> &list, const std::string &word) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

// One category and the extensions that belong to it (lowercase, no dot).
struct Category {
    std::string name;
    std::vector<std::string> extensions;
};

// The single place where categories are defined. To support a new file type,
// add its extension to the right list below. Anything not listed becomes "Others".
inline const std::vector<Category> &categoryTable() {
    static const std::vector<Category> table = {
        {"Images", {"png", "jpg", "jpeg", "jfif", "gif", "bmp", "webp", "svg", "ico", "tif", "tiff",
                    "heic", "heif", "avif", "raw", "cr2", "nef", "arw", "dng", "orf"}},
        {"Documents", {"pdf", "doc", "docx", "docm", "dotx", "txt", "rtf", "odt", "odg", "odf",
                       "md", "markdown", "rst", "tex", "log", "xls", "xlsx", "xlsm", "xltx", "csv", "tsv",
                       "ppt", "pptx", "pptm", "potx", "odp", "ods", "pages", "numbers", "key", "wps", "xps"}},
        {"Audio", {"mp3", "wav", "flac", "aac", "ogg", "m4a", "wma", "opus", "aiff", "aif",
                   "mid", "midi", "amr", "ape"}},
        {"Videos", {"mp4", "mkv", "avi", "mov", "wmv", "webm", "m4v", "mpg", "mpeg", "3gp", "ogv", "vob"}},
        {"Code", {"cpp", "cc", "cxx", "c", "h", "hpp", "hh", "hxx", "py", "js", "mjs", "cjs", "ts", "jsx",
                  "tsx", "vue", "html", "css", "scss", "sass", "less", "java", "kt", "scala", "groovy",
                  "gradle", "rs", "go", "php", "rb", "swift", "dart", "lua", "r", "pl", "m", "cs", "vb",
                  "asm", "json", "xml", "yaml", "yml", "toml", "ini", "cfg", "conf", "sql", "sh", "bash",
                  "zsh", "ps1", "bat", "cmd", "cmake", "mk", "ipynb"}},
        {"Archives", {"zip", "rar", "7z", "tar", "gz", "tgz", "bz2", "tbz2", "xz", "txz", "zst",
                      "lz", "lzma", "cab"}},
        {"Applications", {"exe", "msi", "msix", "appx", "apk", "jar", "appimage", "deb", "rpm", "pkg"}},
        {"Ebooks", {"epub", "mobi", "azw", "azw3", "fb2", "djvu", "cbz", "cbr"}},
        {"Fonts", {"ttf", "otf", "ttc", "woff", "woff2", "fon"}},
        {"Design", {"psd", "ai", "eps", "indd", "xd", "fig", "sketch", "xcf", "kra", "cdr",
                    "blend", "fbx", "obj", "stl", "3ds", "dwg", "dxf"}},
        {"Disk Images", {"iso", "img", "dmg", "vhd", "vhdx", "vmdk"}},
        {"Data", {"db", "sqlite", "sqlite3", "mdb", "accdb", "parquet", "jsonl"}},
    };
    return table;
}

// All category names in display order ("Others" is always last).
inline std::vector<std::string> categoryNames() {
    std::vector<std::string> names;
    for (const Category &c : categoryTable()) names.push_back(c.name);
    names.push_back("Others");
    return names;
}

// Decide the category from an extension given WITHOUT the dot, e.g. "jpg"
inline std::string categoryForExtension(const std::string &ext) {
    for (const Category &c : categoryTable()) {
        if (contains(c.extensions, ext)) return c.name;
    }
    return "Others";
}

// What happened (or will happen) to one file
enum class Status { Ready, Conflict, Moved, Skipped, Failed };

// One planned move
struct PlanItem {
    fs::path source;        // where the file is now
    fs::path destination;   // where it will go: <folder>/<Category>/<name>
    std::string category;
    Status status = Status::Ready;
    std::string message;    // explanation for Skipped / Failed
};

struct ScanResult {
    std::vector<PlanItem> items;
    int ignored = 0;        // program files, hidden and system files that were left alone
    std::string error;      // non-empty if the folder could not be read
};

// Files that belong to this project are never moved.
inline bool isProtectedFile(const fs::path &file, const fs::path &exePath) {
    static const std::vector<std::string> names = {
        "main.cpp", "file_organizer.cpp", "gui_main.cpp", "organizer_core.h",
        "organizer", "organizer.exe", "fileorganizer.exe", "build.bat",
        "app.rc", "app.manifest"};
    if (contains(names, toLower(file.filename().u8string()))) return true;

    std::error_code ec;
    if (!exePath.empty() && fs::equivalent(file, exePath, ec) && !ec) return true;
    return false;
}

// Hidden/system files (for example desktop.ini) are left alone for safety.
inline bool isHiddenOrSystem(const fs::path &p) {
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) != 0;
#else
    std::string n = p.filename().u8string();
    return !n.empty() && n[0] == '.';
#endif
}

// Scan ONLY the files directly inside `dir` (not subfolders, not symlinks).
inline ScanResult scanDirectory(const fs::path &dir, const fs::path &exePath) {
    ScanResult result;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    if (ec) {
        result.error = "Cannot read the folder: " + ec.message();
        return result;
    }

    const fs::directory_iterator end;
    while (it != end) {
        const fs::directory_entry &entry = *it;

        // symlink_status() does NOT follow links, so links show up as "symlink", not "regular".
        std::error_code e2;
        fs::file_status st = entry.symlink_status(e2);
        if (!e2 && st.type() == fs::file_type::regular) {
            if (isProtectedFile(entry.path(), exePath) || isHiddenOrSystem(entry.path())) {
                result.ignored++;
            } else {
                std::string ext = toLower(entry.path().extension().u8string());
                if (!ext.empty()) ext = ext.substr(1);   // remove the leading dot

                PlanItem item;
                item.source = entry.path();
                item.category = categoryForExtension(ext);
                item.destination = dir / item.category / entry.path().filename();

                // Preview-time conflict check (re-checked again right before moving)
                std::error_code e3;
                fs::file_status dst = fs::symlink_status(item.destination, e3);
                if (!e3 && dst.type() != fs::file_type::not_found) item.status = Status::Conflict;

                result.items.push_back(item);
            }
        }

        it.increment(ec);
        if (ec) {
            result.error = "Stopped while reading the folder: " + ec.message();
            break;
        }
    }

    // Sort by category, then by file name, so the preview is easy to read
    std::sort(result.items.begin(), result.items.end(),
              [](const PlanItem &a, const PlanItem &b) {
                  if (a.category != b.category) return a.category < b.category;
                  return toLower(a.source.filename().u8string()) <
                         toLower(b.source.filename().u8string());
              });
    return result;
}

// Move ONE file. Never overwrites, never deletes. Fills in status + message.
inline void moveOne(PlanItem &item) {
    std::error_code ec;

    // 1. make sure the category folder exists
    fs::path destDir = item.destination.parent_path();
    fs::create_directories(destDir, ec);
    if (ec || !fs::is_directory(destDir, ec)) {
        item.status = Status::Failed;
        item.message = "Cannot create folder '" + item.category + "'";
        return;
    }

    // 2. never overwrite: skip if something already exists there
    std::error_code e2;
    fs::file_status dst = fs::symlink_status(item.destination, e2);
    if (!e2 && dst.type() != fs::file_type::not_found) {
        item.status = Status::Skipped;
        item.message = "a file with this name already exists in " + item.category;
        return;
    }

#ifdef _WIN32
    // Flag 0 = fail if the destination exists, and do not copy across drives.
    if (MoveFileExW(item.source.c_str(), item.destination.c_str(), 0)) {
        item.status = Status::Moved;
        return;
    }
    DWORD code = GetLastError();
    std::string text = std::system_category().message(static_cast<int>(code));
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    item.status = Status::Failed;
    if (code == ERROR_NOT_SAME_DEVICE)
        text += " (different drive; the file was left where it is)";
    item.message = text;
#else
    fs::rename(item.source, item.destination, ec);
    if (ec) {
        item.status = Status::Failed;
        item.message = ec.message();
        if (ec == std::errc::cross_device_link)
            item.message += " (different drive; the file was left where it is)";
    } else {
        item.status = Status::Moved;
    }
#endif
}

}  // namespace organizer
