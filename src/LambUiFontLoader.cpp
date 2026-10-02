#include "LambUiFontLoader.hpp"

#include <LambUI/UIFontAtlas.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

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
    std::set<int> sizes{pixelHeight};

    const std::filesystem::path manifestPath = fontsDirectory / kManifestFileName;
    if (std::filesystem::is_regular_file(manifestPath)) {
        std::ifstream manifestStream(manifestPath);
        nlohmann::json manifest;
        try {
            manifestStream >> manifest;
            const auto requestedSizes = manifest.value("sizes", nlohmann::json::array());
            if (!requestedSizes.is_array()) throw std::invalid_argument("font sizes must be an array");
            for (const auto& size : requestedSizes) {
                if (!size.is_number_integer() || size < 1 || size > 512)
                    throw std::invalid_argument("font sizes must be integers from 1 to 512");
                sizes.insert(size.get<int>());
            }
            for (const auto& entry : manifest.value("fonts", nlohmann::json::array())) {
                const std::string fileName = entry.at("file").get<std::string>();
                manifestedFiles[fileName] = entry.value("fallback", false);
            }
        } catch (const std::exception& e) {
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
        const std::string name = filePath.stem().string();
        for (int size : sizes) {
            auto atlas = std::make_unique<LambUI::FontAtlas>();
            if (!atlas->LoadFromFile(filePath.string(), size)) {
                std::fprintf(stderr, "LambUiFontLoader: failed to load font: %s at %d px\n", filePath.string().c_str(), size);
                allSucceeded = false;
                continue;
            }
            if (size == pixelHeight && (isFallback || fallbackName.empty())) fallbackName = name;
            atlases_[name][size] = std::move(atlas);
        }
    }

    if (fallbackName.empty()) {
        std::fprintf(stderr, "LambUiFontLoader: no fonts loaded from %s\n", fontsDirectory.string().c_str());
        return false;
    }

    defaultPixelHeight_ = pixelHeight;
    const auto& defaultAtlas = atlases_.at(fallbackName).at(pixelHeight);
    textMeasurer_ = std::make_shared<LambUI::FontAtlasTextMeasurer>(*defaultAtlas);
    defaultFontHandle_ = defaultAtlas.get();
    forEachFont([&](void* handle, const LambUI::FontAtlas& atlas) {
        textMeasurer_->RegisterFont(handle, atlas);
    });

    return allSucceeded;
}

void* LambUiFontLoader::getFontHandle(const std::string& name, int pixelHeight) const {
    const auto it = atlases_.find(name);
    if (it == atlases_.end()) return nullptr;
    const auto size = it->second.find(pixelHeight == 0 ? defaultPixelHeight_ : pixelHeight);
    return size != it->second.end() ? size->second.get() : nullptr;
}

std::shared_ptr<LambUI::ITextMeasurer> LambUiFontLoader::textMeasurer() const {
    return textMeasurer_;
}

void LambUiFontLoader::forEachFont(const std::function<void(void*, const LambUI::FontAtlas&)>& callback) const {
    forEachNamedFont([&](const std::string&, int, void* handle, const LambUI::FontAtlas& atlas) {
        callback(handle, atlas);
    });
}

void LambUiFontLoader::forEachNamedFont(const std::function<void(const std::string&, int, void*, const LambUI::FontAtlas&)>& callback) const {
    for (const auto& [name, sizes] : atlases_) {
        for (const auto& [size, atlas] : sizes) callback(name, size, atlas.get(), *atlas);
    }
}

