#if !defined( TINYGL_H )

#include <stdint.h>

#define TG_OFFSET_OF( type, member ) ((uint32_t)((size_t)(&((type*)0)->member)))
#define TG_DEBUG_CHECKS 1

enum
{
	TG_FLOAT,
	TG_INT,
	TG_BOOL,
	TG_SAMPLER,
	TG_UNKNOWN,
};

typedef struct
{
	const char* name;
	uint32_t hash;
	uint32_t size;
	uint32_t type;
	uint32_t offset;
	uint32_t location;
} tgVertexAttribute;

#define TG_ATTRIBUTE_MAX_COUNT 16
typedef struct
{
	uint32_t buffer_size;
	uint32_t vertex_stride;
	uint32_t primitive;
	uint32_t usage;

	uint32_t attribute_count;
	tgVertexAttribute attributes[ TG_ATTRIBUTE_MAX_COUNT ];
} tgVertexData;

typedef struct
{
	union
	{
		struct
		{
			uint64_t fullscreen    : 2;
			uint64_t hud           : 5;
			uint64_t depth         : 25;
			uint64_t translucency  : 32;
		};

		uint64_t key;
	};
} tgRenderState;


struct tgShader;
typedef struct tgShader tgShader;

typedef struct
{
	tgVertexData data;
	tgShader* program;
	tgRenderState state;
	uint32_t attributeCount;

	uint32_t index0;
	uint32_t index1;
	uint32_t buffer_number;
	uint32_t need_new_sync;
	uint32_t buffer_count;
	uint32_t buffers[ 3 ];
	GLsync fences[ 3 ];
} tgRenderable;

#define TG_UNIFORM_NAME_LENGTH 64
#define TG_UNIFORM_MAX_COUNT 16

typedef struct
{
	char name[ TG_UNIFORM_NAME_LENGTH ];
	uint32_t id;
	uint32_t hash;
	uint32_t size;
	uint32_t type;
	uint32_t location;
} tgUniform;

struct tgShader
{
	uint32_t program;
	uint32_t uniform_count;
	tgUniform uniforms[ TG_UNIFORM_MAX_COUNT ];
};

typedef struct
{
	uint32_t vert_count;
	void* verts;
	tgRenderable* r;
	uint32_t texture_count;
	uint32_t textures[ 8 ];
} tgDrawCall;

void* tgMakeCtx( uint32_t max_draw_calls );
void tgFreeCtx( void* ctx );

void tgMakeVertexData( tgVertexData* vd, uint32_t buffer_size, uint32_t vertex_stride, uint32_t primitive, uint32_t usage );
void tgAddAttribute( tgVertexData* vd, char* name, uint32_t size, uint32_t type, uint32_t offset );
void tgMakeRenderable( tgRenderable* r, tgVertexData* vd );

// Must be called after tgMakeRenderable
void tgSetShader( tgRenderable* r, tgShader* s );
void tgLoadShader( tgShader* s, const char* vertex, const char* pixel );
void tgFreeShader( tgShader* s );

// TODO: Think about how glUseProgram affects these functions, maybe some error
//       checking to make sure that sending doesn't happen if SetActive was not called
void tgSetActiveShader( tgShader* s );
void tgDeactivateShader( );
void tgSendF32( tgShader* s, char* uniform_name, uint32_t size, float* floats, uint32_t count );
void tgSendMatrix( tgShader* s, char* uniform_name, float* floats );
void tgSendTexture( tgShader* s, char* uniform_name, uint32_t index );

void tgPushDrawCall( void* ctx, tgDrawCall call );
void tgPresent( void* ctx );

typedef void (*tgSwapBuffers)( );
void tgFlush( void* ctx, tgSwapBuffers swap );

void tgPerspective( float* m, float y_fov_radians, float aspect, float n, float f );

