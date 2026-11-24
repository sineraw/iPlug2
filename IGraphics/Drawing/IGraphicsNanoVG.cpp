/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for  more info.

 ==============================================================================
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "IGraphicsNanoVG.h"
#include "ITextEntryControl.h"

#if defined IGRAPHICS_GL
  #if defined OS_MAC
    #if defined IGRAPHICS_GL2
      #define NANOVG_GL2_IMPLEMENTATION
    #elif defined IGRAPHICS_GL3
      #include <OpenGL/gl3.h>
      #define NANOVG_GL3_IMPLEMENTATION
    #elif defined IGRAPHICS_GLES2
      #include <libGLESv2/angle_gl.h>
      #define NANOVG_GLES2_IMPLEMENTATION
    #elif defined IGRAPHICS_GLES3
      #include <libGLESv2/angle_gl.h>
      #define NANOVG_GLES3_IMPLEMENTATION
    #endif
  #elif defined OS_IOS
    #if defined IGRAPHICS_GLES2
      #include <libGLESv2/angle_gl.h>
      #define NANOVG_GLES2_IMPLEMENTATION
    #elif defined IGRAPHICS_GLES3
      #include <libGLESv2/angle_gl.h>
      #define NANOVG_GLES3_IMPLEMENTATION
    #else
      #error Define either IGRAPHICS_GLES2 or IGRAPHICS_GLES3 for IGRAPHICS_NANOVG with OS_IOS
    #endif
  #elif defined OS_WIN
    #pragma comment(lib, "opengl32.lib")
    #if defined IGRAPHICS_GL2
      #define NANOVG_GL2_IMPLEMENTATION
    #elif defined IGRAPHICS_GL3
      #define NANOVG_GL3_IMPLEMENTATION
    #else
      #error Define either IGRAPHICS_GL2 or IGRAPHICS_GL3 when using IGRAPHICS_GL and IGRAPHICS_NANOVG with OS_WIN
    #endif
  #elif defined OS_LINUX
    #error NOT IMPLEMENTED
  #elif defined OS_WEB
    #if defined IGRAPHICS_GLES2
      #define NANOVG_GLES2_IMPLEMENTATION
    #elif defined IGRAPHICS_GLES3
      #define NANOVG_GLES3_IMPLEMENTATION
    #else
      #error Define either IGRAPHICS_GLES2 or IGRAPHICS_GLES3 when using IGRAPHICS_GL and IGRAPHICS_NANOVG with OS_WEB
    #endif
  #endif
  #include "nanovg_gl.h"
  #include "nanovg_gl_utils.h"
#elif defined IGRAPHICS_METAL
  #include "nanovg_mtl.h"
  #if defined OS_MAC || defined OS_IOS
    //even though this is a .cpp we are in an objc(pp) compilation unit
    #import <Metal/Metal.h>
    #import <CoreFoundation/CoreFoundation.h>
  #endif
#else
  #error you must define either IGRAPHICS_GL2, IGRAPHICS_GLES2 etc or IGRAPHICS_METAL when using IGRAPHICS_NANOVG
#endif

#include <string>
#include <map>

using namespace iplug;
using namespace igraphics;

#pragma mark - Private Classes and Structs

class IGraphicsNanoVG::Bitmap : public APIBitmap
{
public:
  Bitmap(NVGcontext* pContext, const char* path, double sourceScale, int nvgImageID, bool shared = false);
  Bitmap(IGraphicsNanoVG* pGraphics, NVGcontext* pContext, int width, int height, float scale, float drawScale);
  Bitmap(NVGcontext* pContext, int width, int height, const uint8_t* pData, float scale, float drawScale);
  virtual ~Bitmap();
  NVGframebuffer* GetFBO() const { return mFBO; }
private:
  IGraphicsNanoVG *mGraphics = nullptr;
  NVGcontext* mVG;
  NVGframebuffer* mFBO = nullptr;
  bool mSharedTexture = false;
};

IGraphicsNanoVG::Bitmap::Bitmap(NVGcontext* pContext, const char* path, double sourceScale, int nvgImageID, bool shared)
{
  assert(nvgImageID > 0);
  
  mVG = pContext;
  mSharedTexture = shared;
  int w = 0, h = 0;
  nvgImageSize(mVG, nvgImageID, &w, &h);
  
  SetBitmap(nvgImageID, w, h, sourceScale, 1.f);
}

IGraphicsNanoVG::Bitmap::Bitmap(IGraphicsNanoVG* pGraphics, NVGcontext* pContext, int width, int height, float scale, float drawScale)
{
  assert(width > 0 && height > 0);
  mGraphics = pGraphics;
  mVG = pContext;
  mFBO = nvgCreateFramebuffer(pContext, width, height, 0);
  
  nvgBindFramebuffer(mFBO);
  
#ifdef IGRAPHICS_METAL
  mnvgClearWithColor(mVG, nvgRGBAf(0, 0, 0, 0));
#else
  glViewport(0, 0, width, height);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
#endif
  nvgBeginFrame(mVG, width, height, 1.f);
  nvgEndFrame(mVG);
  
  SetBitmap(mFBO->image, width, height, scale, drawScale);
}

IGraphicsNanoVG::Bitmap::Bitmap(NVGcontext* pContext, int width, int height, const uint8_t* pData, float scale, float drawScale)
{
  int idx = nvgCreateImageRGBA(pContext, width, height, 0, pData);
  mVG = pContext;
  SetBitmap(idx, width, height, scale, drawScale);
}

IGraphicsNanoVG::Bitmap::~Bitmap()
{
  if(!mSharedTexture)
  {
    if(mFBO)
      mGraphics->DeleteFBO(mFBO);
    else
      nvgDeleteImage(mVG, GetBitmap());
  }
}

// Fonts
static StaticStorage<IFontData> sFontCache;

extern std::map<std::string, MTLTexturePtr> gTextureMap;

// Retrieving pixels
static void nvgReadPixels(NVGcontext* pContext, int image, int x, int y, int width, int height, void* pData)
{
#if defined(IGRAPHICS_GL)
  glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pData);
#elif defined(IGRAPHICS_METAL)
  mnvgReadPixels(pContext, image, x, y, width, height, pData);
#endif
}

#pragma mark - Utilities

BEGIN_IPLUG_NAMESPACE
BEGIN_IGRAPHICS_NAMESPACE

NVGcolor NanoVGColor(const IColor& color, const IBlend* pBlend)
{
  NVGcolor c;
  c.r = (float) color.R / 255.0f;
  c.g = (float) color.G / 255.0f;
  c.b = (float) color.B / 255.0f;
  c.a = (BlendWeight(pBlend) * color.A) / 255.0f;
  return c;
}

void NanoVGRect(NVGcontext* pContext, const IRECT& r)
{
  nvgRect(pContext, r.L, r.T, r.W(), r.H());
}

void NanoVGSetBlendMode(NVGcontext* pContext, const IBlend* pBlend)
{
  if (!pBlend)
  {
    nvgGlobalCompositeOperation(pContext, NVG_SOURCE_OVER);
      return;
  }
  
  switch (pBlend->mMethod)
  {
    case EBlend::SrcOver:    nvgGlobalCompositeOperation(pContext, NVG_SOURCE_OVER);                break;
    case EBlend::SrcIn:      nvgGlobalCompositeOperation(pContext, NVG_SOURCE_IN);                  break;
    case EBlend::SrcOut:     nvgGlobalCompositeOperation(pContext, NVG_SOURCE_OUT);                 break;
    case EBlend::SrcAtop:    nvgGlobalCompositeOperation(pContext, NVG_ATOP);                       break;
    case EBlend::DstOver:    nvgGlobalCompositeOperation(pContext, NVG_DESTINATION_OVER);           break;
    case EBlend::DstIn:      nvgGlobalCompositeOperation(pContext, NVG_DESTINATION_IN);             break;
    case EBlend::DstOut:     nvgGlobalCompositeOperation(pContext, NVG_DESTINATION_OUT);            break;
    case EBlend::DstAtop:    nvgGlobalCompositeOperation(pContext, NVG_DESTINATION_ATOP);           break;
    case EBlend::Add:        nvgGlobalCompositeBlendFunc(pContext, NVG_SRC_ALPHA, NVG_DST_ALPHA);   break;
    case EBlend::XOR:        nvgGlobalCompositeOperation(pContext, NVG_XOR);                        break;
  }
}

