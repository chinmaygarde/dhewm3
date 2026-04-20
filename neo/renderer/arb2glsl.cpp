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
** ARB2GLSL.CPP
**
** Runtime translator from ARB assembly vertex/fragment programs to
** GLSL ES 3.00 source.  The engine loads .vfp files as plain text and
** passes them through here before handing them to the GLES3 driver.
** This means all existing .vfp data (including mods) is reused without
** hand-written GLSL.
*/

#include "sys/platform.h"
#include "framework/Common.h"
#include "idlib/Str.h"
#include "idlib/containers/List.h"

#include "renderer/arb2glsl.h"

#ifdef GLES3_BACKEND

// ---------------------------------------------------------------------------
// Internal data structures — reset at the start of every Translate call
// ---------------------------------------------------------------------------

struct arbParam_t {
	idStr name;
	bool  isEnvAlias;
	int   envIndex;   // valid when isEnvAlias
	idStr constInit;  // e.g. "vec4(0.0, 0.5, 0.0, 1.0)" when literal
};

struct arbSampler_t {
	int   texUnit;
	idStr uniformName; // "uTex0", "uTex1", ...
};

static idList<arbParam_t>   s_params;
static idList<arbSampler_t> s_samplers;
static idList<idStr>        s_temps;
static int  s_maxEnvIndex;       // highest program.env[N] index seen (-1 = none)
static bool s_positionInvariant; // OPTION ARB_position_invariant present
static bool s_needsMVPRows;      // state.matrix.mvp.row[N] referenced → uniform vec4 uMVPRows[4]
static bool s_isFragment;        // true when translating a fragment program

// vertex program inputs / outputs
static bool s_needsPosition;
static bool s_needsTexCoord0;
static bool s_needsTexCoord1;
static bool s_needsNormal;
static bool s_needsTangent;
static bool s_needsBitangent;
static bool s_needsColor;
static int  s_maxWriteTexCoord;  // highest result.texcoord[N] index written (-1 = none)
static bool s_writesVaryingColor;// result.color in VP → vColor varying to FP

// fragment program inputs
static bool s_needsFragPos;
static int  s_maxReadTexCoord;   // highest fragment.texcoord[N] index read (-1 = none)
static bool s_needsFragColor;

static void ResetState( void ) {
	s_params.Clear();
	s_samplers.Clear();
	s_temps.Clear();
	s_maxEnvIndex       = -1;
	s_positionInvariant = false;
	s_needsMVPRows      = false;
	s_isFragment        = false;
	s_needsPosition     = false;
	s_needsTexCoord0    = false;
	s_needsTexCoord1    = false;
	s_needsNormal       = false;
	s_needsTangent      = false;
	s_needsBitangent    = false;
	s_needsColor        = false;
	s_maxWriteTexCoord  = -1;
	s_writesVaryingColor= false;
	s_needsFragPos      = false;
	s_maxReadTexCoord   = -1;
	s_needsFragColor    = false;
}

// ---------------------------------------------------------------------------
// Low-level text helpers
// ---------------------------------------------------------------------------

static const char *SkipWhitespace( const char *p ) {
	while ( *p == ' ' || *p == '\t' ) ++p;
	return p;
}

static const char *SkipNewline( const char *p ) {
	if ( *p == '\r' ) ++p;
	if ( *p == '\n' ) ++p;
	return p;
}

// Read a token delimited by whitespace, comma, semicolon, or newline.
static const char *ReadToken( const char *p, idStr &tok ) {
	tok.Clear();
	p = SkipWhitespace( p );
	while ( *p && *p != ' ' && *p != '\t' && *p != ',' && *p != ';'
	        && *p != '\n' && *p != '\r' ) {
		tok.Append( *p++ );
	}
	return p;
}

static const char *SkipCommaOrWhitespace( const char *p ) {
	p = SkipWhitespace( p );
	if ( *p == ',' ) ++p;
	return p;
}

// Returns true if the string looks like a bare numeric literal (int or float,
// optionally negative).
static bool IsNumericLiteral( const char *s ) {
	if ( *s == '-' ) ++s;
	if ( !*s ) return false;
	bool hasDot = false;
	while ( *s ) {
		if ( *s == '.' ) { hasDot = true; ++s; continue; }
		if ( *s < '0' || *s > '9' ) return false;
		++s;
	}
	(void)hasDot;
	return true;
}

// ---------------------------------------------------------------------------
// PARAM lookup helpers
// ---------------------------------------------------------------------------

static const arbParam_t *FindParam( const char *name ) {
	for ( int i = 0; i < s_params.Num(); i++ ) {
		if ( s_params[i].name == name ) return &s_params[i];
	}
	return NULL;
}

static bool HasSampler( int unit ) {
	for ( int i = 0; i < s_samplers.Num(); i++ ) {
		if ( s_samplers[i].texUnit == unit ) return true;
	}
	return false;
}