#if TG_DEBUG_CHECKS

	#define TG_PRINT_GL_ERRORS( ) tgPrintGLErrors_internal( __FILE__, __LINE__ )
	void tgPrintGLErrors_internal( char* file, uint32_t line );

#endif

#define TINYGL_H
#endif

#ifdef TINYGL_IMPL

#if TG_DEBUG_CHECKS

	#include <stdio.h>
	#include <assert.h>
	#define TG_ASSERT assert

#else

	#define TG_ASSERT( ... )

#endif

typedef struct
{
	uint32_t max_draw_calls;
	uint32_t count;
	tgDrawCall* calls;
} tgContext;

#include <stdlib.h> // malloc, free, NULL
#include <string.h> // memset

void* tgMakeCtx( uint32_t max_draw_calls )
{
	tgContext* ctx = (tgContext*)malloc( sizeof( tgContext ) );
	ctx->max_draw_calls = max_draw_calls;
	ctx->count = 0;
	ctx->calls = (tgDrawCall*)malloc( sizeof( tgDrawCall ) * 1024 );
	if ( !ctx->calls )
	{
		free( ctx );
		return 0;
	}
	return ctx;
}

void tgFreeCtx( void* ctx )
{
	tgContext* context = (tgContext*)ctx;
	free( context->calls );
	free( context );
}

static uint32_t tg_djb2( unsigned char* str )
{
	uint32_t hash = 5381;
	int c;

	while ( c = *str++ )
		hash = ((hash << 5) + hash) + c;

	return hash;
}

void tgMakeVertexData( tgVertexData* vd, uint32_t buffer_size, uint32_t vertex_stride, uint32_t primitive, uint32_t usage )
{
	vd->buffer_size = buffer_size;
	vd->vertex_stride = vertex_stride;
	vd->primitive = primitive;
	vd->usage = usage;
	vd->attribute_count = 0;
}

static uint32_t tgGetGLType( uint32_t type )
{
	switch ( type )
	{
	case GL_INT:
	case GL_INT_VEC2:
	case GL_INT_VEC3:
	case GL_INT_VEC4:
		return TG_INT;

	case GL_FLOAT:
	case GL_FLOAT_VEC2:
	case GL_FLOAT_VEC3:
	case GL_FLOAT_VEC4:
	case GL_FLOAT_MAT2:
	case GL_FLOAT_MAT3:
	case GL_FLOAT_MAT4:
		return TG_FLOAT;

	case GL_BOOL:
	case GL_BOOL_VEC2:
	case GL_BOOL_VEC3:
	case GL_BOOL_VEC4:
		return TG_BOOL;

	case GL_SAMPLER_1D:
	case GL_SAMPLER_2D:
	case GL_SAMPLER_3D:
		return TG_SAMPLER;

	default:
		return TG_UNKNOWN;
	}
}

void tgAddAttribute( tgVertexData* vd, char* name, uint32_t size, uint32_t type, uint32_t offset )
{
	tgVertexAttribute va;
	va.name = name;
	va.hash = tg_djb2( name );
	va.size = size;
	va.type = type;
	va.offset = offset;

	TG_ASSERT( vd->attribute_count < TG_ATTRIBUTE_MAX_COUNT );
	vd->attributes[ vd->attribute_count++ ] = va;
}

void tgMakeRenderable( tgRenderable* r, tgVertexData* vd )
{
	r->data = *vd;
	r->index0 = 0;
	r->index1 = 0;
	r->buffer_number = 0;
	r->need_new_sync = 0;
	r->program = 0;
	r->state.key = 0;;

	if ( vd->usage == GL_STATIC_DRAW )
	{
		r->buffer_count = 1;
		r->need_new_sync = 1;
	}
	else r->buffer_count = 3;
}