NVGpaint NanoVGPaint(NVGcontext* pContext, const IPattern& pattern, const IBlend* pBlend)
{
  assert(pattern.NStops() > 0);
  
  double s[2], e[2];
  
  NVGcolor icol = NanoVGColor(pattern.GetStop(0).mColor, pBlend);
  NVGcolor ocol = NanoVGColor(pattern.GetStop(pattern.NStops() - 1).mColor, pBlend);
    
  // Invert transform
  IMatrix inverse = IMatrix(pattern.mTransform).Invert();
  inverse.TransformPoint(s[0], s[1], 0.0, 0.0);

  if (pattern.mType == EPatternType::Radial)
  {
    return nvgRadialGradient(pContext, s[0], s[1], inverse.mXX * pattern.GetStop(0).mOffset, inverse.mXX, icol, ocol);
  }
  else
  {
    inverse.TransformPoint(e[0], e[1], 0.0, 1.0);
    
    return nvgLinearGradient(pContext, s[0], s[1], e[0], e[1], icol, ocol);
  }
}

END_IGRAPHICS_NAMESPACE
END_IPLUG_NAMESPACE

#pragma mark -

IGraphicsNanoVG::IGraphicsNanoVG(IGEditorDelegate& dlg, int w, int h, int fps, float scale)
: IGraphics(dlg, w, h, fps, scale)
{
  DBGMSG("IGraphics NanoVG @ %i FPS\n", fps);
  StaticStorage<IFontData>::Accessor storage(sFontCache);
  storage.Retain();
}

IGraphicsNanoVG::~IGraphicsNanoVG() 
{
  StaticStorage<IFontData>::Accessor storage(sFontCache);
  storage.Release();
  ClearFBOStack();
}

const char* IGraphicsNanoVG::GetDrawingAPIStr()
{
#if defined IGRAPHICS_METAL
  return "NanoVG | Metal";
#else
  #if defined OS_WEB
    return "NanoVG | WebGL";
  #else
    #if defined IGRAPHICS_GL2
      return "NanoVG | GL2";
    #elif defined IGRAPHICS_GL3
      return "NanoVG | GL3";
    #elif defined IGRAPHICS_GLES2
      return "NanoVG | GLES2";
    #elif defined IGRAPHICS_GLES3
      return "NanoVG | GLES3";
    #endif
  #endif
#endif
}

bool IGraphicsNanoVG::BitmapExtSupported(const char* ext)
{
  char extLower[32];
  ToLower(extLower, ext);
  return (strstr(extLower, "png") != nullptr) || (strstr(extLower, "jpg") != nullptr) || (strstr(extLower, "jpeg") != nullptr);
}

IBitmap IGraphicsNanoVG::LoadBitmap(const char* name, int nStates, bool framesAreHorizontal, int targetScale)
{
  if (targetScale == 0)
    targetScale = GetRoundedScreenScale();

  // NanoVG does not use the global static cache, since bitmaps are textures linked to a context
  StaticStorage<APIBitmap>::Accessor storage(mBitmapCache);
  APIBitmap* pAPIBitmap = storage.Find(name, targetScale);
  
  // If the bitmap is not already cached at the targetScale
  if (!pAPIBitmap)
  {
    const char* ext = name + strlen(name) - 1;
    while (ext >= name && *ext != '.') --ext;
    ++ext;

    WDL_String fullPathOrResourceID;
    int sourceScale = 0;
    EResourceLocation resourceFound = SearchImageResource(name, ext, fullPathOrResourceID, targetScale, sourceScale);

    bool bitmapTypeSupported = BitmapExtSupported(ext); // KTX textures pass this test (if ext is .ktx.png)
    
    if(resourceFound == EResourceLocation::kNotFound || !bitmapTypeSupported)
    {
      assert(0 && "Bitmap not found");
      return IBitmap(); // return invalid IBitmap
    }

    pAPIBitmap = LoadAPIBitmap(fullPathOrResourceID.Get(), sourceScale, resourceFound, ext);
    
    storage.Add(pAPIBitmap, name, sourceScale);

    assert(pAPIBitmap && "Bitmap not loaded");
  }
  
  return IBitmap(pAPIBitmap, nStates, framesAreHorizontal, name);
}

APIBitmap* IGraphicsNanoVG::LoadAPIBitmap(const char* fileNameOrResID, int scale, EResourceLocation location, const char* ext)
{
  int idx = 0;
  int nvgImageFlags = 0;
  
  // Preloaded textures are only supported with Metal, not ANGLE/GLES
#if defined OS_IOS && defined IGRAPHICS_METAL
  if (location == EResourceLocation::kPreloadedTexture)
  {
    idx = mnvgCreateImageFromHandle(mVG, gTextureMap[fileNameOrResID], nvgImageFlags);
  }
  else
#endif
  
#ifdef OS_WIN
  if (location == EResourceLocation::kWinBinary)
  {
    const void* pResData = nullptr;

    int size = 0;
    pResData = LoadWinResource(fileNameOrResID, ext, size, GetWinModuleHandle());

    if (pResData)
    {
      ScopedGLContext scopedGLCtx {this};
      idx = nvgCreateImageMem(mVG, nvgImageFlags, (unsigned char*) pResData, size);
    }
  }
  else
#endif
  if (location == EResourceLocation::kAbsolutePath)
  {
    ScopedGLContext scopedGLCtx {this};
    idx = nvgCreateImage(mVG, fileNameOrResID, nvgImageFlags);
  }

  return new Bitmap(mVG, fileNameOrResID, scale, idx, location == EResourceLocation::kPreloadedTexture);
}

APIBitmap* IGraphicsNanoVG::LoadAPIBitmap(const char* name, const void* pData, int dataSize, int scale)
{
  StaticStorage<APIBitmap>::Accessor storage(mBitmapCache);
  APIBitmap* pBitmap = storage.Find(name, scale);

  if (!pBitmap)
  {
    int idx = 0;
    int nvgImageFlags = 0;

    {
      ScopedGLContext scopedGLCtx {this};
      idx = nvgCreateImageMem(mVG, nvgImageFlags, (unsigned char*)pData, dataSize);
    }
    
    pBitmap = new Bitmap(mVG, name, scale, idx, false);

    storage.Add(pBitmap, name, scale);
  }

  return pBitmap;
}

APIBitmap* IGraphicsNanoVG::CreateAPIBitmap(int width, int height, float scale, double drawScale, bool cacheable)
{
  if (mInDraw)
  {
    nvgEndFrame(mVG);
  }
  
  APIBitmap* pAPIBitmap = new Bitmap(this, mVG, width, height, scale, drawScale);

  if (mInDraw)
  {
    nvgBindFramebuffer(mMainFrameBuffer); // begin main frame buffer update
    nvgBeginFrame(mVG, WindowWidth(), WindowHeight(), GetScreenScale());
  }
  
  return pAPIBitmap;
}

