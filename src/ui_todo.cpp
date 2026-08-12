#include "ui_todo.h"

#include "config.h"
#include "design_system.h"
#include "ui_chrome.h"

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace FercedColors;
using namespace FercedChrome;

namespace {

LGFX_Sprite& g = uiSprite;
constexpr int MARK_W = 11;
constexpr int CONTENT_TOP = 88;
constexpr int CONTENT_BOTTOM = 424;
constexpr int CHECK_X = 28;
constexpr int TEXT_X = 72;
constexpr int FOLD_X = 426;
constexpr int TITLE_W = 326;
constexpr int TITLE_LINE_H = 23;
constexpr int SUB_LINE_H = 19;
constexpr int CARD_GAP = 8;
constexpr int MAX_LINES = 8;
constexpr int DETAIL_DESCRIPTION_LINES = 24;
constexpr int DETAIL_DESCRIPTION = 1;
constexpr int MAX_HITS = 24;
constexpr int REMINDER_X = 24;
constexpr int REMINDER_Y = 62;
constexpr int REMINDER_W = SCREEN_W - REMINDER_X * 2;
constexpr int REMINDER_H = 356;
constexpr int REMINDER_BUTTON_Y = 338;
constexpr int REMINDER_BUTTON_H = 54;

struct HitRegion {
    int16_t x0, y0, x1, y1;
    TodoUiAction action;
};

HitRegion hits[MAX_HITS];
uint8_t hitCount = 0;
int16_t scrollY = 0;
int16_t detailScrollY = 0;
int16_t contentHeight = 0;
char address[48] = {};
bool ready = false;
bool detail = false;
uint32_t detailId = 0;

bool localTimeSafe(const time_t* input, struct tm* output) {
#ifdef FERCED_SIM
    // MinGW no implementa localtime_r. El simulador tiene un solo hilo de UI,
    // así que copiar el buffer de localtime es seguro en este entorno.
    struct tm* value = std::localtime(input);
    if (!value) return false;
    *output = *value;
    return true;
#else
    return localtime_r(input, output) != nullptr;
#endif
}

void addHit(int x0, int y0, int x1, int y1, TodoUiActionType type,
            uint32_t taskId, uint32_t subtaskId = 0) {
    if (hitCount >= MAX_HITS || y1 < CONTENT_TOP || y0 > CONTENT_BOTTOM) return;
    hits[hitCount++] = {static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                        static_cast<int16_t>(x1), static_cast<int16_t>(y1),
                        {type, taskId, subtaskId}};
}

size_t codepointBytes(const char* p) {
    const unsigned char c = static_cast<unsigned char>(*p);
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    return 4;
}

// Envuelve por ancho medido y conserva todo el texto: nunca agrega "...".
int wrapTextSized(const char* text, size_t inputCapacity, int maxW,
                  char lines[][TODO_TITLE_LEN], int maxLines) {
    if (!text || !text[0] || maxLines <= 0) return 0;
    char copy[TODO_DESCRIPTION_LEN];
    const size_t capacity = inputCapacity < sizeof(copy) ? inputCapacity : sizeof(copy);
    std::snprintf(copy, capacity, "%s", text);
    for (char* p = copy; *p; ++p) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    int count = 0;
    char* context = nullptr;
    char* word = strtok_r(copy, " ", &context);
    while (word && count < maxLines) {
        char* remaining = word;
        while (*remaining && count < maxLines) {
            char candidate[TODO_TITLE_LEN];
            if (lines[count][0]) std::snprintf(candidate, sizeof(candidate), "%s %s", lines[count], remaining);
            else std::snprintf(candidate, sizeof(candidate), "%s", remaining);
            if (g.textWidth(candidate) <= maxW) {
                std::snprintf(lines[count], TODO_TITLE_LEN, "%s", candidate);
                remaining += std::strlen(remaining);
                break;
            }
            if (lines[count][0]) { ++count; continue; }
            size_t used = 0;
            while (remaining[used]) {
                const size_t bytes = codepointBytes(remaining + used);
                char piece[TODO_TITLE_LEN];
                const size_t take = used + bytes < sizeof(piece) ? used + bytes : sizeof(piece) - 1;
                std::memcpy(piece, remaining, take); piece[take] = '\0';
                if (g.textWidth(piece) > maxW && used > 0) break;
                used += bytes;
            }
            if (used == 0) used = codepointBytes(remaining);
            const size_t take = used < TODO_TITLE_LEN ? used : TODO_TITLE_LEN - 1;
            std::memcpy(lines[count], remaining, take); lines[count][take] = '\0';
            remaining += used;
            if (*remaining) ++count;
        }
        word = strtok_r(nullptr, " ", &context);
    }
    return count + (count < maxLines && lines[count][0] ? 1 : 0);
}

int wrapText(const char* text, int maxW, char lines[][TODO_TITLE_LEN], int maxLines) {
    return wrapTextSized(text, TODO_TITLE_LEN, maxW, lines, maxLines);
}

int wrapped(const char* text, int width, char lines[][TODO_TITLE_LEN], int maxLines = MAX_LINES) {
    for (int i = 0; i < maxLines; ++i) lines[i][0] = '\0';
    return wrapText(text, width, lines, maxLines);
}

int taskHeight(const TodoItem& item) {
    char lines[MAX_LINES][TODO_TITLE_LEN];
    const int lineCount = wrapped(item.title, TITLE_W, lines);
    int height = 22 + lineCount * TITLE_LINE_H + 24;
    if (!item.collapsed) {
        g.setFont(DS::fontBody());
        for (uint8_t i = 0; i < item.subCount; ++i) {
            char subLines[MAX_LINES][TODO_TITLE_LEN];
            const int n = wrapped(item.subtasks[i].title, 340, subLines);
            height += 13 + n * SUB_LINE_H;
        }
        g.setFont(DS::fontHeading());
    }
    return height;
}

void checkbox(int x, int y, bool done, int size = 22) {
    const int radius = 7;
    if (done) {
        g.fillSmoothRoundRect(x, y, size, size, radius, FG);
        g.drawLine(x + 5, y + size / 2, x + 9, y + size - 6, CANVAS);
        g.drawLine(x + 9, y + size - 6, x + size - 5, y + 6, CANVAS);
    } else {
        g.fillSmoothRoundRect(x, y, size, size, radius, FG_4);
        g.fillSmoothRoundRect(x + 2, y + 2, size - 4, size - 4, radius - 1, CANVAS);
    }
}

void dueText(const TodoItem& item, char* output, size_t size) {
    output[0] = '\0';
    if (!item.dueEpoch) return;
    const time_t now = time(nullptr);
    if (now > 1600000000 && item.dueEpoch < static_cast<uint32_t>(now) && !item.done) {
        std::snprintf(output, size, "VENCIDA"); return;
    }
    time_t due = static_cast<time_t>(item.dueEpoch);
    struct tm value{};
    if (!localTimeSafe(&due, &value)) return;
    strftime(output, size, "%d/%m · %H:%M", &value);
}

void drawEmpty() {
    g.setFont(DS::fontAcento()); g.setTextColor(FG_2, CANVAS); g.setTextDatum(lgfx::top_left);
    g.drawString("nada pendiente.", MARGIN, 176);
    g.setFont(DS::fontBody()); g.setTextColor(FG_3, CANVAS);
    g.drawString("Agregá la próxima desde el teléfono:", MARGIN, 231);
    g.setTextColor(FG, CANVAS); g.drawString(address, MARGIN, 260);
}

void drawList() {
    g.fillScreen(CANVAS);
    hitCount = 0;
    const uint8_t total = todoCount(), pending = todoPending();
    char count[32];
    if (!total) std::snprintf(count, sizeof(count), "VACÍA");
    else if (!pending) std::snprintf(count, sizeof(count), "TODO HECHO");
    else std::snprintf(count, sizeof(count), "%u PENDIENTE%s", pending, pending == 1 ? "" : "S");
    uiEyebrow(g, "TAREAS", count, 1.0f);
    uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);
    if (!total) { scrollY = 0; contentHeight = 0; drawEmpty(); uiRail(g, RAIL_Y, 0.0f); uiAnimReveal(); return; }

