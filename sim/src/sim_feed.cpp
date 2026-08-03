// feed_client para el simulador: en vez de HTTP, lee un fixture en disco que
// se captura del proxy real con tools/fetch_fixture.py. Asi el simulador
// muestra el mismo contenido que la caja, sin arrastrar un cliente HTTP.

#include "feed_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static FeedItem _items[FEED_MAX_ITEMS];
static uint8_t  _count = 0;

// Una imagen por item, en RGB565 crudo tal como la sirve el proxy.
static std::vector<std::vector<uint16_t>> _imgs;
static std::vector<std::string>           _imgKeys;

uint8_t feedCount() { return _count; }

const FeedItem* feedItem(uint8_t index) {
    if (index >= _count) return nullptr;
    return &_items[index];
}

const uint16_t* feedFetchImage(const char* key) {
    if (!key || !key[0]) return nullptr;
    for (size_t i = 0; i < _imgKeys.size(); i++) {
        if (_imgKeys[i] == key && _imgs[i].size() == FEED_IMG_PIXELS) {
            return _imgs[i].data();
        }
    }
    return nullptr;
}

static void setField(char* dst, size_t len, const std::string& src) {
    std::snprintf(dst, len, "%s", src.c_str());
}

static bool loadImage(const std::string& path, std::vector<uint16_t>& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.resize(FEED_IMG_PIXELS);
    const size_t got = std::fread(out.data(), 2, FEED_IMG_PIXELS, f);
    std::fclose(f);
    if (got != FEED_IMG_PIXELS) { out.clear(); return false; }
    return true;
}

// Formato del fixture, una linea por item:
//   texto|autor|handle|epoch|origen|archivo_imagen
// El separador es "|" porque el texto ya viene normalizado a ASCII sin barras
// verticales desde el proxy.
FeedResult feedFetch(uint16_t, uint16_t, uint16_t) {
    const char* path = std::getenv("SIM_FIXTURE");
    std::string file = path ? path : "data/fixture.txt";

    FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        std::printf("[sim] no encuentro el fixture %s\n", file.c_str());
        return _count > 0 ? FEED_STALE_CACHE : FEED_FAILED;
    }

    _count = 0;
    _imgs.clear();
    _imgKeys.clear();

    char line[1024];
    while (std::fgets(line, sizeof(line), f) && _count < FEED_MAX_ITEMS) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;

        std::vector<std::string> parts;
        size_t start = 0;
        for (size_t i = 0; i <= s.size(); i++) {
            if (i == s.size() || s[i] == '|') {
                parts.push_back(s.substr(start, i - start));
                start = i + 1;
            }
        }
        if (parts.size() < 5) continue;

        FeedItem& it = _items[_count];
        std::memset(&it, 0, sizeof(it));
        setField(it.text, FEED_TEXT_LEN, parts[0]);
        setField(it.author, FEED_AUTHOR_LEN, parts[1]);
        setField(it.handle, FEED_HANDLE_LEN, parts[2]);
        it.epoch = (uint32_t)std::strtoul(parts[3].c_str(), nullptr, 10);
        it.origin = parts[4] == "x" ? FEED_FROM_X
                  : parts[4] == "trend" ? FEED_FROM_TREND
                  : FEED_FROM_RSS;

        // El fixture guarda la key pelada, sin extension: con ".bin" son 20
        // caracteres y FEED_IMGKEY_LEN los trunca, con lo cual despues no
        // coincide con nada y la imagen no aparece.
        if (parts.size() >= 6 && !parts[5].empty()) {
            std::vector<uint16_t> px;
            if (loadImage("data/" + parts[5] + ".bin", px)) {
                setField(it.imgKey, FEED_IMGKEY_LEN, parts[5]);
                _imgKeys.push_back(parts[5]);
                _imgs.push_back(std::move(px));
            }
        }
        _count++;
    }
    std::fclose(f);

    std::printf("[sim] fixture: %u items, %zu imagenes\n", _count, _imgs.size());
    return _count > 0 ? FEED_UPDATED : FEED_FAILED;
}
