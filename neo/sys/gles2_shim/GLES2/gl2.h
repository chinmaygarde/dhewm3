/*
 * Shim for <GLES2/gl2.h> on platforms that don't have system GLES2 headers
 * (e.g., macOS desktop). Redirects to SDL's bundled GLES2 headers.
 */
/* Use SDL's own bundled GLES2 type definitions.
 * SDL_opengles2.h falls back to system <GLES2/*.h> on non-MSVC/non-iOS
 * platforms; SDL_USE_BUILTIN_OPENGL_DEFINITIONS forces it to use its own
 * copies (SDL_opengles2_khrplatform.h, SDL_opengles2_gl2platform.h, etc.)
 * which are always available regardless of system GLES2 install status. */
#ifndef SDL_USE_BUILTIN_OPENGL_DEFINITIONS
#  define SDL_USE_BUILTIN_OPENGL_DEFINITIONS 1
#endif
#ifdef D3_SDL3
#  include <SDL3/SDL_opengles2.h>
#else
#  include <SDL_opengles2.h>
#endif