    g.setFont(DS::fontHeading());
    contentHeight = 0;
    for (uint8_t i = 0; i < total; ++i) { const TodoItem* item = todoItem(i); if (item) contentHeight += taskHeight(*item) + CARD_GAP; }
    const int viewport = CONTENT_BOTTOM - CONTENT_TOP;
    const int maxScroll = contentHeight > viewport ? contentHeight - viewport : 0;
    if (scrollY < 0) scrollY = 0;
    if (scrollY > maxScroll) scrollY = maxScroll;

    int y = CONTENT_TOP - scrollY;
    uint8_t doneCount = 0;
    for (uint8_t i = 0; i < total; ++i) {
        const TodoItem* item = todoItem(i);
        if (!item) continue;
        if (item->done) ++doneCount;
        const int height = taskHeight(*item);
        if (y + height >= CONTENT_TOP && y <= CONTENT_BOTTOM) {
            g.drawFastHLine(MARGIN, y + height - 1, SCREEN_W - MARGIN * 2, LINE);
            checkbox(CHECK_X, y + 14, item->done);
            addHit(14, y, 66, y + 54, TODO_UI_TOGGLE_TASK, item->id);
            char lines[MAX_LINES][TODO_TITLE_LEN];
            g.setFont(DS::fontHeading());
            const int n = wrapped(item->title, TITLE_W, lines);
            g.setTextColor(item->done ? FG_4 : FG, CANVAS); g.setTextDatum(lgfx::top_left);
            for (int line = 0; line < n; ++line) g.drawString(lines[line], TEXT_X, y + 12 + line * TITLE_LINE_H);
            addHit(66, y, FOLD_X - 6, y + 20 + n * TITLE_LINE_H + 22, TODO_UI_OPEN_DETAIL, item->id);

            char due[28]; dueText(*item, due, sizeof(due));
            char meta[48] = {};
            if (item->subCount) { uint8_t complete = 0; for (uint8_t s = 0; s < item->subCount; ++s) if (item->subtasks[s].done) ++complete; std::snprintf(meta, sizeof(meta), "%u/%u SUBTAREAS", complete, item->subCount); }
            g.setFont(DS::fontCaption()); g.setTextColor(due[0] && std::strcmp(due, "VENCIDA") == 0 ? DANGER : FG_4, CANVAS);
            const int metaY = y + 15 + n * TITLE_LINE_H;
            if (due[0]) g.drawString(due, TEXT_X, metaY);
            if (meta[0]) { g.setTextDatum(lgfx::top_right); g.setTextColor(FG_4, CANVAS); g.drawString(meta, FOLD_X - 9, metaY); }

            if (item->subCount) {
                g.setTextDatum(lgfx::middle_center); g.setFont(DS::fontBody()); g.setTextColor(FG_3, CANVAS);
                g.drawString(item->collapsed ? "+" : "-", FOLD_X, y + 26);
                addHit(FOLD_X - 20, y, SCREEN_W - 8, y + 55, TODO_UI_TOGGLE_COLLAPSE, item->id);
            }
            if (!item->collapsed) {
                int subY = metaY + 27;
                g.setFont(DS::fontBody()); g.setTextDatum(lgfx::top_left);
                for (uint8_t s = 0; s < item->subCount; ++s) {
                    const TodoSubtask& sub = item->subtasks[s];
                    char subLines[MAX_LINES][TODO_TITLE_LEN];
                    const int sn = wrapped(sub.title, 340, subLines);
                    checkbox(48, subY + 1, sub.done, 18);
                    g.setTextColor(sub.done ? FG_4 : FG_2, CANVAS);
                    for (int line = 0; line < sn; ++line) g.drawString(subLines[line], 78, subY + line * SUB_LINE_H);
                    const int subHeight = 13 + sn * SUB_LINE_H;
                    addHit(38, subY - 5, SCREEN_W - MARGIN, subY + subHeight, TODO_UI_TOGGLE_SUBTASK, item->id, sub.id);
                    subY += subHeight;
                }
            }
        }
        y += height + CARD_GAP;
    }