static void AddSampler( int unit ) {
	if ( HasSampler( unit ) ) return;
	arbSampler_t s;
	s.texUnit = unit;
	char buf[16];
	idStr::snPrintf( buf, sizeof(buf), "uTex%d", unit );
	s.uniformName = buf;
	s_samplers.Append( s );
}

// ---------------------------------------------------------------------------
// Operand translation
//
// Takes a single ARB operand token (with optional swizzle already attached,
// e.g. "vertex.texcoord.xy" or "program.env[12].x") and returns the GLSL
// equivalent.  Bare numeric literals are wrapped in vec4() so GLSL ES 1.00
// type rules are satisfied.
// ---------------------------------------------------------------------------

static idStr TranslateOperand( const char *arb ) {
	// Handle negation prefix
	bool negate = false;
	if ( arb[0] == '-' ) {
		negate = true;
		arb++;
	}

	// Separate base token from swizzle (everything from the second '.' onward
	// that is a swizzle component, not an array index).
	// e.g. "vertex.texcoord" → base="vertex.texcoord", swizzle=""
	//      "tmp.xy"           → base="tmp",             swizzle=".xy"
	//      "program.env[12]"  → base="program.env[12]", swizzle=""
	// Strategy: find the LAST '.' that is followed only by xyzwrgba letters.
	const char *swizzleStart = NULL;
	{
		const char *p = arb;
		while ( *p ) {
			if ( *p == '.' ) {
				// Check if the remainder looks like a swizzle (all x,y,z,w,r,g,b,a)
				const char *q = p + 1;
				bool isSwizzle = (*q != '\0');
				while ( *q ) {
					if ( *q != 'x' && *q != 'y' && *q != 'z' && *q != 'w'
					     && *q != 'r' && *q != 'g' && *q != 'b' && *q != 'a' ) {
						isSwizzle = false;
						break;
					}
					q++;
				}
				if ( isSwizzle ) {
					swizzleStart = p;
					break;
				}
			}
			p++;
		}
	}

	// Extract base without swizzle
	idStr base;
	if ( swizzleStart ) {
		base.Append( arb, (int)(swizzleStart - arb) );
	} else {
		base = arb;
	}
	const char *swizzle = swizzleStart ? swizzleStart : "";

	// Translate base to GLSL
	idStr glsl;

	if ( IsNumericLiteral( base.c_str() ) ) {
		// Scalar literal → vec4 so multi-component instructions type-check
		glsl = "vec4(";
		glsl += base;
		glsl += ")";
		// Don't append swizzle to the vec4() wrapper — the instruction
		// translator will handle write-mask on the destination side.
		// For source operands the swizzle still applies.
		glsl += swizzle;
	} else if ( idStr::Icmpn( base.c_str(), "vertex.position", 15 ) == 0 ) {
		glsl = "aPosition";
		glsl += swizzle;
		s_needsPosition = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.attrib[8]" ) == 0 ) {
		glsl = "aTexCoord0";
		glsl += swizzle;
		s_needsTexCoord0 = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.texcoord[0]" ) == 0
	            || idStr::Icmp( base.c_str(), "vertex.texcoord" ) == 0 ) {
		glsl = "aTexCoord0";
		glsl += swizzle;
		s_needsTexCoord0 = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.texcoord[1]" ) == 0 ) {
		glsl = "aTexCoord1";
		glsl += swizzle;
		s_needsTexCoord1 = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.normal" ) == 0 ) {
		glsl = "aNormal";
		glsl += swizzle;
		s_needsNormal = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.attrib[9]" ) == 0 ) {
		glsl = "aTangent";
		glsl += swizzle;
		s_needsTangent = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.attrib[10]" ) == 0 ) {
		glsl = "aBitangent";
		glsl += swizzle;
		s_needsBitangent = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.attrib[11]" ) == 0 ) {
		glsl = "aNormal";
		glsl += swizzle;
		s_needsNormal = true;
	} else if ( idStr::Icmp( base.c_str(), "vertex.color" ) == 0 ) {
		glsl = "aColor";
		glsl += swizzle;
		s_needsColor = true;
	} else if ( idStr::Icmp( base.c_str(), "fragment.texcoord" ) == 0 ) {
		glsl = "vTexCoord0";
		glsl += swizzle;
		if ( s_maxReadTexCoord < 0 ) s_maxReadTexCoord = 0;
	} else if ( idStr::Icmpn( base.c_str(), "fragment.texcoord[", 18 ) == 0 ) {
		int idx = atoi( base.c_str() + 18 );
		char buf[16]; idStr::snPrintf( buf, sizeof(buf), "vTexCoord%d", idx );
		glsl = buf;
		glsl += swizzle;
		if ( idx > s_maxReadTexCoord ) s_maxReadTexCoord = idx;
	} else if ( idStr::Icmp( base.c_str(), "fragment.color" ) == 0 ) {
		glsl = "vColor";
		glsl += swizzle;
		s_needsFragColor = true;
	} else if ( idStr::Icmp( base.c_str(), "fragment.position" ) == 0 ) {
		glsl = "gl_FragCoord";
		glsl += swizzle;
		s_needsFragPos = true;
	} else if ( idStr::Icmp( base.c_str(), "result.position" ) == 0 ) {
		glsl = "gl_Position";
		glsl += swizzle;
	} else if ( idStr::Icmp( base.c_str(), "result.color" ) == 0 ) {
		// In VP: color varying output (vColor); in FP: fragment color output (fragColor)
		if ( s_isFragment ) {
			glsl = "fragColor";
		} else {
			glsl = "vColor";
			s_writesVaryingColor = true;
		}
		glsl += swizzle;
	} else if ( idStr::Icmpn( base.c_str(), "result.texcoord", 15 ) == 0 ) {
		// result.texcoord or result.texcoord[N]
		int idx = 0;
		if ( base.Length() > 15 && base[15] == '[' ) {
			idx = atoi( base.c_str() + 16 );
		}
		char buf[16]; idStr::snPrintf( buf, sizeof(buf), "vTexCoord%d", idx );
		glsl = buf;
		glsl += swizzle;
		if ( idx > s_maxWriteTexCoord ) s_maxWriteTexCoord = idx;
	} else if ( idStr::Icmpn( base.c_str(), "state.matrix.mvp.row[", 21 ) == 0 ) {
		int idx = atoi( base.c_str() + 21 );
		char buf[32]; idStr::snPrintf( buf, sizeof(buf), "uMVPRows[%d]", idx );
		glsl = buf;
		glsl += swizzle;
		s_needsMVPRows = true;
	} else if ( idStr::Icmpn( base.c_str(), "program.env[", 12 ) == 0 ) {
		// Extract index N from program.env[N]
		int idx = atoi( base.c_str() + 12 );
		if ( idx > s_maxEnvIndex ) s_maxEnvIndex = idx;
		char buf[32];
		idStr::snPrintf( buf, sizeof(buf), "uProgramEnv[%d]", idx );
		glsl = buf;
		glsl += swizzle;
	} else {
		// Check PARAM alias table
		const arbParam_t *param = FindParam( base.c_str() );
		if ( param ) {
			if ( param->isEnvAlias ) {
				char buf[32];
				idStr::snPrintf( buf, sizeof(buf), "uProgramEnv[%d]", param->envIndex );
				glsl = buf;
			} else {
				// Literal const — use name directly (it's declared as const vec4)
				glsl = param->name;
			}
			glsl += swizzle;
		} else {
			// Plain TEMP register — pass through unchanged
			glsl = base;
			glsl += swizzle;
		}
	}

	if ( negate ) {
		idStr neg = "-";
		neg += glsl;
		return neg;
	}
	return glsl;
}

