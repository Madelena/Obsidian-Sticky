// =============================================================================
// CJK FONT
// =============================================================================
// The one translation unit that compiles stb_truetype, turning the `font`
// flash partition into a draw-time glyph source for canvas.cpp and text.cpp.
#include "ui/cjk_font.h"

#include <cinttypes>
#include <cmath>
#include <cstring>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "ui/canvas.h"
#include "ui/font.h"

// Scratch rasterization and stb's own working buffers come out of PSRAM; the
// internal heap is too small for glyph outlines at these sizes.
#define STBTT_malloc(x, u) ((void)(u), heap_caps_malloc((x), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))
#define STBTT_free(x, u) ((void)(u), heap_caps_free(x))
#define STB_TRUETYPE_IMPLEMENTATION

// stb_truetype is vendored verbatim, so its warnings are silenced here rather
// than by loosening the flags for the whole build.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#pragma GCC diagnostic ignored "-Wcast-qual"
#include "stb_truetype.h"
#pragma GCC diagnostic pop

namespace cjk_font {
namespace {

constexpr const char *kTag = "cjk_font";

// Application-defined subtype for the raw TrueType partition, matching the
// `font` row in partitions.csv.
constexpr esp_partition_subtype_t kFontSubtype = static_cast<esp_partition_subtype_t>(0x40);

// Side of the square rasterization scratch, comfortably over the 82 px box of
// the xxlarge face, which the FACES table in tools/gen_font.py sets.
constexpr int kScratchDim = 112;

constexpr uint32_t kSfntVersion = 0x00010000;  // Plain TrueType outlines
constexpr uint32_t kTrueTag = 0x74727565;      // 'true', old Apple flavour

stbtt_fontinfo s_info;
uint8_t *s_scratch = nullptr;
bool s_ready = false;

// Reads the four-byte tag that opens a font file, big-endian as sfnt stores it.
uint32_t sfnt_tag(const uint8_t *bytes)
{
    return (static_cast<uint32_t>(bytes[0]) << 24) | (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) | static_cast<uint32_t>(bytes[3]);
}

// Scale that fits the em box to the bitmap face, capped so the TrueType
// ascent never climbs above the face's baseline and into the line above.
float face_scale(const Font &face)
{
    float scale = stbtt_ScaleForPixelHeight(&s_info, static_cast<float>(face.height));
    int ascent = 0;
    int descent = 0;
    int line_gap = 0;
    stbtt_GetFontVMetrics(&s_info, &ascent, &descent, &line_gap);
    if (ascent > 0) {
        const float capped = static_cast<float>(face.baseline) / static_cast<float>(ascent);
        if (capped < scale) {
            scale = capped;
        }
    }
    return scale;
}

}  // namespace


// Reads the TrueType table directory and returns the file length it implies,
// or 0 when the partition does not start with a TrueType header.
size_t font_file_size(const esp_partition_t *part)
{
    uint8_t header[12] = {};
    if (esp_partition_read(part, 0, header, sizeof(header)) != ESP_OK) {
        return 0;
    }
    const uint32_t tag = sfnt_tag(header);
    if (tag != kSfntVersion && tag != kTrueTag) {
        return 0;
    }
    const size_t tables = (static_cast<size_t>(header[4]) << 8) | header[5];
    if (tables == 0 || tables > 64) {
        return 0;
    }
    size_t end = 12 + tables * 16;
    for (size_t i = 0; i < tables; ++i) {
        uint8_t entry[16] = {};
        if (esp_partition_read(part, 12 + i * 16, entry, sizeof(entry)) != ESP_OK) {
            return 0;
        }
        const size_t offset = (static_cast<size_t>(entry[8]) << 24) | (static_cast<size_t>(entry[9]) << 16) |
                              (static_cast<size_t>(entry[10]) << 8) | entry[11];
        const size_t length = (static_cast<size_t>(entry[12]) << 24) | (static_cast<size_t>(entry[13]) << 16) |
                              (static_cast<size_t>(entry[14]) << 8) | entry[15];
        // Tables are padded to four bytes in the file.
        const size_t table_end = offset + ((length + 3) & ~static_cast<size_t>(3));
        if (table_end > end) {
            end = table_end;
        }
    }
    return end <= part->size ? end : 0;
}


esp_err_t init()
{
    if (s_ready) {
        return ESP_OK;
    }
    const esp_partition_t *part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kFontSubtype, "font");
    if (part == nullptr) {
        ESP_LOGI(kTag, "No font partition, non-Latin text folds to ASCII");
        return ESP_ERR_NOT_FOUND;
    }

