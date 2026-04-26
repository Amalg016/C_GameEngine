// Compile stb_image as a separate translation unit.
// All other files just #include "stb_image.h" without the IMPLEMENTATION define.
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
