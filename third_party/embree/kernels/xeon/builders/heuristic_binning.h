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

#include "priminfo.h"
#include "../geometry/bezier1v.h"

#include "../../algorithms/parallel_reduce.h"
#include "../../algorithms/parallel_partition.h"

namespace embree
{
  namespace isa
  { 
    /*! mapping into bins */
    template<size_t BINS>
      struct BinMapping
      {
      public:
        __forceinline BinMapping() {}
        
        /*! calculates the mapping */
        __forceinline BinMapping(const PrimInfo& pinfo) 
        {
#if defined(__AVX512F__) // FIXME: should only be active if the AVX512 binner is used
          num = BINS;
          assert(num == 16);
#else
          num = min(BINS,size_t(4.0f + 0.05f*pinfo.size()));
          //num = BINS;
#endif
          const vfloat4 diag = (vfloat4) pinfo.centBounds.size();
          scale = select(diag > vfloat4(1E-34f),vfloat4(0.99f*num)/diag,vfloat4(0.0f));
          ofs  = (vfloat4) pinfo.centBounds.lower;
#if defined(__AVX512F__)
          scale16 = scale;
          ofs16 = ofs;
#endif          
        }
        
        /*! returns number of bins */
        __forceinline size_t size() const { return num; }
        
        /*! slower but safe binning */
        __forceinline Vec3ia bin(const Vec3fa& p) const 
        {
          const vint4 i = floori((vfloat4(p)-ofs)*scale);
#if 1
          assert(i[0] >=0 && i[0] < num); 
          assert(i[1] >=0 && i[1] < num);
          assert(i[2] >=0 && i[2] < num);
          return Vec3ia(i);
#else
          return Vec3ia(clamp(i,vint4(0),vint4(num-1)));
#endif
        }

        /*! faster but unsafe binning */
        __forceinline Vec3ia bin_unsafe(const Vec3fa& p) const {
          return Vec3ia(floori((vfloat4(p)-ofs)*scale));
        }

#if defined(__AVX512F__)
        __forceinline vint16 bin16(const Vec3fa& p) const {
          return vint16(vint4(floori((vfloat4(p)-ofs)*scale)));
        }

        __forceinline vint16 bin16(const vfloat16& p) const {
          return floori((p-ofs16)*scale16);
        }

        __forceinline int bin_unsafe(const PrimRef &ref,
                                     const vint16  vSplitPos,
                                     const vbool16 splitDimMask) const
          {
            const vfloat16 lower(*(vfloat4*)&ref.lower);
            const vfloat16 upper(*(vfloat4*)&ref.upper);
            const vfloat16 p = lower + upper;
            const vint16 i = floori((p-ofs16)*scale16);
            return lt(splitDimMask,i,vSplitPos);
          }
#endif
        
        
        /*! returns true if the mapping is invalid in some dimension */
        __forceinline bool invalid(const int dim) const {
          return scale[dim] == 0.0f;
        }
        
        /*! stream output */
        friend std::ostream& operator<<(std::ostream& cout, const BinMapping& mapping) {
          return cout << "BinMapping { num = " << mapping.num << ", ofs = " << mapping.ofs << ", scale = " << mapping.scale << "}";
        }
        
      public:
        size_t num;
        vfloat4 ofs,scale;        //!< linear function that maps to bin ID
#if defined(__AVX512F__)
        vfloat16 ofs16,scale16;        //!< linear function that maps to bin ID
#endif
      };
    
    /*! stores all information to perform some split */
    template<size_t BINS>
      struct BinSplit
      {
        /*! construct an invalid split by default */
        __forceinline BinSplit()
          : sah(inf), dim(-1), pos(0) {}
        
        /*! constructs specified split */
        __forceinline BinSplit(float sah, int dim, int pos, const BinMapping<BINS>& mapping)
          : sah(sah), dim(dim), pos(pos), mapping(mapping) {}
        
        /*! tests if this split is valid */
        __forceinline bool valid() const { return dim != -1; }
        
        /*! calculates surface area heuristic for performing the split */
        __forceinline float splitSAH() const { return sah; }
        
        /*! stream output */
        friend std::ostream& operator<<(std::ostream& cout, const BinSplit& split) {
          return cout << "BinSplit { sah = " << split.sah << ", dim = " << split.dim << ", pos = " << split.pos << "}";
        }
        
