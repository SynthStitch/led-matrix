// Self-playing Tetris for the LED frame: the board in the middle, next piece and score at the sides.
// An AI picks each landing spot, then the piece visibly rotates, slides and drops there.
// Usage: tetris [rpi-rgb-led-matrix flags] [visible-height]    Self-check without panels: tetris --selftest
#include "led-matrix.h"
#include "graphics.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <string>
using namespace rgb_matrix;

// Game speed: one tick per TICK_US. Raise it to slow everything down.
static const int TICK_US = 30000;
static const int MOVE_TICKS = 4;    // ticks per rotate or slide while lining up
static const int FALL_TICKS = 10;   // ticks per row of gravity while lining up
static const int DROP_TICKS = 1;    // ticks per row once lined up

static volatile bool stop = false;
static void on_sig(int) { stop = true; }

enum { BW = 10, BH = 20 };
struct RGB { uint8_t r, g, b; };
static const RGB COL[] = {{0, 240, 240}, {240, 240, 0}, {160, 0, 240}, {0, 240, 0}, {240, 0, 0}, {0, 0, 240}, {240, 160, 0}};
static const int SIZE[] = {4, 2, 3, 3, 3, 3, 3};                 // I O T S Z J L, box size for rotation
static const int SHAPE[7][4][2] = {                                // cells in rotation 0, (x, y) inside the box
    {{0, 1}, {1, 1}, {2, 1}, {3, 1}}, {{0, 0}, {1, 0}, {0, 1}, {1, 1}}, {{1, 0}, {0, 1}, {1, 1}, {2, 1}},
    {{1, 0}, {2, 0}, {0, 1}, {1, 1}}, {{0, 0}, {1, 0}, {1, 1}, {2, 1}}, {{0, 0}, {0, 1}, {1, 1}, {2, 1}},
    {{2, 0}, {0, 1}, {1, 1}, {2, 1}}};

typedef int Board[BH][BW];  // 0 empty, else piece type + 1 (8 = game-over fill)
struct Piece { int type, rot, x, y; };

static void cells(const Piece &p, int out[4][2]) {
  for (int i = 0; i < 4; ++i) {
    int x = SHAPE[p.type][i][0], y = SHAPE[p.type][i][1], n = SIZE[p.type];
    for (int r = 0; r < p.rot; ++r) { int t = x; x = n - 1 - y; y = t; }  // rotate clockwise in the box
    out[i][0] = p.x + x; out[i][1] = p.y + y;
  }
}

static bool fits(const Board b, const Piece &p) {
  int c[4][2];
  cells(p, c);
  for (auto &q : c) {
    if (q[0] < 0 || q[0] >= BW || q[1] >= BH) return false;
    if (q[1] >= 0 && b[q[1]][q[0]]) return false;
  }
  return true;
}

static void lock(Board b, const Piece &p) {
  int c[4][2];
  cells(p, c);
  for (auto &q : c) if (q[1] >= 0) b[q[1]][q[0]] = p.type + 1;
}

static bool row_full(const Board b, int y) { for (int x = 0; x < BW; ++x) if (!b[y][x]) return false; return true; }

static int clear_rows(Board b) {
  int cleared = 0;
  for (int y = BH - 1; y >= 0;) {
    if (!row_full(b, y)) { --y; continue; }
    for (int k = y; k > 0; --k) memcpy(b[k], b[k - 1], sizeof b[k]);
    memset(b[0], 0, sizeof b[0]);
    ++cleared;
  }
  return cleared;
}

// Classic hand-tuned weights: favour cleared lines, punish height, holes and a bumpy surface.
static double evaluate(const Board b, int lines) {
  int h[BW], holes = 0, total = 0, bump = 0;
  for (int x = 0; x < BW; ++x) {
    h[x] = 0;
    for (int y = 0; y < BH; ++y) if (b[y][x]) { h[x] = BH - y; break; }
    for (int y = BH - h[x]; y < BH; ++y) if (!b[y][x]) ++holes;
    total += h[x];
    if (x) bump += abs(h[x] - h[x - 1]);
  }
  return -0.51 * total + 0.76 * lines - 0.36 * holes - 0.18 * bump;
}

