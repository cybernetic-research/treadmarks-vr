// treadmarks-vr: stands in for GLEW + OpenGL 1.x. Every gl* call the game makes is renamed to tmgl_* and handled by
// shim/gl1.cpp, which batches the immediate-mode calls, records the frame and draws it with WebGL 2 (once for the
// flat screen, or once per eye in VR). Constant values are the standard OpenGL ones.
#ifndef TMVR_GLEW_H
#define TMVR_GLEW_H

// shim/gl1.cpp includes this with TMGL_IMPL defined (after the real WebGL headers) to get the prototypes only.
#ifndef TMGL_IMPL
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_ZERO 0
#define GL_ONE 1

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007
#define GL_QUAD_STRIP 0x0008
#define GL_POLYGON 0x0009

#define GL_NEVER 0x0200
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_GREATER 0x0204
#define GL_NOTEQUAL 0x0205
#define GL_GEQUAL 0x0206
#define GL_ALWAYS 0x0207

#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307

#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_CW 0x0900
#define GL_CCW 0x0901

#define GL_CULL_FACE 0x0B44
#define GL_LIGHTING 0x0B50
#define GL_FOG 0x0B60
#define GL_FOG_DENSITY 0x0B62
#define GL_FOG_START 0x0B63
#define GL_FOG_END 0x0B64
#define GL_FOG_MODE 0x0B65
#define GL_FOG_COLOR 0x0B66
#define GL_DEPTH_TEST 0x0B71
#define GL_ALPHA_TEST 0x0BC0
#define GL_BLEND 0x0BE2
#define GL_TEXTURE_2D 0x0DE1
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_EXP 0x0800
#define GL_EXP2 0x0801

#define GL_UNPACK_SWAP_BYTES 0x0CF0
#define GL_UNPACK_LSB_FIRST 0x0CF1
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_SKIP_ROWS 0x0CF3
#define GL_UNPACK_SKIP_PIXELS 0x0CF4
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_MAX_TEXTURE_SIZE 0x0D33

#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001

#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406

#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702

#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190A
#define GL_POINT 0x1B00
#define GL_LINE 0x1B01
#define GL_FILL 0x1B02
#define GL_FLAT 0x1D00
#define GL_SMOOTH 0x1D01
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_REPLACE 0x1E01
#define GL_ADD 0x0104

#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_TEXTURE_ENV 0x2300
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_NEAREST 0x2701
#define GL_NEAREST_MIPMAP_LINEAR 0x2702
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_CLAMP 0x2900
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F

#define GL_R3_G3_B2 0x2A10
#define GL_RGB4 0x804F
#define GL_RGB5 0x8050
#define GL_RGB8 0x8051
#define GL_RGBA4 0x8056
#define GL_RGB5_A1 0x8057
#define GL_RGBA8 0x8058
#define GL_BGRA_EXT 0x80E1
#define GL_RGBA_S3TC 0x83A2
#define GL_COMPRESSED_RGBA_ARB 0x84EE

#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE0_ARB GL_TEXTURE0
#define GL_TEXTURE1_ARB GL_TEXTURE1

#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000

#define GL_NO_ERROR 0
#endif // TMGL_IMPL

#ifdef __cplusplus
extern "C" {
#endif

void tmgl_Begin(GLenum mode);
void tmgl_End(void);
void tmgl_Vertex2f(GLfloat x, GLfloat y);
void tmgl_Vertex2fv(const GLfloat* v);
void tmgl_Vertex3f(GLfloat x, GLfloat y, GLfloat z);
void tmgl_Vertex3fv(const GLfloat* v);
void tmgl_TexCoord2f(GLfloat s, GLfloat t);
void tmgl_TexCoord2fv(const GLfloat* v);
void tmgl_MultiTexCoord2f(GLenum unit, GLfloat s, GLfloat t);
void tmgl_Color3f(GLfloat r, GLfloat g, GLfloat b);
void tmgl_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void tmgl_Color4fv(const GLfloat* v);
void tmgl_Normal3f(GLfloat x, GLfloat y, GLfloat z);

void tmgl_Enable(GLenum cap);
void tmgl_Disable(GLenum cap);
void tmgl_GetBooleanv(GLenum pname, GLboolean* params);
void tmgl_GetIntegerv(GLenum pname, GLint* params);
const GLubyte* tmgl_GetString(GLenum name);
GLenum tmgl_GetError(void);

void tmgl_MatrixMode(GLenum mode);
void tmgl_LoadIdentity(void);
void tmgl_LoadMatrixf(const GLfloat* m);
void tmgl_MultMatrixf(const GLfloat* m);
void tmgl_PushMatrix(void);
void tmgl_PopMatrix(void);
void tmgl_Rotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void tmgl_Translatef(GLfloat x, GLfloat y, GLfloat z);
void tmgl_Scalef(GLfloat x, GLfloat y, GLfloat z);
void tmgl_Frustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void tmgl_Ortho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void tmgl_Viewport(GLint x, GLint y, GLsizei w, GLsizei h);

void tmgl_Clear(GLbitfield mask);
void tmgl_ClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void tmgl_DepthMask(GLboolean flag);
void tmgl_DepthFunc(GLenum func);
void tmgl_BlendFunc(GLenum sfactor, GLenum dfactor);
void tmgl_AlphaFunc(GLenum func, GLclampf ref);
void tmgl_CullFace(GLenum mode);
void tmgl_FrontFace(GLenum mode);
void tmgl_PolygonMode(GLenum face, GLenum mode);
void tmgl_PolygonOffset(GLfloat factor, GLfloat units);
void tmgl_ShadeModel(GLenum mode);
void tmgl_PointSize(GLfloat size);

void tmgl_Fogf(GLenum pname, GLfloat param);
void tmgl_Fogi(GLenum pname, GLint param);
void tmgl_Fogfv(GLenum pname, const GLfloat* params);

void tmgl_TexEnvi(GLenum target, GLenum pname, GLint param);
void tmgl_TexEnvf(GLenum target, GLenum pname, GLfloat param);
void tmgl_ActiveTexture(GLenum texture);
void tmgl_GenTextures(GLsizei n, GLuint* textures);
void tmgl_DeleteTextures(GLsizei n, const GLuint* textures);
void tmgl_BindTexture(GLenum target, GLuint texture);
void tmgl_TexParameteri(GLenum target, GLenum pname, GLint param);
void tmgl_PixelStorei(GLenum pname, GLint param);
void tmgl_TexImage2D(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border,
	GLenum format, GLenum type, const GLvoid* pixels);
void tmgl_TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
	GLenum format, GLenum type, const GLvoid* pixels);
