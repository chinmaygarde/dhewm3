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

/*
** DRAW_GLES3.CPP
**
** OpenGL ES 3.0 rendering backend.
**
** Phase 3: runtime ARB→GLSL translator integration.  All .vfp shaders are
** translated at startup and compiled as GLSL ES 3.00 programs.
** Phase 4+ (vertex arrays, draw calls, shadow volumes) is still stubbed.
*/

// Set to 1 to load soft-particle shaders from inline strings rather than
// from the pak filesystem (default: same as draw_arb2.cpp).
#ifndef D3_INTEGRATE_SOFTPART_SHADERS
  #define D3_INTEGRATE_SOFTPART_SHADERS 1
#endif

#include "sys/platform.h"
#include "framework/Common.h"
#include "framework/FileSystem.h"
#include "renderer/VertexCache.h"
#include "renderer/arb2glsl.h"

#include "renderer/tr_local.h"

// ---------------------------------------------------------------------------
// Soft-particle inline shader strings (identical to draw_arb2.cpp)
// ---------------------------------------------------------------------------

#if D3_INTEGRATE_SOFTPART_SHADERS

// DG: the following two shaders are taken from TheDarkMod 2.04 (glprogs/soft_particle.vfp)
// (C) 2005-2016 Broken Glass Studios (The Dark Mod Team) and the individual authors
//     released under a revised BSD license and GPLv3
static const char* softpartVShader = "!!ARBvp1.0  \n"
	"OPTION ARB_position_invariant;  \n"
	"# NOTE: unlike the TDM shader, the following lines use .texcoord and .color  \n"
	"#   instead of .attrib[8] and .attrib[3], to make it work with non-nvidia drivers \n"
	"#   Furthermore, I added support for a texture matrix \n"
	"PARAM defaultTexCoord = { 0, 0.5, 0, 1 }; \n"
	"MOV    result.texcoord, defaultTexCoord; \n"
	"# program.env[12] is PP_DIFFUSE_MATRIX_S, 13 is PP_DIFFUSE_MATRIX_T \n"
	"DP4    result.texcoord.x, vertex.texcoord, program.env[12]; \n"
	"DP4    result.texcoord.y, vertex.texcoord, program.env[13]; \n"
	"MOV    result.color, vertex.color; \n"
	"END \n";

static const char* softpartFShader = "!!ARBfp1.0  \n"
	"# == Fragment Program == \n"
	"# taken from The Dark Mod 2.04, adjusted for dhewm3 \n"
	"# (C) 2005-2016 Broken Glass Studios (The Dark Mod Team) \n"
	"# \n"
	"# Input textures \n"
	"#   texture[0]   particle diffusemap \n"
	"#   texture[1]   _currentDepth \n"
	"# \n"
	"# Constants set by the engine: \n"
	"#   program.env[22] is reciprocal of _currentDepth size. \n"
	"#   program.env[23] is the particle radius, given as { radius, 1/(fadeRange), 1/radius } \n"
	"#   program.env[24] is the color channel mask. \n"
	"# \n"
	"# Hard-coded constants \n"
	"#    depth_consts allows us to recover the original depth in Doom units. \n"
	"# \n"
	"# next line: prevent dhewm3 from injecting gamma in shader code into this shader \n"
	"# nodhewm3gammahack \n"
	"\n"
	"PARAM   depth_consts = { 0.33333333, -0.33316667, 0.0, 0.0 }; \n"
	"PARAM   particle_radius  = program.env[23]; \n"
	"TEMP    tmp, scene_depth, particle_depth, near_fade, fade; \n"
	"\n"
	"MUL   tmp.xy, fragment.position, program.env[22]; \n"
	"TEX   scene_depth, tmp, texture[1], 2D; \n"
	"MIN   scene_depth, scene_depth, 0.9994; \n"
	"\n"
	"MAD   tmp, scene_depth, depth_consts.x, depth_consts.y; \n"
	"RCP   scene_depth, tmp.x; \n"
	"\n"
	"MAD   tmp, fragment.position.z, depth_consts.x, depth_consts.y; \n"
	"RCP   particle_depth, tmp.x; \n"
	"\n"
	"ADD      tmp, -scene_depth, particle_depth; \n"
	"ADD      tmp, tmp, particle_radius.x; \n"
	"MUL_SAT  fade, tmp, particle_radius.y; \n"
	"\n"
	"MUL_SAT  near_fade, particle_depth, -particle_radius.z;  \n"
	"\n"
	"MUL      fade, near_fade, fade; \n"
	"ADD_SAT  fade, fade, program.env[24]; \n"
	"\n"
	"TEMP  oColor; \n"
	"TEX   oColor, fragment.texcoord, texture[0], 2D; \n"
	"MUL   oColor, oColor, fade; \n"
	"MUL   result.color, oColor, fragment.color; \n"
	"\n"
	"END \n";

