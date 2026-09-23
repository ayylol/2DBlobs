#include "rawdraw.h"
#include "linalg.h"
#include <assert.h>
#include <bits/time.h>
#include <stdint.h>
#include <ncurses.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <math.h>

// Canvas
//#define PIXEL_WIDTH 152*2
// TODO: Allocate a max size, and dynamically use the pixels that are visible
#define PIXEL_WIDTH 220*2
#define PIXEL_HEIGHT 46*4
uint32_t g_pixel_buffer[PIXEL_WIDTH*PIXEL_HEIGHT];
canvas_t g_canvas = {g_pixel_buffer, PIXEL_WIDTH, PIXEL_HEIGHT};
float scalar_field[PIXEL_WIDTH*PIXEL_HEIGHT];

// Controls
int32_t g_mouse_x;
int32_t g_mouse_y;
uint64_t g_mouse_bstate;

void init_ncurses();
void init_color_rgb(int32_t id, uint32_t rgb);
void ncurses_destroy();
void drain_events();
void draw_frame(canvas_t canvas);
void ncurses_present(canvas_t canvas);
void save_ppm(uint32_t *buffer, uint32_t width, uint32_t height);
void load_ppm(uint32_t *buffer, const char* path);
struct timespec timespec_sub(struct timespec start, struct timespec end);
void printw_timespec(struct timespec time);

vec2_t blob_pos = {0.f,0.1f};
#define BLOB_COUNT 100
vec2_t blob_positions[100] = { };
void add_blob(
    float *scalar_field, int32_t w, int32_t h,
    vec2_t blob_pos,
    float blob_strength,
    float blob_support
    ){
  int32_t blob_support_grid = (blob_support*1.25*h)/2;
  ivec2_t blob_grid_pos = to_screen(blob_pos, (ivec2_t){w,h});
  for (int32_t y_offset=-blob_support_grid; y_offset<blob_support_grid; y_offset++){
    for (int32_t x_offset=-blob_support_grid; x_offset<blob_support_grid; x_offset++){
      int32_t x = blob_grid_pos.x+x_offset;
      int32_t y = blob_grid_pos.y+y_offset;
      if (x<0 || y<0 || x>=w || y>=h) continue;

      // Get position of slot
      ivec2_t slot = {.x = x, .y = y};
      vec2_t slot_world_pos = to_world(slot, (ivec2_t){.x = w, .y = h});

      // Evaluate implicit
      vec2_t blob_to_slot = sub_vec2(slot_world_pos, blob_pos);
      float d2 = dot_vec2(blob_to_slot, blob_to_slot);
      if (d2>blob_support*blob_support) continue;
      // TODO: The actual calculation
      float d = sqrt(d2);
      float contribution;
      if (d<blob_support/3.f){
        contribution=1.f-3.f*pow((d/blob_support),2);
      } else{
        contribution=(3.f/2.f)*pow((1-(d/blob_support)),2);
      }
      contribution*=blob_strength;

      // Add to field
      // TODO: THIS IS SOOOO MESSSYYYYY CHANGE THIS SOMEHOW??
      scalar_field[rawdraw_get_i(g_canvas, x,y)] += contribution;
    }
  }
}
void animate_blobs(
    float *scalar_field, int32_t w, int32_t h
    ){
  for (int i=0; i<w*h; i++){ scalar_field[i]=0.f; }
  for (int i=0; i<BLOB_COUNT; i++){
    vec2_t move = mul_vec2_scalar(sub_vec2((vec2_t){(float)rand()/INT32_MAX, (float)rand()/INT32_MAX}, (vec2_t){0.5f,0.5f}), 2.f*0.05f);
    blob_positions[i] = add_vec2(blob_positions[i], move);
    vec2_t lower_bound = to_world((ivec2_t){0,h}, (ivec2_t){w,h});
    vec2_t upper_bound = to_world((ivec2_t){w,0}, (ivec2_t){w,h});
    if (blob_positions[i].x < lower_bound.x){ blob_positions[i].x=lower_bound.x; }
    if (blob_positions[i].y < lower_bound.y){ blob_positions[i].y=lower_bound.y; }
    if (blob_positions[i].x > upper_bound.x){ blob_positions[i].x=upper_bound.x; }
    if (blob_positions[i].y > upper_bound.y){ blob_positions[i].y=upper_bound.y; }
    add_blob(scalar_field, g_canvas.w, g_canvas.h, blob_positions[i], 3.0f, 0.3f);
  }
}

