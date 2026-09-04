#pragma once

#include <filesystem>

// Preloads RmlUi font faces from an assets/fonts directory, driven by an optional
// fonts.json manifest (see assets/fonts/fonts.json for the schema). Must be called
// after Rml::Initialise().
namespace RmlUiFontLoader {

// Loads every font referenced by fontsDirectory/fonts.json. Family, style, and
// weight are read from each font file's own metadata (FreeType name table /
// OS-2 usWeightClass), not inferred from the filename; the manifest only needs
// to flag the fallback face and, rarely, pin a weight for a variable font. Any
// .ttf/.otf/.otc file found in fontsDirectory but not listed in the manifest is
// still loaded the same way, so new font files are never silently dropped.
// Returns false if any font failed to load or the directory is missing;
// individual failures are logged via Rml::Log and do not stop the rest of the
// batch from loading.
bool LoadAll(const std::filesystem::path& fontsDirectory);

} // namespace RmlUiFontLoader