#endif // D3_INTEGRATE_SOFTPART_SHADERS

// ---------------------------------------------------------------------------
// Program table — mirrors progs[] in draw_arb2.cpp
// ---------------------------------------------------------------------------

typedef struct {
	GLenum      target;   // GL_VERTEX_SHADER or GL_FRAGMENT_SHADER
	int         ident;    // program_t value (VPROG_*, FPROG_*)
	const char *name;     // filename in glprogs/ or NULL for inline shaders
} glesProgramDef_t;

static const int MAX_GLES_PROGS = 200;

static const glesProgramDef_t glesProgDefs[] = {
	{ GL_VERTEX_SHADER,   VPROG_TEST,              "test.vfp" },
	{ GL_FRAGMENT_SHADER, FPROG_TEST,              "test.vfp" },
	{ GL_VERTEX_SHADER,   VPROG_INTERACTION,       "interaction.vfp" },
	{ GL_FRAGMENT_SHADER, FPROG_INTERACTION,       "interaction.vfp" },
	{ GL_VERTEX_SHADER,   VPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_FRAGMENT_SHADER, FPROG_BUMPY_ENVIRONMENT, "bumpyEnvironment.vfp" },
	{ GL_VERTEX_SHADER,   VPROG_AMBIENT,           "ambientLight.vfp" },
	{ GL_FRAGMENT_SHADER, FPROG_AMBIENT,           "ambientLight.vfp" },
	{ GL_VERTEX_SHADER,   VPROG_STENCIL_SHADOW,    "shadow.vp" },
	{ GL_VERTEX_SHADER,   VPROG_ENVIRONMENT,       "environment.vfp" },
	{ GL_FRAGMENT_SHADER, FPROG_ENVIRONMENT,       "environment.vfp" },
	{ GL_VERTEX_SHADER,   VPROG_GLASSWARP,         "arbVP_glasswarp.txt" },
	{ GL_FRAGMENT_SHADER, FPROG_GLASSWARP,         "arbFP_glasswarp.txt" },
	{ GL_VERTEX_SHADER,   VPROG_SOFT_PARTICLE,     NULL }, // inline string
	{ GL_FRAGMENT_SHADER, FPROG_SOFT_PARTICLE,     NULL }, // inline string
};
static const int NUM_GLES_PROG_DEFS =
	(int)( sizeof(glesProgDefs) / sizeof(glesProgDefs[0]) );

// Per-linked-program data — one per vertex-shader entry in glesProgDefs
// (the fragment shader entry of the same file shares the linked program).
struct glesProgram_t {
	GLuint  vert;            // compiled vertex shader object
	GLuint  frag;            // compiled fragment shader object
	GLuint  prog;            // linked GL program object
	GLint   uProgramEnvLoc; // location of uProgramEnv[] array, or -1
	GLint   uMVPLoc;        // location of uMVP matrix, or -1
	GLint   uTexLoc[8];     // locations of uTex0..uTex7, or -1
	bool    valid;
};

// Indexed by program_t ident — keyed on VPROG_* entries (vertex side stores
// the linked program; FPROG_* entries in this array are unused by the draw path).
static glesProgram_t s_glesProgs[MAX_GLES_PROGS];

// CPU-side mirror of program.env[] parameters (uploaded to the current
// program's uProgramEnv uniform at draw time).
static float s_programEnv[32][4];

// Single global VAO; bound once for the session so glVertexAttribPointer is legal.
static GLuint s_vao = 0;

// Currently bound VPROG_* ident; -1 means no program is active.
static int s_currentProgIdent = -1;

// Fixed attribute locations bound before linking (must match the vertex
// attribute setup in Phase 4).
#define GLES_ATTRIB_POSITION   0
#define GLES_ATTRIB_TEXCOORD0  1
#define GLES_ATTRIB_TEXCOORD1  2
#define GLES_ATTRIB_NORMAL     3
#define GLES_ATTRIB_TANGENT    4
#define GLES_ATTRIB_BITANGENT  5
#define GLES_ATTRIB_COLOR      6

// ---------------------------------------------------------------------------
// Gamma-hack helper — reproduces the patch logic from draw_arb2.cpp:574.
// Returns a pointer to a (possibly newly allocated) buffer containing the
// patched ARB text, or the original src if no patching is needed.
// outBuf must be freed with delete[] if it differs from src.
// ---------------------------------------------------------------------------

static ID_INLINE bool IsARBIdentChar( int c ) {
	return c == '$' || c == '_'
	       || ( c >= '0' && c <= '9' )
	       || ( c >= 'A' && c <= 'Z' )
	       || ( c >= 'a' && c <= 'z' );
}