      public:
        float sah;                //!< SAH cost of the split
        int dim;                  //!< split dimension
        int pos;                  //!< bin index for splitting
        BinMapping<BINS> mapping; //!< mapping into bins
      };
    
    /*! stores extended information about the split */
    struct SplitInfo
    {
      __forceinline SplitInfo () {}
      
      __forceinline SplitInfo (size_t leftCount, const BBox3fa& leftBounds, size_t rightCount, const BBox3fa& rightBounds)
	: leftCount(leftCount), rightCount(rightCount), leftBounds(leftBounds), rightBounds(rightBounds) {}
      
    public:
      size_t leftCount,rightCount;
      BBox3fa leftBounds,rightBounds;
    };
    
    /*! stores all binning information */
    template<size_t BINS, typename PrimRef>
      struct __aligned(64) BinInfo
    {
      typedef BinSplit<BINS> Split;
      
      __forceinline BinInfo() {
      }
      
      __forceinline BinInfo(EmptyTy) {
	clear();
      }
      
      /*! clears the bin info */
      __forceinline void clear() 
      {
	for (size_t i=0; i<BINS; i++) {
	  bounds[i][0] = bounds[i][1] = bounds[i][2] = empty;
	  counts[i] = 0;
	}
      }
      
      /*! bins an array of primitives */
      __forceinline void bin (const PrimRef* prims, size_t N, const BinMapping<BINS>& mapping)
      {
	if (N == 0) return;
        
	size_t i; 
	for (i=0; i<N-1; i+=2)
        {
          /*! map even and odd primitive to bin */
          const BBox3fa prim0 = prims[i+0].bounds(); 
          const Vec3fa center0 = Vec3fa(center2(prim0)); 
          const Vec3ia bin0 = mapping.bin(center0); 
          
          const BBox3fa prim1 = prims[i+1].bounds(); 
          const Vec3fa center1 = Vec3fa(center2(prim1)); 
          const Vec3ia bin1 = mapping.bin(center1); 
          
          /*! increase bounds for bins for even primitive */
          const unsigned int b00 = bin0.x; bounds[b00][0].extend(prim0);
          const unsigned int b01 = bin0.y; bounds[b01][1].extend(prim0);
          const unsigned int b02 = bin0.z; bounds[b02][2].extend(prim0);
          
          counts[b00][0]++;
          counts[b01][1]++;
          counts[b02][2]++;

          /*! increase bounds of bins for odd primitive */
          const unsigned int b10 = bin1.x;  bounds[b10][0].extend(prim1);
          const unsigned int b11 = bin1.y;  bounds[b11][1].extend(prim1);
          const unsigned int b12 = bin1.z;  bounds[b12][2].extend(prim1);

          counts[b10][0]++;
          counts[b11][1]++;
          counts[b12][2]++;
        }
	
	/*! for uneven number of primitives */
	if (i < N)
        {
          /*! map primitive to bin */
          const BBox3fa prim0 = prims[i].bounds(); const Vec3fa center0 = Vec3fa(center2(prim0)); const Vec3ia bin0 = mapping.bin(center0); 
          
          /*! increase bounds of bins */
          const int b00 = bin0.x; counts[b00][0]++; bounds[b00][0].extend(prim0);
          const int b01 = bin0.y; counts[b01][1]++; bounds[b01][1].extend(prim0);
          const int b02 = bin0.z; counts[b02][2]++; bounds[b02][2].extend(prim0);
        }
      }

      /*! bins an array of primitives */
      __forceinline void bin (const PrimRef* prims, size_t N, const BinMapping<BINS>& mapping, const AffineSpace3fa& space)
      {
	if (N == 0) return;
        
	size_t i; 
	for (i=0; i<N-1; i+=2)
        {
          /*! map even and odd primitive to bin */
          const BBox3fa prim0 = prims[i+0].bounds(space); const Vec3fa center0 = Vec3fa(center2(prim0)); const Vec3ia bin0 = mapping.bin(center0); 
          const BBox3fa prim1 = prims[i+1].bounds(space); const Vec3fa center1 = Vec3fa(center2(prim1)); const Vec3ia bin1 = mapping.bin(center1); 
          
          /*! increase bounds for bins for even primitive */
          const int b00 = bin0.x; counts[b00][0]++; bounds[b00][0].extend(prim0);
          const int b01 = bin0.y; counts[b01][1]++; bounds[b01][1].extend(prim0);
          const int b02 = bin0.z; counts[b02][2]++; bounds[b02][2].extend(prim0);
          
          /*! increase bounds of bins for odd primitive */
          const int b10 = bin1.x; counts[b10][0]++; bounds[b10][0].extend(prim1);
          const int b11 = bin1.y; counts[b11][1]++; bounds[b11][1].extend(prim1);
          const int b12 = bin1.z; counts[b12][2]++; bounds[b12][2].extend(prim1);
        }
	
	/*! for uneven number of primitives */
	if (i < N)
        {
          /*! map primitive to bin */
          const BBox3fa prim0 = prims[i].bounds(space); const Vec3fa center0 = Vec3fa(center2(prim0)); const Vec3ia bin0 = mapping.bin(center0); 
          
          /*! increase bounds of bins */
          const int b00 = bin0.x; counts[b00][0]++; bounds[b00][0].extend(prim0);
          const int b01 = bin0.y; counts[b01][1]++; bounds[b01][1].extend(prim0);
          const int b02 = bin0.z; counts[b02][2]++; bounds[b02][2].extend(prim0);
        }
      }
      
