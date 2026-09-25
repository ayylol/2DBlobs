#include "rawdraw.h"

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>


// PRIVATE FUNCTION IMPLEMENTATIONS
void xor_swap(int *x, int *y)
{
  if (x == y) return;
  *x ^= *y;
  *y ^= *x;
  *x ^= *y;
}

// PUBLIC FUNCTION IMPLEMENTATIONS
void
rawdraw_rect(
    canvas_t canvas,
    int32_t x0, int32_t y0,
    int32_t x1, int32_t y1,
    color_t col)
{
  x1=x1<canvas.w ? x1 : canvas.w-1;
  y1=y1<canvas.h ? y1 : canvas.h-1;
  x0=x0>=0 ? x0 : 0;
  y0=y0>=0 ? y0 : 0;
  for (int32_t x=x0; x<=x1; x++){
    for (int32_t y=y0; y<=y1; y++){
      canvas.buffer[rawdraw_get_i(canvas.w, x, y)]=col;
    }
  }
}

void
rawdraw_fill(canvas_t canvas, color_t col)
{
  rawdraw_rect(canvas, 0, 0, canvas.w-1, canvas.h-1, col);
}

void
rawdraw_point(
    canvas_t canvas,
    int32_t x0, int32_t y0,
    int32_t dim, color_t col) 
{
  x0-=(dim-1)/2;
  y0-=(dim-1)/2;
  int32_t x1 = x0;
  int32_t y1 = y0;
  x1+=(dim-1)/2 + (dim%2 ? 0 : 1);
  y1+=(dim-1)/2 + (dim%2 ? 0 : 1);
  rawdraw_rect(canvas,x0,y0,x1,y1,col);
}

bool is_left_of(
    int32_t x0, int32_t y0,
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2)
{
  int32_t vx0 = -(y1-y0);
  int32_t vy0 = x1-x0;
  int32_t vx1 = x2-x0;
  int32_t vy1 = y2-y0;
  // Take dot product
  int32_t dot = vx0*vx1+vy0*vy1;
  return dot>=0;
}

// Source - https://stackoverflow.com/a/3437484
// Posted by David Titarenco, modified by community. See post 'Timeline' for change history
// Retrieved 2026-05-10, License - CC BY-SA 4.0
 #define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
 #define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })
 #define clamp(a,l,u) \
   min(max(a,l),u)    \

void rawdraw_tri(canvas_t canvas,
    int32_t x0, int32_t y0,
    int32_t x1, int32_t y1,
    int32_t x2, int32_t y2,
    color_t col)
{
#ifdef RAWDRAW_WIREFRAME
  rawdraw_line(canvas, x0, y0, x1, y1, col);
  rawdraw_line(canvas, x1, y1, x2, y2, col);
  rawdraw_line(canvas, x2, y2, x0, y0, col);
#else
  int32_t x_bound_low  = clamp(min(min(x0,x1),x2),0,canvas.w-1);
  int32_t y_bound_low  = clamp(min(min(y0,y1),y2),0,canvas.h-1);
  int32_t x_bound_high = clamp(max(max(x0,x1),x2),0,canvas.w-1);
  int32_t y_bound_high = clamp(max(max(y0,y1),y2),0,canvas.h-1);

  for (int32_t x=x_bound_low; x<x_bound_high; x++){
    for (int32_t y=y_bound_low; y<y_bound_high; y++){
      bool inside = (
          is_left_of(x0,y0, x1,y1, x,y) &&
          is_left_of(x1,y1, x2,y2, x,y) &&
          is_left_of(x2,y2, x0,y0, x,y));
      if (inside){ canvas.buffer[rawdraw_get_i(canvas.w, x, y)]=col; }
    }
  }
#endif
}

// TODO: ADD BOUNDS CHECKING!!!!!!!!!!!
void rawdraw_line(canvas_t canvas,
    int32_t x0, int32_t y0,
    int32_t x1, int32_t y1,
    color_t col)
{
  bool low=true;
  if (abs(y1 - y0) < abs(x1 - x0)){
    if (x0 > x1){ 
      xor_swap(&x0, &x1);
      xor_swap(&y0, &y1);
    }
  }
  else {
    low=false;
    if (y0 > y1){ 
      xor_swap(&x0, &x1);
      xor_swap(&y0, &y1);
    }
  }
  int32_t d_axis1=y1-y0;
  int32_t d_axis2=x1-x0;
  if (!low) {
    int32_t temp=d_axis1;
    d_axis1=d_axis2;
    d_axis2=temp;
  }
  int8_t di=1;
  if (d_axis1<0){
    di=-1;
    d_axis1=-d_axis1;
  }
  int32_t j = low ? y0 : x0;
  int32_t start = low ? x0 : y0;
  int32_t end = low ? x1 : y1;
  int32_t D=2*d_axis1-d_axis2;
  for (int i=start; i<end; i++){
      int32_t x = low ? i : j;
      int32_t y = low ? j : i;
      canvas.buffer[rawdraw_get_i(canvas.w, x, y)] = col;
      if (D>0){
        j=j+di;
        D+=2*(d_axis1-d_axis2);
      } else {
        D+=2*d_axis1;
      }
  }
}

//color_t rawdraw_lerp_color(float lerp_factor, color_t color_a, color_t color_b){
//  
//}

