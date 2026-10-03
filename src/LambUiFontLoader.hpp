#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace LambUI {
class FontAtlas;
class FontAtlasTextMeasurer;
class ITextMeasurer;
}

// Loads every font referenced by fontsDirectory/fonts.json (see
// assets/fonts/fonts.json for the schema) into LambUI::FontAtlas SDF
// bitmaps, and builds a LambUI::FontAtlasTextMeasurer registering all of
// them by opaque handle so LambUI's layout code can measure any of them via
// LambUI::UIWidget::SetFont()/Lua's widget:SetFont(handle).
//
// Unlike the old RmlUi loader, LambUI has no font-family/weight/fallback-
// chain concept at the library level -- each file/size pair is one SDF
// atlas addressed by filename stem (e.g. "Inter-Regular") and pixel size. The
// manifest's "fallback" entry (or the first font found if none is marked)
// becomes the measurer's default atlas, used whenever a widget's font
// handle is null/unknown.
class LambUiFontLoader {
  public:
    LambUiFontLoader();
    ~LambUiFontLoader();

    // Loads every .ttf/.otf/.otc under fontsDirectory at the given SDF bake
    // size plus the manifest's "sizes" entries. Returns false if any font failed to load or the directory is
    // missing; individual failures are logged to stderr and do not stop the
    // rest of the batch from loading.
    bool loadAll(const std::filesystem::path& fontsDirectory, int pixelHeight = 48);

    // Opaque handle for LambUI::UIWidget::SetFont()/Lua's widget:SetFont();
    // Zero size selects the app's default size; unknown names/sizes return nullptr.
    void* getFontHandle(const std::string& name, int pixelHeight = 0) const;
    void* defaultFontHandle() const { return defaultFontHandle_; }

    // shared_ptr because LambUI::UIManager's constructor takes
    // std::shared_ptr<ITextMeasurer> (it may outlive this loader in theory,
    // though in practice LambUiApp keeps both alive for the whole process).
    std::shared_ptr<LambUI::ITextMeasurer> textMeasurer() const;

    // Invokes callback(fontHandle, atlas) for every loaded font, so a
    // renderer (e.g. lambui_backend::VulkanUiRenderer) can upload each
    // atlas's SDF bitmap under the same handle the measurer already knows.
    void forEachFont(const std::function<void(void*, const LambUI::FontAtlas&)>& callback) const;
    void forEachNamedFont(const std::function<void(const std::string&, int, void*, const LambUI::FontAtlas&)>& callback) const;

  private:
    std::unordered_map<std::string, std::unordered_map<int, std::unique_ptr<LambUI::FontAtlas>>> atlases_;
    int defaultPixelHeight_ = 48;
    std::shared_ptr<LambUI::FontAtlasTextMeasurer> textMeasurer_;
    void* defaultFontHandle_ = nullptr;
};