int32_t main(int argc, char* argv[]) {
  init_ncurses();
  int32_t count=0;
  struct timespec last_time={};
  struct timespec target_time = { .tv_sec = 0, .tv_nsec=16666666-1000000 };
  while (1) {
    // NOTE: This is kinda janky since i think it doesnt end up waking
    // up exactly when its supposed to. Leaving it for now.
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    struct timespec elapsed = timespec_sub(last_time, curr_time);
    struct timespec sleep_time = timespec_sub(elapsed, target_time);
    nanosleep(&sleep_time, NULL);
    last_time=curr_time;

    drain_events();

    animate_blobs(scalar_field, PIXEL_WIDTH, PIXEL_HEIGHT);

    draw_frame(g_canvas);
    ncurses_present(g_canvas);
    move(0,0);
    printw_timespec(elapsed);
    move(1,0);
    printw("MOUSE: (%d,%d)", g_mouse_x, g_mouse_y);
    move(2,0);
    printw("MOUSE-BUTTON: %d", (g_mouse_bstate & BUTTON1_PRESSED)!=0);
    refresh();
  }
  ncurses_destroy();
}

void init_ncurses(){
  setlocale(LC_CTYPE, "C.utf8");
  //setlocale(LC_ALL, "");
  initscr();
  cbreak();
  keypad(stdscr, true);
  noecho();
  nodelay(stdscr, TRUE);
  keypad(stdscr, TRUE);
  set_escdelay(0);
  curs_set(0);

  // For mouse movement
  // https://gist.github.com/sylt/93d3f7b77e7f3a881603
  mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, NULL);
  printf("\033[?1003h\n");

  fflush(stdout);
  mouseinterval(0);

  start_color();
  for (int32_t i=16; i<COLOR_PALETTE_COUNT; i++){
    init_color_rgb(i, g_color_palette[i]);
  }
  clear();
}

void draw_frame(canvas_t canvas){
  // Draw Canvas
  rawdraw_fill(canvas, g_color_palette[16]);

  // Draw field
  float color_value_high = 5.f;
  float color_value_low = 1.f;
  for (int32_t x=0; x<canvas.w; x++){
    for (int32_t y=0; y<canvas.h; y++){
      color_t output_color;
      float lerp_factor = (scalar_field[rawdraw_get_i(canvas, x, y)]-color_value_low)/(color_value_high-color_value_low);
      if (lerp_factor > 1.0f){
        output_color = 0xFF;
      } else if (lerp_factor < 0.f){
        lerp_factor = scalar_field[rawdraw_get_i(canvas, x, y)]/color_value_low;
        output_color = (int32_t)(lerp_factor*0xFF)<<8*2;
      } else {
        int32_t r_lerp = lerp_factor*0xFF;
        int32_t b_lerp = (1-lerp_factor)*0xFF;
        output_color = r_lerp + (b_lerp<<8*2);
      }
      canvas.buffer[rawdraw_get_i(canvas, x, y)] = output_color;
      // TODO: DO A BETTER LERP FOR COLOR
      
    }
  }
  rawdraw_line(canvas, 0,0, canvas.w-1,0, g_color_palette[15]);
  rawdraw_line(canvas, 0,0, 0,canvas.h-1, g_color_palette[15]);
  rawdraw_line(canvas, 0,canvas.h-1, canvas.w-1,canvas.h-1, g_color_palette[15]);
  rawdraw_line(canvas, canvas.w-1,0, canvas.w-1,canvas.h-1, g_color_palette[15]);
  /*
  for (int32_t i=0; i<5; i++){
    ivec2_t blob_screen = to_screen(blob_positions[i], (ivec2_t){canvas.w, canvas.h});
    rawdraw_point(canvas, blob_screen.x, blob_screen.y, 3, g_color_palette[15]);
  }
  */
}

