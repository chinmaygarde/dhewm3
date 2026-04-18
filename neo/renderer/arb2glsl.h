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

If you have questions concerning this license or the applicable additional terms, you may contact in writing id Software LLC, c/o ZeniMax Media Inc., Suite 120, Rockville Maryland 20850 USA.

===========================================================================
*/

#ifndef __ARB2GLSL_H__
#define __ARB2GLSL_H__

#ifdef GLES2_BACKEND

#include "idlib/Str.h"

// Translate an ARB assembly program to GLSL ES 1.00.
// arbSrc  : full text of the .vfp (gamma-patched if applicable).
// isFragment: true for !!ARBfp1.0, false for !!ARBvp1.0.
// outGLSL : filled with valid GLSL ES 1.00 source on success.
// Returns false if the input has no recognisable header or no END marker.
bool ARB2GLSL_Translate( const char *arbSrc, bool isFragment, idStr &outGLSL );

#endif /* GLES2_BACKEND */
#endif /* __ARB2GLSL_H__ */
