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

#include <immintrin.h>

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

#define START_TIMER(NAME)                         \
  struct timespec before_ ## NAME;                \
  clock_gettime(CLOCK_REALTIME, &before_ ## NAME) \

#define END_TIMER(NAME, OUT_TIMER)                          \
  struct timespec after_ ## NAME;                           \
  clock_gettime(CLOCK_REALTIME, &after_ ## NAME);           \
  OUT_TIMER = timespec_sub(before_ ## NAME, after_ ## NAME) \

#define TIME(NAME, OUT_TIMER, ...)  \
  START_TIMER(NAME);                \
  __VA_ARGS__                       \
  END_TIMER(NAME, OUT_TIMER);       \



struct timespec timespec_sub(struct timespec start, struct timespec end);
void printw_timespec(struct timespec time);
struct timespec timers[10] = {};

vec2_t blob_pos = {0.f,0.1f};
#define BLOB_COUNT 1000
vec2_t blob_positions[BLOB_COUNT] = { };

void add_blob(
    float *scalar_field, int32_t w, int32_t h,
    vec2_t blob_pos,
    float blob_strength,
    float blob_support
    ){
  int32_t blob_support_grid = (blob_support*1.25*h)/2;
  ivec2_t blob_grid_pos = to_screen(blob_pos, (ivec2_t){w,h});
  float slot_size = 2.f/h;
  for (int32_t y_offset=-blob_support_grid; y_offset<blob_support_grid; y_offset++){
    // Precaclculate values related to y
    int32_t y = blob_grid_pos.y+y_offset;
    if (y<0 || y>=h) continue;
    float y_world = 2*(1-y/(float)h)-1;
    float y_dot_component = (blob_pos.y - y_world)*(blob_pos.y - y_world);
    // TODO: SIMD THIS THING!
    for (int32_t x_base_offset=-blob_support_grid; x_base_offset<blob_support_grid; x_base_offset+=16){
      // NOTE: USING AVX512!!
      int32_t x_base = blob_grid_pos.x+x_base_offset;
      float x_base_world = ((2*(x_base-((w-h)/2.f)))/(float)h)-1;
      // Calculate X offsets
      // x_pos = x_base_world + offset*slot_size
      __m512 offsets_ = _mm512_set_ps( 15.0, 14.0, 13.0, 12.0, 11.0, 10.0, 9.0, 8.0, 7.0, 6.0, 5.0, 4.0, 3.0, 2.0, 1.0, 0.0 );
      __m512 offsets = _mm512_mul_ps(_mm512_set1_ps(slot_size), offsets_);
      __m512 x_positions = _mm512_add_ps(offsets, _mm512_set1_ps(x_base_world));

      // Calculate distance
      __m512 x_diffs = _mm512_sub_ps(_mm512_set1_ps(blob_pos.x), x_positions);
      __m512 x_dot_components = _mm512_mul_ps(x_diffs, x_diffs);
      __m512 dots = _mm512_add_ps(x_dot_components, _mm512_set1_ps(y_dot_component));

      __m512 distance = _mm512_sqrt_ps(dots);
      
      __m512 distance_over_support = _mm512_div_ps(distance, _mm512_set1_ps(blob_support));
      // Calculate result if r < support/3: 1-3(distance/support)^2
      __m512 value_less_than_a_third =
        _mm512_sub_ps(_mm512_set1_ps(1.f),
            _mm512_mul_ps(_mm512_set1_ps(3.f),
              _mm512_mul_ps(distance_over_support, distance_over_support)
            )
          );
      
      // Calculate result if r > support/3: 1.5 * (1 - distance/support)^2
      __m512 one_minus_distance_over_support = _mm512_sub_ps(_mm512_set1_ps(1.f), distance_over_support);
      __m512 value_more_than_a_third =
        _mm512_mul_ps(_mm512_set1_ps(1.5f), _mm512_mul_ps(one_minus_distance_over_support, one_minus_distance_over_support));
      
      // Default to r >= support in register
      __m512 contributions = _mm512_setzero_ps();

      // blend in r < support
      __mmask16 mask_lt_support = _mm512_cmplt_ps_mask(distance, _mm512_set1_ps(blob_support));
      contributions = _mm512_mask_blend_ps(mask_lt_support, contributions, value_more_than_a_third);

      // blend in r < support/3
      __mmask16 mask_lt_support_over_three = _mm512_cmplt_ps_mask(distance, _mm512_set1_ps(blob_support/3.f));
      contributions = _mm512_mask_blend_ps(mask_lt_support_over_three, contributions, value_less_than_a_third);
     
      // Multiply result by strength
      contributions = _mm512_mul_ps(contributions, _mm512_set1_ps(blob_strength));

      // Store contribution into scalar field only if mask allows us!
      __m512i grid_offsets = _mm512_set_epi32( 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0 );

      __m512i x_bases = _mm512_set1_epi32(x_base);
      __m512i x_grid_positions = _mm512_add_epi32(grid_offsets, x_bases);
      __mmask16 mask_in_grid = _mm512_kand(
          _mm512_cmpge_epi32_mask(x_grid_positions, _mm512_set1_epi32(0)),
          _mm512_cmplt_epi32_mask(x_grid_positions, _mm512_set1_epi32(w))
        );

      __m512 value_in_field = _mm512_maskz_loadu_ps(mask_in_grid, &scalar_field[rawdraw_get_i(w,x_base,y)]);
      value_in_field = _mm512_add_ps(contributions, value_in_field);
      _mm512_mask_storeu_ps(&scalar_field[rawdraw_get_i(w,x_base,y)], mask_in_grid, value_in_field);
    }
  }
}


void animate_blobs(
    float *scalar_field, int32_t w, int32_t h
    ){
  const float blob_speed = 0.01f;
  for (int i=0; i<w*h; i++){ scalar_field[i]=0.f; }
  for (int i=0; i<BLOB_COUNT; i++){
    vec2_t move = mul_vec2_scalar(sub_vec2((vec2_t){(float)rand()/INT32_MAX, (float)rand()/INT32_MAX}, (vec2_t){0.5f,0.5f}), 2.f*blob_speed);
    blob_positions[i] = add_vec2(blob_positions[i], move);
    vec2_t lower_bound = to_world((ivec2_t){0,h}, (ivec2_t){w,h});
    vec2_t upper_bound = to_world((ivec2_t){w,0}, (ivec2_t){w,h});
    if (blob_positions[i].x < lower_bound.x){ blob_positions[i].x=lower_bound.x; }
    if (blob_positions[i].y < lower_bound.y){ blob_positions[i].y=lower_bound.y; }
    if (blob_positions[i].x > upper_bound.x){ blob_positions[i].x=upper_bound.x; }
    if (blob_positions[i].y > upper_bound.y){ blob_positions[i].y=upper_bound.y; }
    add_blob(scalar_field, g_canvas.w, g_canvas.h, blob_positions[i], 1.0f, 0.5f);
  }
}

int32_t main(int argc, char* argv[]) {

  // TODO: CHECK IF THE SIMD INTRINSICS ARE AVAILABLE!
  
  init_ncurses();
  int32_t count=0;
  struct timespec last_time={};
  struct timespec target_time = { .tv_sec = 0, .tv_nsec=16666666};
  //struct timespec target_time = { .tv_sec = 0, .tv_nsec=33333333};
  while (1) {
    // TODO: BETTER FRAME TIMING SO THAT IT IS CONSISTENTLY 16ms!!!!
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);
    struct timespec elapsed = timespec_sub(last_time, curr_time);
    struct timespec sleep_time = timespec_sub(elapsed, target_time);
    nanosleep(&sleep_time, NULL);
    last_time=curr_time;

    drain_events();

    TIME(animate_blob, timers[0],
        animate_blobs(scalar_field, g_canvas.w, g_canvas.h);
    );
    TIME(draw, timers[1],
        draw_frame(g_canvas);
    );
    TIME(present, timers[2],
        ncurses_present(g_canvas); 
    );

    move(0,0);
    printw_timespec(elapsed);
    move(1,0);
    printw("MOUSE: (%d,%d)", g_mouse_x, g_mouse_y);
    move(2,0);
    printw("MOUSE-BUTTON: %d", (g_mouse_bstate & BUTTON1_PRESSED)!=0);
    move(3,0);
    printw("ANIMATE BLOB TIMER:");
    move(3,22);
    printw_timespec(timers[0]);
    move(4,0);
    printw("DRAW FRAME TIMER:");
    move(4,22);
    printw_timespec(timers[1]);
    move(5,0);
    printw("PRESENT FRAME TIMER:");
    move(5,22);
    printw_timespec(timers[2]);
    refresh();
    count++;
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
  float color_value_high = 20.f;
  float color_value_low = 1.f;
  
  // NOTE: Needed so that the blobs don't bleed into the border.
  // TODO: Just define left, right, top and bottom walls seperately. Will make this a lot simpler.
  const int left_right_wall=2;
  const int top_bottom_wall=4;
  for (int32_t x=left_right_wall; x<canvas.w-left_right_wall; x++){
    for (int32_t y=top_bottom_wall; y<canvas.h-top_bottom_wall; y++){
      color_t output_color;
      float lerp_factor = (scalar_field[rawdraw_get_i(canvas.w, x, y)]-color_value_low)/(color_value_high-color_value_low);
      if (lerp_factor > 1.0f){
        output_color = 0xFF;
      } else if (lerp_factor < 0.f){
        lerp_factor = scalar_field[rawdraw_get_i(canvas.w, x, y)]/color_value_low;
        output_color = (int32_t)(lerp_factor*0xFF)<<8*2;
      } else {
        int32_t r_lerp = lerp_factor*0xFF;
        int32_t b_lerp = (1-lerp_factor)*0xFF;
        output_color = r_lerp + (b_lerp<<8*2);
      }
      // TODO: DO A BETTER LERP FOR COLOR
      canvas.buffer[rawdraw_get_i(canvas.w, x, y)] = output_color;
    }
  }
  rawdraw_line(canvas, left_right_wall-1,top_bottom_wall-1, canvas.w-left_right_wall+1,top_bottom_wall-1, g_color_palette[15]);
  rawdraw_line(canvas, left_right_wall-1,top_bottom_wall-1, left_right_wall-1,canvas.h-top_bottom_wall+1, g_color_palette[15]);
  rawdraw_line(canvas, left_right_wall-1,canvas.h-top_bottom_wall, canvas.w-left_right_wall+1,canvas.h-top_bottom_wall, g_color_palette[15]);
  rawdraw_line(canvas, canvas.w-left_right_wall,top_bottom_wall-1, canvas.w-left_right_wall,canvas.h-top_bottom_wall, g_color_palette[15]);
  /*
  for (int32_t i=0; i<BLOB_COUNT; i++){
    ivec2_t blob_screen = to_screen(blob_positions[i], (ivec2_t){canvas.w, canvas.h});
    rawdraw_point(canvas, blob_screen.x, blob_screen.y, 5, g_color_palette[15]);
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
      uint32_t is_color0 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+0)];
      uint32_t is_color1 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+1)];
      uint32_t is_color2 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+2)];
      uint32_t is_color3 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+0)];
      uint32_t is_color4 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+1)];
      uint32_t is_color5 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+2)];
      uint32_t is_color6 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+3)];
      uint32_t is_color7 = 0 != canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+3)];
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

      uint32_t color0 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+0)];
      uint32_t color1 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+1)];
      uint32_t color2 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+2)];
      uint32_t color3 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+0)];
      uint32_t color4 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+1)];
      uint32_t color5 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+2)];
      uint32_t color6 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+0, (y*4)+3)];
      uint32_t color7 = canvas.buffer[rawdraw_get_i(canvas.w, (x*2)+1, (y*4)+3)];

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
