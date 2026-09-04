#include "RmlUiFontLoader.hpp"

#include <RmlUi/Core.h>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace {

constexpr const char* kManifestFileName = "fonts.json";
constexpr const char* kFontExtensions[] = {".ttf", ".otf", ".otc"};

bool isFontFile(const std::filesystem::path& path) {
    const std::string extension = path.extension().string();
    for (const char* candidate : kFontExtensions) {
        if (extension.size() == std::string(candidate).size() &&
            std::equal(extension.begin(), extension.end(), candidate, [](char a, char b) { return std::tolower(a) == b; }))
            return true;
    }
    return false;
}

Rml::Style::FontWeight parseWeight(const nlohmann::json& weight) {
    if (weight.is_number_integer())
        return static_cast<Rml::Style::FontWeight>(weight.get<int>());
    const std::string value = weight.get<std::string>();
    if (value == "bold")
        return Rml::Style::FontWeight::Bold;
    if (value == "normal")
        return Rml::Style::FontWeight::Normal;
    return Rml::Style::FontWeight::Auto;
}

// Family, style, and weight are read from the font file's own FreeType metadata
// (name table + OS/2 usWeightClass), so the manifest only needs to steer the
// handful of things RmlUi can't infer: fallback-face selection and, rarely, a
// weight pin for variable fonts.
bool loadFace(const std::filesystem::path& filePath, const nlohmann::json& entry) {
    const bool fallback = entry.value("fallback", false);
    const int faceIndex = entry.value("faceIndex", 0);
    const Rml::Style::FontWeight weight = entry.contains("weight") ? parseWeight(entry.at("weight")) : Rml::Style::FontWeight::Auto;
    return Rml::LoadFontFace(filePath.string(), fallback, weight, faceIndex);
}

} // namespace

namespace RmlUiFontLoader {

bool LoadAll(const std::filesystem::path& fontsDirectory) {
    if (!std::filesystem::is_directory(fontsDirectory)) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "Font directory does not exist: %s", fontsDirectory.string().c_str());
        return false;
    }

    bool allSucceeded = true;
    std::set<std::string> manifestedFiles;

    const std::filesystem::path manifestPath = fontsDirectory / kManifestFileName;
    if (std::filesystem::is_regular_file(manifestPath)) {
        std::ifstream manifestStream(manifestPath);
        nlohmann::json manifest;
        try {
            manifestStream >> manifest;
        } catch (const nlohmann::json::exception& e) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to parse font manifest %s: %s", manifestPath.string().c_str(), e.what());
            allSucceeded = false;
            manifest = nlohmann::json::object();
        }

        for (const auto& entry : manifest.value("fonts", nlohmann::json::array())) {
            const std::string fileName = entry.at("file").get<std::string>();
            manifestedFiles.insert(fileName);

            const std::filesystem::path filePath = fontsDirectory / fileName;
            if (!loadFace(filePath, entry)) {
                Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load font face: %s", filePath.string().c_str());
                allSucceeded = false;
            } else {
                Rml::Log::Message(Rml::Log::LT_INFO, "Successfully loaded font face: %s", filePath.string().c_str());
            }
        }
    }

    // Load any font file present on disk but not covered by the manifest, so
    // dropping a new font into the folder is never silently ignored.
    for (const auto& dirEntry : std::filesystem::directory_iterator(fontsDirectory)) {
        if (!dirEntry.is_regular_file() || !isFontFile(dirEntry.path()))
            continue;
        const std::string fileName = dirEntry.path().filename().string();
        if (manifestedFiles.count(fileName) != 0)
            continue;

        Rml::Log::Message(Rml::Log::LT_WARNING, "Font file not listed in fonts.json, loading with auto-detected metadata: %s", fileName.c_str());
        if (!Rml::LoadFontFace(dirEntry.path().string(), /*fallback_face=*/false)) {
            Rml::Log::Message(Rml::Log::LT_ERROR, "Failed to load font face: %s", dirEntry.path().string().c_str());
            allSucceeded = false;
        }
    }

    return allSucceeded;
}

} // namespace RmlUiFontLoader