static char *FindLineThatStartsWith( char *text, const char *findMe ) {
	char *res = strstr( text, findMe );
	while ( res != NULL ) {
		char *cur = res;
		if ( cur > text ) --cur;
		while ( cur > text && ( *cur == ' ' || *cur == '\t' ) ) --cur;
		if ( cur == text ) return cur;
		if ( *cur == '\n' || *cur == '\r' ) return cur + 1;
		res = strstr( res + 1, findMe );
	}
	return NULL;
}

// Returns a heap-allocated patched copy (caller must delete[]) when gamma
// hack applies, otherwise returns NULL (use src as-is).
static char *ApplyGammaHack( const char *src ) {
	if ( !r_gammaInShader.GetBool() ) return NULL;
	if ( strstr( src, "nodhewm3gammahack" ) ) return NULL;
	if ( strstr( src, "!!ARBfp1.0" ) == NULL ) return NULL;

	const char *start = src;
	const char *end = strstr( start, "END" );
	if ( !end ) return NULL;

	// note that strlen("dhewm3tmpres") == strlen("result.color")
	const char *tmpres = "TEMP dhewm3tmpres; # injected by dhewm3 for gamma correction\n";
	const char *extraLines =
		"# gamma correction in shader, injected by dhewm3 \n"
		"MUL_SAT dhewm3tmpres.xyz, program.env[21], dhewm3tmpres;\n"
		"POW result.color.x, dhewm3tmpres.x, program.env[21].w;\n"
		"POW result.color.y, dhewm3tmpres.y, program.env[21].w;\n"
		"POW result.color.z, dhewm3tmpres.z, program.env[21].w;\n"
		"MOV result.color.w, dhewm3tmpres.w;\n"
		"\nEND\n\n";

	int fullLen = (int)strlen( start ) + (int)strlen( tmpres ) + (int)strlen( extraLines );
	char *outStr = new char[ fullLen + 1 ];

	// Find insertion point: after OPTION line if present, else after first line
	char *tmp = new char[ strlen(start) + 1 ];
	memcpy( tmp, start, strlen(start) + 1 );

	char *insertPos = FindLineThatStartsWith( tmp, "OPTION" );
	if ( insertPos == NULL ) insertPos = tmp;
	while ( *insertPos && *insertPos != '\n' && *insertPos != '\r' ) insertPos++;
	while ( *insertPos == '\n' || *insertPos == '\r' ) insertPos++;

	const char *endInOrig = end;
	const char *insertPosInOrig = start + ( insertPos - tmp );
	delete[] tmp;

	int curLen = (int)( insertPosInOrig - start );
	memcpy( outStr, start, curLen );
	memcpy( outStr + curLen, tmpres, strlen(tmpres) );
	curLen += (int)strlen(tmpres);
	int remLen = (int)( endInOrig - insertPosInOrig );
	memcpy( outStr + curLen, insertPosInOrig, remLen );
	curLen += remLen;
	outStr[curLen] = '\0';

	// Replace all "result.color" with "dhewm3tmpres"
	for ( char *resCol = strstr( outStr, "result.color" );
	      resCol != NULL; resCol = strstr( resCol + 13, "result.color" ) ) {
		memcpy( resCol, "dhewm3tmpres", 12 );
		{
			char *s = resCol - 1;
			while ( s > outStr && ( *s == ' ' || *s == '\t' ) ) --s;
			if ( *s != '=' || s <= outStr + 8 ) continue;
			--s;
			while ( s > outStr && ( *s == ' ' || *s == '\t' ) ) --s;
			if ( s <= outStr + 7 || !IsARBIdentChar( *s ) ) continue;
			--s;
			while ( s > outStr && IsARBIdentChar( *s ) ) --s;
			if ( s <= outStr + 6 || ( *s != ' ' && *s != '\t' ) ) continue;
			--s;
			while ( s > outStr && ( *s == ' ' || *s == '\t' ) ) --s;
			if ( s <= outStr + 5 || *s != 'T' ) continue;
			s -= 5;
			if ( idStr::Cmpn( s, "OUTPUT", 6 ) == 0 ) {
				memcpy( s, "ALIAS ", 6 );
			}
		}
	}

	strcat( outStr, extraLines );
	return outStr;
}

// ---------------------------------------------------------------------------
// Shader compilation & linking
// ---------------------------------------------------------------------------

static GLuint R_GLES2_CompileGLSL( GLenum type, const char *src ) {
	GLuint shader = qglCreateShader( type );
	if ( !shader ) {
		common->Warning( "GLES3: glCreateShader failed" );
		return 0;
	}
	const GLchar *srcp = (const GLchar *)src;
	qglShaderSource( shader, 1, &srcp, NULL );
	qglCompileShader( shader );

	GLint status = GL_FALSE;
	qglGetShaderiv( shader, GL_COMPILE_STATUS, &status );
	if ( !status ) {
		GLchar log[4096];
		GLsizei logLen = 0;
		qglGetShaderInfoLog( shader, (GLsizei)sizeof(log), &logLen, log );
		common->Warning( "GLES3 shader compile failed:\n%s\n--- source ---\n%s\n--- end ---",
		                 log, src );
		qglDeleteShader( shader );
		return 0;
	}
	return shader;
}

