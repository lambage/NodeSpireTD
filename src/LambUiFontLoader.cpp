#include "LambUiFontLoader.hpp"

#include <LambUI/UIFontAtlas.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>

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

} // namespace

LambUiFontLoader::LambUiFontLoader() = default;
LambUiFontLoader::~LambUiFontLoader() = default;

bool LambUiFontLoader::loadAll(const std::filesystem::path& fontsDirectory, int pixelHeight) {
    if (!std::filesystem::is_directory(fontsDirectory)) {
        std::fprintf(stderr, "LambUiFontLoader: font directory does not exist: %s\n", fontsDirectory.string().c_str());
        return false;
    }

    // fileName -> marked as the manifest's fallback/default face.
    std::unordered_map<std::string, bool> manifestedFiles;

    const std::filesystem::path manifestPath = fontsDirectory / kManifestFileName;
    if (std::filesystem::is_regular_file(manifestPath)) {
        std::ifstream manifestStream(manifestPath);
        nlohmann::json manifest;
        try {
            manifestStream >> manifest;
            for (const auto& entry : manifest.value("fonts", nlohmann::json::array())) {
                const std::string fileName = entry.at("file").get<std::string>();
                manifestedFiles[fileName] = entry.value("fallback", false);
            }
        } catch (const nlohmann::json::exception& e) {
            std::fprintf(stderr, "LambUiFontLoader: failed to parse font manifest %s: %s\n", manifestPath.string().c_str(), e.what());
        }
    }

    // Any font file present on disk but not covered by the manifest is still
    // loaded, so dropping a new font into the folder is never silently
    // ignored; it's just not eligible to be the default/fallback atlas.
    for (const auto& dirEntry : std::filesystem::directory_iterator(fontsDirectory)) {
        if (!dirEntry.is_regular_file() || !isFontFile(dirEntry.path()))
            continue;
        const std::string fileName = dirEntry.path().filename().string();
        manifestedFiles.try_emplace(fileName, false);
    }

    bool allSucceeded = true;
    std::string fallbackName;

    for (const auto& [fileName, isFallback] : manifestedFiles) {
        const std::filesystem::path filePath = fontsDirectory / fileName;
        auto atlas = std::make_unique<LambUI::FontAtlas>();
        if (!atlas->LoadFromFile(filePath.string(), pixelHeight)) {
            std::fprintf(stderr, "LambUiFontLoader: failed to load font: %s\n", filePath.string().c_str());
            allSucceeded = false;
            continue;
        }

        const std::string name = filePath.stem().string();
        if (isFallback || fallbackName.empty()) {
            fallbackName = name;
        }
        atlases_[name] = std::move(atlas);
    }

    if (fallbackName.empty()) {
        std::fprintf(stderr, "LambUiFontLoader: no fonts loaded from %s\n", fontsDirectory.string().c_str());
        return false;
    }

    textMeasurer_ = std::make_shared<LambUI::FontAtlasTextMeasurer>(*atlases_.at(fallbackName));
    for (const auto& [name, atlas] : atlases_) {
        textMeasurer_->RegisterFont(atlas.get(), *atlas);
    }

    return allSucceeded;
}

void* LambUiFontLoader::getFontHandle(const std::string& name) const {
    const auto it = atlases_.find(name);
    return it != atlases_.end() ? it->second.get() : nullptr;
}

std::shared_ptr<LambUI::ITextMeasurer> LambUiFontLoader::textMeasurer() const {
    return textMeasurer_;
}

void LambUiFontLoader::forEachFont(const std::function<void(void*, const LambUI::FontAtlas&)>& callback) const {
    for (const auto& [name, atlas] : atlases_) {
        callback(atlas.get(), *atlas);
    }
}