      __forceinline void bin(const PrimRef* prims, size_t begin, size_t end, const BinMapping<BINS>& mapping) {
	bin(prims+begin,end-begin,mapping);
      }

      __forceinline void bin(const PrimRef* prims, size_t begin, size_t end, const BinMapping<BINS>& mapping, const AffineSpace3fa& space) {
	bin(prims+begin,end-begin,mapping,space);
      }
      
      /*! merges in other binning information */
      __forceinline void merge (const BinInfo& other, size_t numBins)
      {
	for (size_t i=0; i<numBins; i++) 
        {
          counts[i] += other.counts[i];
          bounds[i][0].extend(other.bounds[i][0]);
          bounds[i][1].extend(other.bounds[i][1]);
          bounds[i][2].extend(other.bounds[i][2]);
        }
      }

      /*! reducesr binning information */
      static __forceinline const BinInfo reduce (const BinInfo& a, const BinInfo& b)
      {
        BinInfo c;
	for (size_t i=0; i<BINS; i++) 
        {
          c.counts[i] = a.counts[i]+b.counts[i];
          c.bounds[i][0] = embree::merge(a.bounds[i][0],b.bounds[i][0]);
          c.bounds[i][1] = embree::merge(a.bounds[i][1],b.bounds[i][1]);
          c.bounds[i][2] = embree::merge(a.bounds[i][2],b.bounds[i][2]);
        }
        return c;
      }
      
      /*! finds the best split by scanning binning information */
      __forceinline Split best(const BinMapping<BINS>& mapping, const size_t blocks_shift) const
      {
	/* sweep from right to left and compute parallel prefix of merged bounds */
	vfloat4 rAreas[BINS];
	vint4 rCounts[BINS];
	vint4 count = 0; BBox3fa bx = empty; BBox3fa by = empty; BBox3fa bz = empty;
	for (size_t i=mapping.size()-1; i>0; i--)
        {
          count += counts[i];
          rCounts[i] = count;
          bx.extend(bounds[i][0]); rAreas[i][0] = halfArea(bx);
          by.extend(bounds[i][1]); rAreas[i][1] = halfArea(by);
          bz.extend(bounds[i][2]); rAreas[i][2] = halfArea(bz);
          rAreas[i][3] = 0.0f;
        }
	/* sweep from left to right and compute SAH */
	vint4 blocks_add = (1 << blocks_shift)-1;
	vint4 ii = 1; vfloat4 vbestSAH = pos_inf; vint4 vbestPos = 0; 
	count = 0; bx = empty; by = empty; bz = empty;
	for (size_t i=1; i<mapping.size(); i++, ii+=1)
        {
          count += counts[i-1];
          bx.extend(bounds[i-1][0]); float Ax = halfArea(bx);
          by.extend(bounds[i-1][1]); float Ay = halfArea(by);
          bz.extend(bounds[i-1][2]); float Az = halfArea(bz);
          const vfloat4 lArea = vfloat4(Ax,Ay,Az,Az);
          const vfloat4 rArea = rAreas[i];
          const vint4 lCount = (count     +blocks_add) >> blocks_shift;
          const vint4 rCount = (rCounts[i]+blocks_add) >> blocks_shift;
          const vfloat4 sah = lArea*vfloat4(lCount) + rArea*vfloat4(rCount);
          vbestPos = select(sah < vbestSAH,ii ,vbestPos);
          vbestSAH = select(sah < vbestSAH,sah,vbestSAH);
        }
	
	/* find best dimension */
	float bestSAH = inf;
	int   bestDim = -1;
	int   bestPos = 0;
	int   bestLeft = 0;
	for (size_t dim=0; dim<3; dim++) 
        {
          /* ignore zero sized dimensions */
          if (unlikely(mapping.invalid(dim)))
            continue;
          
          /* test if this is a better dimension */
          if (vbestSAH[dim] < bestSAH && vbestPos[dim] != 0) {
            bestDim = dim;
            bestPos = vbestPos[dim];
            bestSAH = vbestSAH[dim];
          }
        }
	
	return Split(bestSAH,bestDim,bestPos,mapping);
      }
      
