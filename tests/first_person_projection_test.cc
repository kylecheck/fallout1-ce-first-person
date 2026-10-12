#include "game/first_person_projection.h"
#include "game/first_person_frame.h"
#include <cassert>
#include <iostream>
#include <vector>
using namespace fallout;
int main() {
    for (const auto& size : { std::pair<int,int>(1280,800), {800,600}, {640,480} }) {
        int w=size.first,h=size.second;
        assert(first_person_projected_horizon(w,h,0)==h*43/100);
        int down=first_person_projected_horizon(w,h,-55);
        assert(down<0);
        assert(first_person_background_split(h,down)==0);
        assert(first_person_background_split(h,2*h)==h);
        // A center ground ray now reaches within the nearest neighboring hex
        // while remaining in front of the renderer's near plane.
        double z=w*0.70*0.74/(h*0.5-down);
        assert(z>0.45 && z<0.8660254038);
        for (int pitch=-55;pitch<=18;pitch++) {
            const int horizon = first_person_projected_horizon(w,h,pitch);
            int split=first_person_background_split(h,horizon);
            assert(split>=0 && split<=h);
            // Exercise the production guide drawing with the real native
            // draw_line. Guard rows catch the formerly negative framebuffer
            // writes; ASan also catches writes outside this allocation.
            const int guard = w * 2 * h;
            std::vector<unsigned char> pixels(guard + w*h + guard, 0xA5);
            unsigned char* buffer = pixels.data() + guard;
            std::fill(buffer, buffer + w*h, 0);
            first_person_draw_depth_guides(buffer,w,h,horizon,7);
            assert(std::all_of(pixels.begin(),pixels.begin()+guard,
                [](unsigned char pixel) { return pixel==0xA5; }));
            assert(std::all_of(pixels.begin()+guard+w*h,pixels.end(),
                [](unsigned char pixel) { return pixel==0xA5; }));
            if (pitch==0) assert(std::count(buffer,buffer+w*h,7)>0);
        }
    }
    FirstPersonFrameRequest scene;
    for(int frame=0;frame<10;frame++) {
        for(int dirtyRect=0;dirtyRect<100;dirtyRect++) scene.request();
        assert(scene.take(true,false));
        assert(!scene.take(true,false));
    }
    scene.request();
    assert(!scene.take(true,true)); // Modal UI must retain pending work.
    assert(!scene.take(false,false));
    assert(scene.take(true,false));
    scene.request(); assert(scene.take(true,false)); // Fresh click flush.
    assert(!scene.take(true,false)); // Present does not render it again.
    std::cout << "Near-ground look, safe fill bounds and scene coalescing passed\n";
}