// Scan an operand token during first pass to update flags (no output).
static void ScanOperand( const char *tok ) {
	// Strip negation
	if ( tok[0] == '-' ) tok++;

	// Strip swizzle
	idStr base;
	const char *p = tok;
	const char *swizzleAt = NULL;
	while ( *p ) {
		if ( *p == '.' ) {
			const char *q = p + 1;
			bool ok = (*q != '\0');
			while (*q) {
				if (*q!='x'&&*q!='y'&&*q!='z'&&*q!='w'&&*q!='r'&&*q!='g'&&*q!='b'&&*q!='a') {
					ok = false; break;
				}
				q++;
			}
			if (ok) { swizzleAt = p; break; }
		}
		p++;
	}
	if (swizzleAt) base.Append(tok, (int)(swizzleAt - tok));
	else base = tok;

	if ( idStr::Icmp( base.c_str(), "vertex.attrib[8]" ) == 0 ) s_needsTexCoord0 = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.texcoord[1]", 18 ) == 0 ) s_needsTexCoord1 = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.texcoord", 15 ) == 0 ) s_needsTexCoord0 = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.position", 15 ) == 0 ) s_needsPosition = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.normal", 13 ) == 0 ) s_needsNormal = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.attrib[9]", 16 ) == 0 ) s_needsTangent = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.attrib[10]", 17 ) == 0 ) s_needsBitangent = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.attrib[11]", 17 ) == 0 ) s_needsNormal = true;
	else if ( idStr::Icmpn( base.c_str(), "vertex.color", 12 ) == 0 ) s_needsColor = true;
	else if ( idStr::Icmp( base.c_str(), "fragment.texcoord" ) == 0 ) {
		if ( s_maxReadTexCoord < 0 ) s_maxReadTexCoord = 0;
	} else if ( idStr::Icmpn( base.c_str(), "fragment.texcoord[", 18 ) == 0 ) {
		int idx = atoi( base.c_str() + 18 );
		if ( idx > s_maxReadTexCoord ) s_maxReadTexCoord = idx;
	} else if ( idStr::Icmpn( base.c_str(), "fragment.color", 14 ) == 0 ) s_needsFragColor = true;
	else if ( idStr::Icmpn( base.c_str(), "fragment.position", 17 ) == 0 ) s_needsFragPos = true;
	else if ( idStr::Icmpn( base.c_str(), "result.texcoord", 15 ) == 0 ) {
		int idx = 0;
		const char *b = base.c_str();
		if ( b[15] == '[' ) idx = atoi( b + 16 );
		if ( idx > s_maxWriteTexCoord ) s_maxWriteTexCoord = idx;
	} else if ( idStr::Icmp( base.c_str(), "result.color" ) == 0 ) s_writesVaryingColor = true;
	else if ( idStr::Icmpn( base.c_str(), "state.matrix.mvp.row[", 21 ) == 0 ) s_needsMVPRows = true;
	else if ( idStr::Icmpn( base.c_str(), "program.env[", 12 ) == 0 ) {
		int idx = atoi( base.c_str() + 12 );
		if ( idx > s_maxEnvIndex ) s_maxEnvIndex = idx;
	}
}

