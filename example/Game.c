/*
MIT License

Copyright (c) 2026 Christian Luppi

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// #define GMT_DISABLE
#include <GameTest.h>

#include <GLFW/glfw3.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define GRID_W    20
#define GRID_H    20
#define CELL_SIZE 30
#define WIN_W     (GRID_W * CELL_SIZE)
#define WIN_H     (GRID_H * CELL_SIZE)
#define MAX_SNAKE (GRID_W * GRID_H)

typedef struct {
  int x, y;
} Vec2;

typedef enum {
  DIR_UP,
  DIR_DOWN,
  DIR_LEFT,
  DIR_RIGHT
} Direction;

static struct {
  Vec2 body[MAX_SNAKE], food;
  int length, game_over, input_count;
  Direction dir, input_queue[2];
  double tick_timer, tick_rate;
} G;

static void place_food(void) {
  for (;;) {
    int x = rand() % GRID_W, y = rand() % GRID_H, ok = 1;
    for (int i = 0; i < G.length; i++)
      if (G.body[i].x == x && G.body[i].y == y) {
        ok = 0;
        break;
      }
    if (ok) {
      G.food.x = x;
      G.food.y = y;
      return;
    }
  }
}

static void queue_dir(Direction d) {
  Direction cur = G.input_count > 0 ? G.input_queue[G.input_count - 1] : G.dir;
  if ((cur == DIR_UP && d == DIR_DOWN) || (cur == DIR_DOWN && d == DIR_UP) ||
      (cur == DIR_LEFT && d == DIR_RIGHT) || (cur == DIR_RIGHT && d == DIR_LEFT) || cur == d)
    return;
  if (G.input_count < 2) G.input_queue[G.input_count++] = d;
}

static void game_init(void) {
  memset(&G, 0, sizeof(G));
  G.length = 3;
  G.dir = DIR_RIGHT;
  G.tick_rate = 0.12;
  for (int i = 0; i < G.length; i++) {
    G.body[i].x = GRID_W / 2 - i;
    G.body[i].y = GRID_H / 2;
  }
  place_food();
}

static void game_step(void) {
  if (G.game_over) return;
  if (G.input_count > 0) {
    G.dir = G.input_queue[0];
    G.input_queue[0] = G.input_queue[1];
    G.input_count--;
  }
  Vec2 head = G.body[0];
  switch (G.dir) {
    case DIR_UP:    head.y++; break;
    case DIR_DOWN:  head.y--; break;
    case DIR_LEFT:  head.x--; break;
    case DIR_RIGHT: head.x++; break;
  }
  if (head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H) {
    G.game_over = 1;
    return;
  }
  for (int i = 0; i < G.length; i++)
    if (G.body[i].x == head.x && G.body[i].y == head.y) {
      G.game_over = 1;
      return;
    }
  int ate = (head.x == G.food.x && head.y == G.food.y);
  for (int i = ate ? G.length : G.length - 1; i > 0; i--) G.body[i] = G.body[i - 1];
  if (ate) G.length++;
  G.body[0] = head;
  if (ate) place_food();
}

static void draw_quad(float x, float y, float w, float h, float r, float g, float b) {
  glColor3f(r, g, b);
  glBegin(GL_QUADS);
  glVertex2f(x, y);
  glVertex2f(x + w, y);
  glVertex2f(x + w, y + h);
  glVertex2f(x, y + h);
  glEnd();
}

static void render(void) {
  glViewport(0, 0, WIN_W, WIN_H);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(-0.002, 1.002, -0.002, 1.002, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  float cw = 1.0f / GRID_W, ch = 1.0f / GRID_H, pad = 0.1f;

  glColor3f(0.15f, 0.15f, 0.15f);
  glBegin(GL_LINES);
  for (int i = 0; i <= GRID_W; i++) {
    float x = (float)i / GRID_W;
    glVertex2f(x, 0);
    glVertex2f(x, 1);
  }
  for (int i = 0; i <= GRID_H; i++) {
    float y = (float)i / GRID_H;
    glVertex2f(0, y);
    glVertex2f(1, y);
  }
  glEnd();

  draw_quad(G.food.x * cw + cw * pad, G.food.y * ch + ch * pad, cw * (1 - 2 * pad), ch * (1 - 2 * pad), 0.9f, 0.2f, 0.2f);
  for (int i = 0; i < G.length; i++) {
    float b = i == 0 ? 1.0f : 0.7f;
    draw_quad(G.body[i].x * cw + cw * pad, G.body[i].y * ch + ch * pad, cw * (1 - 2 * pad), ch * (1 - 2 * pad), 0.2f * b, 0.8f * b, 0.2f * b);
  }

  if (G.game_over) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0, 0, 0, 0.6f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0);
    glVertex2f(1, 0);
    glVertex2f(1, 1);
    glVertex2f(0, 1);
    glEnd();
    glDisable(GL_BLEND);
  }
}

static void key_callback(GLFWwindow* win, int key, int scancode, int action, int mods) {
  if (action != GLFW_PRESS) return;
  if (G.game_over && key == GLFW_KEY_R) {
    game_init();
    return;
  }
  switch (key) {
    case GLFW_KEY_UP:
    case GLFW_KEY_W:      queue_dir(DIR_UP); break;
    case GLFW_KEY_DOWN:
    case GLFW_KEY_S:      queue_dir(DIR_DOWN); break;
    case GLFW_KEY_LEFT:
    case GLFW_KEY_A:      queue_dir(DIR_LEFT); break;
    case GLFW_KEY_RIGHT:
    case GLFW_KEY_D:      queue_dir(DIR_RIGHT); break;
    case GLFW_KEY_ESCAPE: glfwSetWindowShouldClose(win, GLFW_TRUE); break;
  }
}

int main(int argc, char** argv) {
#ifdef GMT_DISABLE
  srand((unsigned int)time(NULL));
#else
  srand(0);  // Fixed seed for deterministic behavior during tests
#endif

  {
    // Init GameTest with command-line args.

    char test_name[256] = {0};
    GMT_Mode test_mode = GMT_Mode_DISABLED;
    GMT_ParseTestMode(argv, argc, &test_mode);
    GMT_ParseTestFilePath(argv, argc, test_name, sizeof(test_name));

    GMT_Setup setup = {
        .mode = test_mode,
        .test_path = test_name,
        .fail_assertion_trigger_count = 1,
    };

    if (!GMT_Init(&setup)) {
      fprintf(stderr, "Failed to initialize GameTest\n");
    }
  }

  GLFWwindow* win = NULL;
  {
    // Init GLFW and create window.

    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    win = glfwCreateWindow(WIN_W, WIN_H, "Snake", NULL, NULL);
    if (!win) {
      glfwTerminate();
      return 1;
    }

    const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
    if (mode)
      glfwSetWindowPos(win, (mode->width - WIN_W) / 2, (mode->height - WIN_H) / 2);

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);
    glfwSetKeyCallback(win, key_callback);
  }

  // Init game
  game_init();
  GMT_SyncSignalString("Init");

  // Main loop
  {
    double prev = glfwGetTime();
    while (!glfwWindowShouldClose(win)) {
      // Update GameTest
      GMT_Update();

      // Update and render the game
      glfwPollEvents();
      double now = glfwGetTime(), dt = now - prev;
      prev = now;
      for (G.tick_timer += dt; G.tick_timer >= G.tick_rate; G.tick_timer -= G.tick_rate)
        game_step();
      if (G.game_over) glfwSetWindowShouldClose(win, GLFW_TRUE);
      render();
      glfwSwapBuffers(win);
    }
  }

  {
    // Quit
    glfwDestroyWindow(win);
    glfwTerminate();
    GMT_Quit();
  }

  return 0;
}