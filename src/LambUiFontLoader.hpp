#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace LambUI {
class FontAtlas;
class FontAtlasTextMeasurer;
}

// Loads every font referenced by fontsDirectory/fonts.json (see
// assets/fonts/fonts.json for the schema) into LambUI::FontAtlas SDF
// bitmaps, and builds a LambUI::FontAtlasTextMeasurer registering all of
// them by opaque handle so LambUI's layout code can measure any of them via
// LambUI::UIWidget::SetFont()/Lua's widget:SetFont(handle).
//
// Unlike the old RmlUi loader, LambUI has no font-family/weight/fallback-
// chain concept at the library level -- each loaded file is just one SDF
// atlas addressed by its own filename stem (e.g. "Inter-Regular"). The
// manifest's "fallback" entry (or the first font found if none is marked)
// becomes the measurer's default atlas, used whenever a widget's font
// handle is null/unknown.
class LambUiFontLoader {
  public:
    // Loads every .ttf/.otf/.otc under fontsDirectory at the given SDF bake
    // size. Returns false if any font failed to load or the directory is
    // missing; individual failures are logged to stderr and do not stop the
    // rest of the batch from loading.
    bool loadAll(const std::filesystem::path& fontsDirectory, int pixelHeight = 48);

    // Opaque handle for LambUI::UIWidget::SetFont()/Lua's widget:SetFont();
    // nullptr if name is unknown (widgets then fall back to the measurer's
    // default atlas).
    void* getFontHandle(const std::string& name) const;

    LambUI::FontAtlasTextMeasurer* textMeasurer() const { return textMeasurer_.get(); }

  private:
    std::unordered_map<std::string, std::unique_ptr<LambUI::FontAtlas>> atlases_;
    std::unique_ptr<LambUI::FontAtlasTextMeasurer> textMeasurer_;
};