// WARNING: Messes with GL global state via glUnmapBuffer( GL_ARRAY_BUFFER ) and
// glBindBuffer( GL_ARRAY_BUFFER, ... ), so call tgMap, fill in data, then call tgUnmap.
void* tgMap( tgRenderable* r, uint32_t count )
{
	// Cannot map a buffer when the buffer is too small
	// Make your buffer is bigger or draw less data
	TG_ASSERT( count <= r->data.buffer_size );

	uint32_t newIndex = r->index1 + count;

	if ( newIndex > r->data.buffer_size )
	{
		// should never overflow a static buffer
		TG_ASSERT( r->data.usage != GL_STATIC_DRAW );

		++r->buffer_number;
		r->buffer_number %= r->buffer_count;
		GLsync fence = r->fences[ r->buffer_number ];

		// Ensure buffer is not in use by GPU
		// If we stall here we are GPU bound
		GLenum result = glClientWaitSync( fence, 0, (GLuint64)1000000000 );
		TG_ASSERT( result != GL_TIMEOUT_EXPIRED );
		TG_ASSERT( result != GL_WAIT_FAILED );
		glDeleteSync( fence );

		r->index0 = 0;
		r->index1 = count;
		r->need_new_sync = 1;
	}

	else
	{
		r->index0 = r->index1;
		r->index1 = newIndex;
	}

	glBindBuffer( GL_ARRAY_BUFFER, r->buffers[ r->buffer_number ] );
	uint32_t stream_size = (r->index1 - r->index0) * r->data.vertex_stride;
	void* memory = glMapBufferRange( GL_ARRAY_BUFFER, r->index0 * r->data.vertex_stride, stream_size, GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT );

#if TG_DEBUG_CHECKS
	if ( !memory )
	{
		printf( "\n%u\n", glGetError( ) );
		TG_ASSERT( memory );
	}
#endif

	return memory;
}

void tgUnmap( )
{
	glUnmapBuffer( GL_ARRAY_BUFFER );
}

void tgSetShader( tgRenderable* r, tgShader* program )
{
	// Cannot set the shader of a Renderable more than once
	TG_ASSERT( !r->program );

	r->program = program;
	glGetProgramiv( program->program, GL_ACTIVE_ATTRIBUTES, &r->attributeCount );

#if TG_DEBUG_CHECKS
	if ( r->attributeCount != r->data.attribute_count )
	{
		printf( "Mismatch between VertexData attribute count (%d), and shader attributeCount (%d).\n",
				r->attributeCount,
				r->data.attribute_count );
		TG_ASSERT( 0 );
	}
#endif

	uint32_t size;
	uint32_t type;
	char buffer[ 256 ];
	uint32_t hash;

	// Query and set all attribute locations as defined by the shader linking
	for ( uint32_t i = 0; i < r->attributeCount; ++i )
	{
		glGetActiveAttrib( program->program, i, 256, 0, &size, (GLenum*)&type, buffer );
		hash = tg_djb2( buffer );
		type = tgGetGLType( type );

#if TG_DEBUG_CHECKS
		tgVertexAttribute* a = 0;

		// Make sure data.AddAttribute( name, ... ) has matching named attribute
		// Also make sure the GL::Type matches
		// This helps to catch common mismatch errors between glsl and C++
		for ( uint32_t j = 0; j < r->data.attribute_count; ++j )
		{
			tgVertexAttribute* b = r->data.attributes + j;

			if ( b->hash == hash )
			{
				a = b;
				break;
			}
		}
#endif

		// Make sure the user did not have a mismatch between VertexData
		// attributes and the attributes defined in the vertex shader
		TG_ASSERT( a );
		TG_ASSERT( a->type == type );

		a->location = i;
	}

	// Generate VBOs and initialize fences
	uint32_t usage = r->data.usage;

	for ( uint32_t i = 0; i < r->buffer_count; ++i )
	{
		GLuint* buffer = (GLuint*)r->buffers + i;

		glGenBuffers( 1, buffer );
		glBindBuffer( GL_ARRAY_BUFFER, *buffer );
		glBufferData( GL_ARRAY_BUFFER, r->data.buffer_size * r->data.vertex_stride, NULL, usage );
		r->fences[ i ] = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
	}

	glBindBuffer( GL_ARRAY_BUFFER, 0 );
}

