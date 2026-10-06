#include "game/first_person_world_gpu.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <vector>
namespace fallout {
namespace {
static void (APIENTRY* pGenFramebuffers)(GLsizei, GLuint*) = nullptr;
static void (APIENTRY* pBindFramebuffer)(GLenum, GLuint) = nullptr;
static void (APIENTRY* pFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint) = nullptr;
static GLenum (APIENTRY* pCheckFramebufferStatus)(GLenum) = nullptr;
static void (APIENTRY* pDeleteFramebuffers)(GLsizei, const GLuint*) = nullptr;
static void (APIENTRY* pGenTextures)(GLsizei, GLuint*) = nullptr;
static void (APIENTRY* pBindTexture)(GLenum, GLuint) = nullptr;
static void (APIENTRY* pTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*) = nullptr;
static void (APIENTRY* pTexParameteri)(GLenum, GLenum, GLint) = nullptr;
static void (APIENTRY* pDeleteTextures)(GLsizei, const GLuint*) = nullptr;
static void (APIENTRY* pDrawBuffers)(GLsizei, const GLenum*) = nullptr;
static void (APIENTRY* pClearBufferfv)(GLenum, GLint, const GLfloat*) = nullptr;
static void (APIENTRY* pClearBufferuiv)(GLenum, GLint, const GLuint*) = nullptr;
static void (APIENTRY* pViewport)(GLint, GLint, GLsizei, GLsizei) = nullptr;
static void (APIENTRY* pEnable)(GLenum) = nullptr;
static void (APIENTRY* pDisable)(GLenum) = nullptr;
static void (APIENTRY* pScissor)(GLint, GLint, GLsizei, GLsizei) = nullptr;
static void (APIENTRY* pDepthFunc)(GLenum) = nullptr;
static void (APIENTRY* pDepthMask)(GLboolean) = nullptr;
static void (APIENTRY* pPixelStorei)(GLenum, GLint) = nullptr;
static void (APIENTRY* pReadBuffer)(GLenum) = nullptr;
static void (APIENTRY* pReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*) = nullptr;
static GLuint (APIENTRY* pCreateShader)(GLenum) = nullptr;
static void (APIENTRY* pShaderSource)(GLuint, GLsizei, const GLchar* const*, const GLint*) = nullptr;
static void (APIENTRY* pCompileShader)(GLuint) = nullptr;
static void (APIENTRY* pGetShaderiv)(GLuint, GLenum, GLint*) = nullptr;
static void (APIENTRY* pGetShaderInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
static void (APIENTRY* pDeleteShader)(GLuint) = nullptr;
static GLuint (APIENTRY* pCreateProgram)(void) = nullptr;
static void (APIENTRY* pAttachShader)(GLuint, GLuint) = nullptr;
static void (APIENTRY* pLinkProgram)(GLuint) = nullptr;
static void (APIENTRY* pGetProgramiv)(GLuint, GLenum, GLint*) = nullptr;
static void (APIENTRY* pGetProgramInfoLog)(GLuint, GLsizei, GLsizei*, GLchar*) = nullptr;
static void (APIENTRY* pDeleteProgram)(GLuint) = nullptr;
static void (APIENTRY* pUseProgram)(GLuint) = nullptr;
static GLint (APIENTRY* pGetUniformLocation)(GLuint, const GLchar*) = nullptr;
static void (APIENTRY* pUniform1ui)(GLint, GLuint) = nullptr;
static void (APIENTRY* pUniform1i)(GLint, GLint) = nullptr;
static void (APIENTRY* pUniform1f)(GLint, GLfloat) = nullptr;
static void (APIENTRY* pUniform2f)(GLint, GLfloat, GLfloat) = nullptr;
static void (APIENTRY* pGenVertexArrays)(GLsizei, GLuint*) = nullptr;
static void (APIENTRY* pBindVertexArray)(GLuint) = nullptr;
static void (APIENTRY* pDeleteVertexArrays)(GLsizei, const GLuint*) = nullptr;
static void (APIENTRY* pGenBuffers)(GLsizei, GLuint*) = nullptr;
static void (APIENTRY* pBindBuffer)(GLenum, GLuint) = nullptr;
static void (APIENTRY* pBufferData)(GLenum, GLsizeiptr, const void*, GLenum) = nullptr;
static void (APIENTRY* pDeleteBuffers)(GLsizei, const GLuint*) = nullptr;
static void (APIENTRY* pEnableVertexAttribArray)(GLuint) = nullptr;
static void (APIENTRY* pVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*) = nullptr;
static void (APIENTRY* pDrawArrays)(GLenum, GLint, GLsizei) = nullptr;
static void (APIENTRY* pActiveTexture)(GLenum) = nullptr;
static GLenum (APIENTRY* pGetError)(void) = nullptr;
static bool loadGl() {
    pGenFramebuffers = reinterpret_cast<decltype(pGenFramebuffers)>(SDL_GL_GetProcAddress("glGenFramebuffers")); if (!pGenFramebuffers) return false;
    pBindFramebuffer = reinterpret_cast<decltype(pBindFramebuffer)>(SDL_GL_GetProcAddress("glBindFramebuffer")); if (!pBindFramebuffer) return false;
    pFramebufferTexture2D = reinterpret_cast<decltype(pFramebufferTexture2D)>(SDL_GL_GetProcAddress("glFramebufferTexture2D")); if (!pFramebufferTexture2D) return false;
    pCheckFramebufferStatus = reinterpret_cast<decltype(pCheckFramebufferStatus)>(SDL_GL_GetProcAddress("glCheckFramebufferStatus")); if (!pCheckFramebufferStatus) return false;
    pDeleteFramebuffers = reinterpret_cast<decltype(pDeleteFramebuffers)>(SDL_GL_GetProcAddress("glDeleteFramebuffers")); if (!pDeleteFramebuffers) return false;
    pGenTextures = reinterpret_cast<decltype(pGenTextures)>(SDL_GL_GetProcAddress("glGenTextures")); if (!pGenTextures) return false;
    pBindTexture = reinterpret_cast<decltype(pBindTexture)>(SDL_GL_GetProcAddress("glBindTexture")); if (!pBindTexture) return false;
    pTexImage2D = reinterpret_cast<decltype(pTexImage2D)>(SDL_GL_GetProcAddress("glTexImage2D")); if (!pTexImage2D) return false;
    pTexParameteri = reinterpret_cast<decltype(pTexParameteri)>(SDL_GL_GetProcAddress("glTexParameteri")); if (!pTexParameteri) return false;
    pDeleteTextures = reinterpret_cast<decltype(pDeleteTextures)>(SDL_GL_GetProcAddress("glDeleteTextures")); if (!pDeleteTextures) return false;
    pDrawBuffers = reinterpret_cast<decltype(pDrawBuffers)>(SDL_GL_GetProcAddress("glDrawBuffers")); if (!pDrawBuffers) return false;
    pClearBufferfv = reinterpret_cast<decltype(pClearBufferfv)>(SDL_GL_GetProcAddress("glClearBufferfv")); if (!pClearBufferfv) return false;
    pClearBufferuiv = reinterpret_cast<decltype(pClearBufferuiv)>(SDL_GL_GetProcAddress("glClearBufferuiv")); if (!pClearBufferuiv) return false;
    pViewport = reinterpret_cast<decltype(pViewport)>(SDL_GL_GetProcAddress("glViewport")); if (!pViewport) return false;
    pEnable = reinterpret_cast<decltype(pEnable)>(SDL_GL_GetProcAddress("glEnable")); if (!pEnable) return false;
    pDisable = reinterpret_cast<decltype(pDisable)>(SDL_GL_GetProcAddress("glDisable")); if (!pDisable) return false;
    pScissor = reinterpret_cast<decltype(pScissor)>(SDL_GL_GetProcAddress("glScissor")); if (!pScissor) return false;
    pDepthFunc = reinterpret_cast<decltype(pDepthFunc)>(SDL_GL_GetProcAddress("glDepthFunc")); if (!pDepthFunc) return false;
    pDepthMask = reinterpret_cast<decltype(pDepthMask)>(SDL_GL_GetProcAddress("glDepthMask")); if (!pDepthMask) return false;
    pPixelStorei = reinterpret_cast<decltype(pPixelStorei)>(SDL_GL_GetProcAddress("glPixelStorei")); if (!pPixelStorei) return false;
    pReadBuffer = reinterpret_cast<decltype(pReadBuffer)>(SDL_GL_GetProcAddress("glReadBuffer")); if (!pReadBuffer) return false;
    pReadPixels = reinterpret_cast<decltype(pReadPixels)>(SDL_GL_GetProcAddress("glReadPixels")); if (!pReadPixels) return false;
    pCreateShader = reinterpret_cast<decltype(pCreateShader)>(SDL_GL_GetProcAddress("glCreateShader")); if (!pCreateShader) return false;
    pShaderSource = reinterpret_cast<decltype(pShaderSource)>(SDL_GL_GetProcAddress("glShaderSource")); if (!pShaderSource) return false;
    pCompileShader = reinterpret_cast<decltype(pCompileShader)>(SDL_GL_GetProcAddress("glCompileShader")); if (!pCompileShader) return false;
    pGetShaderiv = reinterpret_cast<decltype(pGetShaderiv)>(SDL_GL_GetProcAddress("glGetShaderiv")); if (!pGetShaderiv) return false;
    pGetShaderInfoLog = reinterpret_cast<decltype(pGetShaderInfoLog)>(SDL_GL_GetProcAddress("glGetShaderInfoLog")); if (!pGetShaderInfoLog) return false;
    pDeleteShader = reinterpret_cast<decltype(pDeleteShader)>(SDL_GL_GetProcAddress("glDeleteShader")); if (!pDeleteShader) return false;
    pCreateProgram = reinterpret_cast<decltype(pCreateProgram)>(SDL_GL_GetProcAddress("glCreateProgram")); if (!pCreateProgram) return false;
    pAttachShader = reinterpret_cast<decltype(pAttachShader)>(SDL_GL_GetProcAddress("glAttachShader")); if (!pAttachShader) return false;
    pLinkProgram = reinterpret_cast<decltype(pLinkProgram)>(SDL_GL_GetProcAddress("glLinkProgram")); if (!pLinkProgram) return false;
    pGetProgramiv = reinterpret_cast<decltype(pGetProgramiv)>(SDL_GL_GetProcAddress("glGetProgramiv")); if (!pGetProgramiv) return false;
    pGetProgramInfoLog = reinterpret_cast<decltype(pGetProgramInfoLog)>(SDL_GL_GetProcAddress("glGetProgramInfoLog")); if (!pGetProgramInfoLog) return false;
    pDeleteProgram = reinterpret_cast<decltype(pDeleteProgram)>(SDL_GL_GetProcAddress("glDeleteProgram")); if (!pDeleteProgram) return false;
    pUseProgram = reinterpret_cast<decltype(pUseProgram)>(SDL_GL_GetProcAddress("glUseProgram")); if (!pUseProgram) return false;
    pGetUniformLocation = reinterpret_cast<decltype(pGetUniformLocation)>(SDL_GL_GetProcAddress("glGetUniformLocation")); if (!pGetUniformLocation) return false;
    pUniform1ui = reinterpret_cast<decltype(pUniform1ui)>(SDL_GL_GetProcAddress("glUniform1ui")); if (!pUniform1ui) return false;
    pUniform1i = reinterpret_cast<decltype(pUniform1i)>(SDL_GL_GetProcAddress("glUniform1i")); if (!pUniform1i) return false;
    pUniform1f = reinterpret_cast<decltype(pUniform1f)>(SDL_GL_GetProcAddress("glUniform1f")); if (!pUniform1f) return false;
    pUniform2f = reinterpret_cast<decltype(pUniform2f)>(SDL_GL_GetProcAddress("glUniform2f")); if (!pUniform2f) return false;
    pGenVertexArrays = reinterpret_cast<decltype(pGenVertexArrays)>(SDL_GL_GetProcAddress("glGenVertexArrays")); if (!pGenVertexArrays) return false;
    pBindVertexArray = reinterpret_cast<decltype(pBindVertexArray)>(SDL_GL_GetProcAddress("glBindVertexArray")); if (!pBindVertexArray) return false;
    pDeleteVertexArrays = reinterpret_cast<decltype(pDeleteVertexArrays)>(SDL_GL_GetProcAddress("glDeleteVertexArrays")); if (!pDeleteVertexArrays) return false;
    pGenBuffers = reinterpret_cast<decltype(pGenBuffers)>(SDL_GL_GetProcAddress("glGenBuffers")); if (!pGenBuffers) return false;
    pBindBuffer = reinterpret_cast<decltype(pBindBuffer)>(SDL_GL_GetProcAddress("glBindBuffer")); if (!pBindBuffer) return false;
    pBufferData = reinterpret_cast<decltype(pBufferData)>(SDL_GL_GetProcAddress("glBufferData")); if (!pBufferData) return false;
    pDeleteBuffers = reinterpret_cast<decltype(pDeleteBuffers)>(SDL_GL_GetProcAddress("glDeleteBuffers")); if (!pDeleteBuffers) return false;
    pEnableVertexAttribArray = reinterpret_cast<decltype(pEnableVertexAttribArray)>(SDL_GL_GetProcAddress("glEnableVertexAttribArray")); if (!pEnableVertexAttribArray) return false;
    pVertexAttribPointer = reinterpret_cast<decltype(pVertexAttribPointer)>(SDL_GL_GetProcAddress("glVertexAttribPointer")); if (!pVertexAttribPointer) return false;
    pDrawArrays = reinterpret_cast<decltype(pDrawArrays)>(SDL_GL_GetProcAddress("glDrawArrays")); if (!pDrawArrays) return false;
    pActiveTexture = reinterpret_cast<decltype(pActiveTexture)>(SDL_GL_GetProcAddress("glActiveTexture")); if (!pActiveTexture) return false;
    pGetError = reinterpret_cast<decltype(pGetError)>(SDL_GL_GetProcAddress("glGetError")); if (!pGetError) return false;
    return true;
}
struct Texture { std::uint64_t hash; int width, height; GLuint name; };
struct Proxy { std::array<FirstPersonGpuVertex, 4> vertices; std::uint32_t owner; };
static SDL_Window* window = nullptr;
static SDL_GLContext context = nullptr;
static SDL_Window* previousWindow = nullptr;
static SDL_GLContext previousContext = nullptr;
static bool failed = false;
static GLuint program = 0, vao = 0, vbo = 0, fbo = 0;
static GLuint targets[4] {};
static int frameWidth = 0, frameHeight = 0;
static std::vector<Texture> textures;
static std::vector<Proxy> proxies;
static GLint sizeUniform = -1, ownerUniform = -1, farUniform = -1;
constexpr float nearZ = 0.45f, farZ = 128.0f;
static void restore() { SDL_GL_MakeCurrent(previousWindow, previousContext); }
static GLuint shader(GLenum type, const char* source) {
    GLuint name = pCreateShader(type);
    pShaderSource(name, 1, &source, nullptr); pCompileShader(name);
    GLint ok = 0; pGetShaderiv(name, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[2048] {}; pGetShaderInfoLog(name, sizeof(log), nullptr, log);
        SDL_Log("FP GPU shader: %s", log); pDeleteShader(name); return 0; }
    return name;
}
static bool initialize() {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    window = SDL_CreateWindow("Fallout FP offscreen", 0, 0, 16, 16,
        SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!window) return false;
    context = SDL_GL_CreateContext(window);
    if (!context || !loadGl()) return false;
    const char* vs = R"(#version 330 core
layout(location=0) in vec3 position;
layout(location=1) in vec2 texcoord;
uniform vec2 size;
out vec2 uv;
void main() {
    float n=0.45, f=128.0;
    gl_Position=vec4(2.0*position.x/size.x-position.z,
        position.z-2.0*position.y/size.y,
        (f+n)/(f-n)*position.z-2.0*f*n/(f-n), position.z);
    uv=texcoord;
})";
    const char* fs = R"(#version 330 core
in vec2 uv;
uniform sampler2D art;
uniform uint owner;
uniform float farLimit;
layout(location=0) out float indexed;
layout(location=1) out uint picked;
layout(location=2) out uint assisted;
void main() {
    if (1.0/gl_FragCoord.w > farLimit) discard;
    float index=texture(art,uv).r;
    if(index<0.5/255.0) discard;
    indexed=index; picked=owner; assisted=owner;
})";
    GLuint vertex = shader(GL_VERTEX_SHADER, vs), fragment = shader(GL_FRAGMENT_SHADER, fs);
    if (!vertex || !fragment) { if(vertex) pDeleteShader(vertex); if(fragment) pDeleteShader(fragment); return false; }
    program = pCreateProgram(); pAttachShader(program, vertex); pAttachShader(program, fragment);
    pLinkProgram(program); pDeleteShader(vertex); pDeleteShader(fragment);
    GLint ok = 0; pGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) { char log[2048] {}; pGetProgramInfoLog(program,sizeof(log),nullptr,log); SDL_Log("FP GPU link: %s",log); return false; }
    sizeUniform = pGetUniformLocation(program,"size"); ownerUniform = pGetUniformLocation(program,"owner");
    farUniform = pGetUniformLocation(program,"farLimit");
    pUseProgram(program); pUniform1i(pGetUniformLocation(program,"art"),0);
    pGenVertexArrays(1,&vao); pBindVertexArray(vao); pGenBuffers(1,&vbo); pBindBuffer(GL_ARRAY_BUFFER,vbo);
    pEnableVertexAttribArray(0); pVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(FirstPersonGpuVertex),nullptr);
    pEnableVertexAttribArray(1); pVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(FirstPersonGpuVertex),reinterpret_cast<const void*>(3*sizeof(float)));
    pGenFramebuffers(1,&fbo); pGenTextures(4,targets);
    SDL_Log("FP GPU world renderer initialized (OpenGL 3.3 offscreen)");
    return true;
}
static bool resize(int w, int h) {
    if (w==frameWidth && h==frameHeight) return true;
    pBindFramebuffer(GL_FRAMEBUFFER,fbo);
    for(int i=0;i<4;i++) {
        pBindTexture(GL_TEXTURE_2D,targets[i]);
        GLenum format=i==0?GL_RED:(i==3?GL_DEPTH_COMPONENT:GL_RED_INTEGER);
        GLint internal=i==0?GL_R8:(i==3?GL_DEPTH_COMPONENT24:GL_R32UI);
        pTexImage2D(GL_TEXTURE_2D,0,internal,w,h,0,format,i==0?GL_UNSIGNED_BYTE:GL_UNSIGNED_INT,nullptr);
        pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        pFramebufferTexture2D(GL_FRAMEBUFFER,i==3?GL_DEPTH_ATTACHMENT:GL_COLOR_ATTACHMENT0+i,GL_TEXTURE_2D,targets[i],0);
    }
    if(pCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE) return false;
    frameWidth=w; frameHeight=h; return true;
}
static GLuint texture(const unsigned char* pixels,int w,int h) {
    std::uint64_t hash=1469598103934665603ull;
    for(int i=0;i<w*h;i++) hash=(hash^pixels[i])*1099511628211ull;
    for(const auto& t:textures) if(t.hash==hash && t.width==w && t.height==h) return t.name;
    GLuint name=0; pGenTextures(1,&name); pBindTexture(GL_TEXTURE_2D,name);
    pTexImage2D(GL_TEXTURE_2D,0,GL_R8,w,h,0,GL_RED,GL_UNSIGNED_BYTE,pixels);
    pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    pTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    textures.push_back({hash,w,h,name}); return name;
}
static void draw(GLuint tex,const FirstPersonGpuVertex* v,std::uint32_t owner) {
    const FirstPersonGpuVertex triangles[6]={v[0],v[1],v[2],v[0],v[2],v[3]};
    pBindTexture(GL_TEXTURE_2D,tex); pUniform1ui(ownerUniform,owner);
    pBufferData(GL_ARRAY_BUFFER,sizeof(triangles),triangles,GL_STREAM_DRAW); pDrawArrays(GL_TRIANGLES,0,6);
}
}
bool first_person_world_gpu_begin(int w,int h,int horizon,unsigned char sky,unsigned char ground) {
    if(failed || std::getenv("FALLOUT_FP_SOFTWARE") || w<=0 || h<=0) return false;
    previousWindow=SDL_GL_GetCurrentWindow(); previousContext=SDL_GL_GetCurrentContext();
    if(!context) {
        if(!initialize()) { SDL_Log("FP GPU unavailable: %s; using software",SDL_GetError()); failed=true; restore(); return false; }
    } else if(SDL_GL_MakeCurrent(window,context)!=0) { failed=true; restore(); return false; }
    if(!resize(w,h)) { failed=true; restore(); return false; }
    // Bounded art cache. Indexed textures remain correct under native palette fades.
    if(textures.size()>2048) { for(auto& t:textures) { pDeleteTextures(1,&t.name); }
        textures.clear(); }
    proxies.clear(); pBindFramebuffer(GL_FRAMEBUFFER,fbo);
    const GLenum outputs[3]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_COLOR_ATTACHMENT2};
    pDrawBuffers(3,outputs); pViewport(0,0,w,h); pDisable(GL_BLEND); pDisable(GL_CULL_FACE);
    pDisable(GL_SCISSOR_TEST); pEnable(GL_DEPTH_TEST); pDepthFunc(GL_LESS); pDepthMask(GL_TRUE);
    const GLfloat background[4]={sky/255.0f,0,0,0}, depth=1.0f; const GLuint zero[4]={0,0,0,0};
    pClearBufferfv(GL_COLOR,0,background); pClearBufferuiv(GL_COLOR,1,zero); pClearBufferuiv(GL_COLOR,2,zero); pClearBufferfv(GL_DEPTH,0,&depth);
    const GLfloat groundColor[4]={ground/255.0f,0,0,0};
    pEnable(GL_SCISSOR_TEST); pScissor(0,0,w,std::clamp(h-horizon,0,h)); pClearBufferfv(GL_COLOR,0,groundColor); pDisable(GL_SCISSOR_TEST);
    const GLenum sceneOutputs[3]={GL_COLOR_ATTACHMENT0,GL_COLOR_ATTACHMENT1,GL_NONE}; pDrawBuffers(3,sceneOutputs);
    pUseProgram(program); pUniform2f(sizeUniform,static_cast<float>(w),static_cast<float>(h));
    pActiveTexture(GL_TEXTURE0); pPixelStorei(GL_UNPACK_ALIGNMENT,1); pPixelStorei(GL_PACK_ALIGNMENT,1);
    pBindVertexArray(vao); pBindBuffer(GL_ARRAY_BUFFER,vbo); return true;
}
void first_person_world_gpu_quad(const unsigned char* pixels,int w,int h,
    const FirstPersonGpuVertex* vertices,std::uint32_t owner,bool proxy,float farLimit) {
    if(proxy) { Proxy p {}; std::copy(vertices,vertices+4,p.vertices.begin()); p.owner=owner; proxies.push_back(p); return; }
    if(pixels && w>0 && h>0) { pUniform1f(farUniform,farLimit); draw(texture(pixels,w,h),vertices,owner); }
}
bool first_person_world_gpu_read(unsigned char* indexed,float* depth,std::uint32_t* owners,std::uint32_t* assisted) {
    const GLenum outputs[3]={GL_NONE,GL_NONE,GL_COLOR_ATTACHMENT2}; pDrawBuffers(3,outputs);
    pDepthFunc(GL_LEQUAL); pDepthMask(GL_FALSE); pUniform1f(farUniform,farZ);
    std::sort(proxies.begin(),proxies.end(),[](const Proxy& a,const Proxy& b){return a.vertices[0].z>b.vertices[0].z;});
    const unsigned char white=255; GLuint tex=texture(&white,1,1);
    for(const auto& p:proxies) draw(tex,p.vertices.data(),p.owner);
    pReadBuffer(GL_COLOR_ATTACHMENT0); pReadPixels(0,0,frameWidth,frameHeight,GL_RED,GL_UNSIGNED_BYTE,indexed);
    pReadBuffer(GL_COLOR_ATTACHMENT1); pReadPixels(0,0,frameWidth,frameHeight,GL_RED_INTEGER,GL_UNSIGNED_INT,owners);
    pReadBuffer(GL_COLOR_ATTACHMENT2); pReadPixels(0,0,frameWidth,frameHeight,GL_RED_INTEGER,GL_UNSIGNED_INT,assisted);
    pReadPixels(0,0,frameWidth,frameHeight,GL_DEPTH_COMPONENT,GL_FLOAT,depth);
    bool ok=pGetError()==GL_NO_ERROR; if(!ok) { failed=true; SDL_Log("FP GPU frame failed; reverting to software"); }
    restore(); return ok;
}
void first_person_world_gpu_shutdown() {
    if(context && window && pDeleteTextures) {
        SDL_Window* savedWindow=SDL_GL_GetCurrentWindow(); SDL_GLContext saved=SDL_GL_GetCurrentContext();
        SDL_GL_MakeCurrent(window,context);
        for(auto& t:textures) { pDeleteTextures(1,&t.name); }
        textures.clear();
        pDeleteTextures(4,targets); if(fbo)pDeleteFramebuffers(1,&fbo);
        if(vbo)pDeleteBuffers(1,&vbo);
        if(vao)pDeleteVertexArrays(1,&vao);
        if(program)pDeleteProgram(program);
        SDL_GL_MakeCurrent(savedWindow,saved);
    }
    if(context) SDL_GL_DeleteContext(context);
    if(window) SDL_DestroyWindow(window);
    context=nullptr; window=nullptr; program=vao=vbo=fbo=0; frameWidth=frameHeight=0;
}
} // namespace fallout
