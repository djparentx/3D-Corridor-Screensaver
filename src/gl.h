#ifndef GL_LOADER_H
#define GL_LOADER_H

#include <GLES2/gl2.h>

#define GL_FUNCS(X) \
    X(PFNGLACTIVETEXTUREPROC,            ActiveTexture) \
    X(PFNGLATTACHSHADERPROC,             AttachShader) \
    X(PFNGLBINDATTRIBLOCATIONPROC,       BindAttribLocation) \
    X(PFNGLBINDBUFFERPROC,               BindBuffer) \
    X(PFNGLBINDTEXTUREPROC,              BindTexture) \
    X(PFNGLBLENDFUNCPROC,                BlendFunc) \
    X(PFNGLBUFFERDATAPROC,               BufferData) \
    X(PFNGLCLEARPROC,                    Clear) \
    X(PFNGLCLEARCOLORPROC,               ClearColor) \
    X(PFNGLCOMPILESHADERPROC,            CompileShader) \
    X(PFNGLCREATEPROGRAMPROC,            CreateProgram) \
    X(PFNGLCREATESHADERPROC,             CreateShader) \
    X(PFNGLCULLFACEPROC,                 CullFace) \
    X(PFNGLDELETEBUFFERSPROC,            DeleteBuffers) \
    X(PFNGLDELETEPROGRAMPROC,            DeleteProgram) \
    X(PFNGLDELETESHADERPROC,             DeleteShader) \
    X(PFNGLDELETETEXTURESPROC,           DeleteTextures) \
    X(PFNGLDEPTHFUNCPROC,                DepthFunc) \
    X(PFNGLDISABLEPROC,                  Disable) \
    X(PFNGLDRAWARRAYSPROC,               DrawArrays) \
    X(PFNGLDRAWELEMENTSPROC,             DrawElements) \
    X(PFNGLENABLEPROC,                   Enable) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC,  EnableVertexAttribArray) \
    X(PFNGLGENBUFFERSPROC,               GenBuffers) \
    X(PFNGLGENTEXTURESPROC,              GenTextures) \
    X(PFNGLGENERATEMIPMAPPROC,           GenerateMipmap) \
    X(PFNGLGETERRORPROC,                 GetError) \
    X(PFNGLGETPROGRAMINFOLOGPROC,        GetProgramInfoLog) \
    X(PFNGLGETPROGRAMIVPROC,             GetProgramiv) \
    X(PFNGLGETSHADERINFOLOGPROC,         GetShaderInfoLog) \
    X(PFNGLGETSHADERIVPROC,              GetShaderiv) \
    X(PFNGLGETSTRINGPROC,                GetString) \
    X(PFNGLGETUNIFORMLOCATIONPROC,       GetUniformLocation) \
    X(PFNGLLINKPROGRAMPROC,              LinkProgram) \
    X(PFNGLSHADERSOURCEPROC,             ShaderSource) \
    X(PFNGLTEXIMAGE2DPROC,               TexImage2D) \
    X(PFNGLTEXPARAMETERIPROC,            TexParameteri) \
    X(PFNGLUNIFORM1FPROC,                Uniform1f) \
    X(PFNGLUNIFORM1IPROC,                Uniform1i) \
    X(PFNGLUNIFORM2FPROC,                Uniform2f) \
    X(PFNGLUNIFORM3FPROC,                Uniform3f) \
    X(PFNGLUNIFORMMATRIX4FVPROC,         UniformMatrix4fv) \
    X(PFNGLUSEPROGRAMPROC,               UseProgram) \
    X(PFNGLVERTEXATTRIBPOINTERPROC,      VertexAttribPointer) \
    X(PFNGLVIEWPORTPROC,                 Viewport)

struct gl_api {
#define X(type, name) type name;
    GL_FUNCS(X)
#undef X
};

extern struct gl_api gl;

int gl_load(void);

#endif