static GLuint R_GLES2_LinkProgram( GLuint vert, GLuint frag ) {
	GLuint prog = qglCreateProgram();
	if ( !prog ) {
		common->Warning( "GLES3: glCreateProgram failed" );
		return 0;
	}
	qglAttachShader( prog, vert );
	qglAttachShader( prog, frag );

	qglBindAttribLocation( prog, GLES_ATTRIB_POSITION,  "aPosition" );
	qglBindAttribLocation( prog, GLES_ATTRIB_TEXCOORD0, "aTexCoord0" );
	qglBindAttribLocation( prog, GLES_ATTRIB_TEXCOORD1, "aTexCoord1" );
	qglBindAttribLocation( prog, GLES_ATTRIB_NORMAL,    "aNormal" );
	qglBindAttribLocation( prog, GLES_ATTRIB_TANGENT,   "aTangent" );
	qglBindAttribLocation( prog, GLES_ATTRIB_BITANGENT, "aBitangent" );
	qglBindAttribLocation( prog, GLES_ATTRIB_COLOR,     "aColor" );

	qglLinkProgram( prog );

	GLint status = GL_FALSE;
	qglGetProgramiv( prog, GL_LINK_STATUS, &status );
	if ( !status ) {
		GLchar log[4096];
		GLsizei logLen = 0;
		qglGetProgramInfoLog( prog, (GLsizei)sizeof(log), &logLen, log );
		common->Warning( "GLES3 program link failed:\n%s", log );
		qglDeleteProgram( prog );
		return 0;
	}
	return prog;
}

// Translate one ARB shader source and compile it as a GLSL ES shader object.
// Returns 0 on failure.
static GLuint R_GLES2_TranslateAndCompile( const char *arbSrc, bool isFragment ) {
	idStr glsl;
	if ( !ARB2GLSL_Translate( arbSrc, isFragment, glsl ) ) {
		common->Warning( "GLES3: ARB2GLSL translation failed" );
		return 0;
	}
	return R_GLES2_CompileGLSL(
		isFragment ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER,
		glsl.c_str()
	);
}

// ---------------------------------------------------------------------------
// R_GLES2_InitPrograms — load, translate, compile and link all shader pairs
// ---------------------------------------------------------------------------