// ---------------------------------------------------------------------------
// Write-mask / scalar helpers
// ---------------------------------------------------------------------------

// Extract the write-mask suffix from a translated destination operand.
// e.g. "vTexCoord0.xyz" → ".xyz",  "R0" → ""
static idStr GetWriteMask( const idStr &dst ) {
	int n = dst.Length();
	int i = n;
	while ( i > 0 ) {
		char c = dst[i-1];
		if ( c=='x'||c=='y'||c=='z'||c=='w'||c=='r'||c=='g'||c=='b'||c=='a' ) { i--; }
		else break;
	}
	if ( i > 0 && dst[i-1] == '.' && i < n ) {
		return dst.c_str() + i - 1; // includes the leading dot
	}
	return idStr("");
}

// Count swizzle components in a mask like ".xy" → 2, ".x" → 1, "" → 4.
static int MaskComponents( const idStr &mask ) {
	if ( mask.Length() == 0 ) return 4;
	int n = 0;
	for ( int i = 1; i < mask.Length(); i++ ) {
		char c = mask[i];
		if ( c=='x'||c=='y'||c=='z'||c=='w'||c=='r'||c=='g'||c=='b'||c=='a' ) n++;
	}
	return n;
}

// True when expr ends with a single-component swizzle (.x/.y/.z/.w/etc.).
static bool IsScalarExpr( const idStr &expr ) {
	int n = expr.Length();
	return n >= 2 && expr[n-2] == '.' &&
	       (expr[n-1]=='x'||expr[n-1]=='y'||expr[n-1]=='z'||expr[n-1]=='w'||
	        expr[n-1]=='r'||expr[n-1]=='g'||expr[n-1]=='b'||expr[n-1]=='a');
}

// Wrap an expression with a write-mask swizzle so LHS and RHS types agree.
// When the RHS is already scalar and dst is single-component, no conversion
// is needed.  When the RHS is scalar but dst is multi-component, we broadcast
// via vec4() before extracting.
static idStr MaskRHS( const idStr &rhs, const idStr &mask ) {
	if ( mask.Length() == 0 ) {
		// No write mask = full vec4 destination; scalar RHS must be broadcast
		if ( IsScalarExpr( rhs ) ) {
			idStr r = "vec4("; r += rhs; r += ")"; return r;
		}
		return rhs;
	}
	int dstComps = MaskComponents( mask );
	if ( IsScalarExpr( rhs ) ) {
		if ( dstComps == 1 ) return rhs; // scalar → scalar: no wrapping needed
		// scalar → multi-component: broadcast first
		idStr r = "vec4("; r += rhs; r += ")"; r += mask;
		return r;
	}
	idStr r = "("; r += rhs; r += ")"; r += mask;
	return r;
}

// Return a scalar (float) expression from an operand.  When the translated
// operand already ends with a single-component swizzle the extra ".x" that
// RCP/RSQ/EXP/LOG need is omitted to avoid "(tmp.x).x".
static idStr ToScalar( const idStr &a ) {
	int n = a.Length();
	if ( n >= 2 && a[n-2] == '.' ) {
		char c = a[n-1];
		if ( c=='x'||c=='y'||c=='z'||c=='w'||c=='r'||c=='g'||c=='b'||c=='a' )
			return a; // already scalar
	}
	idStr r = "("; r += a; r += ").x";
	return r;
}

