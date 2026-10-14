#pragma once
// Asylum's development preview. A single flat image on a common distant plane,
// projected through each located eye; this is not native world stereo.
#include "core/util/xr_math.h"
#include <openxr/openxr.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace bvr::vr::preview {
inline void clip(const XrView& v,const float p[3],float out[4]);
inline bool plane(const XrView views[2], float sourceAspect, bool fill, float world[4][3]) {
    if(!std::isfinite(sourceAspect) || sourceAspect<=0)return false;
    const auto& a=views[0].pose;const auto& b=views[1].pose;
    const auto& qa=a.orientation;const auto& qb=b.orientation;
    const float sign=qa.x*qb.x+qa.y*qb.y+qa.z*qb.z+qa.w*qb.w<0?-1.f:1.f;
    float q[4]={qa.x+sign*qb.x,qa.y+sign*qb.y,qa.z+sign*qb.z,qa.w+sign*qb.w};
    const float len=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(!std::isfinite(len)||len<0.001f)return false;
    for(auto& v:q)v/=len;
    // Movie screens are vertical in LOCAL/world space. Only the viewer's
    // heading chooses their facing; placing while looking down or rolling
    // the head must never pitch/roll the screen itself.
    const float yaw=std::atan2(2*(q[0]*q[2]+q[3]*q[1]),1-2*(q[0]*q[0]+q[1]*q[1]));
    q[0]=q[2]=0;q[1]=std::sin(yaw*.5f);q[3]=std::cos(yaw*.5f);
    const float centre[3]={(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};
    constexpr float distance=100.f; // effectively infinity; caller anchors once in LOCAL
    float xmin=std::numeric_limits<float>::max(),ymin=xmin,xmax=-xmin,ymax=-xmin;
    for(int e=0;e<2;++e) {
        const auto& v=views[e];
        const float delta[3]={v.pose.position.x-centre[0],v.pose.position.y-centre[1],v.pose.position.z-centre[2]};
        float origin[3];xrmath::quat_rotate(-q[0],-q[1],-q[2],q[3],delta,origin);
        const float xs[2]={std::tan(v.fov.angleLeft),std::tan(v.fov.angleRight)};
        const float ys[2]={std::tan(v.fov.angleDown),std::tan(v.fov.angleUp)};
        for(float x:xs)for(float y:ys) {
            const float ray[3]={x,y,-1};float rotated[3],local[3];
            const auto& eq=v.pose.orientation;
            xrmath::quat_rotate(eq.x,eq.y,eq.z,eq.w,ray,rotated);
            xrmath::quat_rotate(-q[0],-q[1],-q[2],q[3],rotated,local);
            if(!std::isfinite(local[2])||local[2]>=-.001f)return false;
            const float t=(-distance-origin[2])/local[2];
            const float px=origin[0]+t*local[0],py=origin[1]+t*local[1];
            if(!std::isfinite(px)||!std::isfinite(py)||t<=0)return false;
            xmin=std::min(xmin,px);xmax=std::max(xmax,px);
            ymin=std::min(ymin,py);ymax=std::max(ymax,py);
        }
    }
    // The optical frustum is lower than eye level. Its bounding-box centre
    // must not become the screen's placement: that put startup videos low.
    const float fullW=2*std::max(std::fabs(xmin),std::fabs(xmax));
    const float fullH=2*std::max(std::fabs(ymin),std::fabs(ymax));
    const float height=fill?std::max(fullH,fullW/sourceAspect):std::min(ymax-ymin,(xmax-xmin)/sourceAspect);
    if(height<=0)return false;
    // Small coverage margin defeats raster rounding; uniform scaling preserves aspect.
    const float hh=height*.5f*(fill?1.002f:1.f),hw=hh*sourceAspect;
    const float cx=0,cy=0; // centred at the viewer's eye height, facing heading only
    for(int c=0;c<4;++c) {
        const float local[3]={cx+(c&1?hw:-hw),cy+(c&2?-hh:hh),-distance};
        float rotated[3];xrmath::quat_rotate(q[0],q[1],q[2],q[3],local,rotated);
        for(int axis=0;axis<3;++axis)world[c][axis]=centre[axis]+rotated[axis];
    }
    if(!fill) {
        // Fit is binocular: all four source corners must lie inside BOTH
        // asymmetric eyes, not merely inside their combined outer extent.
        float middle[3];for(int axis=0;axis<3;++axis)middle[axis]=(world[0][axis]+world[3][axis])*.5f;
        float scale=1;
        for(int e=0;e<2;++e) {
            float originClip[4];clip(views[e],middle,originClip);
            if(originClip[3]<=0)return false;
            for(int c=0;c<4;++c) {
                float cornerClip[4];clip(views[e],world[c],cornerClip);
                for(int axis=0;axis<2;++axis)for(float sign:{-1.f,1.f}) {
                    const float a=originClip[3]+sign*originClip[axis];
                    const float b=cornerClip[3]+sign*cornerClip[axis];
                    if(a<=0)return false;
                    if(b<0)scale=std::min(scale,a/(a-b));
                }
            }
        }
        scale*=.999f;
        for(int c=0;c<4;++c)for(int axis=0;axis<3;++axis)
            world[c][axis]=middle[axis]+scale*(world[c][axis]-middle[axis]);
    }
    return true;
}
inline void clip(const XrView& v,const float p[3],float out[4]) {
    const auto& e=v.pose;
    const float d[3]={p[0]-e.position.x,p[1]-e.position.y,p[2]-e.position.z};float local[3];
    xrmath::quat_rotate(-e.orientation.x,-e.orientation.y,-e.orientation.z,e.orientation.w,d,local);
    const float l=std::tan(v.fov.angleLeft),r=std::tan(v.fov.angleRight);
    const float u=std::tan(v.fov.angleUp),b=std::tan(v.fov.angleDown),w=-local[2];
    out[0]=(2*local[0]-(r+l)*w)/(r-l);
    out[1]=(2*local[1]-(u+b)*w)/(u-b);
    out[2]=.5f*w;out[3]=w;
}
// Presentation policy shared by runtime and the GPU fixture. A new headset
// pose cannot move a placed screen. Recenter/resize/mode change can.
struct RoomPlane {
    bool valid=false,fill=false;
    unsigned width=0,height=0;
    float world[4][3]{};
    void reset(){valid=false;}
    bool place(const XrView views[2],unsigned w,unsigned h,bool crop){
        if(!w||!h)return false;
        if(!valid||width!=w||height!=h||fill!=crop){
            if(!plane(views,float(w)/h,crop,world))return false;
            width=w;height=h;fill=crop;valid=true;
        }
        return true;
    }
};
}
