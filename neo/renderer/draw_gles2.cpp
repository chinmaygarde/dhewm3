/*
===========================================================================

Doom 3 GPL Source Code
Copyright (C) 1999-2011 id Software LLC, a ZeniMax Media company.

This file is part of the Doom 3 GPL Source Code ("Doom 3 Source Code").

Doom 3 Source Code is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Doom 3 Source Code is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Doom 3 Source Code.  If not, see <http://www.gnu.org/licenses/>.

In addition, the Doom 3 Source Code is also subject to certain additional terms. You should have received a copy of these additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Doom 3 Source Code.  If not, please request a copy in writing from id Software at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.

===========================================================================
*/

/*
** DRAW_GLES2.CPP
**
** OpenGL ES 2.0 rendering backend — Phase 1 infrastructure stub.
** Full implementation follows in later phases (GLSL shaders, vertex arrays,
** texture adaptation, shadow volumes).
**
** Provides stub definitions for the symbols exported by draw_arb2.cpp so
** the rest of the engine compiles and links under GLES2_BACKEND. None of
** the draw paths below produce visible output yet; that is expected.
*/

#include "sys/platform.h"
#include "renderer/VertexCache.h"

#include "renderer/tr_local.h"

/*
=================
R_ARB2_Init

Stub — replaces draw_arb2.cpp's R_ARB2_Init under GLES2_BACKEND.
The GLES2 context and proc table are already set up by the time this runs
(see RenderSystem_init.cpp). Future phases will compile GLSL programs here.
=================
*/
void R_ARB2_Init( void ) {
	common->Printf( "GLES2 backend: R_ARB2_Init stub\n" );
	glConfig.allowARB2Path = false;
}

/*
=================
RB_ARB2_DrawInteractions

Stub — future phases will implement the GLES2 interaction pass here.
=================
*/
void RB_ARB2_DrawInteractions( void ) {
}

/*
=================
R_FindARBProgram

Stub — returns 0 (no program). Material.cpp calls this while parsing .mtr
files; under GLES2_BACKEND the ARB program slots are unused.
=================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	return 0;
}

/*
=================
R_ReloadARBPrograms_f

Stub — registered as a console command by RenderSystem_init.cpp.
=================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
}