static void R_GLES2_InitPrograms( void ) {
	memset( s_glesProgs, 0, sizeof(s_glesProgs) );
	for ( int i = 0; i < MAX_GLES_PROGS; i++ ) {
		s_glesProgs[i].uProgramEnvLoc = -1;
		s_glesProgs[i].uMVPLoc        = -1;
		for ( int j = 0; j < 8; j++ ) s_glesProgs[i].uTexLoc[j] = -1;
	}
	memset( s_programEnv, 0, sizeof(s_programEnv) );

	// Build compiled shader objects for every entry in glesProgDefs[].
	// Vertex shaders are stored at s_glesProgs[ident].vert;
	// Fragment shaders are stored at s_glesProgs[ident].frag.
	// After all individual shaders are compiled, pair and link them.

	// Compiled individual shader objects, keyed by ident
	GLuint compiledShaders[MAX_GLES_PROGS];
	memset( compiledShaders, 0, sizeof(compiledShaders) );

	for ( int i = 0; i < NUM_GLES_PROG_DEFS; i++ ) {
		const glesProgramDef_t &def = glesProgDefs[i];
		bool isFragment = ( def.target == GL_FRAGMENT_SHADER );

		const char *src = NULL;
		char *fileBuffer = NULL;
		char *patched = NULL;

#if D3_INTEGRATE_SOFTPART_SHADERS
		if ( def.ident == VPROG_SOFT_PARTICLE ) {
			src = softpartVShader;
		} else if ( def.ident == FPROG_SOFT_PARTICLE ) {
			src = softpartFShader;
		}
#endif

		if ( !src && def.name ) {
			idStr fullPath = "glprogs/";
			fullPath += def.name;
			fileSystem->ReadFile( fullPath.c_str(), (void **)&fileBuffer, NULL );
			if ( !fileBuffer ) {
				common->Warning( "GLES3: shader file not found: %s", fullPath.c_str() );
				continue;
			}
			src = fileBuffer;
		}

		if ( !src ) {
			common->Warning( "GLES3: no source for program ident %d", def.ident );
			continue;
		}

		// .vfp files contain both a vertex and fragment program concatenated.
		// Extract only the half that matches this entry, mirroring draw_arb2.cpp.
		char *extracted = NULL;
		{
			const char *marker = isFragment ? "!!ARBfp" : "!!ARBvp";
			const char *progStart = strstr( src, marker );
			if ( progStart ) {
				const char *progEnd = strstr( progStart, "END" );
				if ( progEnd ) {
					int len = (int)( progEnd - progStart ) + 3; // include "END"
					extracted = new char[ len + 1 ];
					memcpy( extracted, progStart, len );
					extracted[ len ] = '\0';
					src = extracted;
				}
			}
		}

		// Apply gamma hack for fragment shaders
		if ( isFragment ) {
			patched = ApplyGammaHack( src );
		}

		const char *finalSrc = patched ? patched : src;

		GLuint shader = R_GLES2_TranslateAndCompile( finalSrc, isFragment );

		delete[] patched;
		patched = NULL;
		delete[] extracted;
		extracted = NULL;

		if ( fileBuffer ) {
			fileSystem->FreeFile( fileBuffer );
			fileBuffer = NULL;
		}

		if ( !shader ) {
			common->Warning( "GLES3: compile failed for ident %d (%s)",
			                 def.ident, def.name ? def.name : "<inline>" );
			continue;
		}

		if ( def.ident >= 0 && def.ident < MAX_GLES_PROGS ) {
			compiledShaders[ def.ident ] = shader;
		}
	}

	// Link vertex+fragment pairs.  For each vertex shader entry, find its
	// paired fragment shader by matching filename (or by ident adjacency for
	// inline shaders), then link.
	for ( int i = 0; i < NUM_GLES_PROG_DEFS; i++ ) {
		const glesProgramDef_t &vdef = glesProgDefs[i];
		if ( vdef.target != GL_VERTEX_SHADER ) continue;

		GLuint vert = ( vdef.ident >= 0 && vdef.ident < MAX_GLES_PROGS )
		              ? compiledShaders[ vdef.ident ] : 0;
		if ( !vert ) continue;

		// Find corresponding fragment shader definition
		int fragIdent = -1;
		for ( int j = 0; j < NUM_GLES_PROG_DEFS; j++ ) {
			const glesProgramDef_t &fdef = glesProgDefs[j];
			if ( fdef.target != GL_FRAGMENT_SHADER ) continue;

			bool matched = false;
			if ( vdef.name && fdef.name ) {
				matched = ( idStr::Icmp( vdef.name, fdef.name ) == 0 );
			} else if ( !vdef.name && !fdef.name ) {
				// Both inline — pair by ident proximity (VPROG/FPROG are consecutive)
				matched = ( fdef.ident == vdef.ident + 1 );
			}
			if ( matched ) { fragIdent = fdef.ident; break; }
		}

		// Vertex-only program (shadow.vp has no fragment pair) — skip linking
		if ( fragIdent < 0 ) continue;

		GLuint frag = ( fragIdent >= 0 && fragIdent < MAX_GLES_PROGS )
		              ? compiledShaders[ fragIdent ] : 0;
		if ( !frag ) continue;

		GLuint prog = R_GLES2_LinkProgram( vert, frag );
		if ( !prog ) continue;

		glesProgram_t &gp = s_glesProgs[ vdef.ident ];
		gp.vert  = vert;
		gp.frag  = frag;
		gp.prog  = prog;
		gp.valid = true;

		qglUseProgram( prog );

		gp.uProgramEnvLoc = qglGetUniformLocation( prog, "uProgramEnv" );
		gp.uMVPLoc        = qglGetUniformLocation( prog, "uMVP" );

		for ( int t = 0; t < 8; t++ ) {
			char uname[16];
			idStr::snPrintf( uname, sizeof(uname), "uTex%d", t );
			gp.uTexLoc[t] = qglGetUniformLocation( prog, uname );
			if ( gp.uTexLoc[t] >= 0 ) {
				qglUniform1i( gp.uTexLoc[t], t );
			}
		}

		qglUseProgram( 0 );

		common->Printf( "GLES3: linked %s\n",
		                vdef.name ? vdef.name : "<inline>" );
	}
}

// ---------------------------------------------------------------------------
// Phase 4 draw helpers
// ---------------------------------------------------------------------------

void GLES2_SetProgramEnv( int index, const float *v ); // defined below

// Bind a GLSL program by VPROG_* ident; no-op if already current.
static const glesProgram_t *GLES2_UseProgram( int vprogIdent ) {
	if ( vprogIdent < 0 || vprogIdent >= MAX_GLES_PROGS ) return NULL;
	glesProgram_t &gp = s_glesProgs[ vprogIdent ];
	if ( !gp.valid ) return NULL;
	if ( s_currentProgIdent != vprogIdent ) {
		qglUseProgram( gp.prog );
		s_currentProgIdent = vprogIdent;
	}
	return &gp;
}