// ---------------------------------------------------------------------------
// Instruction translation (second pass, one line at a time)
// ---------------------------------------------------------------------------

static void TranslateInstruction( const char *line, bool isFragment, idStr &body ) {
	const char *p = SkipWhitespace( line );

	// Read opcode
	idStr opcode;
	p = ReadToken( p, opcode );
	if ( opcode.Length() == 0 ) return;

	// Detect _SAT suffix
	bool sat = false;
	int uscore = opcode.Find( '_' );
	idStr baseOp = opcode;
	if ( uscore > 0 ) {
		idStr suffix = opcode.c_str() + uscore + 1;
		if ( idStr::Icmp( suffix.c_str(), "SAT" ) == 0 ) {
			sat = true;
			baseOp = opcode.Left( uscore );
		}
	}

	// Read destination
	idStr rawDst, rawA, rawB, rawC;
	p = SkipCommaOrWhitespace( p );
	p = ReadToken( p, rawDst );

	// Translate destination and extract its write mask so we can apply the
	// same mask to the RHS (GLSL types must match: e.g. dst.x = rhs.x).
	idStr dst  = TranslateOperand( rawDst.c_str() );
	idStr mask = GetWriteMask( dst );

	// Helper macro to read the next comma-separated source token.
	#define READ_SRC(var) \
		p = SkipCommaOrWhitespace( p ); \
		p = ReadToken( p, var );

	// Macro to emit a complete assignment: applies write-mask to rhs, handles _SAT.
	// 'rhsExpr' is the full vec4 RHS expression as an idStr.
	#define EMIT(rhsExpr) \
		do { \
			idStr _r = (rhsExpr); \
			if (sat) { idStr _s = "clamp("; _s += _r; _s += ", 0.0, 1.0)"; _r = _s; } \
			body += "    "; body += dst; body += " = "; body += MaskRHS(_r, mask); body += ";\n"; \
		} while(0)

	if ( idStr::Icmp( baseOp.c_str(), "MOV" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand( rawA.c_str() );
		EMIT(a);

	} else if ( idStr::Icmp( baseOp.c_str(), "ADD" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs; rhs += a; rhs += " + "; rhs += b;
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "SUB" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs; rhs += a; rhs += " - "; rhs += b;
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "MUL" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs; rhs += a; rhs += " * "; rhs += b;
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "MAD" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB); READ_SRC(rawC);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str()),
		      c = TranslateOperand(rawC.c_str());
		idStr rhs; rhs += a; rhs += " * "; rhs += b; rhs += " + "; rhs += c;
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "DP3" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(dot(vec3("; rhs += a; rhs += "), vec3("; rhs += b; rhs += ")))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "DP4" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(dot(vec4("; rhs += a; rhs += "), vec4("; rhs += b; rhs += ")))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "DPH" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(dot(vec3("; rhs += a; rhs += "), vec3("; rhs += b; rhs += ")) + ("; rhs += b; rhs += ").w)";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "RCP" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "vec4(1.0 / "; rhs += ToScalar(a); rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "RSQ" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "vec4(inversesqrt(abs("; rhs += ToScalar(a); rhs += ")))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "POW" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(pow(abs("; rhs += ToScalar(a); rhs += "), "; rhs += ToScalar(b); rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "EXP" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "vec4(exp2("; rhs += ToScalar(a); rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "LOG" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "vec4(log2(abs("; rhs += ToScalar(a); rhs += ")))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "MIN" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "min("; rhs += a; rhs += ", "; rhs += b; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "MAX" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "max("; rhs += a; rhs += ", "; rhs += b; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "ABS" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "abs("; rhs += a; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "FRC" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "fract("; rhs += a; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "FLR" ) == 0 ) {
		READ_SRC(rawA);
		idStr a = TranslateOperand(rawA.c_str());
		idStr rhs = "floor("; rhs += a; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "CMP" ) == 0 ) {
		// CMP dst, c, a, b → dst = (c < 0) ? a : b  (component-wise)
		// step(0.0, c) = 0 when c<0, 1 when c≥0 → mix(a, b, step) = a when c<0, b when c≥0 ✓
		READ_SRC(rawA); READ_SRC(rawB); READ_SRC(rawC);
		idStr c = TranslateOperand(rawA.c_str()),
		      a = TranslateOperand(rawB.c_str()),
		      b = TranslateOperand(rawC.c_str());
		idStr rhs = "mix("; rhs += a; rhs += ", "; rhs += b; rhs += ", step(vec4(0.0), "; rhs += c; rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "LRP" ) == 0 ) {
		// LRP dst, t, a, b → dst = mix(b, a, t)
		READ_SRC(rawA); READ_SRC(rawB); READ_SRC(rawC);
		idStr t = TranslateOperand(rawA.c_str()),
		      a = TranslateOperand(rawB.c_str()),
		      b = TranslateOperand(rawC.c_str());
		idStr rhs = "mix("; rhs += b; rhs += ", "; rhs += a; rhs += ", "; rhs += t; rhs += ")";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "SLT" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(lessThan("; rhs += a; rhs += ", "; rhs += b; rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "SGE" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(greaterThanEqual("; rhs += a; rhs += ", "; rhs += b; rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "XPD" ) == 0 ) {
		READ_SRC(rawA); READ_SRC(rawB);
		idStr a = TranslateOperand(rawA.c_str()), b = TranslateOperand(rawB.c_str());
		idStr rhs = "vec4(cross(vec3("; rhs += a; rhs += "), vec3("; rhs += b; rhs += ")), 0.0)";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "TEX" ) == 0 ) {
		READ_SRC(rawA);
		idStr coord = TranslateOperand(rawA.c_str());
		p = SkipCommaOrWhitespace( p );
		idStr texTok; p = ReadToken( p, texTok );
		int unit = 0;
		if ( idStr::Icmpn( texTok.c_str(), "texture[", 8 ) == 0 ) {
			unit = atoi( texTok.c_str() + 8 );
		}
		AddSampler( unit );
		p = SkipCommaOrWhitespace( p );
		idStr typeTok; p = ReadToken( p, typeTok );
		char ubuf[16]; idStr::snPrintf( ubuf, sizeof(ubuf), "uTex%d", unit );
		idStr rhs = "texture("; rhs += ubuf; rhs += ", vec2("; rhs += coord; rhs += "))";
		EMIT(rhs);

	} else if ( idStr::Icmp( baseOp.c_str(), "TXP" ) == 0 ) {
		READ_SRC(rawA);
		idStr coord = TranslateOperand(rawA.c_str());
		p = SkipCommaOrWhitespace( p );
		idStr texTok; p = ReadToken( p, texTok );
		int unit = 0;
		if ( idStr::Icmpn( texTok.c_str(), "texture[", 8 ) == 0 ) {
			unit = atoi( texTok.c_str() + 8 );
		}
		AddSampler( unit );
		p = SkipCommaOrWhitespace( p );
		idStr typeTok; p = ReadToken( p, typeTok );
		char ubuf[16]; idStr::snPrintf( ubuf, sizeof(ubuf), "uTex%d", unit );
		idStr rhs = "textureProj("; rhs += ubuf; rhs += ", vec3("; rhs += coord; rhs += "))";
		EMIT(rhs);

	} else {
		common->Warning( "ARB2GLSL: unknown opcode '%s'", baseOp.c_str() );
		body += "    /* ARB2GLSL: unhandled opcode: ";
		body += opcode.c_str();
		body += " */\n";
	}

	#undef READ_SRC
	#undef EMIT
}

// ---------------------------------------------------------------------------
// First pass — scan declarations and note which attributes/varyings are needed
// ---------------------------------------------------------------------------

static void ScanLine( const char *line ) {
	const char *p = SkipWhitespace( line );

	idStr tok;
	p = ReadToken( p, tok );
	if ( tok.Length() == 0 ) return;

	if ( idStr::Icmp( tok.c_str(), "TEMP" ) == 0 ) {
		// TEMP name1, name2, ...;  (may span to end of line)
		while ( *p ) {
			p = SkipWhitespace( p );
			if ( !*p || *p == '\n' || *p == '\r' || *p == ';' ) break;
			idStr name;
			p = ReadToken( p, name );
			// strip trailing semicolon
			if ( name.Length() > 0 && name[name.Length()-1] == ';' ) {
				name = name.Left( name.Length() - 1 );
			}
			if ( name.Length() > 0 ) {
				s_temps.Append( name );
			}
			p = SkipCommaOrWhitespace( p );
		}
		return;
	}

	if ( idStr::Icmp( tok.c_str(), "PARAM" ) == 0 ) {
		idStr name;
		p = ReadToken( p, name );
		// skip '='
		p = SkipWhitespace( p );
		if ( *p == '=' ) p++;
		p = SkipWhitespace( p );

		arbParam_t param;
		param.name = name;

		if ( *p == '{' ) {
			// Literal: PARAM name = { a, b, c, d };
			param.isEnvAlias = false;
			p++; // skip '{'
			float vals[4] = {0,0,0,0};
			for ( int i = 0; i < 4; i++ ) {
				p = SkipWhitespace( p );
				idStr v; p = ReadToken( p, v );
				// strip trailing comma or brace
				while ( v.Length() > 0 ) {
					char last = v[v.Length()-1];
					if ( last == ',' || last == '}' || last == ';' )
						v = v.Left(v.Length()-1);
					else break;
				}
				vals[i] = (float)atof( v.c_str() );
				p = SkipCommaOrWhitespace( p );
			}
			char buf[128];
			idStr::snPrintf( buf, sizeof(buf), "vec4(%.6g, %.6g, %.6g, %.6g)",
			                 vals[0], vals[1], vals[2], vals[3] );
			param.constInit = buf;
			s_params.Append( param );
		} else if ( idStr::Icmpn( p, "program.env[", 12 ) == 0 ) {
			// Alias: PARAM name = program.env[N];
			param.isEnvAlias = true;
			param.envIndex   = atoi( p + 12 );
			if ( param.envIndex > s_maxEnvIndex ) s_maxEnvIndex = param.envIndex;
			s_params.Append( param );
		}
		// (Other PARAM forms are rare and silently ignored)
		return;
	}

	if ( idStr::Icmp( tok.c_str(), "OPTION" ) == 0 ) {
		idStr opt; p = ReadToken( p, opt );
		if ( idStr::Icmp( opt.c_str(), "ARB_position_invariant;" ) == 0
		     || idStr::Icmp( opt.c_str(), "ARB_position_invariant" ) == 0 ) {
			s_positionInvariant = true;
		}
		return;
	}

	// For instruction lines, scan all tokens after the opcode for known operands
	if ( idStr::Icmp( tok.c_str(), "END" ) == 0 ) return;
	if ( tok[0] == '#' ) return; // comment

	// Skip opcode (already read as tok), skip destination (first operand)
	idStr dst; p = SkipCommaOrWhitespace( p ); p = ReadToken( p, dst );
	ScanOperand( dst.c_str() );

	// Scan source operands
	while ( *p && *p != '\n' && *p != '\r' ) {
		p = SkipCommaOrWhitespace( p );
		if ( !*p || *p == '\n' || *p == '\r' || *p == '#' ) break;
		idStr src; p = ReadToken( p, src );
		if ( src.Length() == 0 ) break;
		// Skip type tokens like "2D" after TEX/TXP
		if ( idStr::Icmp( src.c_str(), "2D" ) == 0 ) break;
		// Handle texture[N] samplers in TEX/TXP
		if ( idStr::Icmpn( src.c_str(), "texture[", 8 ) == 0 ) {
			int unit = atoi( src.c_str() + 8 );
			AddSampler( unit );
			continue;
		}
		ScanOperand( src.c_str() );
	}
}

// ---------------------------------------------------------------------------
// ARB2GLSL_Translate — public entry point
// ---------------------------------------------------------------------------

bool ARB2GLSL_Translate( const char *arbSrc, bool isFragment, idStr &outGLSL ) {
	outGLSL.Clear();
	ResetState();
	s_isFragment = isFragment;

	if ( !arbSrc || !*arbSrc ) {
		common->Warning( "ARB2GLSL_Translate: empty input" );
		return false;
	}

	// Detect and validate header
	bool isVP = ( strstr( arbSrc, "!!ARBvp1.0" ) != NULL );
	bool isFP = ( strstr( arbSrc, "!!ARBfp1.0" ) != NULL );
	if ( !isVP && !isFP ) {
		common->Warning( "ARB2GLSL_Translate: no !!ARBvp1.0 or !!ARBfp1.0 header" );
		return false;
	}
	if ( isFP != isFragment ) {
		common->Warning( "ARB2GLSL_Translate: header/type mismatch" );
		return false;
	}
	if ( strstr( arbSrc, "END" ) == NULL ) {
		common->Warning( "ARB2GLSL_Translate: no END marker" );
		return false;
	}

	// -----------------------------------------------------------------------
	// First pass — line by line, collect declarations and flag operands
	// -----------------------------------------------------------------------
	{
		const char *p = arbSrc;
		while ( *p ) {
			// Skip whitespace before line
			while ( *p == ' ' || *p == '\t' ) p++;
			// Find end of line
			const char *lineEnd = p;
			while ( *lineEnd && *lineEnd != '\n' && *lineEnd != '\r' ) lineEnd++;

			// Make a local copy with null terminator (use idStr)
			idStr line;
			line.Append( p, (int)(lineEnd - p) );

			// Strip inline comment
			int hashPos = line.Find( '#' );
			if ( hashPos >= 0 ) {
				line = line.Left( hashPos );
			}

			ScanLine( line.c_str() );

			p = lineEnd;
			p = SkipNewline( p );
		}
	}

	// VP always needs position
	if ( !isFragment ) {
		s_needsPosition = true;
	}

	// -----------------------------------------------------------------------
	// Emit preamble
	// -----------------------------------------------------------------------
	idStr out;
	out += "#version 300 es\n";
	out += "precision highp float;\n\n";

	// program.env uniform array — use a fixed size so VP and FP always agree,
	// preventing "uniform declared as different types" link errors.
	if ( s_maxEnvIndex >= 0 ) {
		out += "uniform vec4 uProgramEnv[32];\n";
	}

	// state.matrix.mvp rows (used by shaders that don't use ARB_position_invariant)
	if ( s_needsMVPRows ) {
		out += "uniform vec4 uMVPRows[4];\n";
	}

	// Sampler uniforms
	for ( int i = 0; i < s_samplers.Num(); i++ ) {
		out += "uniform sampler2D ";
		out += s_samplers[i].uniformName;
		out += ";\n";
	}

	// uMVP for position-invariant vertex shaders
	if ( !isFragment && s_positionInvariant ) {
		out += "uniform mat4 uMVP;\n";
	}

	// Literal PARAM consts
	for ( int i = 0; i < s_params.Num(); i++ ) {
		if ( !s_params[i].isEnvAlias ) {
			out += "const vec4 ";
			out += s_params[i].name;
			out += " = ";
			out += s_params[i].constInit;
			out += ";\n";
		}
	}

	if ( !isFragment ) {
		// Vertex shader inputs
		if ( s_needsPosition )   out += "in vec4 aPosition;\n";
		if ( s_needsTexCoord0 )  out += "in vec4 aTexCoord0;\n";
		if ( s_needsTexCoord1 )  out += "in vec4 aTexCoord1;\n";
		if ( s_needsNormal )     out += "in vec4 aNormal;\n";
		if ( s_needsTangent )    out += "in vec4 aTangent;\n";
		if ( s_needsBitangent )  out += "in vec4 aBitangent;\n";
		if ( s_needsColor )      out += "in vec4 aColor;\n";
		// Vertex shader outputs — one varying per result.texcoord[N] index written
		for ( int i = 0; i <= s_maxWriteTexCoord; i++ ) {
			char buf[32]; idStr::snPrintf( buf, sizeof(buf), "out vec4 vTexCoord%d;\n", i );
			out += buf;
		}
		if ( s_writesVaryingColor ) out += "out vec4 vColor;\n";
	} else {
		// Fragment shader inputs — one varying per fragment.texcoord[N] index read
		for ( int i = 0; i <= s_maxReadTexCoord; i++ ) {
			char buf[32]; idStr::snPrintf( buf, sizeof(buf), "in vec4 vTexCoord%d;\n", i );
			out += buf;
		}
		if ( s_needsFragColor )    out += "in vec4 vColor;\n";
		// Fragment output (replaces gl_FragColor in GLSL ES 3.00)
		out += "out vec4 fragColor;\n";
	}

	out += "\nvoid main() {\n";

	// TEMP declarations inside main
	for ( int i = 0; i < s_temps.Num(); i++ ) {
		out += "    vec4 ";
		out += s_temps[i];
		out += ";\n";
	}
	if ( s_temps.Num() > 0 ) out += "\n";

	// -----------------------------------------------------------------------
	// Second pass — translate instructions into body
	// -----------------------------------------------------------------------
	idStr body;
	{
		const char *p = arbSrc;
		while ( *p ) {
			while ( *p == ' ' || *p == '\t' ) p++;
			const char *lineEnd = p;
			while ( *lineEnd && *lineEnd != '\n' && *lineEnd != '\r' ) lineEnd++;

			idStr line;
			line.Append( p, (int)(lineEnd - p) );

			// Strip inline comment
			int hashPos = line.Find( '#' );
			if ( hashPos >= 0 ) line = line.Left( hashPos );

			const char *lp = SkipWhitespace( line.c_str() );

			// Skip header, declarations, END
			if ( *lp == '\0' ) { /* blank */ }
			else if ( idStr::Icmpn( lp, "!!ARB", 5 ) == 0 ) { /* header */ }
			else if ( idStr::Icmpn( lp, "TEMP", 4 ) == 0 ) { /* handled in pass 1 */ }
			else if ( idStr::Icmpn( lp, "PARAM", 5 ) == 0 ) { /* handled in pass 1 */ }
			else if ( idStr::Icmpn( lp, "OPTION", 6 ) == 0 ) { /* handled in pass 1 */ }
			else if ( idStr::Icmpn( lp, "ATTRIB", 6 ) == 0 ) { /* skip ATTRIB decls */ }
			else if ( idStr::Icmpn( lp, "OUTPUT", 6 ) == 0 ) { /* skip OUTPUT decls */ }
			else if ( idStr::Icmp( lp, "END" ) == 0 ) { /* done */ }
			else {
				TranslateInstruction( lp, isFragment, body );
			}

			p = lineEnd;
			p = SkipNewline( p );
		}
	}

	// Post-body epilogue for position-invariant VP
	if ( !isFragment && s_positionInvariant ) {
		body += "    gl_Position = uMVP * aPosition;\n";
	}

	out += body;
	out += "}\n";

	outGLSL = out;
	return true;
}

#endif /* GLES3_BACKEND */