GLuint tgCompileShader( const char* Shader, uint32_t type )
{
	GLuint handle = glCreateShader( type );
	glShaderSource( handle, 1, (const GLchar**)&Shader, NULL );
	glCompileShader( handle );

	uint32_t compiled;
	glGetShaderiv( handle, GL_COMPILE_STATUS, &compiled );

#if TG_DEBUG_CHECKS
	if ( !compiled )
	{
		printf( "Shader of type %d failed compilation.\n", type );
		char out[ 2000 ];
		GLsizei outLen;
		glGetShaderInfoLog( handle, 2000, &outLen, out );
		printf( "%s\n", out );
		TG_ASSERT( 0 );
	}
#endif

	return handle;
}

void tgLoadShader( tgShader* s, const char* vertex, const char* pixel )
{
	// Compile vertex and pixel Shader
	uint32_t program = glCreateProgram( );
	uint32_t vs = tgCompileShader( vertex, GL_VERTEX_SHADER );
	uint32_t ps = tgCompileShader( pixel, GL_FRAGMENT_SHADER );
	glAttachShader( program, vs );
	glAttachShader( program, ps );

	// Link the Shader to form a program
	glLinkProgram( program );

	uint32_t linked;
	glGetProgramiv( program, GL_LINK_STATUS, &linked );

#if TG_DEBUG_CHECKS
	if ( !linked )
	{
		printf( "Shaders failed to link.\n" );
		char out[ 2000 ];
		GLsizei outLen;
		glGetProgramInfoLog( program, 2000, &outLen, out );
		printf( "%s\n", out );
		TG_ASSERT( 0 );
	}
#endif

	glDetachShader( program, vs );
	glDetachShader( program, ps );

	// Insert Shader into the Shaders array for future lookups
	s->program = program;

	// Query Uniform information and fill out the Shader Uniforms
	GLint uniform_count;
	uint32_t nameSize = sizeof( char ) * TG_UNIFORM_NAME_LENGTH;
	glGetProgramiv( program, GL_ACTIVE_UNIFORMS, &uniform_count );
	TG_ASSERT( uniform_count < TG_UNIFORM_MAX_COUNT );
	s->uniform_count = uniform_count;

	for ( uint32_t i = 0; i < (uint32_t)uniform_count; ++i )
	{
		uint32_t nameLength;
		tgUniform u;

		glGetActiveUniform( program, (GLint)i, nameSize, &nameLength, &u.size, (GLenum*)&u.type, u.name );

		// Uniform named in a Shader is too long for the UNIFORM_NAME_LENGTH constant
		TG_ASSERT( nameLength <= TG_UNIFORM_NAME_LENGTH );

		u.location = glGetUniformLocation( program, u.name );
		u.type = tgGetGLType( u.type );
		u.hash = tg_djb2( u.name );
		u.id = i;

		// @TODO: Perhaps need to handle appended [0] to Uniform names?

		s->uniforms[ i ] = u;
	}

#if TG_DEBUG_CHECKS
	// prevent hash collisions
	for ( uint32_t i = 0; i < (uint32_t)uniform_count; ++i )
		for ( uint32_t j = i + 1; j < (uint32_t)uniform_count; ++j )
			TG_ASSERT( s->uniforms[ i ].hash != s->uniforms[ j ].hash );
#endif
}

void tgFreeShader( tgShader* s )
{
	glDeleteProgram( s->program );
	memset( s, 0, sizeof( tgShader ) );
}

tgUniform* tgFindUniform( tgShader* s, char* name )
{
	uint32_t uniform_count = s->uniform_count;
	tgUniform* uniforms = s->uniforms;
	uint32_t hash = tg_djb2( name );

	for ( uint32_t i = 0; i < uniform_count; ++i )
	{
		tgUniform* u = uniforms + i;

		if ( u->hash == hash )
		{
			return u;
		}
	}

	return 0;
}