// Upload the full 32-slot s_programEnv mirror to the shader's uProgramEnv uniform.
static void GLES2_UploadProgramEnv( const glesProgram_t &gp ) {
	if ( gp.uProgramEnvLoc >= 0 )
		qglUniform4fv( gp.uProgramEnvLoc, 32, (float *)s_programEnv );
}

// Compute MVP = projection * modelView and upload to the shader's uMVP uniform.
static void GLES2_ComputeAndUploadMVP( const glesProgram_t &gp, const float *mv ) {
	if ( gp.uMVPLoc < 0 ) return;
	const float *proj = backEnd.viewDef->projectionMatrix;
	float mvp[16];
	for ( int c = 0; c < 4; c++ )
		for ( int r = 0; r < 4; r++ ) {
			mvp[c*4+r] = 0;
			for ( int k = 0; k < 4; k++ )
				mvp[c*4+r] += proj[k*4+r] * mv[c*4+k];
		}
	qglUniformMatrix4fv( gp.uMVPLoc, 1, GL_FALSE, mvp );
}

// Set up all vertex attribute pointers for an idDrawVert base.
// The caller must have already called vertexCache.Position() to bind the VBO.
static void GLES2_SetupInteractionAttribs( const idDrawVert *base ) {
	qglEnableVertexAttribArray( GLES_ATTRIB_POSITION );
	qglVertexAttribPointer( GLES_ATTRIB_POSITION,  3, GL_FLOAT,         GL_FALSE,
	                        sizeof(idDrawVert), &base->xyz );

	qglEnableVertexAttribArray( GLES_ATTRIB_TEXCOORD0 );
	qglVertexAttribPointer( GLES_ATTRIB_TEXCOORD0, 2, GL_FLOAT,         GL_FALSE,
	                        sizeof(idDrawVert), &base->st );

	qglEnableVertexAttribArray( GLES_ATTRIB_NORMAL );
	qglVertexAttribPointer( GLES_ATTRIB_NORMAL,    3, GL_FLOAT,         GL_FALSE,
	                        sizeof(idDrawVert), &base->normal );

	qglEnableVertexAttribArray( GLES_ATTRIB_TANGENT );
	qglVertexAttribPointer( GLES_ATTRIB_TANGENT,   3, GL_FLOAT,         GL_FALSE,
	                        sizeof(idDrawVert), &base->tangents[0] );

	qglEnableVertexAttribArray( GLES_ATTRIB_BITANGENT );
	qglVertexAttribPointer( GLES_ATTRIB_BITANGENT, 3, GL_FLOAT,         GL_FALSE,
	                        sizeof(idDrawVert), &base->tangents[1] );

	qglEnableVertexAttribArray( GLES_ATTRIB_COLOR );
	qglVertexAttribPointer( GLES_ATTRIB_COLOR,     4, GL_UNSIGNED_BYTE, GL_TRUE,
	                        sizeof(idDrawVert), base->color );
}

// ---------------------------------------------------------------------------
// Per-interaction draw callback — called by RB_CreateSingleDrawInteractions.
// ---------------------------------------------------------------------------

