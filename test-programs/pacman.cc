// Pac-Man for the LED frame: a random maze full of dots, Pac-Man facing where he goes, four ghosts, power pellets.
// Usage: pacman [rpi-rgb-led-matrix flags] [visible-height]    Self-check without panels: pacman --selftest
// visible-height: rows actually on panels, when the canvas is taller (e.g. an empty port 1). Default: canvas height.
#include "led-matrix.h"
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <queue>
#include <vector>
using namespace rgb_matrix;

static volatile bool stop = false;
static void on_sig(int) { stop = true; }

struct RGB { uint8_t r, g, b; };
static const int DX[] = {0, 1, 0, -1}, DY[] = {-1, 0, 1, 0}, BIT[] = {1, 2, 4, 8};  // up, right, down, left
static int CW, CH, OX, OY;              // maze size in 8-pixel cells, and its pixel offset
static std::vector<int> walls;          // per cell: wall bits
static std::vector<char> dots;          // 0 eaten, 1 dot, 2 power pellet

static int cell(int x, int y) { return y * CW + x; }
static bool inside(int x, int y) { return x >= 0 && y >= 0 && x < CW && y < CH; }
static bool open_(int c, int d) { return !(walls[c] & BIT[d]); }
static int next(int c, int d) { return cell(c % CW + DX[d], c / CW + DY[d]); }
static void carve(int c, int d) { walls[c] &= ~BIT[d]; walls[next(c, d)] &= ~BIT[(d + 2) % 4]; }

static void make_maze() {
  walls.assign(CW * CH, 15);
  std::vector<char> seen(CW * CH, 0);
  std::vector<int> stack = {0};
  seen[0] = 1;
  while (!stack.empty()) {  // recursive backtracker: every cell reachable
    int c = stack.back(), opts[4], n = 0;
    for (int d = 0; d < 4; ++d)
      if (inside(c % CW + DX[d], c / CW + DY[d]) && !seen[next(c, d)]) opts[n++] = d;
    if (!n) { stack.pop_back(); continue; }
    int d = opts[rand() % n];
    carve(c, d);
    seen[next(c, d)] = 1;
    stack.push_back(next(c, d));
  }
  for (int c = 0; c < CW * CH; ++c) {  // braid: knock out every dead end, plus some extra loops, like a real board
    int opens = 0, opts[4], n = 0;
    for (int d = 0; d < 4; ++d) {
      if (open_(c, d)) ++opens;
      else if (inside(c % CW + DX[d], c / CW + DY[d])) opts[n++] = d;
    }
    if (n && (opens == 1 || rand() % 6 == 0)) carve(c, opts[rand() % n]);
  }
  dots.assign(CW * CH, 1);
  dots[cell(0, 0)] = dots[cell(CW - 1, 0)] = dots[cell(0, CH - 1)] = dots[cell(CW - 1, CH - 1)] = 2;
}

// First step from `from` toward the nearest cell where goal(c) holds, never entering `blocked` cells. -1 if none.
template <class F> static int first_step(int from, F goal, const std::vector<char> &blocked) {
  std::vector<int> first(CW * CH, -2);
  std::queue<int> q;
  first[from] = -1;
  q.push(from);
  while (!q.empty()) {
    int c = q.front(); q.pop();
    if (c != from && goal(c)) return first[c];
    for (int d = 0; d < 4; ++d) {
      if (!open_(c, d)) continue;
      int n = next(c, d);
      if (first[n] != -2 || blocked[n]) continue;
      first[n] = c == from ? d : first[c];
      q.push(n);
    }
  }
  return -1;
}

struct Actor { int x, y, dir, home; RGB col; int scared; bool eyes; };
static Actor pac;
static std::vector<Actor> ghosts;

static void place(Actor &a, int c) { a.x = OX + (c % CW) * 8 + 1; a.y = OY + (c / CW) * 8 + 1; }
static bool centred(const Actor &a) { return (a.x - OX - 1) % 8 == 0 && (a.y - OY - 1) % 8 == 0; }
static int cell_of(const Actor &a) { return cell((a.x - OX - 1 + 4) / 8, (a.y - OY - 1 + 4) / 8); }
static int any_open(int c) { int opts[4], n = 0; for (int d = 0; d < 4; ++d) if (open_(c, d)) opts[n++] = d; return opts[rand() % n]; }
static void step(Actor &a) { if (centred(a) && !open_(cell_of(a), a.dir)) return; a.x += DX[a.dir]; a.y += DY[a.dir]; }