      /*! calculates extended split information */
      __forceinline void getSplitInfo(const BinMapping<BINS>& mapping, const Split& split, SplitInfo& info) const // FIXME: still required?
      {
	if (split.dim == -1) {
	  new (&info) SplitInfo(0,empty,0,empty);
	  return;
	}
	
	size_t leftCount = 0;
	BBox3fa leftBounds = empty;
	for (size_t i=0; i<split.pos; i++) {
	  leftCount += counts[i][split.dim];
	  leftBounds.extend(bounds[i][split.dim]);
	}
	size_t rightCount = 0;
	BBox3fa rightBounds = empty;
	for (size_t i=split.pos; i<mapping.size(); i++) {
	  rightCount += counts[i][split.dim];
	  rightBounds.extend(bounds[i][split.dim]);
	}
	new (&info) SplitInfo(leftCount,leftBounds,rightCount,rightBounds);
      }
      
    private:
      BBox3fa bounds[BINS][3]; //!< geometry bounds for each bin in each dimension
      vint4    counts[BINS];    //!< counts number of primitives that map into the bins
    };

#if defined(__AVX512F__)

    /* 16 bins in-register binner */
    template<typename PrimRef>
      struct __aligned(64) BinInfo<16,PrimRef>
    {
      typedef BinSplit<16> Split;
      
      __forceinline BinInfo() {
      }
      
      __forceinline BinInfo(EmptyTy) {
	clear();
      }
      
      /*! clears the bin info */
      __forceinline void clear() 
      {
        lower[0] = lower[1] = lower[2] = pos_inf;
        upper[0] = upper[1] = upper[2] = neg_inf;
        count[0] = count[1] = count[2] = 0;
      }


      static __forceinline vfloat16 prefix_area_rl(const vfloat16 min_x,
                                                   const vfloat16 min_y,
                                                   const vfloat16 min_z,
                                                   const vfloat16 max_x,
                                                   const vfloat16 max_y,
                                                   const vfloat16 max_z)
      {
        const vfloat16 r_min_x = reverse_prefix_min(min_x);
        const vfloat16 r_min_y = reverse_prefix_min(min_y);
        const vfloat16 r_min_z = reverse_prefix_min(min_z);
        const vfloat16 r_max_x = reverse_prefix_max(max_x);
        const vfloat16 r_max_y = reverse_prefix_max(max_y);
        const vfloat16 r_max_z = reverse_prefix_max(max_z);
        const vfloat16 dx = r_max_x - r_min_x;
        const vfloat16 dy = r_max_y - r_min_y;
        const vfloat16 dz = r_max_z - r_min_z;
        const vfloat16 area_rl = dx*dy+dx*dz+dy*dz;
        return area_rl;
      }

      static __forceinline vfloat16 prefix_area_lr(const vfloat16 min_x,
                                                   const vfloat16 min_y,
                                                   const vfloat16 min_z,
                                                   const vfloat16 max_x,
                                                   const vfloat16 max_y,
                                                   const vfloat16 max_z)
      {
        const vfloat16 r_min_x = prefix_min(min_x);
        const vfloat16 r_min_y = prefix_min(min_y);
        const vfloat16 r_min_z = prefix_min(min_z);
        const vfloat16 r_max_x = prefix_max(max_x);
        const vfloat16 r_max_y = prefix_max(max_y);
        const vfloat16 r_max_z = prefix_max(max_z);
        const vfloat16 dx = r_max_x - r_min_x;
        const vfloat16 dy = r_max_y - r_min_y;
        const vfloat16 dz = r_max_z - r_min_z;
        const vfloat16 area_lr = dx*dy+dx*dz+dy*dz;
        return area_lr;
      }