static void RB_GLES2_DrawInteraction( const drawInteraction_t *din ) {
	static const float zero[4]   = { 0, 0, 0, 0 };
	static const float one[4]    = { 1, 1, 1, 1 };
	static const float negOne[4] = { -1, -1, -1, -1 };

	// vertex environment parameters
	GLES2_SetProgramEnv( PP_LIGHT_ORIGIN,      din->localLightOrigin.ToFloatPtr() );
	GLES2_SetProgramEnv( PP_VIEW_ORIGIN,       din->localViewOrigin.ToFloatPtr() );
	GLES2_SetProgramEnv( PP_LIGHT_PROJECT_S,   din->lightProjection[0].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_LIGHT_PROJECT_T,   din->lightProjection[1].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_LIGHT_PROJECT_Q,   din->lightProjection[2].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_LIGHT_FALLOFF_S,   din->lightProjection[3].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_BUMP_MATRIX_S,     din->bumpMatrix[0].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_BUMP_MATRIX_T,     din->bumpMatrix[1].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_DIFFUSE_MATRIX_S,  din->diffuseMatrix[0].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_DIFFUSE_MATRIX_T,  din->diffuseMatrix[1].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_SPECULAR_MATRIX_S, din->specularMatrix[0].ToFloatPtr() );
	GLES2_SetProgramEnv( PP_SPECULAR_MATRIX_T, din->specularMatrix[1].ToFloatPtr() );

	switch ( din->vertexColor ) {
	case SVC_IGNORE:
		GLES2_SetProgramEnv( PP_COLOR_MODULATE, zero );
		GLES2_SetProgramEnv( PP_COLOR_ADD,      one );
		break;
	case SVC_MODULATE:
		GLES2_SetProgramEnv( PP_COLOR_MODULATE, one );
		GLES2_SetProgramEnv( PP_COLOR_ADD,      zero );
		break;
	case SVC_INVERSE_MODULATE:
		GLES2_SetProgramEnv( PP_COLOR_MODULATE, negOne );
		GLES2_SetProgramEnv( PP_COLOR_ADD,      one );
		break;
	}

	// fragment environment parameters (slots 0 and 1 are safe — interaction.vfp
	// vertex shader does not read those slots)
	GLES2_SetProgramEnv( 0, din->diffuseColor.ToFloatPtr() );
	GLES2_SetProgramEnv( 1, din->specularColor.ToFloatPtr() );

	if ( r_gammaInShader.GetBool() ) {
		float parm[4];
		parm[0] = parm[1] = parm[2] = r_brightness.GetFloat();
		parm[3] = 1.0f / r_gamma.GetFloat();
		GLES2_SetProgramEnv( PP_GAMMA_BRIGHTNESS, parm );
	}

	const glesProgram_t &gp = s_glesProgs[ s_currentProgIdent ];
	GLES2_UploadProgramEnv( gp );
	GLES2_ComputeAndUploadMVP( gp, din->surf->space->modelViewMatrix );

	GL_SelectTexture( 1 ); din->bumpImage->Bind();
	GL_SelectTexture( 2 ); din->lightFalloffImage->Bind();
	GL_SelectTexture( 3 ); din->lightImage->Bind();
	GL_SelectTexture( 4 ); din->diffuseImage->Bind();
	GL_SelectTexture( 5 ); din->specularImage->Bind();

	RB_DrawElementsWithCounters( din->surf->geo );
}

// ---------------------------------------------------------------------------
// Per-surface-chain interaction setup.
// ---------------------------------------------------------------------------

static void RB_GLES2_CreateDrawInteractions( const drawSurf_t *surf ) {
	if ( !surf ) return;

	int vprogIdent = r_testARBProgram.GetBool() ? VPROG_TEST : VPROG_INTERACTION;
	const glesProgram_t *gp = GLES2_UseProgram( vprogIdent );
	if ( !gp ) return;

	GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHMASK | backEnd.depthFunc );

	// texture 0: normalisation cube map / ambient normal map
	GL_SelectTexture( 0 );
	if ( backEnd.vLight->lightShader->IsAmbientLight() ) {
		globalImages->ambientNormalMap->Bind();
	} else {
		globalImages->normalCubeMapImage->Bind();
	}

	// texture 6: specular lookup table
	GL_SelectTexture( 6 );
	if ( r_testARBProgram.GetBool() ) {
		globalImages->specular2DTableImage->Bind();
	} else {
		globalImages->specularTableImage->Bind();
	}

	for ( ; surf ; surf = surf->nextOnLight ) {
		idDrawVert *ac = (idDrawVert *)vertexCache.Position( surf->geo->ambientCache );
		GLES2_SetupInteractionAttribs( ac );
		RB_CreateSingleDrawInteractions( surf, RB_GLES2_DrawInteraction );
	}

	// unbind textures
	GL_SelectTexture( 6 ); globalImages->BindNull();
	GL_SelectTexture( 5 ); globalImages->BindNull();
	GL_SelectTexture( 4 ); globalImages->BindNull();
	GL_SelectTexture( 3 ); globalImages->BindNull();
	GL_SelectTexture( 2 ); globalImages->BindNull();
	GL_SelectTexture( 1 ); globalImages->BindNull();

	backEnd.glState.currenttmu = -1;
	GL_SelectTexture( 0 );
}

// ---------------------------------------------------------------------------
// GLES3 depth fill — called from RB_STD_FillDepthBuffer in draw_common.cpp.
// ---------------------------------------------------------------------------