void tmgl_GetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params);
void tmgl_ReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, GLvoid* pixels);

// treadmarks-vr additions, called from the platform layer and the few marked places in the game.
enum { TMGL_PHASE_WORLD = 1, TMGL_PHASE_HUD = 2 };
void tmgl_Phase(int phase);           // which layer the following draws belong to (3D world or 2D screen)
void tmgl_Init(int width, int height); // after the WebGL 2 context exists
void tmgl_EndFrame(void);              // draws the recorded frame (flat screen, or both eyes in VR)

#ifdef __cplusplus
}
#endif

#ifndef TMGL_IMPL
#define glBegin tmgl_Begin
#define glEnd tmgl_End
#define glVertex2f tmgl_Vertex2f
#define glVertex2fv tmgl_Vertex2fv
#define glVertex3f tmgl_Vertex3f
#define glVertex3fv tmgl_Vertex3fv
#define glTexCoord2f tmgl_TexCoord2f
#define glTexCoord2fv tmgl_TexCoord2fv
#define glMultiTexCoord2f tmgl_MultiTexCoord2f
#define glMultiTexCoord2fARB tmgl_MultiTexCoord2f
#define glColor3f tmgl_Color3f
#define glColor4f tmgl_Color4f
#define glColor4fv tmgl_Color4fv
#define glNormal3f tmgl_Normal3f
#define glEnable tmgl_Enable
#define glDisable tmgl_Disable
#define glGetBooleanv tmgl_GetBooleanv
#define glGetIntegerv tmgl_GetIntegerv
#define glGetString tmgl_GetString
#define glGetError tmgl_GetError
#define glMatrixMode tmgl_MatrixMode
#define glLoadIdentity tmgl_LoadIdentity
#define glLoadMatrixf tmgl_LoadMatrixf
#define glMultMatrixf tmgl_MultMatrixf
#define glPushMatrix tmgl_PushMatrix
#define glPopMatrix tmgl_PopMatrix
#define glRotatef tmgl_Rotatef
#define glTranslatef tmgl_Translatef
#define glScalef tmgl_Scalef
#define glFrustum tmgl_Frustum
#define glOrtho tmgl_Ortho
#define glViewport tmgl_Viewport
#define glClear tmgl_Clear
#define glClearColor tmgl_ClearColor
#define glDepthMask tmgl_DepthMask
#define glDepthFunc tmgl_DepthFunc
#define glBlendFunc tmgl_BlendFunc
#define glAlphaFunc tmgl_AlphaFunc
#define glCullFace tmgl_CullFace
#define glFrontFace tmgl_FrontFace
#define glPolygonMode tmgl_PolygonMode
#define glPolygonOffset tmgl_PolygonOffset
#define glShadeModel tmgl_ShadeModel
#define glPointSize tmgl_PointSize
#define glFogf tmgl_Fogf
#define glFogi tmgl_Fogi
#define glFogfv tmgl_Fogfv
#define glTexEnvi tmgl_TexEnvi
#define glTexEnvf tmgl_TexEnvf
#define glActiveTexture tmgl_ActiveTexture
#define glActiveTextureARB tmgl_ActiveTexture
#define glGenTextures tmgl_GenTextures
#define glDeleteTextures tmgl_DeleteTextures
#define glBindTexture tmgl_BindTexture
#define glTexParameteri tmgl_TexParameteri
#define glPixelStorei tmgl_PixelStorei
#define glTexImage2D tmgl_TexImage2D
#define glTexSubImage2D tmgl_TexSubImage2D
#define glGetTexLevelParameteriv tmgl_GetTexLevelParameteriv
#define glReadPixels tmgl_ReadPixels

#define GLEW_ARB_multitexture 1
#define GLEW_EXT_compiled_vertex_array 0
#define GLEW_EXT_texture_edge_clamp 1
#define GLEW_S3_s3tc 0
#define GLEW_ARB_texture_compression 0
static inline GLenum glewInit(void) { return 0; }
#endif // TMGL_IMPL

#endif