void IGraphicsNanoVG::GetLayerBitmapData(const ILayerPtr& layer, RawBitmapData& data)
{
  const APIBitmap* pBitmap = layer->GetAPIBitmap();
  int size = pBitmap->GetWidth() * pBitmap->GetHeight() * 4;
  
  data.Resize(size);
  
  if (data.GetSize() >= size)
  {
    PushLayer(layer.get());
    nvgReadPixels(mVG, pBitmap->GetBitmap(), 0, 0, pBitmap->GetWidth(), pBitmap->GetHeight(), data.Get());
    PopLayer();
  }
}

void IGraphicsNanoVG::ApplyShadowMask(ILayerPtr& layer, RawBitmapData& mask, const IShadow& shadow)
{
  const APIBitmap* pBitmap = layer->GetAPIBitmap();
  int width = pBitmap->GetWidth();
  int height = pBitmap->GetHeight();
  int size = width * height * 4;
  
  if (mask.GetSize() >= size)
  {
    if (!shadow.mDrawForeground)
    {
      PushLayer(layer.get());
      nvgGlobalCompositeBlendFunc(mVG, NVG_ZERO, NVG_ZERO);
      PathRect(layer->Bounds());
      nvgFillColor(mVG, NanoVGColor(COLOR_TRANSPARENT));
      nvgFill(mVG);
      PopLayer();
    }
    
    IRECT bounds(layer->Bounds());
    
    Bitmap maskRawBitmap(mVG, width, height, mask.Get(), pBitmap->GetScale(), pBitmap->GetDrawScale());
    APIBitmap* shadowBitmap = CreateAPIBitmap(width, height, pBitmap->GetScale(), pBitmap->GetDrawScale());
    IBitmap tempLayerBitmap(shadowBitmap, 1, false);
    IBitmap maskBitmap(&maskRawBitmap, 1, false);
    ILayer shadowLayer(shadowBitmap, layer->Bounds(), nullptr, IRECT());
    
    PathTransformSave();
    PushLayer(layer.get());
    PushLayer(&shadowLayer);
    DrawBitmap(maskBitmap, bounds, 0, 0, nullptr);
    IBlend blend1(EBlend::SrcIn, 1.0);
    PathRect(layer->Bounds());
    PathTransformTranslate(-shadow.mXOffset, -shadow.mYOffset);
    PathFill(shadow.mPattern, IFillOptions(), &blend1);
    PopLayer();
    IBlend blend2(EBlend::DstOver, shadow.mOpacity);
    bounds.Translate(shadow.mXOffset, shadow.mYOffset);
    DrawBitmap(tempLayerBitmap, bounds, 0, 0, &blend2);
    PopLayer();
    PathTransformRestore();
  }
}

void IGraphicsNanoVG::OnViewInitialized(void* pContext)
{  
#if defined IGRAPHICS_METAL
  mVG = nvgCreateContext(pContext, NVG_ANTIALIAS | NVG_TRIPLE_BUFFER); //TODO: NVG_STENCIL_STROKES currently has issues
#else
  mVG = nvgCreateContext(NVG_ANTIALIAS /*| NVG_STENCIL_STROKES*/);
#endif

  if (mVG == nullptr)
    DBGMSG("Could not init nanovg.\n");
}

#if defined IGRAPHICS_GL && defined OS_WIN
namespace
{
#ifndef GL_MAP_READ_BIT
#define GL_MAP_READ_BIT 0x0001
#endif
#ifndef GL_SYNC_GPU_COMMANDS_COMPLETE
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#endif
#ifndef GL_TIMEOUT_EXPIRED
#define GL_TIMEOUT_EXPIRED 0x911B
#endif
#ifndef GL_WAIT_FAILED
#define GL_WAIT_FAILED 0x911D
#endif

// glad_GL2 does not expose GL 3.2 sync objects. The Windows context is 3.3 core, so load them directly.
struct GlReadbackFns
{
  bool tried = false;
  bool ok = false;
  void* (APIENTRY* mapRange)(GLenum, GLintptr, GLsizeiptr, GLbitfield) = nullptr;
  GLsync (APIENTRY* fence)(GLenum, GLbitfield) = nullptr;
  GLenum (APIENTRY* clientWait)(GLsync, GLbitfield, unsigned long long) = nullptr;
  void (APIENTRY* deleteSync)(GLsync) = nullptr;
};

GlReadbackFns gReadbackFns;

bool EnsureReadbackFns()
{
  if (gReadbackFns.tried)
    return gReadbackFns.ok;
  gReadbackFns.tried = true;
  gReadbackFns.mapRange = reinterpret_cast<decltype(gReadbackFns.mapRange)>(wglGetProcAddress("glMapBufferRange"));
  gReadbackFns.fence = reinterpret_cast<decltype(gReadbackFns.fence)>(wglGetProcAddress("glFenceSync"));
  gReadbackFns.clientWait = reinterpret_cast<decltype(gReadbackFns.clientWait)>(wglGetProcAddress("glClientWaitSync"));
  gReadbackFns.deleteSync = reinterpret_cast<decltype(gReadbackFns.deleteSync)>(wglGetProcAddress("glDeleteSync"));
  gReadbackFns.ok = gReadbackFns.mapRange && gReadbackFns.fence && gReadbackFns.clientWait && gReadbackFns.deleteSync;
  return gReadbackFns.ok;
}
}
#endif

#if defined IGRAPHICS_GL && defined OS_WIN
namespace
{
#ifndef GL_R8
#define GL_R8 0x8229
#endif
#ifndef GL_RG
#define GL_RG 0x8227
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_RED
#define GL_RED 0x1903
#endif

struct Nv12Pack
{
  GLuint progY = 0;
  GLuint progUv = 0;
  GLuint vao = 0;
  GLuint vbo = 0;
  GLuint yTex = 0;
  GLuint yFbo = 0;
  GLuint uvTex = 0;
  GLuint uvFbo = 0;
  GLint locSizeY = -1;
  GLint locSizeUv = -1;
  int w = 0;
  int h = 0;
  bool failed = false;
};

void (APIENTRY* pGenVao)(GLsizei, GLuint*) = nullptr;
void (APIENTRY* pBindVao)(GLuint) = nullptr;
void (APIENTRY* pDelVao)(GLsizei, const GLuint*) = nullptr;

bool EnsureVaoFns()
{
  if (pGenVao)
    return true;
  pGenVao = reinterpret_cast<decltype(pGenVao)>(wglGetProcAddress("glGenVertexArrays"));
  pBindVao = reinterpret_cast<decltype(pBindVao)>(wglGetProcAddress("glBindVertexArray"));
  pDelVao = reinterpret_cast<decltype(pDelVao)>(wglGetProcAddress("glDeleteVertexArrays"));
  return pGenVao && pBindVao && pDelVao;
}

GLuint CompileShader(GLenum type, const char* src)
{
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok)
  {
    glDeleteShader(shader);
    return 0;
  }
  return shader;
}

GLuint LinkProgram(const char* fragSrc)
{
  static const char* kVert = R"(#version 330 core
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
  const GLuint vs = CompileShader(GL_VERTEX_SHADER, kVert);
  const GLuint fs = CompileShader(GL_FRAGMENT_SHADER, fragSrc);
  if (!vs || !fs)
  {
    if (vs) glDeleteShader(vs);
    if (fs) glDeleteShader(fs);
    return 0;
  }
  const GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok)
  {
    glDeleteProgram(prog);
    return 0;
  }
  return prog;
}

void DestroyNv12Pack(Nv12Pack* pack)
{
  if (!pack)
    return;
  if (pack->yFbo) { GLuint id = pack->yFbo; glDeleteFramebuffers(1, &id); }
  if (pack->uvFbo) { GLuint id = pack->uvFbo; glDeleteFramebuffers(1, &id); }
  if (pack->yTex) { GLuint id = pack->yTex; glDeleteTextures(1, &id); }
  if (pack->uvTex) { GLuint id = pack->uvTex; glDeleteTextures(1, &id); }
  if (pack->progY) glDeleteProgram(pack->progY);
  if (pack->progUv) glDeleteProgram(pack->progUv);
  if (pack->vbo) { GLuint id = pack->vbo; glDeleteBuffers(1, &id); }
  if (pack->vao && pDelVao) pDelVao(1, &pack->vao);
  delete pack;
}