void RB_GLES2_FillDepthBuffer( drawSurf_t **drawSurfs, int numDrawSurfs ) {
	if ( !backEnd.viewDef->viewEntitys ) return;

	const glesProgram_t *gp = GLES2_UseProgram( VPROG_INTERACTION );
	if ( !gp ) return;

	qglColorMask( 0, 0, 0, 0 );
	qglDepthMask( GL_TRUE );
	GL_State( GLS_DEPTHFUNC_LESS );
	qglEnable( GL_STENCIL_TEST );
	qglStencilFunc( GL_ALWAYS, 1, 255 );
	qglPolygonOffset( r_offsetFactor.GetFloat(), r_offsetUnits.GetFloat() );

	for ( int i = 0; i < numDrawSurfs; i++ ) {
		const drawSurf_t *surf = drawSurfs[i];
		const srfTriangles_t *tri = surf->geo;
		if ( !tri || !tri->ambientCache ) continue;
		if ( surf->material->Coverage() == MC_TRANSLUCENT ) continue;
		if ( !surf->material->IsDrawn() ) continue;
		if ( !tri->numIndexes ) continue;

		idDrawVert *ac = (idDrawVert *)vertexCache.Position( tri->ambientCache );
		qglEnableVertexAttribArray( GLES_ATTRIB_POSITION );
		qglVertexAttribPointer( GLES_ATTRIB_POSITION, 3, GL_FLOAT, GL_FALSE,
		                        sizeof(idDrawVert), &ac->xyz );

		GLES2_ComputeAndUploadMVP( *gp, surf->space->modelViewMatrix );
		RB_DrawElementsWithCounters( tri );
	}

	qglColorMask( 1, 1, 1, 1 );
}

// ---------------------------------------------------------------------------
// CPU-side program.env[] mirror — called instead of qglProgramEnvParameter4fvARB
// ---------------------------------------------------------------------------

void GLES2_SetProgramEnv( int index, const float *v ) {
	if ( index >= 0 && index < 32 ) {
		s_programEnv[index][0] = v[0];
		s_programEnv[index][1] = v[1];
		s_programEnv[index][2] = v[2];
		s_programEnv[index][3] = v[3];
	}
}

// ---------------------------------------------------------------------------
// Exported entry points (symbol names match draw_arb2.cpp's exports)
// ---------------------------------------------------------------------------

/*
=================
R_ARB2_Init
=================
*/
void R_ARB2_Init( void ) {
	glConfig.allowARB2Path = false;
	common->Printf( "GLES3 backend: translating and compiling shaders\n" );
	R_GLES2_InitPrograms();

	qglGenVertexArrays( 1, &s_vao );
	qglBindVertexArray( s_vao );
	s_currentProgIdent = -1;

	common->Printf( "GLES3 backend: shader init complete\n" );
}

/*
=================
RB_ARB2_DrawInteractions
=================
*/
void RB_ARB2_DrawInteractions( void ) {
	GL_SelectTexture( 0 );

	for ( viewLight_t *vLight = backEnd.viewDef->viewLights;
	      vLight; vLight = vLight->next ) {

		backEnd.vLight = vLight;

		if ( vLight->lightShader->IsFogLight() )   continue;
		if ( vLight->lightShader->IsBlendLight() )  continue;
		if ( !vLight->localInteractions &&
		     !vLight->globalInteractions &&
		     !vLight->translucentInteractions )      continue;

		// Shadow volumes are Phase 6; always let the stencil test pass.
		qglStencilFunc( GL_ALWAYS, 128, 255 );

		backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
		RB_GLES2_CreateDrawInteractions( vLight->localInteractions );
		RB_GLES2_CreateDrawInteractions( vLight->globalInteractions );

		if ( !r_skipTranslucent.GetBool() ) {
			qglStencilFunc( GL_ALWAYS, 128, 255 );
			backEnd.depthFunc = GLS_DEPTHFUNC_LESS;
			RB_GLES2_CreateDrawInteractions( vLight->translucentInteractions );
			backEnd.depthFunc = GLS_DEPTHFUNC_EQUAL;
		}
	}

	qglStencilFunc( GL_ALWAYS, 128, 255 );
	qglUseProgram( 0 );
	s_currentProgIdent = -1;
}

/*
=================
R_FindARBProgram

Material.cpp calls this while parsing .mtr files to locate additional
programs. Under GLES3_BACKEND the ARB program slot mechanism is unused;
return 0 to signal "not found" (materials that rely on custom programs
will fall back to default rendering).
=================
*/
int R_FindARBProgram( GLenum target, const char *program ) {
	return 0;
}

/*
=================
R_ReloadARBPrograms_f

Console command registered by RenderSystem_init.cpp.
=================
*/
void R_ReloadARBPrograms_f( const idCmdArgs &args ) {
	common->Printf( "GLES3: reloading shaders\n" );

	// Delete existing linked programs and shader objects
	for ( int i = 0; i < MAX_GLES_PROGS; i++ ) {
		glesProgram_t &gp = s_glesProgs[i];
		if ( !gp.valid ) continue;
		if ( gp.prog ) qglDeleteProgram( gp.prog );
		if ( gp.vert ) qglDeleteShader( gp.vert );
		if ( gp.frag ) qglDeleteShader( gp.frag );
		gp = glesProgram_t();
		gp.uProgramEnvLoc = -1;
		gp.uMVPLoc        = -1;
		for ( int j = 0; j < 8; j++ ) gp.uTexLoc[j] = -1;
	}

	R_GLES2_InitPrograms();
}