static void reset_positions() {
  place(pac, cell(CW / 2, CH - 1));
  pac.dir = any_open(cell_of(pac));
  const int gx[] = {CW / 2 - 1, CW / 2, CW / 2 + 1, CW / 2}, gy[] = {CH / 2, CH / 2, CH / 2, CH / 2 - 1};
  for (int i = 0; i < 4; ++i) {
    ghosts[i].home = cell(gx[i], gy[i]);
    place(ghosts[i], ghosts[i].home);
    ghosts[i].dir = any_open(ghosts[i].home);
    ghosts[i].scared = 0;
    ghosts[i].eyes = false;
  }
}

static void pac_decide() {
  int c = cell_of(pac);
  if (dots[c] == 2) for (auto &g : ghosts) if (!g.eyes) { g.scared = 400; g.dir = (g.dir + 2) % 4; }
  dots[c] = 0;
  std::vector<char> danger(CW * CH, 0), none(CW * CH, 0);
  bool hunting = false;
  for (auto &g : ghosts) {
    if (g.eyes) continue;
    if (g.scared > 60) { hunting = true; continue; }
    int gc = cell_of(g);
    danger[gc] = 1;
    if (open_(gc, g.dir)) danger[next(gc, g.dir)] = 1;
  }
  danger[c] = 0;
  int d = -1;
  if (hunting) d = first_step(c, [&](int t) { for (auto &g : ghosts) if (!g.eyes && g.scared > 60 && cell_of(g) == t) return true; return false; }, danger);
  if (d < 0) d = first_step(c, [&](int t) { return dots[t] != 0; }, danger);
  if (d < 0) d = first_step(c, [&](int t) { return dots[t] != 0; }, none);  // boxed in: take the risk
  pac.dir = d >= 0 ? d : any_open(c);
}

static void ghost_decide(Actor &g) {
  int c = cell_of(g), back = (g.dir + 2) % 4, opts[4], n = 0;
  if (g.eyes && c == g.home) { g.eyes = false; g.scared = 0; }
  for (int d = 0; d < 4; ++d) if (open_(c, d) && d != back) opts[n++] = d;
  if (!n) opts[n++] = back;
  if (g.eyes) {
    std::vector<char> none(CW * CH, 0);
    int d = first_step(c, [&](int t) { return t == g.home; }, none);
    g.dir = d >= 0 ? d : opts[0];
  } else if (g.scared || rand() % 4 == 0) {
    g.dir = opts[rand() % n];
  } else {  // head for Pac-Man's cell
    int pc = cell_of(pac), best = 1 << 30;
    for (int i = 0; i < n; ++i) {
      int t = next(c, opts[i]), dist = abs(t % CW - pc % CW) + abs(t / CW - pc / CW);
      if (dist < best) { best = dist; g.dir = opts[i]; }
    }
  }
}

static void draw_pac(Canvas *cv, int x, int y, int dir, float mouth) {  // mouth: cosine of the half-angle; > 1 closed
  for (int j = 0; j < 7; ++j)
    for (int i = 0; i < 7; ++i) {
      int dx = i - 3, dy = j - 3, r2 = dx * dx + dy * dy;
      if (r2 > 11) continue;
      if (r2 && (dx * DX[dir] + dy * DY[dir]) > mouth * sqrtf(r2)) continue;
      cv->SetPixel(x + i, y + j, 255, 255, 0);
    }
}

static void draw_ghost(Canvas *cv, const Actor &g, int tick) {
  static const char *body[] = {"..###..", ".#####.", "#######", "#######", "#######", "#######", "#.#.#.#", ".#.#.#."};
  bool flash = g.scared && g.scared < 90 && (tick / 8) % 2;
  RGB c = g.scared ? (flash ? RGB{255, 255, 255} : RGB{33, 33, 255}) : g.col;
  if (!g.eyes)
    for (int j = 0; j < 7; ++j) {
      const char *row = body[j == 6 ? 6 + (tick / 6) % 2 : j];
      for (int i = 0; i < 7; ++i) if (row[i] == '#') cv->SetPixel(g.x + i, g.y + j, c.r, c.g, c.b);
    }
  if (g.scared && !g.eyes) {  // frightened face
    cv->SetPixel(g.x + 2, g.y + 3, 255, 184, 151);
    cv->SetPixel(g.x + 4, g.y + 3, 255, 184, 151);
    return;
  }
  int pc = DX[g.dir] > 0, pr = DY[g.dir] > 0;
  for (int e = 1; e <= 4; e += 3)
    for (int j = 2; j <= 3; ++j)
      for (int i = 0; i < 2; ++i) {
        bool pupil = i == pc && j - 2 == pr;
        cv->SetPixel(g.x + e + i, g.y + j, pupil ? 33 : 255, pupil ? 33 : 255, 255);
      }
}