    g.fillRect(0, 0, SCREEN_W, CONTENT_TOP, CANVAS);
    uiEyebrow(g, "TAREAS", count, 1.0f); uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);
    g.fillRect(0, CONTENT_BOTTOM + 1, SCREEN_W, SCREEN_H - CONTENT_BOTTOM - 1, CANVAS);
    g.setFont(DS::fontCaption()); g.setTextColor(FG_4, CANVAS); g.setTextDatum(lgfx::top_left); g.drawString(address, MARGIN, 430);
    if (scrollY > 0) { g.setTextDatum(lgfx::top_right); g.drawString("ARRIBA", SCREEN_W - MARGIN, 88); }
    if (scrollY < maxScroll) { g.setTextDatum(lgfx::top_right); g.drawString("DESLIZÁ PARA SEGUIR", SCREEN_W - MARGIN, 430); }
    uiRail(g, RAIL_Y, total ? static_cast<float>(doneCount) / total : 0.0f);
    uiAnimReveal();
}

void dateTimeText(uint32_t epoch, char* output, size_t size) {
    output[0] = '\0';
    if (!epoch) return;
    time_t value = static_cast<time_t>(epoch);
    struct tm local{};
    if (!localTimeSafe(&value, &local)) return;
    strftime(output, size, "%d/%m/%Y · %H:%M", &local);
}

