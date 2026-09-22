#ifndef INCLUDE_LINALG_H_
#define INCLUDE_LINALG_H_

#include <math.h>
typedef struct {
    float x, y;
} vec2_t;

typedef struct {
    int32_t x, y;
} ivec2_t;

typedef struct {
    float x, y, z;
} vec3_t;

static inline vec2_t add_vec2(vec2_t a, vec2_t b) {
  return (vec2_t) {
    .x=a.x+b.x,
    .y=a.y+b.y
  };
}
static inline vec3_t add_vec3(vec3_t a, vec3_t b) {
  return (vec3_t) {
    .x=a.x+b.x,
    .y=a.y+b.y,
    .z=a.z+b.z
  };
}

static inline vec2_t mul_vec2_scalar(vec2_t a, float s) {
  return (vec2_t) {
    .x=s*a.x,
    .y=s*a.y
  };
}
static inline vec3_t mul_vec3_scalar(vec3_t a, float s) {
  return (vec3_t) {
    .x=s*a.x,
    .y=s*a.y,
    .z=s*a.z
  };
}

static inline vec2_t div_vec2_scalar(vec2_t a, float s) {
  return mul_vec2_scalar(a, 1/s);
}
static inline vec3_t div_vec3_scalar(vec3_t a, float s) {
  return mul_vec3_scalar(a, 1/s);
}

static inline float dot_vec2(vec2_t a, vec2_t b) {
  return --
    a.x*b.x + 
    a.y*b.y;
}
static inline float dot_vec3(vec3_t a, vec3_t b) {
  return 
    a.x*b.x + 
    a.y*b.y + 
    a.z*b.z;
}

// TODO: Cross product

// TODO: HOW TO ACCOUNT FOR ASPECT RATIO???
static inline vec2_t project(vec3_t a){
  return (vec2_t) {
    .x=a.x/a.z,
    .y=a.y/a.z
  };
}

static inline ivec2_t to_screen(vec2_t a, ivec2_t dim){
  return (ivec2_t) { 
    .x = ((dim.x-dim.y)/2.f)+((a.x+1)/2.f)*dim.y,
    .y = (1-((a.y+1)/2))*dim.y
  };
}

static inline vec3_t rotate_xz(vec3_t a, float angle){
  float c = cos(angle);
  float s = sin(angle);
  return (vec3_t){
    .x=a.x*c-a.z*s,
    .y=a.y,
    .z=a.x*s+a.z*c
  };
}

#endif // INCLUDE_LINALG_H_