      /*! bins an array of primitives */
      __forceinline void bin (const PrimRef* prims, size_t N, const BinMapping<16>& mapping)
      {

        const vfloat16 init_min(pos_inf);
        const vfloat16 init_max(neg_inf);

        vfloat16 min_x0,min_x1,min_x2;
        vfloat16 min_y0,min_y1,min_y2;
        vfloat16 min_z0,min_z1,min_z2;
        vfloat16 max_x0,max_x1,max_x2;
        vfloat16 max_y0,max_y1,max_y2;
        vfloat16 max_z0,max_z1,max_z2;
        vint16 count0,count1,count2;

        min_x0 = init_min;
        min_x1 = init_min;
        min_x2 = init_min;
        min_y0 = init_min;
        min_y1 = init_min;
        min_y2 = init_min;
        min_z0 = init_min;
        min_z1 = init_min;
        min_z2 = init_min;

        max_x0 = init_max;
        max_x1 = init_max;
        max_x2 = init_max;
        max_y0 = init_max;
        max_y1 = init_max;
        max_y2 = init_max;
        max_z0 = init_max;
        max_z1 = init_max;
        max_z2 = init_max;

        count0 = vint16::zero();
        count1 = vint16::zero();
        count2 = vint16::zero();

        const vint16 step16(step);

	for (size_t i=0; i<N; i++)
        {
          /*! map even and odd primitive to bin */
          const BBox3fa prim0 = prims[i].bounds();
          const vfloat16 center0 = vfloat16((vfloat4)prim0.lower) + vfloat16((vfloat4)prim0.upper); 
          const vint16 bin = mapping.bin16(center0);

          const vfloat16 b_min_x = prims[i].lower.x;
          const vfloat16 b_min_y = prims[i].lower.y;
          const vfloat16 b_min_z = prims[i].lower.z;
          const vfloat16 b_max_x = prims[i].upper.x;
          const vfloat16 b_max_y = prims[i].upper.y;
          const vfloat16 b_max_z = prims[i].upper.z;

          const vint16 bin0 = shuffle<0>(bin);
          const vint16 bin1 = shuffle<1>(bin);
          const vint16 bin2 = shuffle<2>(bin);

          const vbool16 m_update_x = step16 == bin0;
          const vbool16 m_update_y = step16 == bin1;
          const vbool16 m_update_z = step16 == bin2;

          assert(__popcnt((size_t)m_update_x) == 1);
          assert(__popcnt((size_t)m_update_y) == 1);
          assert(__popcnt((size_t)m_update_z) == 1);

          min_x0 = mask_min(m_update_x,min_x0,min_x0,b_min_x);
          min_y0 = mask_min(m_update_x,min_y0,min_y0,b_min_y);
          min_z0 = mask_min(m_update_x,min_z0,min_z0,b_min_z);
          // ------------------------------------------------------------------------      
          max_x0 = mask_max(m_update_x,max_x0,max_x0,b_max_x);
          max_y0 = mask_max(m_update_x,max_y0,max_y0,b_max_y);
          max_z0 = mask_max(m_update_x,max_z0,max_z0,b_max_z);
          // ------------------------------------------------------------------------
          min_x1 = mask_min(m_update_y,min_x1,min_x1,b_min_x);
          min_y1 = mask_min(m_update_y,min_y1,min_y1,b_min_y);
          min_z1 = mask_min(m_update_y,min_z1,min_z1,b_min_z);      
          // ------------------------------------------------------------------------      
          max_x1 = mask_max(m_update_y,max_x1,max_x1,b_max_x);
          max_y1 = mask_max(m_update_y,max_y1,max_y1,b_max_y);
          max_z1 = mask_max(m_update_y,max_z1,max_z1,b_max_z);
          // ------------------------------------------------------------------------
          min_x2 = mask_min(m_update_z,min_x2,min_x2,b_min_x);
          min_y2 = mask_min(m_update_z,min_y2,min_y2,b_min_y);
          min_z2 = mask_min(m_update_z,min_z2,min_z2,b_min_z);
          // ------------------------------------------------------------------------      
          max_x2 = mask_max(m_update_z,max_x2,max_x2,b_max_x);
          max_y2 = mask_max(m_update_z,max_y2,max_y2,b_max_y);
          max_z2 = mask_max(m_update_z,max_z2,max_z2,b_max_z);
          // ------------------------------------------------------------------------
          count0 = mask_add(m_update_x,count0,count0,vint16(1));
          count1 = mask_add(m_update_y,count1,count1,vint16(1));
          count2 = mask_add(m_update_z,count2,count2,vint16(1));      
        }

        lower[0] = Vec3vf16( min_x0, min_y0, min_z0 );
        lower[1] = Vec3vf16( min_x1, min_y1, min_z1 );
        lower[2] = Vec3vf16( min_x2, min_y2, min_z2 );

        upper[0] = Vec3vf16( max_x0, max_y0, max_z0 );
        upper[1] = Vec3vf16( max_x1, max_y1, max_z1 );
        upper[2] = Vec3vf16( max_x2, max_y2, max_z2 );

        count[0] = count0;
        count[1] = count1;
        count[2] = count2;
      }

