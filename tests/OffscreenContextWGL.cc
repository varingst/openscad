﻿/*

Create an OpenGL context without creating an OpenGL Window. for Windows.

For more info:

   http://www.nullterminator.net/opengl32.html by Blaine Hodge 
   http://msdn.microsoft.com/en-us/library/ee418815(v=vs.85).aspx
   http://www.cprogramming.com/tutorial/wgl_wiggle_functions.html by RoD
    ( which includes robot.cc by Steven Billington )
   http://blogs.msdn.com/b/oldnewthing/archive/2006/12/04/1205831.aspx by Tom
*/

#include <windows.h>

#include "OffscreenContext.h"
#include "printutils.h"
#include "imageutils.h"
#include "fbo.h"
#include <vector>
#include <GL/gl.h>

using namespace std;

struct OffscreenContext
{
  HWND window;
  HDC dev_context;
  HGLRC openGLContext;
  int width;
  int height;
  fbo_t *fbo;
};

void offscreen_context_init(OffscreenContext &ctx, int width, int height)
{
  ctx.window = (HWND)NULL;
  ctx.dev_context = (HDC)NULL;
  ctx.openGLContext = (HGLRC)NULL;
  ctx.width = width;
  ctx.height = height;
  ctx.fbo = NULL;
}

void glewCheck() {
#ifdef DEBUG
  cerr << "GLEW version " << glewGetString(GLEW_VERSION) << "\n";
  cerr << (const char *)glGetString(GL_RENDERER) << "(" << (const char *)glGetString(GL_VENDOR) << ")\n"
       << "OpenGL version " << (const char *)glGetString(GL_VERSION) << "\n";
  //cerr  << "Extensions: " << (const char *)glGetString(GL_EXTENSIONS) << "\n";

  if (GLEW_ARB_framebuffer_object) {
    cerr << "ARB_FBO supported\n";
  }
  if (GLEW_EXT_framebuffer_object) {
    cerr << "EXT_FBO supported\n";
  }
  if (GLEW_EXT_packed_depth_stencil) {
    cerr << "EXT_packed_depth_stencil\n";
  }
#endif
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) 
{
  return DefWindowProc( hwnd, message, wparam, lparam );
}

bool create_wgl_dummy_context(OffscreenContext &ctx)
{
  // this function alters ctx->window and ctx->openGLContext 
  //  and ctx->dev_context if successfull

  // create window

  HINSTANCE inst = GetModuleHandle(0);
  WNDCLASS wc;
  ZeroMemory( &wc, sizeof( wc ) );
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.lpszClassName = "OpenSCAD";
  RegisterClass( &wc );

  HWND window = CreateWindow( "OpenSCAD", "OpenSCAD", 
    WS_CAPTION | WS_POPUPWINDOW,  //| WS_VISIBLE,
    0, 0, ctx.width, ctx.height, NULL, NULL, inst, NULL );

  if ( window==NULL ) {
    cerr << "MS GDI - CreateWindow failed\n";
    return false;
  }

  // create WGL context, make current

  PIXELFORMATDESCRIPTOR pixformat;
  int chosenformat;
  HDC dev_context = GetDC( window );
  if ( dev_context == NULL ) {
      cerr << "MS GDI - GetDC failed\n";
      return false;
  }

  ZeroMemory( &pixformat, sizeof( pixformat ) );
  pixformat.nSize = sizeof( pixformat );
  pixformat.nVersion = 1;
  pixformat.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL;
  pixformat.iPixelType = PFD_TYPE_RGBA;
  pixformat.cColorBits = 24;
  pixformat.cDepthBits = 16;
  pixformat.iLayerType = PFD_MAIN_PLANE;
  chosenformat = ChoosePixelFormat( dev_context, &pixformat );
  if (chosenformat==0) {
    cerr << "MS GDI - ChoosePixelFormat failed\n";
    return false;
  }

  bool spfok = SetPixelFormat( dev_context, chosenformat, &pixformat );
  if (!spfok) {
    cerr << "MS GDI - SetPixelFormat failed\n";
    return false;
  }

  HGLRC gl_render_context = wglCreateContext( dev_context );
  if ( gl_render_context == NULL ) {
      cerr << "MS WGL - wglCreateContext failed\n";
      ReleaseDC( ctx.window, ctx.dev_context );
      return false;
  }

  bool mcok = wglMakeCurrent( dev_context, gl_render_context );
  if (!mcok) {
    cerr << "MS WGL - wglMakeCurrent failed\n";
    return false;
  }

  ctx.window = window;
  ctx.dev_context = dev_context;
  ctx.openGLContext = gl_render_context;

  return true;
}


OffscreenContext *create_offscreen_context(int w, int h)
{
  OffscreenContext *ctx = new OffscreenContext;
  offscreen_context_init( *ctx, w, h );

  // Before an FBO can be setup, a WGL context must be created. 
  // This call alters ctx->window and ctx->openGLContext 
  //  and ctx->dev_context if successfull
  if (!create_wgl_dummy_context( *ctx )) {
    return NULL;
  }

  GLenum err = glewInit(); // must come after Context creation and before FBO calls.
  if (GLEW_OK != err) {
    cerr << "Unable to init GLEW: " << glewGetErrorString(err) << "\n";
    return NULL;
  }
  glewCheck();

  ctx->fbo = fbo_new();
  if (!fbo_init(ctx->fbo, w, h)) {
    return NULL;
  }

  return ctx;
}

bool teardown_offscreen_context(OffscreenContext *ctx)
{
  if (ctx) {
    fbo_unbind(ctx->fbo);
    fbo_delete(ctx->fbo);

    wglMakeCurrent( NULL, NULL );
    wglDeleteContext( ctx->openGLContext );
    ReleaseDC( ctx->window, ctx->dev_context );

    return true;
  }
  return false;
}

/*!
  Capture framebuffer from OpenGL and write it to the given filename as PNG.
*/
bool save_framebuffer(OffscreenContext *ctx, const char *filename)
{
  if (!ctx || !filename) return false;
  int samplesPerPixel = 4; // R, G, B and A
  vector<GLubyte> pixels(ctx->width * ctx->height * samplesPerPixel);
  glReadPixels(0, 0, ctx->width, ctx->height, GL_RGBA, GL_UNSIGNED_BYTE, &pixels[0]);

  // Flip it vertically - images read from OpenGL buffers are upside-down
  int rowBytes = samplesPerPixel * ctx->width;
  unsigned char *flippedBuffer = (unsigned char *)malloc(rowBytes * ctx->height);
  if (!flippedBuffer) {
    std::cerr << "Unable to allocate flipped buffer for corrected image.";
    return 1;
  }
  flip_image(&pixels[0], flippedBuffer, samplesPerPixel, ctx->width, ctx->height);

  bool writeok = write_png(filename, flippedBuffer, ctx->width, ctx->height);

  free(flippedBuffer);

  return writeok;
}

void bind_offscreen_context(OffscreenContext *ctx)
{
  if (ctx) fbo_bind(ctx->fbo);
}
