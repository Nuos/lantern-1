// ======================================================================== //
// Copyright 2009-2015 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

#pragma once

#include "../default.h"
#include "../globals.h"

namespace embree
{
  template<typename Vertex>
    struct BezierCurveT
  {
    Vertex v0,v1,v2,v3;
    float t0,t1;
    int depth;

    __forceinline BezierCurveT() {}

    __forceinline BezierCurveT(const Vertex& v0, 
                               const Vertex& v1, 
                               const Vertex& v2, 
                               const Vertex& v3,
                               const float t0 = 0.0f,
                               const float t1 = 1.0f,
                               const int depth = 0)
      : v0(v0), v1(v1), v2(v2), v3(v3), t0(t0), t1(t1), depth(depth) {}

    __forceinline const BBox3fa bounds() const {
      BBox3fa b = merge(BBox3fa(v0),BBox3fa(v1),BBox3fa(v2),BBox3fa(v3));
      return enlarge(b,Vertex(b.upper.w));
    }

    __forceinline void subdivide(BezierCurveT& left, BezierCurveT& right) const
    {
      const Vertex p00 = v0;
      const Vertex p01 = v1;
      const Vertex p02 = v2;
      const Vertex p03 = v3;

      const Vertex p10 = (p00 + p01) * 0.5f;
      const Vertex p11 = (p01 + p02) * 0.5f;
      const Vertex p12 = (p02 + p03) * 0.5f;
      const Vertex p20 = (p10 + p11) * 0.5f;
      const Vertex p21 = (p11 + p12) * 0.5f;
      const Vertex p30 = (p20 + p21) * 0.5f;

      const float t01 = (t0 + t1) * 0.5f;

      left.v0 = p00;
      left.v1 = p10;
      left.v2 = p20;
      left.v3 = p30;
      left.t0 = t0;
      left.t1 = t01;
      left.depth = depth-1;
        
      right.v0 = p30;
      right.v1 = p21;
      right.v2 = p12;
      right.v3 = p03;
      right.t0 = t01;
      right.t1 = t1;
      right.depth = depth-1;
    }

    __forceinline Vertex eval(const float t) const
    {
      const float t0 = 1.0f - t, t1 = t;

      const Vertex p00 = v0;
      const Vertex p01 = v1;
      const Vertex p02 = v2;
      const Vertex p03 = v3;

      const Vertex p10 = p00 * t0 + p01 * t1;
      const Vertex p11 = p01 * t0 + p02 * t1;
      const Vertex p12 = p02 * t0 + p03 * t1;
      const Vertex p20 = p10 * t0 + p11 * t1;
      const Vertex p21 = p11 * t0 + p12 * t1;
      const Vertex p30 = p20 * t0 + p21 * t1;
      return p30;
    }
    
    __forceinline Vertex eval_du(const float t) const
    {
      const float t0 = 1.0f - t, t1 = t;
      float B0 = -3.0f*(t0*t0);
      float B1 = -6.0f*(t0*t1) + 3.0f*(t0*t0);
      float B2 = +6.0f*(t0*t1) - 3.0f*(t1*t1);
      float B3 = +3.0f*(t1*t1);
      return B0*v0 + B1*v1 + B2*v2 + B3*v3;
    }

    __forceinline Vertex eval_dudu(const float t) const
    {
      const float t0 = 1.0f - t, t1 = t;
      float C0 = 6.0f*t0;
      float C1 = 6.0f*t1 - 12.0f*t0;
      float C2 = 6.0f*t0 - 12.0f*t1;
      float C3 = 6.0f*t1;
      return C0*v0 + C1*v1 + C2*v2 + C3*v3;
    }

    friend inline std::ostream& operator<<(std::ostream& cout, const BezierCurveT& curve) {
      return cout << "{ v0 = " << curve.v0 << ", v1 = " << curve.v1 << ", v2 = " << curve.v2 << ", v3 = " << curve.v3 << ", depth = " << curve.depth << " }";
    }
  };

  struct BezierCoefficients
  {
    enum { N = 16 };
  public:
    BezierCoefficients(int shift);
  public:
    float c0[N+1][N];
    float c1[N+1][N];
    float c2[N+1][N];
    float c3[N+1][N];
  };
  extern BezierCoefficients bezier_coeff0;
  extern BezierCoefficients bezier_coeff1;