void tgSetActiveShader( tgShader* s )
{
	glUseProgram( s->program );
}

void tgDeactivateShader( )
{
	glUseProgram( 0 );
}

void tgSendF32( tgShader* s, char* uniform_name, uint32_t size, float* floats, uint32_t count )
{
	tgUniform* u = tgFindUniform( s, uniform_name );

	TG_ASSERT( u );
	TG_ASSERT( size == u->size );
	TG_ASSERT( u->type == TG_FLOAT );

	switch ( count )
	{
	case 1:
		glUniform1f( u->location, floats[ 0 ] );
		break;

	case 2:
		glUniform2f( u->location, floats[ 0 ], floats[ 1 ] );
		break;

	case 3:
		glUniform3f( u->location, floats[ 0 ], floats[ 1 ], floats[ 2 ] );
		break;

	case 4:
	{
		glUniform4f( u->location, floats[ 0 ], floats[ 1 ], floats[ 2 ], 1.0f );
	}	break;

	default:
		TG_ASSERT( 0 );
		break;
	}
}

void tgSendMatrix( tgShader* s, char* uniform_name, float* floats )
{
	tgUniform* u = tgFindUniform( s, uniform_name );

	TG_ASSERT( u );
	TG_ASSERT( u->size == 1 );
	TG_ASSERT( u->type == TG_FLOAT );

	glUniformMatrix4fv( u->id, 1, 0, floats );
}

void tgSendTexture( tgShader* s, char* uniform_name, uint32_t index )
{
	tgUniform* u = tgFindUniform( s, uniform_name );

	TG_ASSERT( u );
	TG_ASSERT( u->type == TG_SAMPLER );

	glUniform1i( u->location, index );
}

static uint32_t tgCallSortPred( tgDrawCall* a, tgDrawCall* b )
{
	return a->r->state.key < b->r->state.key;
}

static void tgQSort( tgDrawCall* items, uint32_t count )
{
	if ( count <= 1 ) return;

	tgDrawCall pivot = items[ count - 1 ];
	uint32_t low = 0;
	for ( uint32_t i = 0; i < count - 1; ++i )
	{
		if ( tgCallSortPred( items + i, &pivot ) )
		{
			tgDrawCall tmp = items[ i ];
			items[ i ] = items[ low ];
			items[ low ] = tmp;
			low++;
		}
	}

	items[ count - 1 ] = items[ low ];
	items[ low ] = pivot;
	tgQSort( items, low );
	tgQSort( items + low + 1, count - 1 - low );
}

void tgPushDrawCall( void* ctx, tgDrawCall call )
{
	tgContext* context = (tgContext*)ctx;
	TG_ASSERT( context->count < context->max_draw_calls );
	context->calls[ context->count++ ] = call;
}

uint32_t tgGetGLEnum( uint32_t type )
{
	switch ( type )
	{
	case TG_FLOAT:
		return GL_FLOAT;
		break;

	case TG_INT:
		return GL_UNSIGNED_BYTE;
		break;

	default:
		TG_ASSERT( 0 );
		return ~0;
	}
}

void tgDoMap( tgDrawCall* call, tgRenderable* render )
{
	uint32_t count = call->vert_count;
	void* driver_memory = tgMap( render, count );
	memcpy( driver_memory, call->verts, render->data.vertex_stride * count );
	tgUnmap( );
}

