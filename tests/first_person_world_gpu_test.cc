#include "game/first_person_world_gpu.h"
#include <SDL.h>
#include <cassert>
#include <cstdio>
#include <vector>
using namespace fallout;
static void quad(int x0,int y0,int x1,int y1,float z,unsigned char* art,int w,int h,unsigned id,bool proxy=false) {
    FirstPersonGpuVertex v[4]={{x0*z,y0*z,z,0,0},{x1*z,y0*z,z,1,0},{x1*z,y1*z,z,1,1},{x0*z,y1*z,z,0,1}};
    first_person_world_gpu_quad(art,w,h,v,id,proxy);
}
int main(){
    assert(SDL_Init(SDL_INIT_VIDEO)==0);
    // Simulate SDL's existing presentation context: the world backend must
    // restore it after reading a frame, not leave the hidden context active.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window* presentation=SDL_CreateWindow("test presentation",0,0,16,16,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    assert(presentation);
    SDL_GLContext saved=SDL_GL_CreateContext(presentation);
    assert(saved);
    if(!first_person_world_gpu_begin(64,64,32,3,4)){std::fprintf(stderr,"GPU unavailable: %s\n",SDL_GetError());return 1;}
    unsigned char back=11, front[4]={0,22,22,22};
    quad(8,8,56,56,4,&back,1,1,17);
    quad(16,16,48,48,2,front,2,2,23);
    quad(16,16,48,48,2,nullptr,0,0,23,true);
    // A closer wall must occlude both exact picks and assisted picks.
    unsigned char wall=31;quad(24,24,32,32,1,&wall,1,1,29);
    std::vector<unsigned char> c(4096);std::vector<float>d(4096);std::vector<unsigned> ids(4096),p(4096);
    assert(first_person_world_gpu_read(c.data(),d.data(),ids.data(),p.data()));
    assert(SDL_GL_GetCurrentContext()==saved);
    auto at=[](int x,int y){return (63-y)*64+x;};
    assert(c[at(1,1)]==3 && c[at(1,60)]==4);
    assert(c[at(12,12)]==11 && ids[at(12,12)]==17);
    assert(c[at(20,20)]==11 && ids[at(20,20)]==17 && p[at(20,20)]==23);
    assert(c[at(40,20)]==22 && ids[at(40,20)]==23);
    assert(c[at(28,28)]==31 && ids[at(28,28)]==29 && p[at(28,28)]==0);
    assert(d[at(28,28)]<d[at(40,20)]);
    // Resize and near-plane clipping: crossing vertices must not drop a face.
    assert(first_person_world_gpu_begin(32,32,16,3,4));
    FirstPersonGpuVertex crossing[4]={{-1,-1,0.2f,0,0},{40,8,2,1,0},
        {40,56,2,1,1},{-1,8,0.2f,0,1}};
    first_person_world_gpu_quad(&back,1,1,crossing,41);
    assert(first_person_world_gpu_read(c.data(),d.data(),ids.data(),p.data()));
    bool visible=false;
    for(int i=0;i<1024;i++) { if(ids[i]==41) visible=true; }
    assert(visible);
    first_person_world_gpu_shutdown();
    SDL_setenv("FALLOUT_FP_SOFTWARE","1",1);
    assert(!first_person_world_gpu_begin(32,32,16,3,4));
    SDL_GL_DeleteContext(saved);
    SDL_DestroyWindow(presentation);
    SDL_Quit();
    std::puts("GPU indexed color, depth, transparency and occluded pick tests passed");
}