static Piece spawn(int type) { return {type, 0, 3, -1}; }

// Best (rot, x) for `type` dropped straight down from its spawn row. Returns false if nothing fits.
static bool plan(const Board b, int type, int &best_rot, int &best_x) {
  double best = -1e9;
  bool found = false;
  for (int rot = 0; rot < 4; ++rot)
    for (int x = -2; x < BW; ++x) {
      Piece p = {type, rot, x, -1};
      if (!fits(b, p)) continue;
      while (fits(b, {type, rot, x, p.y + 1})) ++p.y;
      Board t;
      memcpy(t, b, sizeof t);
      lock(t, p);
      double s = evaluate(t, clear_rows(t));
      if (s > best) { best = s; best_rot = rot; best_x = x; found = true; }
    }
  return found;
}

struct Bag {  // 7-bag randomiser, like modern Tetris: every piece once per 7
  int order[7], next = 7;
  int take() {
    if (next == 7) {
      for (int i = 0; i < 7; ++i) order[i] = i;
      for (int i = 6; i > 0; --i) { int j = rand() % (i + 1), t = order[i]; order[i] = order[j]; order[j] = t; }
      next = 0;
    }
    return order[next++];
  }
};

static int selftest() {
  srand(1);
  Board b = {};
  Bag bag;
  int lines = 0, pieces = 0;
  for (; pieces < 1000; ++pieces) {
    int type = bag.take(), rot = 0, x = 0;
    if (!fits(b, spawn(type)) || !plan(b, type, rot, x)) break;
    Piece p = {type, rot, x, -1};
    while (fits(b, {type, rot, x, p.y + 1})) ++p.y;
    lock(b, p);
    lines += clear_rows(b);
  }
  Board t = {};
  for (int x = 0; x < BW; ++x) t[BH - 1][x] = t[BH - 3][x] = 1;
  t[BH - 2][0] = 1;
  if (clear_rows(t) != 2 || t[BH - 1][0] != 1 || t[BH - 1][1] != 0) { printf("FAIL: row clearing\n"); return 1; }
  if (pieces < 1000 || lines < 300) { printf("FAIL: AI topped out after %d pieces, %d lines\n", pieces, lines); return 1; }
  printf("selftest OK: AI placed %d pieces and cleared %d lines; row clearing correct\n", pieces, lines);
  return 0;
}

static void block(Canvas *cv, int x, int y, int cs, RGB c) {
  for (int j = 0; j < cs; ++j)
    for (int i = 0; i < cs; ++i) {
      bool edge = i == cs - 1 || j == cs - 1;  // darker bottom-right edge so blocks read separately
      cv->SetPixel(x + i, y + j, edge ? c.r / 3 : c.r, edge ? c.g / 3 : c.g, edge ? c.b / 3 : c.b);
    }
}