  struct BezierCurve3fa : public BezierCurveT<Vec3fa>
  {
    //using BezierCurveT<Vec3fa>::BezierCurveT; // FIXME: not supported by VS2010

	__forceinline BezierCurve3fa() {}
	__forceinline BezierCurve3fa(const Vec3fa& v0, const Vec3fa& v1, const Vec3fa& v2, const Vec3fa& v3, const float t0 = 0.0f, const float t1 = 1.0f, const int depth = 0)
      : BezierCurveT<Vec3fa>(v0,v1,v2,v3,t0,t1,depth) {}

#if defined(__SSE__)
    template<int M>
      __forceinline Vec4<vfloat<M>> eval0(const vbool<M>& valid, const int ofs, const int size) const
    {
      assert(size <= BezierCoefficients::N);
      assert(ofs < size);
      Vec4<vfloat<M>> r;
      r  = Vec4<vfloat<M>>(v0) * vfloat<M>::loadu(&bezier_coeff0.c0[size][ofs]);
      r += Vec4<vfloat<M>>(v1) * vfloat<M>::loadu(&bezier_coeff0.c1[size][ofs]); // FIXME: use fmadd
      r += Vec4<vfloat<M>>(v2) * vfloat<M>::loadu(&bezier_coeff0.c2[size][ofs]);
      r += Vec4<vfloat<M>>(v3) * vfloat<M>::loadu(&bezier_coeff0.c3[size][ofs]);
      return r;
    }
#endif

#if defined(__SSE__)
    template<int M>
      __forceinline Vec4<vfloat<M>> eval1(const vbool<M>& valid, const int ofs, const int size) const
    {
      assert(size <= BezierCoefficients::N);
      assert(ofs < size);
      Vec4<vfloat<M>> r;
      r  = Vec4<vfloat<M>>(v0) * vfloat<M>::loadu(&bezier_coeff1.c0[size][ofs]);
      r += Vec4<vfloat<M>>(v1) * vfloat<M>::loadu(&bezier_coeff1.c1[size][ofs]); // FIXME: use fmadd
      r += Vec4<vfloat<M>>(v2) * vfloat<M>::loadu(&bezier_coeff1.c2[size][ofs]);
      r += Vec4<vfloat<M>>(v3) * vfloat<M>::loadu(&bezier_coeff1.c3[size][ofs]);
      return r;
    }
#endif

#if defined(__SSE__)
    __forceinline BBox3fa bounds(int N) const
    {
      if (likely(N == 4))
      {
        const Vec4vf4 pi = eval0(vbool4(true),0,4);
        const Vec3fa lower(reduce_min(pi.x),reduce_min(pi.y),reduce_min(pi.z));
        const Vec3fa upper(reduce_max(pi.x),reduce_max(pi.y),reduce_max(pi.z));
        const Vec3fa upper_r = Vec3fa(reduce_max(abs(pi.w)));
        return enlarge(BBox3fa(min(lower,v3),max(upper,v3)),max(upper_r,Vec3fa(v3.w)));
      } 
      else
      {
        Vec4vfx pl(pos_inf), pu(neg_inf);
        for (int i=0; i<N; i+=VSIZEX)
        {
          vboolx valid = vintx(i)+vintx(step) < vintx(N);
          const Vec4vfx pi = eval0(valid,i,N);
          
          pl.x = select(valid,min(pl.x,pi.x),pl.x); // FIXME: use masked min
          pl.y = select(valid,min(pl.y,pi.y),pl.y); 
          pl.z = select(valid,min(pl.z,pi.z),pl.z); 
          pl.w = select(valid,min(pl.w,pi.w),pl.w); 
          
          pu.x = select(valid,max(pu.x,pi.x),pu.x); // FIXME: use masked min
          pu.y = select(valid,max(pu.y,pi.y),pu.y); 
          pu.z = select(valid,max(pu.z,pi.z),pu.z); 
          pu.w = select(valid,max(pu.w,pi.w),pu.w); 
        }
        const Vec3fa lower(reduce_min(pl.x),reduce_min(pl.y),reduce_min(pl.z));
        const Vec3fa upper(reduce_max(pu.x),reduce_max(pu.y),reduce_max(pu.z));
        const Vec3fa upper_r = Vec3fa(reduce_max(max(-pl.w,pu.w)));
        return enlarge(BBox3fa(min(lower,v3),max(upper,v3)),max(upper_r,Vec3fa(abs(v3.w))));
      }
    }
#endif
  };
}
