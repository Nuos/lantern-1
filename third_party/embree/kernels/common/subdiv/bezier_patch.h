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

#include "catmullclark_patch.h"
#include "bezier_curve.h"

namespace embree
{  
  template<class T, class S>
    static __forceinline T deCasteljau(const S& uu, const T& v0, const T& v1, const T& v2, const T& v3)
  {
    const S one_minus_uu = 1.0f - uu;      
    const T v0_1 = one_minus_uu * v0   + uu * v1;
    const T v1_1 = one_minus_uu * v1   + uu * v2;
    const T v2_1 = one_minus_uu * v2   + uu * v3;      
    const T v0_2 = one_minus_uu * v0_1 + uu * v1_1;
    const T v1_2 = one_minus_uu * v1_1 + uu * v2_1;      
    const T v0_3 = one_minus_uu * v0_2 + uu * v1_2;
    return v0_3;
  }
  
  template<class T, class S>
    static __forceinline T deCasteljau_tangent(const S& uu, const T& v0, const T& v1, const T& v2, const T& v3)
  {
    const S one_minus_uu = 1.0f - uu;      
    const T v0_1         = one_minus_uu * v0   + uu * v1;
    const T v1_1         = one_minus_uu * v1   + uu * v2;
    const T v2_1         = one_minus_uu * v2   + uu * v3;      
    const T v0_2         = one_minus_uu * v0_1 + uu * v1_1;
    const T v1_2         = one_minus_uu * v1_1 + uu * v2_1;      
    return S(3.0f)*(v1_2-v0_2);
  }

  class BezierBasis
  {
  public:

    template<class T>
      static __forceinline Vec4<T>  eval(const T& uu)
    {
      const T t  =  uu;
      const T s  = 1.0f - t;
      const T n0 = s * s * s;
      const T n1 = 3.0f * (s * t * s);
      const T n2 = 3.0f * (t * s * t);
      const T n3 = t * t * t;
      return Vec4<T>(n0,n1,n2,n3);
    }
    
    template<class T>
      static __forceinline Vec4<T>  derivative(const T& u)
    {
      const T t  = u;
      const T s  = 1.0f - u;
      const T n0 = -3.0f*(s*s);
      const T n1 = -6.0f*(s*t) + 3.0f*(s*s);
      const T n2 = +6.0f*(s*t) - 3.0f*(t*t);
      const T n3 = 3.0f*(t*t);
      return Vec4<T>(n0,n1,n2,n3);
    }

    template<class T>
      static __forceinline Vec4<T>  derivative2(const T& u)
    {
      const T t1 = u;
      const T t0 = 1.0f - t1;
      const T n0 = 6.0f*t0;
      const T n1 = 6.0f*t1 - 12.0f*t0;
      const T n2 = 6.0f*t0 - 12.0f*t1;
      const T n3 = 6.0f*t1;
      return Vec4<T>(n0,n1,n2,n3);
    }
  };

