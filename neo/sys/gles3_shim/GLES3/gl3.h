/*
 * Shim for <GLES3/gl3.h> on platforms that don't have system GLES3 headers
 * (e.g., macOS desktop). Redirects to SDL's bundled GLES2/3 headers.
 */
/* Use SDL's own bundled GLES2/3 type definitions.
 * SDL_opengles2.h exposes ES 3.0 symbols (GL_ES_VERSION_3_0) and is
 * always available. SDL_USE_BUILTIN_OPENGL_DEFINITIONS forces it to use
 * its own copies regardless of system GLES install status. */
#ifndef SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#  define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#endif
#ifdef D3_SDL3
#  include <SDL3/SDL_opengles2.h>
#else
#  include <SDL_opengles2.h>
#endif
