// padel_client para el simulador: en vez de HTTP, lee un fixture en disco que
// se genera del circuito real con
//
//   cd proxy && go run ./cmd/padelcheck -fixture ../sim/data/padel.txt
//
// Con nombres y largos de verdad: la pantalla de padel se rompe justamente con
// los apellidos largos ("S. Pineda Cabello"), y un fixture inventado los
// esconderia.

#include "padel_client.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static PadelMatch _matches[PADEL_MAX_MATCHES];
static uint8_t    _matchCount = 0;
static PadelTour  _tours[PADEL_MAX_TOURS];
static uint8_t    _tourCount = 0;
static PadelTour  _live;
static bool       _hasLive = false;
static char       _fecha[24] = {0};

uint8_t           padelMatchCount() { return _matchCount; }
uint8_t           padelTourCount() { return _tourCount; }
const PadelMatch* padelMatch(uint8_t i) { return i < _matchCount ? &_matches[i] : nullptr; }
const PadelTour*  padelTour(uint8_t i) { return i < _tourCount ? &_tours[i] : nullptr; }
const PadelTour*  padelLive() { return _hasLive ? &_live : nullptr; }
const char*       padelFecha() { return _fecha; }

uint8_t padelScreenCount() {
    return (uint8_t)((_hasLive ? 1 : 0) + _matchCount + _tourCount);
}

static void setField(char* dst, size_t len, const std::string& src) {
    std::snprintf(dst, len, "%s", src.c_str());
}

static std::vector<std::string> partir(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (size_t i = 0; i <= s.size(); i++) {
        if (i == s.size() || s[i] == '|') {
            out.push_back(s.substr(start, i - start));
            start = i + 1;
        }
    }
    return out;
}

static void leerTorneo(PadelTour& t, const std::vector<std::string>& p) {
    std::memset(&t, 0, sizeof(t));
    setField(t.name, sizeof(t.name), p[1]);
    setField(t.cat, sizeof(t.cat), p[2]);
    setField(t.city, sizeof(t.city), p[3]);
    setField(t.country, sizeof(t.country), p[4]);
    setField(t.rango, sizeof(t.rango), p[5]);
    t.faltan = (uint16_t)std::strtoul(p[6].c_str(), nullptr, 10);
}

PadelResult padelFetch() {
    const char* path = std::getenv("SIM_PADEL");
    const std::string file = path ? path : "data/padel.txt";

    FILE* f = std::fopen(file.c_str(), "r");
    if (!f) {
        std::printf("[sim] no encuentro el fixture de padel %s\n", file.c_str());
        return PADEL_FAILED;
    }

    _matchCount = 0;
    _tourCount = 0;
    _hasLive = false;
    _fecha[0] = '\0';

    char line[1024];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
        if (s.empty() || s[0] == '#') continue;

        const std::vector<std::string> p = partir(s);
        if (p.empty()) continue;

        if (p[0] == "L" && p.size() >= 9) {
            leerTorneo(_live, p);
            _live.live = true;
            _live.day = (uint8_t)std::strtoul(p[7].c_str(), nullptr, 10);
            _live.days = (uint8_t)std::strtoul(p[8].c_str(), nullptr, 10);
            if (p.size() >= 10) setField(_fecha, sizeof(_fecha), p[9]);
            _hasLive = _live.name[0] != '\0';
        } else if (p[0] == "T" && p.size() >= 7 && _tourCount < PADEL_MAX_TOURS) {
            leerTorneo(_tours[_tourCount], p);
            if (_tours[_tourCount].name[0]) _tourCount++;
        } else if (p[0] == "M" && p.size() >= 14 && _matchCount < PADEL_MAX_MATCHES) {
            PadelMatch& m = _matches[_matchCount];
            std::memset(&m, 0, sizeof(m));
            setField(m.time, sizeof(m.time), p[1]);
            setField(m.court, sizeof(m.court), p[2]);
            setField(m.round, sizeof(m.round), p[3]);
            m.gender = p[4].empty() ? 0 : p[4][0];
            setField(m.a1, sizeof(m.a1), p[5]);
            setField(m.a2, sizeof(m.a2), p[6]);
            setField(m.b1, sizeof(m.b1), p[7]);
            setField(m.b2, sizeof(m.b2), p[8]);
            setField(m.seedA, sizeof(m.seedA), p[9]);
            setField(m.seedB, sizeof(m.seedB), p[10]);
            setField(m.scoreA, sizeof(m.scoreA), p[11]);
            setField(m.scoreB, sizeof(m.scoreB), p[12]);
            m.state = (uint8_t)std::strtoul(p[13].c_str(), nullptr, 10);
            if (m.a1[0] && m.b1[0]) _matchCount++;
        }
    }
    std::fclose(f);

    std::printf("[sim] padel: %s, %u partidos, %u proximos\n",
                _hasLive ? _live.name : "sin torneo en juego",
                _matchCount, _tourCount);
    return padelScreenCount() > 0 ? PADEL_UPDATED : PADEL_FAILED;
}