GLuint MakeTex(GLenum internalFmt, GLenum fmt, int w, int h)
{
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, nullptr);
  return tex;
}

GLuint MakeFbo(GLuint tex)
{
  GLuint fbo = 0;
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
  return fbo;
}

Nv12Pack* EnsureNv12Pack(Nv12Pack* existing, int w, int h)
{
  if (existing && existing->failed)
    return existing;
  if (!EnsureVaoFns() || (w % 2) || (h % 2))
    return existing;

  Nv12Pack* pack = existing ? existing : new Nv12Pack();
  if (!pack->progY)
  {
    static const char* kY = R"(#version 330 core
uniform sampler2D uSrc;
uniform ivec2 uSize;
out vec4 o;
void main() {
  ivec2 p = ivec2(gl_FragCoord.xy);
  ivec2 s = ivec2(p.x, uSize.y - 1 - p.y);
  vec3 rgb = texelFetch(uSrc, s, 0).rgb;
  float y = dot(rgb, vec3(0.2126, 0.7152, 0.0722));
  o = vec4(y, 0.0, 0.0, 1.0);
}
)";
    static const char* kUv = R"(#version 330 core
uniform sampler2D uSrc;
uniform ivec2 uSize;
out vec4 o;
void main() {
  ivec2 d = ivec2(gl_FragCoord.xy);
  int x0 = d.x * 2;
  int yTop = d.y * 2;
  int y0 = uSize.y - 1 - yTop;
  int y1 = y0 - 1;
  int x1 = min(x0 + 1, uSize.x - 1);
  y1 = max(y1, 0);
  vec3 a = texelFetch(uSrc, ivec2(x0, y0), 0).rgb;
  vec3 b = texelFetch(uSrc, ivec2(x1, y0), 0).rgb;
  vec3 c = texelFetch(uSrc, ivec2(x0, y1), 0).rgb;
  vec3 dlt = texelFetch(uSrc, ivec2(x1, y1), 0).rgb;
  vec3 rgb = (a + b + c + dlt) * 0.25;
  float u = dot(rgb, vec3(-0.114572, -0.385428, 0.5)) + 0.5;
  float v = dot(rgb, vec3(0.5, -0.454153, -0.045847)) + 0.5;
  o = vec4(clamp(u, 0.0, 1.0), clamp(v, 0.0, 1.0), 0.0, 1.0);
}
)";
    pack->progY = LinkProgram(kY);
    pack->progUv = LinkProgram(kUv);
    if (!pack->progY || !pack->progUv)
    {
      pack->failed = true;
      return pack;
    }
    glUseProgram(pack->progY);
    glUniform1i(glGetUniformLocation(pack->progY, "uSrc"), 0);
    pack->locSizeY = glGetUniformLocation(pack->progY, "uSize");
    glUseProgram(pack->progUv);
    glUniform1i(glGetUniformLocation(pack->progUv, "uSrc"), 0);
    pack->locSizeUv = glGetUniformLocation(pack->progUv, "uSize");
    glUseProgram(0);

    const float tri[] = { -1.f, -1.f, 3.f, -1.f, -1.f, 3.f };
    pGenVao(1, &pack->vao);
    glGenBuffers(1, &pack->vbo);
    pBindVao(pack->vao);
    glBindBuffer(GL_ARRAY_BUFFER, pack->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
    pBindVao(0);
  }

  if (pack->w != w || pack->h != h)
  {
    if (pack->yFbo) { GLuint id = pack->yFbo; glDeleteFramebuffers(1, &id); pack->yFbo = 0; }
    if (pack->uvFbo) { GLuint id = pack->uvFbo; glDeleteFramebuffers(1, &id); pack->uvFbo = 0; }
    if (pack->yTex) { GLuint id = pack->yTex; glDeleteTextures(1, &id); pack->yTex = 0; }
    if (pack->uvTex) { GLuint id = pack->uvTex; glDeleteTextures(1, &id); pack->uvTex = 0; }
    pack->yTex = MakeTex(GL_R8, GL_RED, w, h);
    pack->uvTex = MakeTex(GL_RG8, GL_RG, w / 2, h / 2);
    pack->yFbo = MakeFbo(pack->yTex);
    pack->uvFbo = MakeFbo(pack->uvTex);
    pack->w = w;
    pack->h = h;
  }
  return pack;
}

void DrawNv12(Nv12Pack* pack, GLuint srcTex, int w, int h)
{
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, srcTex);
  pBindVao(pack->vao);

  glBindFramebuffer(GL_FRAMEBUFFER, pack->yFbo);
  glViewport(0, 0, w, h);
  glUseProgram(pack->progY);
  if (pack->locSizeY >= 0)
    glUniform2i(pack->locSizeY, w, h);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  glBindFramebuffer(GL_FRAMEBUFFER, pack->uvFbo);
  glViewport(0, 0, w / 2, h / 2);
  glUseProgram(pack->progUv);
  if (pack->locSizeUv >= 0)
    glUniform2i(pack->locSizeUv, w, h);
  glDrawArrays(GL_TRIANGLES, 0, 3);
}
}
#endif

void IGraphicsNanoVG::ReleaseAsyncReadback()
{
#if defined IGRAPHICS_GL && defined OS_WIN
  for (auto& slot : mAsyncRead)
  {
    if (slot.fence && gReadbackFns.deleteSync)
    {
      gReadbackFns.deleteSync(static_cast<GLsync>(slot.fence));
      slot.fence = nullptr;
    }
    if (slot.pbo)
    {
      GLuint id = slot.pbo;
      glDeleteBuffers(1, &id);
      slot.pbo = 0;
    }
    slot.busy = false;
    slot.nv12 = false;
    slot.w = 0;
    slot.h = 0;
    slot.held = nullptr;
  }
  DestroyNv12Pack(static_cast<Nv12Pack*>(mNv12Pack));
  mNv12Pack = nullptr;
#elif defined IGRAPHICS_METAL && (defined OS_MAC || defined OS_IOS)
  for (auto& slot : mAsyncRead)
  {
    if (slot.fence)
    {
      CFRelease(slot.fence);
      slot.fence = nullptr;
    }
    if (slot.gpuBuf)
    {
      CFRelease(slot.gpuBuf);
      slot.gpuBuf = nullptr;
    }
    slot.rowBytes = 0;
    slot.busy = false;
    slot.nv12 = false;
    slot.w = 0;
    slot.h = 0;
    slot.held = nullptr;
  }
#endif
}

bool IGraphicsNanoVG::SupportsAsyncReadback() const
{
#if defined IGRAPHICS_GL && defined OS_WIN
  return EnsureReadbackFns();
#elif defined IGRAPHICS_METAL && (defined OS_MAC || defined OS_IOS)
  return mVG && mnvgCommandQueue(mVG);
#else
  return false;
#endif
}

bool IGraphicsNanoVG::CanQueueLayerReadback() const
{
#if defined IGRAPHICS_GL && defined OS_WIN
  if (!gReadbackFns.ok)
    return false;
#elif defined IGRAPHICS_METAL && (defined OS_MAC || defined OS_IOS)
  if (!mVG)
    return false;
#else
  return false;
#endif
  for (const auto& slot : mAsyncRead)
  {
    if (!slot.busy)
      return true;
  }
  return false;
}

