#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
namespace bg3::hair_visibility {
using V3=std::array<double,3>;
struct Bounds { V3 lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY}; };
inline bool model_valid(const float m[16]) {
  for(unsigned i=0;i<16;++i)if(!std::isfinite(m[i]))return false;
  if(m[3]!=0 || m[7]!=0 || m[11]!=0 || m[15]!=1)return false;
  const double terms[6]={double(m[0])*m[5]*m[10],double(m[1])*m[6]*m[8],double(m[2])*m[4]*m[9],
                         double(m[2])*m[5]*m[8],double(m[1])*m[4]*m[10],double(m[0])*m[6]*m[9]};
  double determinant=0,magnitude=0,frobenius2=0;
  for(unsigned i=0;i<6;++i){determinant+=(i<3?terms[i]:-terms[i]);magnitude+=std::abs(terms[i]);}
  for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)frobenius2+=double(m[row*4+col])*m[row*4+col];
  constexpr double gamma=128.0*std::numeric_limits<float>::epsilon()/(1.0-128.0*std::numeric_limits<float>::epsilon());
  // |det| / ||M||_F^2 <= sigma_min. Subtract the float arithmetic
  // envelope before using it as a lower bound. Native normalize(M*N)
  // must not square an arbitrarily tiny transformed unit normal.
  const double lower_det=std::abs(determinant)-gamma*magnitude;
  const double lower_sigma=lower_det/(frobenius2*(1+gamma));
  const double required_sigma=std::max(4.0*std::sqrt(double(std::numeric_limits<float>::min())),
                                       4.0*gamma*std::sqrt(frobenius2));
  return std::isfinite(lower_sigma) && lower_sigma>=required_sigma;
}
inline bool add(Bounds& b,const float p[3],const float m[16]) {
  if(!model_valid(m))return false;
  for(unsigned j=0;j<3;++j) {
    double value=m[12+j],scale=std::abs(value);
    for(unsigned k=0;k<3;++k) {
      if(!std::isfinite(p[k]))return false;
      value+=double(p[k])*m[k*4+j];scale+=std::abs(double(p[k])*m[k*4+j]);
    }
    // Outward envelope for native float32 transform and accumulated arithmetic.
    const double error=scale*(128.0*std::numeric_limits<float>::epsilon()/(1.0-128.0*std::numeric_limits<float>::epsilon()));
    if(!std::isfinite(value) || std::abs(value)+error>std::numeric_limits<float>::max()/4)return false;
    b.lo[j]=std::min(b.lo[j],value-error);b.hi[j]=std::max(b.hi[j],value+error);
  }
  return true;
}
// The recovered static VS uses normalized N (component magnitude <=1),
// sin in [-1,1], UV root weight squared in [0,1], and UNORM noise in [0,1].
// Bound every term without requiring its per-vertex noise sample or phase.
inline V3 wind(float strength,float dx,float dz,float material) {
  const double t=double(strength)*0.10000000149011612;
  const double noise=std::abs(1-t)+std::abs(t);
  const double a=-0.30000001192092896+(0.800000011920929+0.30000001192092896)*t;
  const double f=std::max(1.0,std::abs(a));
  const double wave=std::pow(std::max(1.0,f-1),2)+f+1;
  const double first=std::pow(std::max(noise,1e-4),1.5)*std::abs(t)*0.0020000000949949026;
  const double second=wave*0.3333333432674408*noise*std::abs(t)*std::abs(double(material));
  const double gamma=128.0*std::numeric_limits<float>::epsilon()/(1.0-128.0*std::numeric_limits<float>::epsilon());
  return {(first+(1+std::abs(double(dx)))*second)*(1+gamma),
          (first+0.5*second)*(1+gamma),
          (first+(1+std::abs(double(dz)))*second)*(1+gamma)};
}
inline bool expand(Bounds& b,V3 pad) {
  for(unsigned j=0;j<3;++j) {
    if(!std::isfinite(pad[j]) || pad[j]<0)return false;
    b.lo[j]-=pad[j];b.hi[j]+=pad[j];
  }
  return true;
}
enum class Decision { Native, Cards, Unknown };
inline Decision select(const Bounds& b,const float vp[16],double width,double height,double near_m) {
  if(!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||!std::isfinite(near_m)||near_m<0)return Decision::Unknown;
  for(unsigned i=0;i<16;++i)if(!std::isfinite(vp[i]))return Decision::Unknown;
  // The audited subset certifies camera-relative projection with zero clip
  // translation. Other camera origins retain native hair instead of relying
  // on an ill-conditioned inverse for the near-distance protection.
  if(vp[12]!=0 || vp[13]!=0 || vp[15]!=0)return Decision::Unknown;
  // Perspective camera centre solves the clip x/y/w rows. Native W and this
  // projection are in the same camera-relative coordinate system.
  double A[3][4]={{vp[0],vp[4],vp[8],-vp[12]}, {vp[1],vp[5],vp[9],-vp[13]}, {vp[3],vp[7],vp[11],-vp[15]}};
  for(unsigned c=0;c<3;++c) {
    unsigned pivot=c;for(unsigned r=c+1;r<3;++r)if(std::abs(A[r][c])>std::abs(A[pivot][c]))pivot=r;
    if(A[pivot][c]==0 || !std::isfinite(A[pivot][c]))return Decision::Unknown;
    for(unsigned k=0;k<4;++k)std::swap(A[c][k],A[pivot][k]);
    double d=A[c][c];for(unsigned k=c;k<4;++k)A[c][k]/=d;
    for(unsigned r=0;r<3;++r)if(r!=c) {double f=A[r][c];for(unsigned k=c;k<4;++k)A[r][k]-=f*A[c][k];}
  }
  double distance2=0;
  for(unsigned j=0;j<3;++j) {
    if(!std::isfinite(b.lo[j])||!std::isfinite(b.hi[j])||b.lo[j]>b.hi[j]||!std::isfinite(A[j][3]))return Decision::Unknown;
    double d=std::max({b.lo[j]-A[j][3],A[j][3]-b.hi[j],0.0});distance2+=d*d;
  }
  if(distance2<=near_m*near_m)return Decision::Native;
  double lo[2]={INFINITY,INFINITY},hi[2]={-INFINITY,-INFINITY};
  for(unsigned corner=0;corner<8;++corner) {
    double p[3];for(unsigned j=0;j<3;++j)p[j]=(corner&(1u<<j))?b.hi[j]:b.lo[j];
    double clip[3]={vp[12],vp[13],vp[15]},scale[3]={std::abs(clip[0]),std::abs(clip[1]),std::abs(clip[2])};
    for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k) {unsigned col=k==2?3:k;double v=p[j]*vp[j*4+col];clip[k]+=v;scale[k]+=std::abs(v);}
    constexpr double g=128.0*std::numeric_limits<float>::epsilon()/(1.0-128.0*std::numeric_limits<float>::epsilon());
    const double ew=g*scale[2];if(!std::isfinite(clip[2])||clip[2]<=ew)return Decision::Unknown;
    for(unsigned k=0;k<2;++k) {
      double center=clip[k]/clip[2],error=(g*scale[k]+std::abs(center)*ew)/(clip[2]-ew);
      if(!std::isfinite(center)||!std::isfinite(error))return Decision::Unknown;
      lo[k]=std::min(lo[k],center-error);hi[k]=std::max(hi[k],center+error);
    }
  }
  // At most one pixel along BOTH axes, not an area rule that could discard
  // a long thin visible strand. Pixel extent includes float projection error.
  return (hi[0]-lo[0])*width*0.5<=1.0 && (hi[1]-lo[1])*height*0.5<=1.0 ? Decision::Cards : Decision::Native;
}
}