static void tgRender( tgDrawCall* call )
{
	tgRenderable* render = call->r;
	uint32_t texture_count = call->texture_count;
	uint32_t* textures = call->textures;

	if ( render->data.usage == GL_STATIC_DRAW )
	{
		if ( render->need_new_sync )
		{
			render->need_new_sync = 0;
			tgDoMap( call, render );
		}
	}
	else tgDoMap( call, render );

	tgVertexData* data = &render->data;
	tgVertexAttribute* attributes = data->attributes;
	uint32_t vertexStride = data->vertex_stride;
	uint32_t attributeCount = data->attribute_count;

	tgSetActiveShader( render->program );

	uint32_t bufferNumber = render->buffer_number;
	uint32_t buffer = render->buffers[ bufferNumber ];
	glBindBuffer( GL_ARRAY_BUFFER, buffer );

	for ( uint32_t i = 0; i < attributeCount; ++i )
	{
		tgVertexAttribute* attribute = attributes + i;

		uint32_t location = attribute->location;
		uint32_t size = attribute->size;
		uint32_t type = tgGetGLEnum( attribute->type );
		uint32_t offset = attribute->offset;

		glEnableVertexAttribArray( location );
		glVertexAttribPointer( location, size, type, GL_FALSE, vertexStride, (void*)((size_t)offset) );
	}

	for ( uint32_t i = 0; i < texture_count; ++i )
	{
		uint32_t gl_id = textures[ i ];

		glActiveTexture( GL_TEXTURE0 + i );
		glBindTexture( GL_TEXTURE_2D, gl_id );
	}

	uint32_t streamOffset = render->index0;
	uint32_t streamSize = render->index1 - streamOffset;
	glDrawArrays( data->primitive, streamOffset, streamSize );

	if ( render->need_new_sync )
	{
		// @TODO: This shouldn't be called for static buffers, only needed for streaming.
		render->fences[ bufferNumber ] = glFenceSync( GL_SYNC_GPU_COMMANDS_COMPLETE, 0 );
		render->need_new_sync = 0;
	}

	for ( uint32_t i = 0; i < attributeCount; ++i )
	{
		tgVertexAttribute* attribute = attributes + i;

		uint32_t location = attribute->location;
		glDisableVertexAttribArray( location );
	}

	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glUseProgram( 0 );
}

void tgPresent( void* ctx )
{
	tgContext* context = (tgContext*)ctx;
	glClear( GL_COLOR_BUFFER_BIT );

	tgQSort( context->calls, context->count );

	// flush all draw calls to the GPU
	for ( uint32_t i = 0; i < context->count; ++i )
	{
		tgDrawCall* call = context->calls + i;
		tgRender( call );
	}
}

void tgFlush( void* ctx, tgSwapBuffers swap )
{
	tgPresent( ctx );
	tgContext* context = (tgContext*)ctx;
	context->count = 0;
	swap( );
}

#include <math.h>

void tgPerspective( float* m, float y_fov_radians, float aspect, float n, float f )
{
	float a = 1.0f / (float) tanf( y_fov_radians / 2.0f );

	m[ 0 ] = a / aspect;
	m[ 1 ] = 0;
	m[ 2 ] = 0;
	m[ 3 ] = 0;

	m[ 4 ] = 0;
	m[ 5 ] = a;
	m[ 6 ] = 0;
	m[ 7 ] = 0;

	m[ 8 ] = 0;
	m[ 9 ] = 0;
	m[ 10 ] = -((f + n) / (f - n));
	m[ 11 ] = -1.0f;

	m[ 12 ] = 0;
	m[ 13 ] = 0;
	m[ 14 ] = -((2.0f * f * n) / (f - n));
	m[ 15 ] = 0;
}

#pragma comment( lib, "glu32.lib" )

#if TG_DEBUG_CHECKS
#include <GL/GLU.h>
void tgPrintGLErrors_internal( char* file, uint32_t line )
{
	GLenum code = glGetError( );

	if ( code != GL_NO_ERROR )
	{
		char* last_slash = file;

		while ( *file )
		{
			char c = *file;
			if ( c == '\\' || c == '/' )
				last_slash = file + 1;
			++file;
		}

		const char* str = gluErrorString( code );
		printf( "OpenGL Error %s ( %u ): %u, %s\n", last_slash, line, code, str );
	}
}
#endif

#endif // TINYGL_IMPL