bool IGraphicsNanoVG::PollAsyncReadback(RawBitmapData& data, int& width, int& height, bool& nv12)
{
  nv12 = false;
#if defined IGRAPHICS_GL && defined OS_WIN
  if (!gReadbackFns.ok)
    return false;

  for (auto& slot : mAsyncRead)
  {
    if (!slot.busy || !slot.fence)
      continue;

    const GLenum state = gReadbackFns.clientWait(static_cast<GLsync>(slot.fence), 0, 0);
    if (state == GL_TIMEOUT_EXPIRED || state == GL_WAIT_FAILED)
      continue;

    const int bytes = slot.nv12 ? (slot.w * slot.h * 3 / 2) : (slot.w * slot.h * 4);
    bool ok = false;
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.pbo);
    if (void* ptr = gReadbackFns.mapRange(GL_PIXEL_PACK_BUFFER, 0, bytes, GL_MAP_READ_BIT))
    {
      data.Resize(bytes);
      if (data.GetSize() >= bytes)
      {
        std::memcpy(data.Get(), ptr, static_cast<size_t>(bytes));
        width = slot.w;
        height = slot.h;
        nv12 = slot.nv12;
        ok = true;
      }
      glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
    }
    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

    gReadbackFns.deleteSync(static_cast<GLsync>(slot.fence));
    slot.fence = nullptr;
    slot.busy = false;
    slot.nv12 = false;
    slot.held = nullptr;
    if (ok)
      return true;
  }
#elif defined IGRAPHICS_METAL && (defined OS_MAC || defined OS_IOS)
  for (auto& slot : mAsyncRead)
  {
    if (!slot.busy || !slot.fence)
      continue;

    id<MTLCommandBuffer> cmd = (__bridge id<MTLCommandBuffer>)slot.fence;
    const MTLCommandBufferStatus status = cmd.status;
    if (status == MTLCommandBufferStatusError)
    {
      CFRelease(slot.fence);
      slot.fence = nullptr;
      slot.busy = false;
      slot.held = nullptr;
      continue;
    }
    if (status != MTLCommandBufferStatusCompleted)
      continue;

    id<MTLBuffer> buf = slot.gpuBuf ? (__bridge id<MTLBuffer>)slot.gpuBuf : nil;
    const int tight = slot.w * slot.h * 4;
    const int srcRow = slot.rowBytes > 0 ? slot.rowBytes : (slot.w * 4);
    data.Resize(tight);
    bool ok = false;
    if (data.GetSize() >= tight && buf && buf.contents)
    {
      const uint8_t* src = static_cast<const uint8_t*>(buf.contents);
      uint8_t* dst = data.Get();
      if (srcRow == slot.w * 4)
      {
        std::memcpy(dst, src, static_cast<size_t>(tight));
      }
      else
      {
        for (int y = 0; y < slot.h; ++y)
          std::memcpy(dst + static_cast<size_t>(y) * slot.w * 4,
                      src + static_cast<size_t>(y) * srcRow,
                      static_cast<size_t>(slot.w) * 4u);
      }
      width = slot.w;
      height = slot.h;
      nv12 = false;
      ok = true;
    }

    CFRelease(slot.fence);
    slot.fence = nullptr;
    slot.busy = false;
    slot.held = nullptr;
    if (ok)
      return true;
  }
#else
  (void) data;
  (void) width;
  (void) height;
#endif
  return false;
}

void IGraphicsNanoVG::QueueLayerReadback(ILayerPtr layer, bool nv12)
{
#if defined IGRAPHICS_GL && defined OS_WIN
  if (!EnsureReadbackFns())
    return;

  const APIBitmap* bmp = layer ? layer->GetAPIBitmap() : nullptr;
  auto* bitmap = dynamic_cast<const Bitmap*>(bmp);
  NVGframebuffer* fb = bitmap ? bitmap->GetFBO() : nullptr;
  if (!bmp || !fb || fb->texture == 0)
    return;

  const int w = bmp->GetWidth();
  const int h = bmp->GetHeight();
  if (w < 2 || h < 2)
    return;

  AsyncReadSlot* slot = nullptr;
  for (auto& candidate : mAsyncRead)
  {
    if (!candidate.busy)
    {
      slot = &candidate;
      break;
    }
  }
  if (!slot)
    return;

  // Snapshot NanoVG's GL state first. Creating the NV12 targets binds a framebuffer;
  // saving that afterwards made the rest of the frame draw into it and crashed the host.
  GLint prevFbo = 0;
  GLint prevRbo = 0;
  GLint prevVp[4] = {};
  GLint prevProgram = 0;
  GLint prevVao = 0;
  GLint prevArray = 0;
  GLint prevPack = 0;
  GLint prevTex = 0;
  GLint prevActiveTex = 0;
  GLint prevAlign = 4;
  GLint prevRowLength = 0;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
  glGetIntegerv(GL_RENDERBUFFER_BINDING, &prevRbo);
  glGetIntegerv(GL_VIEWPORT, prevVp);
  glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
  glGetIntegerv(0x85B5, &prevVao); // GL_VERTEX_ARRAY_BINDING
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArray);
  glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPack);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
  glGetIntegerv(GL_PACK_ALIGNMENT, &prevAlign);
  glGetIntegerv(0x0D02, &prevRowLength); // GL_PACK_ROW_LENGTH
  const GLboolean blend = glIsEnabled(GL_BLEND);
  const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
  const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
  const GLboolean stencil = glIsEnabled(GL_STENCIL_TEST);

  bool packedNv12 = false;
  if (nv12 && (w % 2) == 0 && (h % 2) == 0)
  {
    mNv12Pack = EnsureNv12Pack(static_cast<Nv12Pack*>(mNv12Pack), w, h);
    auto* pack = static_cast<Nv12Pack*>(mNv12Pack);
    packedNv12 = pack && !pack->failed && pack->yFbo && pack->uvFbo;
  }

  const int bytes = packedNv12 ? (w * h * 3 / 2) : (w * h * 4);
  if (!slot->pbo)
  {
    GLuint id = 0;
    glGenBuffers(1, &id);
    slot->pbo = id;
  }

  glDisable(GL_BLEND);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_STENCIL_TEST);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glPixelStorei(0x0D02, 0);

  glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
  if (slot->w != w || slot->h != h || slot->nv12 != packedNv12)
  {
    glBufferData(GL_PIXEL_PACK_BUFFER, bytes, nullptr, GL_STREAM_READ);
    slot->w = w;
    slot->h = h;
  }

  if (packedNv12)
  {
    DrawNv12(static_cast<Nv12Pack*>(mNv12Pack), fb->texture, w, h);
    glBindBuffer(GL_PIXEL_PACK_BUFFER, slot->pbo);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<Nv12Pack*>(mNv12Pack)->yFbo);
    glReadPixels(0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<Nv12Pack*>(mNv12Pack)->uvFbo);
    glReadPixels(0, 0, w / 2, h / 2, GL_RG, GL_UNSIGNED_BYTE, reinterpret_cast<void*>(static_cast<std::uintptr_t>(w * h)));
  }
  else
  {
    glBindFramebuffer(GL_FRAMEBUFFER, fb->fbo);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  }

  glBindBuffer(GL_PIXEL_PACK_BUFFER, static_cast<GLuint>(prevPack));
  glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
  glBindRenderbuffer(GL_RENDERBUFFER, static_cast<GLuint>(prevRbo));
  glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
  glUseProgram(static_cast<GLuint>(prevProgram));
  if (pBindVao)
    pBindVao(static_cast<GLuint>(prevVao));
  glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArray));
  glActiveTexture(static_cast<GLenum>(prevActiveTex));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
  glPixelStorei(GL_PACK_ALIGNMENT, prevAlign);
  glPixelStorei(0x0D02, prevRowLength);
  if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
  if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
  if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
  if (stencil) glEnable(GL_STENCIL_TEST); else glDisable(GL_STENCIL_TEST);

  if (slot->fence)
    gReadbackFns.deleteSync(static_cast<GLsync>(slot->fence));
  slot->fence = gReadbackFns.fence(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();
  slot->busy = true;
  slot->nv12 = packedNv12;
  slot->held = std::move(layer);
#elif defined IGRAPHICS_METAL && (defined OS_MAC || defined OS_IOS)
  (void) nv12; // Packed NV12 is the Windows GL path; Metal copies RGBA asynchronously.

  const APIBitmap* bmp = layer ? layer->GetAPIBitmap() : nullptr;
  auto* bitmap = dynamic_cast<const Bitmap*>(bmp);
  NVGframebuffer* fb = bitmap ? bitmap->GetFBO() : nullptr;
  if (!mVG || !bmp || !fb)
    return;

  id<MTLTexture> tex = (__bridge id<MTLTexture>)mnvgImageHandleNoWait(mVG, fb->image);
  id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)mnvgCommandQueue(mVG);
  if (!tex || !queue)
    return;

  const int w = bmp->GetWidth();
  const int h = bmp->GetHeight();
  if (w < 2 || h < 2)
    return;

  AsyncReadSlot* slot = nullptr;
  for (auto& candidate : mAsyncRead)
  {
    if (!candidate.busy)
    {
      slot = &candidate;
      break;
    }
  }
  if (!slot)
    return;

  const int srcW = std::min(w, static_cast<int>(tex.width));
  const int srcH = std::min(h, static_cast<int>(tex.height));
  const int rowBytes = static_cast<int>(((static_cast<size_t>(srcW) * 4u + 255u) / 256u) * 256u);
  const size_t bytes = static_cast<size_t>(rowBytes) * static_cast<size_t>(srcH);

  id<MTLBuffer> buf = slot->gpuBuf ? (__bridge id<MTLBuffer>)slot->gpuBuf : nil;
  if (!buf || buf.length < bytes)
  {
    if (slot->gpuBuf)
    {
      CFRelease(slot->gpuBuf);
      slot->gpuBuf = nullptr;
    }
    id<MTLDevice> device = tex.device ? tex.device : (__bridge id<MTLDevice>)mnvgDevice(mVG);
    if (!device)
      return;
    buf = [device newBufferWithLength:bytes options:MTLResourceStorageModeShared];
    if (!buf)
      return;
    slot->gpuBuf = (void*)CFBridgingRetain(buf);
  }

  if (slot->fence)
  {
    CFRelease(slot->fence);
    slot->fence = nullptr;
  }

  id<MTLCommandBuffer> cmd = [queue commandBuffer];
  if (!cmd)
    return;
  id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
  if (!blit)
    return;
  [blit copyFromTexture:tex
            sourceSlice:0
            sourceLevel:0
           sourceOrigin:MTLOriginMake(0, 0, 0)
             sourceSize:MTLSizeMake(srcW, srcH, 1)
               toBuffer:(__bridge id<MTLBuffer>)slot->gpuBuf
      destinationOffset:0
 destinationBytesPerRow:static_cast<NSUInteger>(rowBytes)
destinationBytesPerImage:bytes];
  [blit endEncoding];
  [cmd commit];

  slot->fence = (void*)CFBridgingRetain(cmd);
  slot->rowBytes = rowBytes;
  slot->w = srcW;
  slot->h = srcH;
  slot->nv12 = false;
  slot->busy = true;
  slot->held = std::move(layer);