void drawDetail() {
    const TodoItem* item = todoGetById(detailId);
    if (!item) { detail = false; detailId = 0; drawList(); return; }
    g.fillScreen(CANVAS);
    hitCount = 0;

    char right[28] = {};
    if (item->done) std::snprintf(right, sizeof(right), "HECHA");
    else if (item->subCount) {
        uint8_t done = 0; for (uint8_t i = 0; i < item->subCount; ++i) if (item->subtasks[i].done) ++done;
        std::snprintf(right, sizeof(right), "%u/%u SUBTAREAS", done, item->subCount);
    } else std::snprintf(right, sizeof(right), "PENDIENTE");
    uiEyebrow(g, "<  TAREAS", right, 1.0f);
    uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);
    addHit(0, 0, 155, CONTENT_TOP + 2, TODO_UI_BACK, item->id);

    // Medición completa antes de dibujar para poder limitar el scroll.
    g.setFont(DS::fontTitular());
    char titleLines[MAX_LINES][TODO_TITLE_LEN];
    const int titleCount = wrapped(item->title, 374, titleLines);
    int measured = 18 + titleCount * 38 + 22;
    if (item->dueEpoch || item->reminderEpoch) measured += 58;
    char descriptionLines[DETAIL_DESCRIPTION_LINES][TODO_TITLE_LEN];
    for (int i = 0; i < DETAIL_DESCRIPTION_LINES; ++i) descriptionLines[i][0] = '\0';
    g.setFont(DS::fontBody());
    const int descriptionCount = wrapTextSized(item->description, TODO_DESCRIPTION_LEN, 424,
                                               descriptionLines, DETAIL_DESCRIPTION_LINES);
    if (descriptionCount) measured += 34 + descriptionCount * 21 + 18;
    if (item->subCount) measured += 38;
    for (uint8_t i = 0; i < item->subCount; ++i) {
        char subLines[MAX_LINES][TODO_TITLE_LEN];
        measured += 12 + wrapped(item->subtasks[i].title, 356, subLines) * SUB_LINE_H;
    }
    contentHeight = measured;
    const int viewport = CONTENT_BOTTOM - CONTENT_TOP;
    const int maxScroll = contentHeight > viewport ? contentHeight - viewport : 0;
    if (detailScrollY < 0) detailScrollY = 0;
    if (detailScrollY > maxScroll) detailScrollY = maxScroll;

    int y = CONTENT_TOP + 10 - detailScrollY;
    checkbox(MARGIN, y + 6, item->done, 24);
    addHit(14, y - 4, 62, y + 52, TODO_UI_TOGGLE_TASK, item->id);
    g.setFont(DS::fontTitular()); g.setTextColor(item->done ? FG_4 : FG, CANVAS); g.setTextDatum(lgfx::top_left);
    for (int i = 0; i < titleCount; ++i) g.drawString(titleLines[i], 68, y + i * 38);
    y += titleCount * 38 + 20;

    if (item->dueEpoch || item->reminderEpoch) {
        char due[32], reminder[32]; dateTimeText(item->dueEpoch, due, sizeof(due)); dateTimeText(item->reminderEpoch, reminder, sizeof(reminder));
        g.drawFastHLine(MARGIN, y, SCREEN_W - MARGIN * 2, LINE);
        g.setFont(DS::fontCaption()); g.setTextColor(FG_4, CANVAS); g.setTextDatum(lgfx::top_left);
        if (due[0]) { g.drawString("VENCE", MARGIN, y + 12); g.setTextColor(FG_2, CANVAS); g.drawString(due, 90, y + 12); }
        if (reminder[0]) { g.setTextColor(FG_4, CANVAS); g.drawString("AVISA", MARGIN, y + 34); g.setTextColor(FG_2, CANVAS); g.drawString(reminder, 90, y + 34); }
        y += 58;
    }

    if (descriptionCount) {
        g.setFont(DS::fontCaption()); g.setTextColor(FG_4, CANVAS); g.drawString("DESCRIPCIÓN", MARGIN, y + 12);
        y += 34;
        g.setFont(DS::fontBody()); g.setTextColor(FG_2, CANVAS);
        for (int i = 0; i < descriptionCount; ++i) g.drawString(descriptionLines[i], MARGIN, y + i * 21);
        y += descriptionCount * 21 + 18;
    }

    if (item->subCount) {
        g.drawFastHLine(MARGIN, y, SCREEN_W - MARGIN * 2, LINE);
        g.setFont(DS::fontCaption()); g.setTextColor(FG_4, CANVAS); g.drawString("SUBTAREAS", MARGIN, y + 14);
        y += 38;
        g.setFont(DS::fontBody());
        for (uint8_t i = 0; i < item->subCount; ++i) {
            const TodoSubtask& sub = item->subtasks[i];
            char subLines[MAX_LINES][TODO_TITLE_LEN];
            const int n = wrapped(sub.title, 356, subLines);
            checkbox(MARGIN, y + 1, sub.done, 18);
            g.setTextColor(sub.done ? FG_4 : FG_2, CANVAS);
            for (int line = 0; line < n; ++line) g.drawString(subLines[line], 62, y + line * SUB_LINE_H);
            const int height = 12 + n * SUB_LINE_H;
            addHit(18, y - 6, SCREEN_W - MARGIN, y + height, TODO_UI_TOGGLE_SUBTASK, item->id, sub.id);
            y += height;
        }
    }

    // Chasis fijo por encima del contenido desplazado.
    g.fillRect(0, 0, SCREEN_W, CONTENT_TOP, CANVAS);
    uiEyebrow(g, "<  TAREAS", right, 1.0f); uiMark(g, SCREEN_W - MARGIN - MARK_W, 20, 1.0f);
    g.fillRect(0, CONTENT_BOTTOM + 1, SCREEN_W, SCREEN_H - CONTENT_BOTTOM - 1, CANVAS);
    g.setFont(DS::fontCaption()); g.setTextColor(FG_4, CANVAS); g.setTextDatum(lgfx::top_left);
    g.drawString("TOCÁ LA CASILLA PARA COMPLETAR", MARGIN, 430);
    if (detailScrollY < maxScroll) { g.setTextDatum(lgfx::top_right); g.drawString("SEGUIR", SCREEN_W - MARGIN, 430); }
    uiRail(g, RAIL_Y, item->subCount ? static_cast<float>(item->done ? item->subCount : 0) / item->subCount : (item->done ? 1.0f : 0.0f));
    uiAnimReveal();
}

}  // namespace