      __forceinline void bin(const PrimRef* prims, size_t begin, size_t end, const BinMapping<16>& mapping) {
	bin(prims+begin,end-begin,mapping);
      }

      /*! merges in other binning information */
      __forceinline void merge (const BinInfo& other, size_t numBins)
      {
        for (size_t i=0; i<3; i++)
        {
          lower[i]  = min(lower[i],other.lower[i]);
          upper[i]  = max(upper[i],other.upper[i]);
          count[i] += other.count[i];
        }
      }

      /*! reducesr binning information */
      static __forceinline const BinInfo reduce (const BinInfo& a, const BinInfo& b)
      {
        BinInfo c;
	for (size_t i=0; i<3; i++) 
        {
          c.counts[i] = a.counts[i] + b.counts[i];
          c.lower[i]  = min(a.lower[i],b.lower[i]);
          c.upper[i]  = max(a.upper[i],b.upper[i]);
        }
        return c;
      }

      __forceinline vfloat16 shift_right1_zero_extend(const vfloat16 &a) const
      {
        vfloat16 z = vfloat16::zero();
        return align_shift_right<1>(z,a);
      }  

      __forceinline vint16 shift_right1_zero_extend(const vint16 &a) const
      {
        vint16 z = vint16::zero();
        return align_shift_right<1>(z,a);
      }  

      /*! finds the best split by scanning binning information */
      __forceinline Split best(const BinMapping<16>& mapping, const size_t blocks_shift) const
      {

	/* find best dimension */
	float bestSAH = inf;
	int   bestDim = -1;
	int   bestPos = 0;
	int   bestLeft = 0;
	const vint16 blocks_add = (1 << blocks_shift)-1;
        const vfloat16 inf(pos_inf);
	for (size_t dim=0; dim<3; dim++) 
        {
          /* ignore zero sized dimensions */
          if (unlikely(mapping.invalid(dim)))
            continue;

          const vfloat16 rArea16 = prefix_area_rl(lower[dim].x,lower[dim].y,lower[dim].z, upper[dim].x,upper[dim].y,upper[dim].z);
          const vfloat16 lArea16 = prefix_area_lr(lower[dim].x,lower[dim].y,lower[dim].z, upper[dim].x,upper[dim].y,upper[dim].z);
          const vint16  lCount16 = prefix_sum(count[dim]);
          const vint16  rCount16 = reverse_prefix_sum(count[dim]);

          /* compute best split in this dimension */
          const vfloat16 leftArea  = lArea16;
          const vfloat16 rightArea = shift_right1_zero_extend(rArea16);
          const vint16 lC = lCount16;
          const vint16 rC = shift_right1_zero_extend(rCount16);
          const vint16 leftCount  = ( lC + blocks_add) >> blocks_shift;
          const vint16 rightCount = ( rC + blocks_add) >> blocks_shift;
          const vbool16 valid = (leftArea < inf) & (rightArea < inf) & vbool16(0x7fff); // handles inf entries
          const vfloat16 sah = select(valid,leftArea*vfloat16(leftCount) + rightArea*vfloat16(rightCount),vfloat16(pos_inf));
          /* test if this is a better dimension */
          if (any(sah < vfloat16(bestSAH))) 
          {
            const size_t index = select_min(sah);            
            assert(index < 15);
            assert(sah[index] < bestSAH);
            bestDim = dim;
            bestPos = index+1;
            bestSAH = sah[index];
          }
        }
	
	return Split(bestSAH,bestDim,bestPos,mapping);

      }
            
    private:
      Vec3vf16 lower[3];
      Vec3vf16 upper[3];
      vint16   count[3];
    };
#endif
  }
}