#else
  (void) layer;
  (void) nv12;
#endif
}

void IGraphicsNanoVG::OnViewDestroyed()
{
  ReleaseAsyncReadback();

  // need to remove all the controls to free framebuffers, before deleting context
  RemoveAllControls();

  StaticStorage<APIBitmap>::Accessor storage(mBitmapCache);
  storage.Clear();
  
  if(mMainFrameBuffer != nullptr)
    nvgDeleteFramebuffer(mMainFrameBuffer);
  
  mMainFrameBuffer = nullptr;
  
  if(mVG)
    nvgDeleteContext(mVG);
  
  mVG = nullptr;
}

void IGraphicsNanoVG::DrawResize()
{
  ScopedGLContext scopedGLCtx {this};

  if (mMainFrameBuffer != nullptr)
    nvgDeleteFramebuffer(mMainFrameBuffer);
  
  if (mVG)
  {
    mMainFrameBuffer = nvgCreateFramebuffer(mVG, WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale(), 0);
  
    if (mMainFrameBuffer == nullptr)
      DBGMSG("Could not init FBO.\n");
  }
}

void IGraphicsNanoVG::BeginFrame()
{
  mInDraw = true;
  IGraphics::BeginFrame(); // start perf graph timing

#ifdef IGRAPHICS_GL
    glViewport(0, 0, WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale());
    glClearColor(0.f, 0.f, 0.f, 0.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  #if defined OS_MAC || defined OS_IOS
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &mInitialFBO); // stash apple fbo
  #endif
#endif
  
  nvgBindFramebuffer(mMainFrameBuffer); // begin main frame buffer update
  nvgBeginFrame(mVG, WindowWidth(), WindowHeight(), GetScreenScale());
}

void IGraphicsNanoVG::EndFrame()
{
  nvgEndFrame(mVG); // end main frame buffer update
  nvgBindFramebuffer(nullptr);
  nvgBeginFrame(mVG, WindowWidth(), WindowHeight(), GetScreenScale());
  
  NVGpaint img = nvgImagePattern(mVG, 0, 0, WindowWidth(), WindowHeight(), 0, mMainFrameBuffer->image, 1.0f);

  nvgSave(mVG);
  nvgResetTransform(mVG);
  nvgTranslate(mVG, mXTranslation, mYTranslation);
  nvgBeginPath(mVG);
  nvgRect(mVG, 0, 0, WindowWidth(), WindowHeight());
  nvgFillPaint(mVG, img);
  nvgFill(mVG);
  nvgRestore(mVG);
  
#if (defined OS_MAC || defined OS_IOS) && defined IGRAPHICS_GL
  glBindFramebuffer(GL_FRAMEBUFFER, mInitialFBO); // restore apple fbo
#endif

  nvgEndFrame(mVG);
  
  mInDraw = false;
  ClearFBOStack();
}

void IGraphicsNanoVG::DrawBitmap(const IBitmap& bitmap, const IRECT& dest, int srcX, int srcY, const IBlend* pBlend)
{
  APIBitmap* pAPIBitmap = bitmap.GetAPIBitmap();
  
  assert(pAPIBitmap);
    
  // First generate a scaled image paint
  NVGpaint imgPaint;
  double scale = 1.0 / (pAPIBitmap->GetScale() * pAPIBitmap->GetDrawScale());

  nvgTransformScale(imgPaint.xform, scale, scale);

  imgPaint.xform[4] = dest.L - srcX;
  imgPaint.xform[5] = dest.T - srcY;
  imgPaint.extent[0] = bitmap.W() * bitmap.GetScale();
  imgPaint.extent[1] = bitmap.H() * bitmap.GetScale();
  imgPaint.image = pAPIBitmap->GetBitmap();
  imgPaint.radius = imgPaint.feather = 0.f;
  imgPaint.innerColor = imgPaint.outerColor = nvgRGBAf(1, 1, 1, BlendWeight(pBlend));
    
  // Now draw
    
  nvgBeginPath(mVG); // Clears any existing path
  nvgRect(mVG, dest.L, dest.T, dest.W(), dest.H());
  nvgFillPaint(mVG, imgPaint);
  NanoVGSetBlendMode(mVG, pBlend);
  nvgFill(mVG);
  nvgGlobalCompositeOperation(mVG, NVG_SOURCE_OVER);
  nvgBeginPath(mVG); // Clears the bitmap rect from the path state
}

