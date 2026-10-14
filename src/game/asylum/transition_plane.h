#pragma once
#include "core/vr/flat_preview.h"
#include <array>
namespace bvr::aa {
// One common close plane, covering BOTH asymmetric/canted eye frusta. Full
// head orientation is intentional for an ephemeral screen-covering transition.
inline bool transition_plane(const XrView views[2],float aspect,float clips[2][4][4]){
 if(!std::isfinite(aspect)||aspect<=0)return false;
 const auto&a=views[0].pose;const auto&b=views[1].pose;
 const float sign=a.orientation.x*b.orientation.x+a.orientation.y*b.orientation.y+a.orientation.z*b.orientation.z+a.orientation.w*b.orientation.w<0?-1.f:1.f;
 float q[4]={a.orientation.x+sign*b.orientation.x,a.orientation.y+sign*b.orientation.y,a.orientation.z+sign*b.orientation.z,a.orientation.w+sign*b.orientation.w};
 const float length=std::sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);if(!std::isfinite(length)||length<.001f)return false;for(float&v:q)v/=length;
 const float centre[3]={(a.position.x+b.position.x)*.5f,(a.position.y+b.position.y)*.5f,(a.position.z+b.position.z)*.5f};
 constexpr float distance=.8f;float hw{},hh{};
 for(const auto&v:std::array<XrView,2>{views[0],views[1]}){
  const float delta[3]={v.pose.position.x-centre[0],v.pose.position.y-centre[1],v.pose.position.z-centre[2]};float origin[3];xrmath::quat_rotate(-q[0],-q[1],-q[2],q[3],delta,origin);
  for(float x:{std::tan(v.fov.angleLeft),std::tan(v.fov.angleRight)})for(float y:{std::tan(v.fov.angleDown),std::tan(v.fov.angleUp)}){
   const float ray[3]={x,y,-1};float worldRay[3],local[3];const auto&e=v.pose.orientation;
   xrmath::quat_rotate(e.x,e.y,e.z,e.w,ray,worldRay);xrmath::quat_rotate(-q[0],-q[1],-q[2],q[3],worldRay,local);
   if(!std::isfinite(local[2])||local[2]>=-.001f)return false;const float t=(-distance-origin[2])/local[2];if(!std::isfinite(t)||t<=0)return false;
   hw=std::fmax(hw,std::fabs(origin[0]+t*local[0]));hh=std::fmax(hh,std::fabs(origin[1]+t*local[1]));
  }
 }
 hw=std::fmax(hw,hh*aspect)*1.06f;hh=hw/aspect;
 for(int c=0;c<4;++c){const float local[3]={c&1?hw:-hw,c&2?-hh:hh,-distance};float point[3];xrmath::quat_rotate(q[0],q[1],q[2],q[3],local,point);for(int j=0;j<3;++j)point[j]+=centre[j];for(int e=0;e<2;++e)vr::preview::clip(views[e],point,clips[e][c]);}
 return true;
}
}
