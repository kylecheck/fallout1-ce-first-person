#include "game/first_person_projection.h"
#include "game/first_person_frame.h"
#include <cassert>
#include <iostream>
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
            int split=first_person_background_split(h,first_person_projected_horizon(w,h,pitch));
            assert(split>=0 && split<=h);
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