void uiTodoSetup() { uiAnimSetup(); ready = true; }
void uiTodoDraw(const char* direccion) { if (!ready) return; if (direccion) std::snprintf(address, sizeof(address), "%s", direccion); if (detail) drawDetail(); else drawList(); }
TodoUiAction uiTodoTap(int16_t x, int16_t y) { for (int i = static_cast<int>(hitCount) - 1; i >= 0; --i) if (x >= hits[i].x0 && x <= hits[i].x1 && y >= hits[i].y0 && y <= hits[i].y1) return hits[i].action; return {TODO_UI_NONE, 0, 0}; }
void uiTodoScroll(int16_t delta) { if (detail) { detailScrollY += delta; drawDetail(); } else { scrollY += delta; drawList(); } }
void uiTodoOpen(uint32_t taskId) { detail = true; detailId = taskId; detailScrollY = 0; drawDetail(); }
void uiTodoBack() { detail = false; detailId = 0; drawList(); }
bool uiTodoIsDetail() { return detail; }
uint8_t uiTodoVisibles() { return hitCount; }

void uiTodoDrawReminder(const TodoItem* item) {
    if (!ready || !item) return;

    // Es una tarjeta que interrumpe cualquier app, no una pantalla nueva: el
    // sprite conserva lo que se estaba mirando y la tarjeta se apoya encima.
    g.fillSmoothRoundRect(REMINDER_X + 4, REMINDER_Y + 6,
                          REMINDER_W, REMINDER_H, 26, SURFACE);
    g.fillSmoothRoundRect(REMINDER_X, REMINDER_Y,
                          REMINDER_W, REMINDER_H, 26, FG);

    const uint16_t inkMuted = uiAnimLerp(CANVAS, FG, 0.46f);
    g.setTextDatum(lgfx::top_left);
    g.setFont(DS::fontCaption());
    g.setTextColor(inkMuted, FG);
    g.drawString("RECORDATORIO", REMINDER_X + 24, REMINDER_Y + 24);

    // Campana abstracta, dibujada sin depender de glifos de la fuente.
    const int bellX = REMINDER_X + REMINDER_W - 43;
    const int bellY = REMINDER_Y + 30;
    g.drawCircle(bellX, bellY, 10, CANVAS);
    g.fillRect(bellX - 10, bellY, 21, 10, CANVAS);
    g.fillSmoothCircle(bellX, bellY + 13, 3, CANVAS);

    g.setFont(DS::fontHeading());
    char titleLines[5][TODO_TITLE_LEN];
    const int titleCount = wrapped(item->title, REMINDER_W - 48, titleLines, 5);
    g.setTextColor(CANVAS, FG);
    for (int i = 0; i < titleCount; ++i) {
        g.drawString(titleLines[i], REMINDER_X + 24, REMINDER_Y + 62 + i * 27);
    }

    char when[32] = {};
    dateTimeText(item->reminderEpoch, when, sizeof(when));
    g.setFont(DS::fontCaption());
    g.setTextColor(inkMuted, FG);
    if (when[0]) g.drawString(when, REMINDER_X + 24, REMINDER_Y + 208);
    if (item->subCount) {
        uint8_t complete = 0;
        for (uint8_t i = 0; i < item->subCount; ++i) if (item->subtasks[i].done) ++complete;
        char progress[32];
        std::snprintf(progress, sizeof(progress), "%u/%u SUBTAREAS", complete, item->subCount);
        g.setTextDatum(lgfx::top_right);
        g.drawString(progress, REMINDER_X + REMINDER_W - 24, REMINDER_Y + 208);
    }

    const int completeX = 48, completeW = 122;
    const int snoozeX = 180, snoozeW = 118;
    const int openX = 308, openW = 124;
    const uint16_t pale = uiAnimLerp(CANVAS, FG, 0.90f);
    g.fillSmoothRoundRect(completeX, REMINDER_BUTTON_Y, completeW, REMINDER_BUTTON_H, 14, CANVAS);
    g.fillSmoothRoundRect(snoozeX, REMINDER_BUTTON_Y, snoozeW, REMINDER_BUTTON_H, 14, pale);
    g.fillSmoothRoundRect(openX, REMINDER_BUTTON_Y, openW, REMINDER_BUTTON_H, 14, pale);
    g.setFont(DS::fontCaption());
    g.setTextDatum(lgfx::middle_center);
    g.setTextColor(FG, CANVAS);
    g.drawString("COMPLETAR", completeX + completeW / 2, REMINDER_BUTTON_Y + REMINDER_BUTTON_H / 2);
    g.setTextColor(FG, pale);
    g.drawString("+10 MIN", snoozeX + snoozeW / 2, REMINDER_BUTTON_Y + REMINDER_BUTTON_H / 2);
    g.drawString("ABRIR", openX + openW / 2, REMINDER_BUTTON_Y + REMINDER_BUTTON_H / 2);

    uiRailEn(g, REMINDER_X + 24, REMINDER_W - 48,
             REMINDER_Y + REMINDER_H - 14, 1.0f);
    uiAnimReveal();
}

TodoReminderAction uiTodoReminderTap(int16_t x, int16_t y) {
    if (y < REMINDER_BUTTON_Y || y > REMINDER_BUTTON_Y + REMINDER_BUTTON_H) {
        return TODO_REMINDER_NONE;
    }
    if (x >= 36 && x <= 174) return TODO_REMINDER_COMPLETE;
    if (x >= 176 && x <= 302) return TODO_REMINDER_SNOOZE;
    if (x >= 304 && x <= 444) return TODO_REMINDER_OPEN;
    return TODO_REMINDER_NONE;
}