  template<typename Vertex>
    __forceinline Vertex computeInnerBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x) {
    return 1.0f / 36.0f * (16.0f * v[y][x] + 4.0f * (v[y-1][x] +  v[y+1][x] + v[y][x-1] + v[y][x+1]) + (v[y-1][x-1] + v[y+1][x+1] + v[y-1][x+1] + v[y+1][x-1]));
  }
  
  template<typename Vertex>
    __forceinline Vertex computeTopEdgeBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x) {
    return 1.0f / 18.0f * (8.0f * v[y][x] + 4.0f * v[y-1][x] + 2.0f * (v[y][x-1] + v[y][x+1]) + (v[y-1][x-1] + v[y-1][x+1]));
  }

  template<typename Vertex>
    __forceinline Vertex computeBottomEdgeBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x) {
    return 1.0f / 18.0f * (8.0f * v[y][x] + 4.0f * v[y+1][x] + 2.0f * (v[y][x-1] + v[y][x+1]) + v[y+1][x-1] + v[y+1][x+1]);
  }
  
  template<typename Vertex>
    __forceinline Vertex computeLeftEdgeBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x) {
    return 1.0f / 18.0f * (8.0f * v[y][x] + 4.0f * v[y][x-1] + 2.0f * (v[y-1][x] + v[y+1][x]) + v[y-1][x-1] + v[y+1][x-1]);
  }
  
  template<typename Vertex>
    __forceinline Vertex computeRightEdgeBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x) {
    return 1.0f / 18.0f * (8.0f * v[y][x] + 4.0f * v[y][x+1] + 2.0f * (v[y-1][x] + v[y+1][x]) + v[y-1][x+1] + v[y+1][x+1]);
  }
  
  template<typename Vertex>
    __forceinline Vertex computeCornerBezierControlPoint(const Vertex v[4][4], const size_t y, const size_t x, const ssize_t delta_y, const ssize_t delta_x)
  {
    return 1.0f / 9.0f * (4.0f * v[y][x] + 2.0f * (v[y+delta_y][x] + v[y][x+delta_x]) + v[y+delta_y][x+delta_x]);
  }

  template<typename Vertex, typename Vertex_t>
    class __aligned(64) BezierPatchT
  {
   public:
      Vertex matrix[4][4];
    
  public:

    __forceinline BezierPatchT() {}

    __forceinline BezierPatchT (const HalfEdge* edge, const char* vertices, size_t stride);

    __forceinline BezierPatchT(const CatmullClarkPatchT<Vertex,Vertex_t>& patch);

    __forceinline BezierPatchT(const CatmullClarkPatchT<Vertex,Vertex_t>& patch,
                               const BezierCurveT<Vertex>* border0,
                               const BezierCurveT<Vertex>* border1,
                               const BezierCurveT<Vertex>* border2,
                               const BezierCurveT<Vertex>* border3);
                               
    __forceinline BezierPatchT(const BSplinePatchT<Vertex,Vertex_t>& source)
    {
      /* compute inner bezier control points */
      matrix[0][0] = computeInnerBezierControlPoint(source.v,1,1);
      matrix[0][3] = computeInnerBezierControlPoint(source.v,1,2);
      matrix[3][3] = computeInnerBezierControlPoint(source.v,2,2);
      matrix[3][0] = computeInnerBezierControlPoint(source.v,2,1);
      
      /* compute top edge control points */
      matrix[0][1] = computeRightEdgeBezierControlPoint(source.v,1,1);
      matrix[0][2] = computeLeftEdgeBezierControlPoint(source.v,1,2); 
      
      /* compute buttom edge control points */
      matrix[3][1] = computeRightEdgeBezierControlPoint(source.v,2,1);
      matrix[3][2] = computeLeftEdgeBezierControlPoint(source.v,2,2);
      
      /* compute left edge control points */
      matrix[1][0] = computeBottomEdgeBezierControlPoint(source.v,1,1);
      matrix[2][0] = computeTopEdgeBezierControlPoint(source.v,2,1);
      
      /* compute right edge control points */
      matrix[1][3] = computeBottomEdgeBezierControlPoint(source.v,1,2);
      matrix[2][3] = computeTopEdgeBezierControlPoint(source.v,2,2);
      
      /* compute corner control points */
      matrix[1][1] = computeCornerBezierControlPoint(source.v,1,1, 1, 1);
      matrix[1][2] = computeCornerBezierControlPoint(source.v,1,2, 1,-1);
      matrix[2][2] = computeCornerBezierControlPoint(source.v,2,2,-1,-1);
      matrix[2][1] = computeCornerBezierControlPoint(source.v,2,1,-1, 1);      
    }

    static __forceinline Vertex_t eval(const Vertex matrix[4][4], const float uu, const float vv) 
    {      
      const Vec4f Bu = BezierBasis::eval(uu);
      const Vec4f Bv = BezierBasis::eval(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t eval_du(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vec4f Bu = BezierBasis::derivative(uu);
      const Vec4f Bv = BezierBasis::eval(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t eval_dv(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vec4f Bu = BezierBasis::eval(uu);
      const Vec4f Bv = BezierBasis::derivative(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t eval_dudu(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vec4f Bu = BezierBasis::derivative2(uu);
      const Vec4f Bv = BezierBasis::eval(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t eval_dvdv(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vec4f Bu = BezierBasis::eval(uu);
      const Vec4f Bv = BezierBasis::derivative2(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t eval_dudv(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vec4f Bu = BezierBasis::derivative(uu);
      const Vec4f Bv = BezierBasis::derivative(vv);
      return 
        (Bu.x * matrix[0][0] + Bu.y * matrix[0][1] + Bu.z * matrix[0][2] + Bu.w * matrix[0][3]) * Bv.x + 
        (Bu.x * matrix[1][0] + Bu.y * matrix[1][1] + Bu.z * matrix[1][2] + Bu.w * matrix[1][3]) * Bv.y + 
        (Bu.x * matrix[2][0] + Bu.y * matrix[2][1] + Bu.z * matrix[2][2] + Bu.w * matrix[2][3]) * Bv.z + 
        (Bu.x * matrix[3][0] + Bu.y * matrix[3][1] + Bu.z * matrix[3][2] + Bu.w * matrix[3][3]) * Bv.w; 
    }

    static __forceinline Vertex_t normal(const Vertex matrix[4][4], const float uu, const float vv) 
    {
      const Vertex_t dPdu = eval_du(matrix,uu,vv);
      const Vertex_t dPdv = eval_dv(matrix,uu,vv);
      return cross(dPdv,dPdu);
    }

    __forceinline Vertex_t normal(const float uu, const float vv) 
    {
      const Vertex_t dPdu = eval_du(matrix,uu,vv);
      const Vertex_t dPdv = eval_dv(matrix,uu,vv);
      return cross(dPdv,dPdu);
    }

    __forceinline Vertex_t eval(const float uu, const float vv) const {
      return eval(matrix,uu,vv);
    }

    __forceinline Vertex_t eval_du(const float uu, const float vv) const { 
      return eval_du(matrix,uu,vv);
    }

    __forceinline Vertex_t eval_dv(const float uu, const float vv) const {
      return eval_dv(matrix,uu,vv);
    }

    __forceinline Vertex_t eval_dudu(const float uu, const float vv) const { 
      return eval_dudu(matrix,uu,vv);
    }
    
    __forceinline Vertex_t eval_dvdv(const float uu, const float vv) const { 
      return eval_dvdv(matrix,uu,vv);
    }

    __forceinline Vertex_t eval_dudv(const float uu, const float vv) const { 
      return eval_dudv(matrix,uu,vv);
    }

    __forceinline void eval(const float u, const float v, Vertex* P, Vertex* dPdu, Vertex* dPdv, Vertex* ddPdudu, Vertex* ddPdvdv, Vertex* ddPdudv, const float dscale = 1.0f) const
    {
      if (P) {
        *P = eval(u,v); 
      }
      if (dPdu) {
        assert(dPdu); *dPdu = eval_du(u,v)*dscale; 
        assert(dPdv); *dPdv = eval_dv(u,v)*dscale; 
      }
      if (ddPdudu) {
        assert(ddPdudu); *ddPdudu = eval_dudu(u,v)*sqr(dscale); 
        assert(ddPdvdv); *ddPdvdv = eval_dvdv(u,v)*sqr(dscale); 
        assert(ddPdudv); *ddPdudv = eval_dudv(u,v)*sqr(dscale); 
      }
    }

    template<class vfloat>
      __forceinline vfloat eval(const size_t i, const vfloat& uu, const vfloat& vv, const Vec4<vfloat>& u_n, const Vec4<vfloat>& v_n) const
      {
        const vfloat curve0_x = v_n[0] * vfloat(matrix[0][0][i]) + v_n[1] * vfloat(matrix[1][0][i]) + v_n[2] * vfloat(matrix[2][0][i]) + v_n[3] * vfloat(matrix[3][0][i]);
        const vfloat curve1_x = v_n[0] * vfloat(matrix[0][1][i]) + v_n[1] * vfloat(matrix[1][1][i]) + v_n[2] * vfloat(matrix[2][1][i]) + v_n[3] * vfloat(matrix[3][1][i]);
        const vfloat curve2_x = v_n[0] * vfloat(matrix[0][2][i]) + v_n[1] * vfloat(matrix[1][2][i]) + v_n[2] * vfloat(matrix[2][2][i]) + v_n[3] * vfloat(matrix[3][2][i]);
        const vfloat curve3_x = v_n[0] * vfloat(matrix[0][3][i]) + v_n[1] * vfloat(matrix[1][3][i]) + v_n[2] * vfloat(matrix[2][3][i]) + v_n[3] * vfloat(matrix[3][3][i]);
        return u_n[0] * curve0_x + u_n[1] * curve1_x + u_n[2] * curve2_x + u_n[3] * curve3_x;
      }

    template<typename vbool, typename vfloat>
      __forceinline void eval(const vbool& valid, const vfloat& uu, const vfloat& vv, 
                              float* P, float* dPdu, float* dPdv, float* ddPdudu, float* ddPdvdv, float* ddPdudv,
                              const float dscale, const size_t dstride, const size_t N) const
      {
        if (P) {
          const Vec4<vfloat> u_n = BezierBasis::eval(uu); 
          const Vec4<vfloat> v_n = BezierBasis::eval(vv); 
          for (size_t i=0; i<N; i++) vfloat::store(valid,P+i*dstride,eval(i,uu,vv,u_n,v_n));
        }
        if (dPdu) 
        {
          {
            assert(dPdu);
            const Vec4<vfloat> u_n = BezierBasis::derivative(uu);
            const Vec4<vfloat> v_n = BezierBasis::eval(vv); 
            for (size_t i=0; i<N; i++) vfloat::store(valid,dPdu+i*dstride,eval(i,uu,vv,u_n,v_n)*dscale);
          }
          {
            assert(dPdv);
            const Vec4<vfloat> u_n = BezierBasis::eval(uu);
            const Vec4<vfloat> v_n = BezierBasis::derivative(vv); 
            for (size_t i=0; i<N; i++) vfloat::store(valid,dPdv+i*dstride,eval(i,uu,vv,u_n,v_n)*dscale);
          }
        }
        if (ddPdudu) 
        {
          {
            assert(ddPdudu);
            const Vec4<vfloat> u_n = BezierBasis::derivative2(uu);
            const Vec4<vfloat> v_n = BezierBasis::eval(vv); 
            for (size_t i=0; i<N; i++) vfloat::store(valid,ddPdudu+i*dstride,eval(i,uu,vv,u_n,v_n)*sqr(dscale));
          }
          {
            assert(ddPdvdv);
            const Vec4<vfloat> u_n = BezierBasis::eval(uu);
            const Vec4<vfloat> v_n = BezierBasis::derivative2(vv); 
            for (size_t i=0; i<N; i++) vfloat::store(valid,ddPdvdv+i*dstride,eval(i,uu,vv,u_n,v_n)*sqr(dscale));
          }
          {
            assert(ddPdudv);
            const Vec4<vfloat> u_n = BezierBasis::derivative(uu);
            const Vec4<vfloat> v_n = BezierBasis::derivative(vv); 
            for (size_t i=0; i<N; i++) vfloat::store(valid,ddPdudv+i*dstride,eval(i,uu,vv,u_n,v_n)*sqr(dscale));
          }
        }
      }
    
    template<class T>
      static __forceinline Vec3<T> eval(const Vertex matrix[4][4], const T& uu, const T& vv) 
    {      
      const T one_minus_uu = 1.0f - uu;
      const T one_minus_vv = 1.0f - vv;      

      const T B0_u = one_minus_uu * one_minus_uu * one_minus_uu;
      const T B0_v = one_minus_vv * one_minus_vv * one_minus_vv;
      const T B1_u = 3.0f * (one_minus_uu * uu * one_minus_uu);
      const T B1_v = 3.0f * (one_minus_vv * vv * one_minus_vv);
      const T B2_u = 3.0f * (uu * one_minus_uu * uu);
      const T B2_v = 3.0f * (vv * one_minus_vv * vv);
      const T B3_u = uu * uu * uu;
      const T B3_v = vv * vv * vv;
      
      const T x = 
	(B0_u * matrix[0][0].x + B1_u * matrix[0][1].x + B2_u * matrix[0][2].x + B3_u * matrix[0][3].x) * B0_v + 
	(B0_u * matrix[1][0].x + B1_u * matrix[1][1].x + B2_u * matrix[1][2].x + B3_u * matrix[1][3].x) * B1_v + 
	(B0_u * matrix[2][0].x + B1_u * matrix[2][1].x + B2_u * matrix[2][2].x + B3_u * matrix[2][3].x) * B2_v + 
	(B0_u * matrix[3][0].x + B1_u * matrix[3][1].x + B2_u * matrix[3][2].x + B3_u * matrix[3][3].x) * B3_v; 
      
      const T y = 
	(B0_u * matrix[0][0].y + B1_u * matrix[0][1].y + B2_u * matrix[0][2].y + B3_u * matrix[0][3].y) * B0_v + 
	(B0_u * matrix[1][0].y + B1_u * matrix[1][1].y + B2_u * matrix[1][2].y + B3_u * matrix[1][3].y) * B1_v + 
	(B0_u * matrix[2][0].y + B1_u * matrix[2][1].y + B2_u * matrix[2][2].y + B3_u * matrix[2][3].y) * B2_v + 
	(B0_u * matrix[3][0].y + B1_u * matrix[3][1].y + B2_u * matrix[3][2].y + B3_u * matrix[3][3].y) * B3_v; 
      
      const T z = 
	(B0_u * matrix[0][0].z + B1_u * matrix[0][1].z + B2_u * matrix[0][2].z + B3_u * matrix[0][3].z) * B0_v + 
	(B0_u * matrix[1][0].z + B1_u * matrix[1][1].z + B2_u * matrix[1][2].z + B3_u * matrix[1][3].z) * B1_v + 
	(B0_u * matrix[2][0].z + B1_u * matrix[2][1].z + B2_u * matrix[2][2].z + B3_u * matrix[2][3].z) * B2_v + 
	(B0_u * matrix[3][0].z + B1_u * matrix[3][1].z + B2_u * matrix[3][2].z + B3_u * matrix[3][3].z) * B3_v; 
            
      return Vec3<T>(x,y,z);
    }

    template<typename vfloat>
      __forceinline Vec3<vfloat> eval(const vfloat& uu, const vfloat& vv) const {     
      return eval(matrix,uu,vv);
    }

    template<class T>
      static __forceinline Vec3<T> normal(const Vertex matrix[4][4], const T& uu, const T& vv) 
    {
      
      const Vec3<T> matrix_00 = Vec3<T>(matrix[0][0].x,matrix[0][0].y,matrix[0][0].z);
      const Vec3<T> matrix_01 = Vec3<T>(matrix[0][1].x,matrix[0][1].y,matrix[0][1].z);
      const Vec3<T> matrix_02 = Vec3<T>(matrix[0][2].x,matrix[0][2].y,matrix[0][2].z);
      const Vec3<T> matrix_03 = Vec3<T>(matrix[0][3].x,matrix[0][3].y,matrix[0][3].z);

      const Vec3<T> matrix_10 = Vec3<T>(matrix[1][0].x,matrix[1][0].y,matrix[1][0].z);
      const Vec3<T> matrix_11 = Vec3<T>(matrix[1][1].x,matrix[1][1].y,matrix[1][1].z);
      const Vec3<T> matrix_12 = Vec3<T>(matrix[1][2].x,matrix[1][2].y,matrix[1][2].z);
      const Vec3<T> matrix_13 = Vec3<T>(matrix[1][3].x,matrix[1][3].y,matrix[1][3].z);

      const Vec3<T> matrix_20 = Vec3<T>(matrix[2][0].x,matrix[2][0].y,matrix[2][0].z);
      const Vec3<T> matrix_21 = Vec3<T>(matrix[2][1].x,matrix[2][1].y,matrix[2][1].z);
      const Vec3<T> matrix_22 = Vec3<T>(matrix[2][2].x,matrix[2][2].y,matrix[2][2].z);
      const Vec3<T> matrix_23 = Vec3<T>(matrix[2][3].x,matrix[2][3].y,matrix[2][3].z);

      const Vec3<T> matrix_30 = Vec3<T>(matrix[3][0].x,matrix[3][0].y,matrix[3][0].z);
      const Vec3<T> matrix_31 = Vec3<T>(matrix[3][1].x,matrix[3][1].y,matrix[3][1].z);
      const Vec3<T> matrix_32 = Vec3<T>(matrix[3][2].x,matrix[3][2].y,matrix[3][2].z);
      const Vec3<T> matrix_33 = Vec3<T>(matrix[3][3].x,matrix[3][3].y,matrix[3][3].z);
            
      /* tangentU */
      const Vec3<T> col0 = deCasteljau(vv, matrix_00, matrix_10, matrix_20, matrix_30);
      const Vec3<T> col1 = deCasteljau(vv, matrix_01, matrix_11, matrix_21, matrix_31);
      const Vec3<T> col2 = deCasteljau(vv, matrix_02, matrix_12, matrix_22, matrix_32);
      const Vec3<T> col3 = deCasteljau(vv, matrix_03, matrix_13, matrix_23, matrix_33);
      
      const Vec3<T> tangentU = deCasteljau_tangent(uu, col0, col1, col2, col3);
      
      /* tangentV */
      const Vec3<T> row0 = deCasteljau(uu, matrix_00, matrix_01, matrix_02, matrix_03);
      const Vec3<T> row1 = deCasteljau(uu, matrix_10, matrix_11, matrix_12, matrix_13);
      const Vec3<T> row2 = deCasteljau(uu, matrix_20, matrix_21, matrix_22, matrix_23);
      const Vec3<T> row3 = deCasteljau(uu, matrix_30, matrix_31, matrix_32, matrix_33);
      
      const Vec3<T> tangentV = deCasteljau_tangent(vv, row0, row1, row2, row3);
      
      /* normal = tangentU x tangentV */
      const Vec3<T> n = cross(tangentV,tangentU);
      return n;
    }

    template<typename vfloat>
      __forceinline Vec3<vfloat> normal(const vfloat& uu, const vfloat& vv) const {     
      return normal(matrix,uu,vv);
    }
  };

  typedef BezierPatchT<Vec3fa,Vec3fa_t> BezierPatch3fa;
}