    // Map only the file, not the 8 MB partition: the flash cache addresses
    // 16 MB, and the partition's tail lies beyond it.
    const size_t file_size = font_file_size(part);
    if (file_size == 0) {
        ESP_LOGI(kTag, "Font partition holds no TrueType font, non-Latin text folds to ASCII");
        return ESP_ERR_NOT_FOUND;
    }
    const void *mapped = nullptr;
    esp_partition_mmap_handle_t handle = 0;
    const esp_err_t err =
        esp_partition_mmap(part, 0, file_size, ESP_PARTITION_MMAP_DATA, &mapped, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(kTag, "Font partition not mappable (%s), folding to ASCII", esp_err_to_name(err));
        return ESP_ERR_NOT_FOUND;
    }

    const uint8_t *bytes = static_cast<const uint8_t *>(mapped);
    const uint32_t tag = sfnt_tag(bytes);
    if ((tag != kSfntVersion && tag != kTrueTag) || stbtt_InitFont(&s_info, bytes, 0) == 0) {
        esp_partition_munmap(handle);
        ESP_LOGI(kTag, "Font partition holds no TrueType font, non-Latin text folds to ASCII");
        return ESP_ERR_NOT_FOUND;
    }

    s_scratch = static_cast<uint8_t *>(
        heap_caps_malloc(kScratchDim * kScratchDim, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (s_scratch == nullptr) {
        esp_partition_munmap(handle);
        ESP_LOGW(kTag, "No PSRAM for the glyph scratch, folding to ASCII");
        return ESP_ERR_NO_MEM;
    }

    s_ready = true;
    ESP_LOGI(kTag, "TrueType font mapped from partition at 0x%" PRIx32 ", %d glyphs",
             part->address, s_info.numGlyphs);
    return ESP_OK;
}


bool available()
{
    return s_ready;
}


bool has_glyph(uint32_t code_point)
{
    return s_ready && stbtt_FindGlyphIndex(&s_info, static_cast<int>(code_point)) != 0;
}


int advance(uint32_t code_point, const Font &face)
{
    if (!s_ready) {
        return 0;
    }
    int advance_units = 0;
    int left_bearing = 0;
    stbtt_GetCodepointHMetrics(&s_info, static_cast<int>(code_point), &advance_units,
                               &left_bearing);
    return static_cast<int>(std::lround(advance_units * face_scale(face)));
}


// TODO: add a rendered-glyph cache if a full screen of Chinese ever measures
// slower than 300 ms; every glyph is rasterized from outlines on each draw.
void draw(uint32_t code_point, int x, int y, const Font &face, bool black)
{
    if (!s_ready) {
        return;
    }
    const float scale = face_scale(face);
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    stbtt_GetCodepointBitmapBox(&s_info, static_cast<int>(code_point), scale, scale, &x0, &y0, &x1,
                                &y1);
    int width = x1 - x0;
    int height = y1 - y0;
    if (width <= 0 || height <= 0) {
        return;
    }
    width = width < kScratchDim ? width : kScratchDim;
    height = height < kScratchDim ? height : kScratchDim;

    std::memset(s_scratch, 0, static_cast<size_t>(width) * height);
    stbtt_MakeCodepointBitmap(&s_info, s_scratch, width, height, width, scale, scale,
                              static_cast<int>(code_point));

    // The box offsets are relative to the baseline, which sits at the bitmap
    // face's ascent so Latin and CJK on one line share it.
    const int left = x + x0;
    const int top = y + face.baseline + y0;
    for (int row = 0; row < height; ++row) {
        const uint8_t *coverage = s_scratch + static_cast<size_t>(row) * width;
        for (int column = 0; column < width; ++column) {
            if (coverage[column] >= 128) {
                canvas::set_pixel(left + column, top + row, black);
            }
        }
    }
}

}  // namespace cjk_font