static void draw(Canvas *cv, int tick, RGB wall, float pac_mouth, bool show_pac) {
  for (int c = 0; c < CW * CH; ++c) {
    int x0 = OX + (c % CW) * 8, y0 = OY + (c / CW) * 8;
    for (int k = 0; k <= 8; ++k) {
      if (walls[c] & 1) cv->SetPixel(x0 + k, y0, wall.r, wall.g, wall.b);
      if (walls[c] & 4) cv->SetPixel(x0 + k, y0 + 8, wall.r, wall.g, wall.b);
      if (walls[c] & 8) cv->SetPixel(x0, y0 + k, wall.r, wall.g, wall.b);
      if (walls[c] & 2) cv->SetPixel(x0 + 8, y0 + k, wall.r, wall.g, wall.b);
    }
    if (dots[c] == 1) cv->SetPixel(x0 + 4, y0 + 4, 255, 184, 151);
    if (dots[c] == 2 && (tick / 10) % 2)
      for (int j = 3; j <= 5; ++j) for (int i = 3; i <= 5; ++i) cv->SetPixel(x0 + i, y0 + j, 255, 184, 151);
  }
  for (auto &g : ghosts) draw_ghost(cv, g, tick);
  if (show_pac) draw_pac(cv, pac.x, pac.y, pac.dir, pac_mouth);
}

static int reachable(int from) {
  std::vector<char> none(CW * CH, 0);
  int count = 0;
  first_step(from, [&](int) { ++count; return false; }, none);
  return count;
}

static int selftest() {
  CW = 11; CH = 7;
  for (int k = 0; k < 500; ++k) {
    make_maze();
    if (reachable(0) != CW * CH - 1) { printf("FAIL: unreachable cells\n"); return 1; }
    for (int c = 0; c < CW * CH; ++c) {
      int opens = 0;
      for (int d = 0; d < 4; ++d) opens += open_(c, d);
      for (int d = 0; d < 4; ++d)
        if (open_(c, d) && !inside(c % CW + DX[d], c / CW + DY[d])) { printf("FAIL: hole in outer wall\n"); return 1; }
      if (opens < 2) { printf("FAIL: dead end\n"); return 1; }
    }
  }
  printf("selftest OK: 500 mazes connected, closed and without dead ends\n");
  return 0;
}

int main(int argc, char **argv) {
  srand(time(NULL));
  if (argc > 1 && !strcmp(argv[1], "--selftest")) return selftest();
  RGBMatrix::Options opts;
  RuntimeOptions rt;
  RGBMatrix *m = RGBMatrix::CreateFromFlags(&argc, &argv, &opts, &rt);
  if (!m) return 1;
  const int W = m->width(), H = argc > 1 ? atoi(argv[1]) : m->height();
  CW = (W - 1) / 8; CH = (H - 1) / 8;
  OX = (W - (CW * 8 + 1)) / 2; OY = (H - (CH * 8 + 1)) / 2;
  ghosts = {{0, 0, 0, 0, {255, 0, 0}, 0, false}, {0, 0, 0, 0, {255, 184, 255}, 0, false},
            {0, 0, 0, 0, {0, 255, 255}, 0, false}, {0, 0, 0, 0, {255, 184, 82}, 0, false}};
  make_maze();
  reset_positions();
  signal(SIGTERM, on_sig); signal(SIGINT, on_sig);

  FrameCanvas *off = m->CreateFrameCanvas();
  const float chomp[] = {1.1f, 0.85f, 0.6f, 0.85f};
  int dying = 0, clearing = 0;
  for (int tick = 0; !stop; ++tick) {
    RGB wall = {33, 33, 222};
    float mouth = chomp[(tick / 3) % 4];
    bool show_pac = true;
    if (dying) {  // mouth opens all the way round, then a fresh start on the same board
      mouth = 0.85f - 2.0f * (60 - dying) / 60;
      show_pac = dying > 8;
      if (--dying == 0) reset_positions();
    } else if (clearing) {  // board cleared: walls flash, then a new maze
      if ((clearing / 10) % 2) wall = {255, 255, 255};
      if (--clearing == 0) { make_maze(); reset_positions(); }
    } else {
      if (tick % 5) { if (centred(pac)) pac_decide(); step(pac); }
      for (auto &g : ghosts) {
        bool moves = g.eyes || (g.scared ? tick % 2 == 0 : tick % 4 != 0);
        if (moves) { if (centred(g)) ghost_decide(g); step(g); }
        if (g.scared) --g.scared;
        if (g.eyes || abs(g.x - pac.x) >= 5 || abs(g.y - pac.y) >= 5) continue;
        if (g.scared) { g.eyes = true; g.scared = 0; }
        else dying = 60;
      }
      bool left = false;
      for (char d : dots) left |= d != 0;
      if (!left) clearing = 90;
    }
    off->Clear();
    draw(off, tick, wall, mouth, show_pac);
    off = m->SwapOnVSync(off);
    usleep(15000);
  }
  delete m;
  return 0;
}