uint32_t g_color_palette[COLOR_PALETTE_COUNT] = {
  // xterm default colors.
  0x000000, // COLOR_BLACK
  0xcd0000, // COLOR_BLUE
  0x00cd00, // COLOR_GREEN
  0xcdcd00, // COLOR_CYAN
  0x0000cd, // COLOR_RED
  0xcd00cd, // COLOR_MAGENTA
  0x00cdcd, // COLOR_YELLOW
  0xe5e5e5, // COLOR_WHITE
  0x4d4d4d, // HI COLOR_BLACK
  0xff0000, // HI COLOR_BLUE
  0x00ff00, // HI COLOR_GREEN
  0xffff00, // HI COLOR_CYAN
  0x0000ff, // HI COLOR_RED
  0xff00ff, // HI COLOR_MAGENTA
  0x00ffff, // HI COLOR_YELLOW
  0xffffff, // HI COLOR_WHITE

  // Netscape palette (ONLY THESE ARE USED!)
  0x000000, 0x000033, 0x000066, 0x000099, 0x0000CC, 0x0000FF,
  0x003300, 0x003333, 0x003366, 0x003399, 0x0033CC, 0x0033FF,
  0x006600, 0x006633, 0x006666, 0x006699, 0x0066CC, 0x0066FF,
  0x009900, 0x009933, 0x009966, 0x009999, 0x0099CC, 0x0099FF,
  0x00CC00, 0x00CC33, 0x00CC66, 0x00CC99, 0x00CCCC, 0x00CCFF,
  0x00FF00, 0x00FF33, 0x00FF66, 0x00FF99, 0x00FFCC, 0x00FFFF,

  0x330000, 0x330033, 0x330066, 0x330099, 0x3300CC, 0x3300FF,
  0x333300, 0x333333, 0x333366, 0x333399, 0x3333CC, 0x3333FF,
  0x336600, 0x336633, 0x336666, 0x336699, 0x3366CC, 0x3366FF,
  0x339900, 0x339933, 0x339966, 0x339999, 0x3399CC, 0x3399FF,
  0x33CC00, 0x33CC33, 0x33CC66, 0x33CC99, 0x33CCCC, 0x33CCFF,
  0x33FF00, 0x33FF33, 0x33FF66, 0x33FF99, 0x33FFCC, 0x33FFFF,

  0x660000, 0x660033, 0x660066, 0x660099, 0x6600CC, 0x6600FF,
  0x663300, 0x663333, 0x663366, 0x663399, 0x6633CC, 0x6633FF,
  0x666600, 0x666633, 0x666666, 0x666699, 0x6666CC, 0x6666FF,
  0x669900, 0x669933, 0x669966, 0x669999, 0x6699CC, 0x6699FF,
  0x66CC00, 0x66CC33, 0x66CC66, 0x66CC99, 0x66CCCC, 0x66CCFF,
  0x66FF00, 0x66FF33, 0x66FF66, 0x66FF99, 0x66FFCC, 0x66FFFF,

  0x990000, 0x990033, 0x990066, 0x990099, 0x9900CC, 0x9900FF,
  0x993300, 0x993333, 0x993366, 0x993399, 0x9933CC, 0x9933FF,
  0x996600, 0x996633, 0x996666, 0x996699, 0x9966CC, 0x9966FF,
  0x999900, 0x999933, 0x999966, 0x999999, 0x9999CC, 0x9999FF,
  0x99CC00, 0x99CC33, 0x99CC66, 0x99CC99, 0x99CCCC, 0x99CCFF,
  0x99FF00, 0x99FF33, 0x99FF66, 0x99FF99, 0x99FFCC, 0x99FFFF,

  0xCC0000, 0xCC0033, 0xCC0066, 0xCC0099, 0xCC00CC, 0xCC00FF,
  0xCC3300, 0xCC3333, 0xCC3366, 0xCC3399, 0xCC33CC, 0xCC33FF,
  0xCC6600, 0xCC6633, 0xCC6666, 0xCC6699, 0xCC66CC, 0xCC66FF,
  0xCC9900, 0xCC9933, 0xCC9966, 0xCC9999, 0xCC99CC, 0xCC99FF,
  0xCCCC00, 0xCCCC33, 0xCCCC66, 0xCCCC99, 0xCCCCCC, 0xCCCCFF,
  0xCCFF00, 0xCCFF33, 0xCCFF66, 0xCCFF99, 0xCCFFCC, 0xCCFFFF,

  0xFF0000, 0xFF0033, 0xFF0066, 0xFF0099, 0xFF00CC, 0xFF00FF,
  0xFF3300, 0xFF3333, 0xFF3366, 0xFF3399, 0xFF33CC, 0xFF33FF,
  0xFF6600, 0xFF6633, 0xFF6666, 0xFF6699, 0xFF66CC, 0xFF66FF,
  0xFF9900, 0xFF9933, 0xFF9966, 0xFF9999, 0xFF99CC, 0xFF99FF,
  0xFFCC00, 0xFFCC33, 0xFFCC66, 0xFFCC99, 0xFFCCCC, 0xFFCCFF,
  0xFFFF00, 0xFFFF33, 0xFFFF66, 0xFFFF99, 0xFFFFCC, 0xFFFFFF,

  // Extra shades of gray
  0x080808, 0x121212, 0x1C1C1C, 0x262626, 0x303030, 0x3A3A3A,
  0x444444, 0x4E4E4E, 0x585858, 0x626262, 0x6C6C6C, 0x767676,
  0x808080, 0x8A8A8A, 0x949494, 0x9E9E9E, 0xA8A8A8, 0xB2B2B2,
  0xBCBCBC, 0xC6C6C6, 0xD0D0D0, 0xDADADA, 0xE4E4E4, 0xEEEEEE,
};