// TODO: Move this to an ncurses specific file
void ncurses_present(canvas_t canvas){
  int32_t terminal_width=canvas.w/2;
  int32_t terminal_height=canvas.h/4;
  assert(terminal_width*2==canvas.w);
  assert(terminal_height*4==canvas.h);
  for (int32_t x=0; x<terminal_width; x++){
    for (int32_t y=0; y<terminal_height; y++){
      // NOTE: !!! Ordered in the same manner as the unicode braille characters !!!
      uint32_t is_color0 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+0)];
      uint32_t is_color1 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+1)];
      uint32_t is_color2 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+2)];
      uint32_t is_color3 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+0)];
      uint32_t is_color4 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+1)];
      uint32_t is_color5 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+2)];
      uint32_t is_color6 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+3)];
      uint32_t is_color7 = 0 != canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+3)];
      // Get Byte Representing Bit Pattern
      uint8_t bit_pattern =
        is_color0 << 0 | is_color3 << 3 |
        is_color1 << 1 | is_color4 << 4 |
        is_color2 << 2 | is_color5 << 5 |
        is_color6 << 6 | is_color7 << 7;
      // Convert Bit Pattern to Appropriate UTF-8 Encoding
      uint8_t braille_char[4]={};
      braille_char[0] = 0xE2;
      braille_char[1] = 0xA0 | (bit_pattern >> 6);
      braille_char[2] = 0x80 | (bit_pattern & 0x3F);
      // Average Color
      uint32_t color_count = is_color0 + is_color1 + is_color2 + is_color3 + is_color4 + is_color5 + is_color6 + is_color7;
      // Avoid division by zero
      if (color_count == 0) {
        mvaddch(y, x, ' ');
        continue;
      }

      uint32_t color0 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+0)];
      uint32_t color1 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+1)];
      uint32_t color2 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+2)];
      uint32_t color3 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+0)];
      uint32_t color4 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+1)];
      uint32_t color5 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+2)];
      uint32_t color6 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+0, (y*4)+3)];
      uint32_t color7 = canvas.buffer[rawdraw_get_i(canvas, (x*2)+1, (y*4)+3)];

      // FIXME: COLORS ARE WRONG! BUT THERE IS ANOTHER MISTAKE SOMEWHERE ELSE SO IT CANCELS
      // OUT AND DISPLAYS RIGHT!
      uint8_t channel_averages[3] = {
        (rawdraw_channel_blue(color0) + rawdraw_channel_blue(color1) +
         rawdraw_channel_blue(color2) + rawdraw_channel_blue(color3) +
         rawdraw_channel_blue(color4) + rawdraw_channel_blue(color5) +
         rawdraw_channel_blue(color6) + rawdraw_channel_blue(color7)) / color_count,

        (rawdraw_channel_green(color0) + rawdraw_channel_green(color1) +
         rawdraw_channel_green(color2) + rawdraw_channel_green(color3) +
         rawdraw_channel_green(color4) + rawdraw_channel_green(color5) +
         rawdraw_channel_green(color6) + rawdraw_channel_green(color7)) / color_count,

        (rawdraw_channel_red(color0) + rawdraw_channel_red(color1) +
         rawdraw_channel_red(color2) + rawdraw_channel_red(color3) +
         rawdraw_channel_red(color4) + rawdraw_channel_red(color5) +
         rawdraw_channel_red(color6) + rawdraw_channel_red(color7)) / color_count,
      };
      assert(channel_averages[0]<=0xFF);
      assert(channel_averages[1]<=0xFF);
      assert(channel_averages[2]<=0xFF);

      uint8_t channel_indexes[3];
      for (uint32_t i=0; i<3; i++){
        if      (channel_averages[i] < 0x19*1) { channel_indexes[i]=0; } 
        else if (channel_averages[i] < 0x19*3) { channel_indexes[i]=1; }
        else if (channel_averages[i] < 0x19*5) { channel_indexes[i]=2; }
        else if (channel_averages[i] < 0x19*7) { channel_indexes[i]=3; }
        else if (channel_averages[i] < 0x19*9) { channel_indexes[i]=4; }
        else                                   { channel_indexes[i]=5; }
      }
      uint32_t color_index=(channel_indexes[0] + channel_indexes[1]*6 + channel_indexes[2]*6*6)+16;
      
      attron(COLOR_PAIR(color_index));
      mvaddstr(y, x, (const char *)braille_char);
    }
  }
}

void
init_color_rgb(int32_t id, color_t rgb) {
  uint32_t r = (rawdraw_channel_red(rgb) * 1000 ) / 255;
  uint32_t g = (rawdraw_channel_green(rgb) * 1000 ) / 255;
  uint32_t b = (rawdraw_channel_blue(rgb) * 1000 ) / 255;

  init_color(id, r, g, b);
  init_pair(id, id, COLOR_BLACK);
}

void ncurses_destroy(){
  printf("\033[?1003l\n"); // Disable mouse movement events, as l = low
  endwin();
}

void drain_events() {
  int32_t event_key = 0;
  do {
    event_key = getch();
    switch (event_key){
      case 'q':
        ncurses_destroy();
        exit(0);
      case KEY_MOUSE:
        MEVENT event;
        if (getmouse(&event)== OK){
          g_mouse_x=event.x;
          g_mouse_y=event.y;
          g_mouse_bstate=event.bstate;
        }
    }
  } while(event_key != ERR);
  return;
}

struct timespec timespec_sub(struct timespec start, struct timespec end){
  struct timespec temp;
  if ((end.tv_nsec-start.tv_nsec)<0){
    temp.tv_sec=end.tv_sec-start.tv_sec-1;
    temp.tv_nsec=1000000000+end.tv_nsec-start.tv_nsec;
  } else {
    temp.tv_sec=end.tv_sec-start.tv_sec;
    temp.tv_nsec=end.tv_nsec-start.tv_nsec;
  }
  return temp;
}

void printw_timespec(struct timespec time){
  int64_t sec=time.tv_sec;
  int64_t msec=time.tv_nsec/1000000;
  int64_t usec=(time.tv_nsec - msec*1000000)/1000;
  int64_t nsec=time.tv_nsec - usec*1000 - msec*1000000;
  printw("%lds %ldms %ldus %ldns", sec, msec, usec, nsec);
}

void save_ppm(uint32_t *buffer, uint32_t width, uint32_t height){
  printf("P3\n%d %d\n255\n", width, height);
  for (uint32_t i=0; i < width*height; i++){
    uint8_t r=rawdraw_channel_red(buffer[i]);
    uint8_t g=rawdraw_channel_green(buffer[i]);
    uint8_t b=rawdraw_channel_blue(buffer[i]);
    printf("%d %d %d\n", r, g, b); 
  }
}