void IGraphicsNanoVG::PathClear()
{
  nvgBeginPath(mVG);
}

void IGraphicsNanoVG::PathClose()
{
  nvgClosePath(mVG);
}

void IGraphicsNanoVG::PathArc(float cx, float cy, float r, float a1, float a2, EWinding winding)
{
  nvgArc(mVG, cx, cy, r, DegToRad(a1 - 90.f), DegToRad(a2 - 90.f), winding == EWinding::CW ? NVG_CW : NVG_CCW);
}

void IGraphicsNanoVG::PathMoveTo(float x, float y)
{
  nvgMoveTo(mVG, x, y);
}

void IGraphicsNanoVG::PathLineTo(float x, float y)
{
  nvgLineTo(mVG, x, y);
}

void IGraphicsNanoVG::PathCubicBezierTo(float c1x, float c1y, float c2x, float c2y, float x2, float y2)
{
  nvgBezierTo(mVG, c1x, c1y, c2x, c2y, x2, y2);
}

void IGraphicsNanoVG::PathQuadraticBezierTo(float cx, float cy, float x2, float y2)
{
  nvgQuadTo(mVG, cx, cy, x2, y2);
}

void IGraphicsNanoVG::PathSetWinding(bool clockwise)
{
  nvgPathWinding(mVG, clockwise ? NVG_CW : NVG_CCW);
}

IColor IGraphicsNanoVG::GetPoint(int x, int y)
{
  return COLOR_BLACK; //TODO:
}

void IGraphicsNanoVG::PrepareAndMeasureText(const IText& text, const char* str, IRECT& r, double& x, double & y) const
{
  float fbounds[4];
  
  assert(nvgFindFont(mVG, text.mFont) != -1 && "No font found - did you forget to load it?");
  
  nvgFontBlur(mVG, 0);
  nvgFontSize(mVG, text.mSize);
  nvgFontFace(mVG, text.mFont);
  
  int align = 0;
  
  switch (text.mAlign)
  {
    case EAlign::Near:     align = NVG_ALIGN_LEFT;     x = r.L;        break;
    case EAlign::Center:   align = NVG_ALIGN_CENTER;   x = r.MW();     break;
    case EAlign::Far:      align = NVG_ALIGN_RIGHT;    x = r.R;        break;
  }
  
  switch (text.mVAlign)
  {
    case EVAlign::Top:     align |= NVG_ALIGN_TOP;     y = r.T;        break;
    case EVAlign::Middle:  align |= NVG_ALIGN_MIDDLE;  y = r.MH();     break;
    case EVAlign::Bottom:  align |= NVG_ALIGN_BOTTOM;  y = r.B;        break;
  }
  
  nvgTextAlign(mVG, align);
  nvgTextBounds(mVG, x, y, str, NULL, fbounds);
  
  r = IRECT(fbounds[0], fbounds[1], fbounds[2], fbounds[3]);
}

float IGraphicsNanoVG::DoMeasureText(const IText& text, const char* str, IRECT& bounds) const
{
  IRECT r = bounds;
  double x, y;
  PrepareAndMeasureText(text, str, bounds, x, y);
  DoMeasureTextRotation(text, r, bounds);
  
  return bounds.W();
}

void IGraphicsNanoVG::DoDrawText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
  IRECT measured = bounds;
  double x, y;
  
  PrepareAndMeasureText(text, str, measured, x, y);
  PathTransformSave();
  DoTextRotation(text, bounds, measured);
  nvgFillColor(mVG, NanoVGColor(text.mFGColor, pBlend));
  NanoVGSetBlendMode(mVG, pBlend);
  nvgText(mVG, x, y, str, NULL);
  nvgGlobalCompositeOperation(mVG, NVG_SOURCE_OVER);
  PathTransformRestore();
}

void IGraphicsNanoVG::PathStroke(const IPattern& pattern, float thickness, const IStrokeOptions& options, const IBlend* pBlend)
{
  // First set options
  switch (options.mCapOption)
  {
    case ELineCap::Butt:   nvgLineCap(mVG, NVG_BUTT);     break;
    case ELineCap::Round:  nvgLineCap(mVG, NVG_ROUND);    break;
    case ELineCap::Square: nvgLineCap(mVG, NVG_SQUARE);   break;
  }
  
  switch (options.mJoinOption)
  {
    case ELineJoin::Miter: nvgLineJoin(mVG, NVG_MITER);   break;
    case ELineJoin::Round: nvgLineJoin(mVG, NVG_ROUND);   break;
    case ELineJoin::Bevel: nvgLineJoin(mVG, NVG_BEVEL);   break;
  }
  
  nvgMiterLimit(mVG, options.mMiterLimit);
  nvgStrokeWidth(mVG, thickness);
 
  // NanoVG does not support dashed paths
  if (pattern.mType == EPatternType::Solid)
    nvgStrokeColor(mVG, NanoVGColor(pattern.GetStop(0).mColor, pBlend));
  else
    nvgStrokePaint(mVG, NanoVGPaint(mVG, pattern, pBlend));
  
  nvgPathWinding(mVG, NVG_CCW);
  NanoVGSetBlendMode(mVG, pBlend);
  nvgStroke(mVG);
  nvgGlobalCompositeOperation(mVG, NVG_SOURCE_OVER);
    
  if (!options.mPreserve)
    nvgBeginPath(mVG); // Clears the path state
}

void IGraphicsNanoVG::PathFill(const IPattern& pattern, const IFillOptions& options, const IBlend* pBlend)
{
  switch(options.mFillRule)
  {
    // This concept of fill vs. even/odd winding does not really translate to nanovg.
    // Instead the caller is responsible for settting winding correctly for each subpath
    // based on whether it's a solid (NVG_CCW) or hole (NVG_CW).
    case EFillRule::Winding:
      nvgPathWinding(mVG, NVG_CCW);
      break;
    case EFillRule::EvenOdd:
      nvgPathWinding(mVG, NVG_CW);
      break;
    case EFillRule::Preserve:
      // don't set a winding rule for the path, to preserve individual windings on subpaths
    default:
      break;
  }
  
  if (pattern.mType == EPatternType::Solid)
    nvgFillColor(mVG, NanoVGColor(pattern.GetStop(0).mColor, pBlend));
  else
    nvgFillPaint(mVG, NanoVGPaint(mVG, pattern, pBlend));
  
  NanoVGSetBlendMode(mVG, pBlend);
  nvgFill(mVG);
  nvgGlobalCompositeOperation(mVG, NVG_SOURCE_OVER);

  if (!options.mPreserve)
    nvgBeginPath(mVG); // Clears the path state
}

bool IGraphicsNanoVG::LoadAPIFont(const char* fontID, const PlatformFontPtr& font)
{
  StaticStorage<IFontData>::Accessor storage(sFontCache);
  IFontData* cached = storage.Find(fontID);
    
  if (cached)
  {
    nvgCreateFontFaceMem(mVG, fontID, cached->Get(), cached->GetSize(), cached->GetFaceIdx(), 0);
    return true;
  }
    
  IFontDataPtr data = font->GetFontData();

  if (data->IsValid() && nvgCreateFontFaceMem(mVG, fontID, data->Get(), data->GetSize(), data->GetFaceIdx(), 0) != -1)
  {
    storage.Add(data.release(), fontID);
    return true;
  }

  return false;
}