int main(int argc, char **argv) {
  srand(time(NULL));
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
  RGBMatrix::Options opts;
  RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  const int W = m->width(), H = argc > 1 ? atoi(argv[1]) : m->height();
  const int cs = (H - 2) / BH, bx = (W - BW * cs) / 2, by = (H - BH * cs) / 2;
  Font font;
  bool have_font = font.LoadFont("/opt/rpi-rgb-led-matrix/fonts/4x6.bdf");
  const Color label(120, 120, 140), value(255, 255, 255);
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);

  Board b = {};
  Bag bag;
  int next = bag.take(), lines = 0, score = 0, target_rot = 0, target_x = 0;
  Piece cur = spawn(bag.take());
  plan(b, cur.type, target_rot, target_x);
  enum { MOVE, DROP, CLEAR, OVER } state = MOVE;
  int timer = 0, over_row = 0;
  FrameCanvas *off = m->CreateFrameCanvas();

  for (int tick = 0; !stop; ++tick) {
    ++timer;
    if (state == MOVE) {  // line up with the planned spot, one rotate or slide at a time
      if (timer % MOVE_TICKS == 0) {
        Piece t = cur;
        if (cur.rot != target_rot) t.rot = (t.rot + 1) % 4;
        else if (cur.x != target_x) t.x += cur.x < target_x ? 1 : -1;
        if (t.rot == cur.rot && t.x == cur.x) state = DROP;
        else if (fits(b, t)) cur = t;
        else state = DROP;  // blocked on the way: drop where it is
      }
      if (state == MOVE && timer % FALL_TICKS == 0 && fits(b, {cur.type, cur.rot, cur.x, cur.y + 1})) ++cur.y;
    } else if (state == DROP && timer % DROP_TICKS == 0) {
      if (fits(b, {cur.type, cur.rot, cur.x, cur.y + 1})) ++cur.y;
      else {
        lock(b, cur);
        bool any = false;
        for (int y = 0; y < BH; ++y) any |= row_full(b, y);
        state = any ? CLEAR : MOVE;
        timer = 0;
        if (!any) {
          cur = spawn(next);
          next = bag.take();
          if (!fits(b, cur) || !plan(b, cur.type, target_rot, target_x)) { state = OVER; over_row = BH; }
        }
      }
    } else if (state == CLEAR && timer >= 18) {  // full rows flash, then fall away
      int n = clear_rows(b);
      lines += n;
      score += n == 1 ? 40 : n == 2 ? 100 : n == 3 ? 300 : 1200;
      cur = spawn(next);
      next = bag.take();
      timer = 0;
      state = MOVE;
      if (!fits(b, cur) || !plan(b, cur.type, target_rot, target_x)) { state = OVER; over_row = BH; }
    } else if (state == OVER && timer % 2 == 0) {  // fill the board from the bottom, then start over
      if (over_row > 0) { --over_row; for (int x = 0; x < BW; ++x) b[over_row][x] = 8; }
      else if (timer > 2 * BH + 40) {
        memset(b, 0, sizeof b);
        lines = score = 0;
        cur = spawn(bag.take());
        next = bag.take();
        plan(b, cur.type, target_rot, target_x);
        state = MOVE;
        timer = 0;
      }
    }

    off->Clear();
    for (int i = -1; i <= BW * cs; ++i) {  // well outline
      off->SetPixel(bx + i, by - 1, 70, 70, 90);
      off->SetPixel(bx + i, by + BH * cs, 70, 70, 90);
    }
    for (int j = -1; j <= BH * cs; ++j) {
      off->SetPixel(bx - 1, by + j, 70, 70, 90);
      off->SetPixel(bx + BW * cs, by + j, 70, 70, 90);
    }
    for (int y = 0; y < BH; ++y) {
      bool flash = state == CLEAR && row_full(b, y) && (timer / 3) % 2 == 0;
      for (int x = 0; x < BW; ++x) {
        if (!b[y][x]) continue;
        RGB c = flash ? RGB{255, 255, 255} : b[y][x] == 8 ? RGB{90, 90, 90} : COL[b[y][x] - 1];
        block(off, bx + x * cs, by + y * cs, cs, c);
      }
    }
    if (state == MOVE || state == DROP) {
      int c[4][2];
      cells(cur, c);
      for (auto &q : c) if (q[1] >= 0) block(off, bx + q[0] * cs, by + q[1] * cs, cs, COL[cur.type]);
    }
    const int lx = bx - 4 * cs - 6, rx = bx + BW * cs + 4;  // side columns
    if (have_font) {
      DrawText(off, font, lx, 7, label, "NEXT");
      DrawText(off, font, rx, 7, label, "LINES");
      DrawText(off, font, rx, 14, value, std::to_string(lines).c_str());
      DrawText(off, font, rx, 25, label, "SCORE");
      DrawText(off, font, rx, 32, value, std::to_string(score).c_str());
    }
    int c[4][2];
    cells({next, 0, 0, 0}, c);
    for (auto &q : c) block(off, lx + q[0] * cs, 11 + q[1] * cs, cs, COL[next]);

    off = m->SwapOnVSync(off);
    usleep(TICK_US);
  }
  delete m;
  return 0;
}