void IGraphicsNanoVG::UpdateLayer()
{
  if (mLayers.empty())
  {
    nvgEndFrame(mVG);
#ifdef IGRAPHICS_GL
    glViewport(0, 0, WindowWidth() * GetScreenScale(), WindowHeight() * GetScreenScale());
#endif
    nvgBindFramebuffer(mMainFrameBuffer);
    nvgBeginFrame(mVG, WindowWidth(), WindowHeight(), GetScreenScale());
  }
  else
  {
    nvgEndFrame(mVG);
#ifdef IGRAPHICS_GL
    const double scale = GetBackingPixelScale();
    glViewport(0, 0, mLayers.top()->Bounds().W() * scale, mLayers.top()->Bounds().H() * scale);
#endif
    nvgBindFramebuffer(dynamic_cast<const Bitmap*>(mLayers.top()->GetAPIBitmap())->GetFBO());
    nvgBeginFrame(mVG, mLayers.top()->Bounds().W() * GetDrawScale(), mLayers.top()->Bounds().H() * GetDrawScale(), GetScreenScale());
  }
}

void IGraphicsNanoVG::PathTransformSetMatrix(const IMatrix& m)
{
  double xTranslate = 0.0;
  double yTranslate = 0.0;
  
  if (!mLayers.empty())
  {
    IRECT bounds = mLayers.top()->Bounds();
    
    xTranslate = -bounds.L;
    yTranslate = -bounds.T;
  }
  
  nvgResetTransform(mVG);
  nvgScale(mVG, GetDrawScale(), GetDrawScale());
  nvgTranslate(mVG, xTranslate, yTranslate);
  nvgTransform(mVG, m.mXX, m.mYX, m.mXY, m.mYY, m.mTX, m.mTY);
}

void IGraphicsNanoVG::SetClipRegion(const IRECT& r)
{
  nvgScissor(mVG, r.L, r.T, r.W(), r.H());
}

void IGraphicsNanoVG::DrawDottedLine(const IColor& color, float x1, float y1, float x2, float y2, const IBlend* pBlend, float thickness, float dashLen)
{
  const float xd = x1 - x2;
  const float yd = y1 - y2;
  const float len = std::sqrt(xd * xd + yd * yd);
  
  const float segs = std::round(len / dashLen);
  const float incr = 1.f / segs;

  float xs = x1;
  float ys = y1;

  PathMoveTo(xs, ys);

  for (int i = 1; i < static_cast<int>(segs); i+=2)
  {
    float progress = incr * static_cast<float>(i);
  
    float xe = x1 + progress * (x2 - x1);
    float ye = y1 + progress * (y2 - y1);
    
    PathLineTo(xe, ye);
    
    progress += incr;
    
    xs = x1 + progress * (x2 - x1);
    ys = y1 + progress * (y2 - y1);
    
    PathMoveTo(xs, ys);
  }
  
  PathStroke(color, thickness, IStrokeOptions(), pBlend);
}

void IGraphicsNanoVG::DrawDottedRect(const IColor& color, const IRECT& bounds, const IBlend* pBlend, float thickness, float dashLen)
{
  const int xsegs = static_cast<int>(std::ceil(bounds.W() / (dashLen * 2.f)));
  const int ysegs = static_cast<int>(std::ceil(bounds.H() / (dashLen * 2.f)));
  
  float x1 = bounds.L;
  float y1 = bounds.T;
  
  float x2 = x1;
  float y2 = y1;
  
  PathMoveTo(x1, y1);

  for(int j = 0; j < 2; j++)
  {
    for (int i = 0; i < xsegs; i++)
    {
      x2 = Clip(x1 + dashLen, bounds.L, bounds.R);
      PathLineTo(x2, y2);
      x1 = Clip(x2 + dashLen, bounds.L, bounds.R);
      PathMoveTo(x1, y1);
    }
    
    x2 = x1;
    
    for (int i = 0; i < ysegs; i++)
    {
      y2 = Clip(y1 + dashLen, bounds.T, bounds.B);
      PathLineTo(x2, y2);
      y1 = Clip(y2 + dashLen, bounds.T, bounds.B);
      PathMoveTo(x1, y1);
    }
    
    y2 = y1;
    
    dashLen = -dashLen;
  }
  
  PathStroke(color, thickness, IStrokeOptions(), pBlend);
}

void IGraphicsNanoVG::DeleteFBO(NVGframebuffer* pBuffer)
{
  if (!mInDraw)
    nvgDeleteFramebuffer(pBuffer);
  else
  {
    WDL_MutexLock lock(&mFBOMutex);
    mFBOStack.push(pBuffer);
  }
}

void IGraphicsNanoVG::ClearFBOStack()
{
  WDL_MutexLock lock(&mFBOMutex);
  while (!mFBOStack.empty())
  {
    nvgDeleteFramebuffer(mFBOStack.top());
    mFBOStack.pop();
  }
}

void IGraphicsNanoVG::DrawFastDropShadow(const IRECT& innerBounds, const IRECT& outerBounds, float xyDrop, float roundness, float blur, IBlend* pBlend)
{
  NVGpaint shadowPaint = nvgBoxGradient(mVG, innerBounds.L + xyDrop, innerBounds.T + xyDrop, innerBounds.W(), innerBounds.H(), roundness, blur, NanoVGColor(COLOR_BLACK_DROP_SHADOW, pBlend), NanoVGColor(COLOR_TRANSPARENT, nullptr));
  nvgBeginPath(mVG);
  nvgRect(mVG, outerBounds.L, outerBounds.T, outerBounds.W(), outerBounds.H());
  nvgFillPaint(mVG, shadowPaint);
  nvgGlobalCompositeOperation(mVG, NVG_SOURCE_OVER);
  nvgFill(mVG);
  nvgBeginPath(mVG);
}

void IGraphicsNanoVG::DrawMultiLineText(const IText& text, const char* str, const IRECT& bounds, const IBlend* pBlend)
{
  nvgSave(mVG);
  nvgFontSize(mVG, text.mSize);
  nvgFontFace(mVG, text.mFont);
 
  float x = 0.0, y = 0.0;
  const float width = bounds.W();
  int align = 0;
  float yOffsetScale = 0.0;
  
  switch (text.mAlign)
  {
    case EAlign::Near:     align = NVG_ALIGN_LEFT;     x = bounds.L;        break;
    case EAlign::Center:   align = NVG_ALIGN_CENTER;   x = bounds.MW();     break;
    case EAlign::Far:      align = NVG_ALIGN_RIGHT;    x = bounds.R;        break;
  }
    
  switch (text.mVAlign)
  {
    case EVAlign::Top:
    {
      align |= NVG_ALIGN_TOP;
      y = bounds.T;
      yOffsetScale = 0.0;
      break;
    }
    case EVAlign::Middle:
    {
      align |= NVG_ALIGN_MIDDLE;
      y = bounds.MH();
      yOffsetScale = 0.5;
      break;
    }
    case EVAlign::Bottom:
    {
      align |= NVG_ALIGN_BOTTOM;
      y = bounds.B;
      yOffsetScale = 1.0;
      break;
    }
  }
  
  nvgTextAlign(mVG, align);
  
  NVGtextRow rows[3];
  const char* start;
  const char* end;
  int nRows = 0;
  int lines = 0;
  float lineHeight;
  nvgTextMetrics(mVG, NULL, NULL, &lineHeight);
  nvgFillColor(mVG, NanoVGColor(text.mFGColor, pBlend));

  for (auto run : {0, 1})
  {
    start = str;
    end = str + strlen(str);
    while ((nRows = nvgTextBreakLines(mVG, start, end, width, rows, 3))) {
      for (int i = 0; i < nRows; i++) {
        if (run == 0)
        {
          lines++;
        }
        else
        {
          NVGtextRow* row = &rows[i];
          nvgText(mVG, x, y - ((lines*lineHeight)*yOffsetScale), row->start, row->end);
          y += lineHeight;
        }
      }
      start = rows[nRows-1].next;
    }
  }
  
  nvgRestore(mVG);
}
