// This code is part of the project "Smaller and Faster: Parallel
// Processing of Compressed Graphs with Ligra+", presented at the IEEE
// Data Compression Conference, 2015.
// Copyright (c) 2015 Julian Shun, Laxman Dhulipala and Guy Blelloch
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights (to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to
// permit persons to whom the Software is furnished to do so, subject to
// the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#ifndef BYTECODE_H
#define BYTECODE_H

#include <iostream>
#include <fstream>
#include <stdlib.h>
#include <cmath>
#include "parallel.h"
#include "utils.h"
#include <stdio.h>
#include <string.h>

#define LAST_BIT_SET(b) (b & (0x80))
#define EDGE_SIZE_PER_BYTE 7

typedef unsigned char uchar;

/* Reads the first edge of an out-edge list, which is the signed
   difference between the target and source. 
*/

inline intE eatWeight(uchar* &start) {
  uchar fb = *start++;
  intE edgeRead = (fb & 0x3f);
  if (LAST_BIT_SET(fb)) {
    int shiftAmount = 6;
    while (1) {
      uchar b = *start;
      edgeRead |= ((b & 0x7f) << shiftAmount);
      start++;
      if (LAST_BIT_SET(b))
        shiftAmount += EDGE_SIZE_PER_BYTE;
      else 
        break;
    }
  }
  return (fb & 0x40) ? -edgeRead : edgeRead;
}

inline intE eatFirstEdge(uchar* &start, uintE source) {
  uchar fb = *start++;
  intE edgeRead = (fb & 0x3f);
  if (LAST_BIT_SET(fb)) {
    int shiftAmount = 6;
    while (1) {
      uchar b = *start;
      edgeRead |= ((b & 0x7f) << shiftAmount);
      start++;
      if (LAST_BIT_SET(b))
        shiftAmount += EDGE_SIZE_PER_BYTE;
      else 
        break;
    }
  }
  return (fb & 0x40) ? source - edgeRead : source + edgeRead;
}

/*
  Reads any edge of an out-edge list after the first edge. 
*/
inline uintE eatEdge(uchar* &start) {
  uintE edgeRead = 0;
  int shiftAmount = 0;

  while (1) {
    uchar b = *start++;
    edgeRead += ((b & 0x7f) << shiftAmount);
    if (LAST_BIT_SET(b))
      shiftAmount += EDGE_SIZE_PER_BYTE;
    else 
      break;
  } 
  return edgeRead;
}

/*
  The main decoding work-horse. First eats the specially coded first 
  edge, and then eats the remaining |d-1| many edges that are normally
  coded. 
*/
template <class T, class F>
  inline void decode(T t, F &f, uchar* edgeStart, const uintE &source, const uintT &degree) {
  uintE edgesRead = 0;
  if (degree > 0) {
    // Eat first edge, which is compressed specially 
    uintE startEdge = eatFirstEdge(edgeStart,source);
    if (!t.srcTarg(f, source,startEdge,edgesRead)) {
      return;
    }
    for (edgesRead = 1; edgesRead < degree; edgesRead++) {
      // Eat the next 'edge', which is a difference, and reconstruct edge.
      uintE edgeRead = eatEdge(edgeStart);
      uintE edge = startEdge + edgeRead;
      startEdge = edge;
      if (!t.srcTarg(f, source, edge, edgesRead)) {
        break; 
      }
    }
  }
}

//decode edges for weighted graph
template <class T, class F>
  inline void decodeWgh(T t, F &f, uchar* edgeStart, const uintE &source, const uintT &degree) {
  uintT edgesRead = 0;
  if (degree > 0) {
    // Eat first edge, which is compressed specially 
    uintE startEdge = eatFirstEdge(edgeStart,source);
    intE weight = eatWeight(edgeStart);
    if (!t.srcTarg(f, source,startEdge, weight, edgesRead)) {
      return;
    }
    for (edgesRead = 1; edgesRead < degree; edgesRead++) {
      uintE edgeRead = eatEdge(edgeStart);
      uintE edge = startEdge + edgeRead;
      startEdge = edge;
      intE weight = eatWeight(edgeStart);
      if (!t.srcTarg(f, source, edge, weight, edgesRead)) {
        break; 
      }
    }
  }
}


/*
  Compresses the first edge, writing target-source and a sign bit. 
*/
long compressFirstEdge(uchar *start, long offset, uintE source, uintE target) {

  //cout << "compressFirstEdge - Source Vertex = " << source << " - Offset = " << offset << " - Target = " << target << endl;

  uchar* saveStart = start;
  long saveOffset = offset;

  intE preCompress = (intE) target - source;

  //cout << "Pre-compress - Difference = " << preCompress << endl;

  int bytesUsed = 0;
  uchar firstByte = 0;
  intE toCompress = abs(preCompress);
  firstByte = toCompress & 0x3f; // 0011|1111
  if (preCompress < 0) {
    firstByte |= 0x40;
  }
  toCompress = toCompress >> 6;
  if (toCompress > 0) {
    firstByte |= 0x80;
  }
  start[offset] = firstByte;
  offset++;

  uchar curByte = toCompress & 0x7f;
  while ((curByte > 0) || (toCompress > 0)) {
    bytesUsed++;
    uchar toWrite = curByte;
    toCompress = toCompress >> 7;
    // Check to see if there's any bits left to represent
    curByte = toCompress & 0x7f;
    if (toCompress > 0) {
      toWrite |= 0x80; 
    }
    start[offset] = toWrite;
    offset++;
  }
  return offset;
}

/*
  Should provide the difference between this edge and the previous edge
*/

long compressEdge(uchar *start, long curOffset, uintE e) {
  uchar curByte = e & 0x7f;
  int bytesUsed = 0;
  while ((curByte > 0) || (e > 0)) {
    bytesUsed++;
    uchar toWrite = curByte;
    e = e >> 7;
    // Check to see if there's any bits left to represent
    curByte = e & 0x7f;
    if (e > 0) {
      toWrite |= 0x80; 
    }
    start[curOffset] = toWrite;
    curOffset++;
  }
  return curOffset;
}

/*
  Takes: 
    1. The edge array of chars to write into
    2. The current offset into this array
    3. The vertices degree
    4. The vertices vertex number
    5. The array of saved out-edges we're compressing
  Returns:
    The new offset into the edge array
*/
long sequentialCompressEdgeSet(uchar *edgeArray, long currentOffset, uintT degree, 
                                uintE vertexNum, uintE *savedEdges, int vertex_per_numa_node,
                                bool compress_flag, bool *edge_first_compress_flag) {
    
  //cout << "sequentialCompressEdgeSet - Current Offset = " << currentOffset << " - Degree = " << degree << " - Current Vertex = " << vertexNum << endl;

  if (degree > 0) {
    // Added Mohamed 
    // Define last NUMA node used
    int last_numa_node = -1;

    // define the current numa node
    int current_numa_node = -1;

    // Commented by Mohamed
    // // Compress the first edge whole, which is signed difference coded
    // currentOffset = compressFirstEdge(edgeArray, currentOffset, 
    //                                    vertexNum, savedEdges[0]);
    // for (uintT edgeI=1; edgeI < degree; edgeI++) {
    for (uintT edgeI=0; edgeI < degree; edgeI++) {

      // Added by Mohamed
      current_numa_node = savedEdges[edgeI] / vertex_per_numa_node;
      //cout << "Current NUMA Node = " << current_numa_node << endl;
      //cout << "Previous NUMA Node = " << last_numa_node << endl;

      uintE difference = 0;
      if (current_numa_node == last_numa_node)
      {
        // Set the edge first compress flag to false
        edge_first_compress_flag[edgeI] = false;

        // Store difference between current and previous edge.
        difference = savedEdges[edgeI] - savedEdges[edgeI - 1];
    
        //cout << "sequentialCompressEdgeSet - Edge # " << edgeI << "(" <<savedEdges[edgeI] << ") - Difference = " << difference << endl;

        // Check if compress flag is true
        if (compress_flag == true)
        {
          currentOffset = compressEdge(edgeArray, currentOffset, difference);
        }
      }
      else
      {
        // Set the edge first compress flag to true
        edge_first_compress_flag[edgeI] = true;

        // Store difference between current edge and current vertex index 
        difference = savedEdges[edgeI] - vertexNum;

        // Check if compress flag is true
        if (compress_flag == true)
        {
          // Compress similar to the first edge whole, which is signed difference coded
          currentOffset = compressFirstEdge(edgeArray, currentOffset, vertexNum, savedEdges[edgeI]); 
        }
      }

      // Set the last NUMA node to current
      last_numa_node = current_numa_node;

      // Commented by Mohamed
      // // Store difference between cur and prev edge. 
      // uintE difference = savedEdges[edgeI] - 
      //                   savedEdges[edgeI - 1];
    
      // //cout << "sequentialCompressEdgeSet - Edge # " << edgeI << " - Difference = " << difference << endl;

      // currentOffset = compressEdge(edgeArray, currentOffset, difference);
    }
    // Increment nWritten after all of vertex n's neighbors are written
  }
  return currentOffset;
}

/*
  Compresses the edge set in parallel. 
*/
// #define NUMA_NODES 1
uintE *parallelCompressEdges(uintE *edges, uintT *offsets, long n, long m, uintE* Degrees) {
  cout << "parallel compressing, (n,m) = (" << n << "," << m << ")" << endl;
  uintE **edgePts = newA(uintE*, n);
  uintT *degrees = newA(uintT, n+1);
  long *charsUsedArr = newA(long, n);
  long *compressionStarts = newA(long, n+1);
  {parallel_for(long i=0; i<n; i++) { 
      degrees[i] = Degrees[i];
    charsUsedArr[i] = ceil((degrees[i] * 9) / 8) + 4;
  }}
  degrees[n] = 0;
  sequence::plusScan(degrees,degrees, n+1);
  long toAlloc = sequence::plusScan(charsUsedArr,charsUsedArr,n);
  uintE* iEdges = newA(uintE,toAlloc);

  // Added Mohamed
  //cout << "Number of NUMA Nodes = " << NUMA_NODES << endl;
  // Calculate the number of vertices per NUMA node
  // int vertex_per_numa_node = ceil(n * 1.0 / NUMA_NODES);
  //cout << "Vertices/NUMA Node = " << vertex_per_numa_node << endl;

  int numa_nodes_configs[4] = {1, 2, 4, 8}; 
  int vertex_per_numa_node = -1;
  bool compress_flag = false;
  bool **edge_first_compress_flag = new bool*[4];
  int index = 0;
  for (index = 0; index < 4; index++)
  {
      edge_first_compress_flag[index] = new bool[m];
  }
  bool *all_same_flag = new bool[m];
  for (index = 0; index < 4; index++)
  {
    // Set the number of numa nodes
    vertex_per_numa_node = ceil(n * 1.0 / numa_nodes_configs[index]);;

    // Check if the current numa config. is to be compressed or not
    if (vertex_per_numa_node == 1 || vertex_per_numa_node == 8)
    {
      compress_flag = true;
    }
    else
    {
      compress_flag = false;
    }

    {parallel_for(long i=0; i<n; i++) {
      //cout << "Compress edges of vertex " << i << endl;
      edgePts[i] = iEdges+charsUsedArr[i];
      long charsUsed = sequentialCompressEdgeSet((uchar *)(iEdges+charsUsedArr[i]), 0, 
        degrees[i+1]-degrees[i], i, 
        edges + offsets[i], vertex_per_numa_node, 
        compress_flag, edge_first_compress_flag[index] + offsets[i]);
      charsUsedArr[i] = charsUsed;
    }}
  }

  // Loop on the edge first flag arrays to set the all_same flag array
  int flag_sum = 0;
  for (index = 0; index < m; index++)
  {
    for (int k = 0; k < 4; k++)
    {
      flag_sum += edge_first_compress_flag[k][index]; 
    }

    // Check if flag sum is equal to zero or 4 (can check with 0 or 2 and skip using numa nodes = 2 & 4)
    if (flag_sum == 0 || flag_sum == 4)
    {
      all_same_flag[index] = true;
    }
    else
    {
      all_same_flag[index] = false;
    }

    flag_sum = 0;
  }

  cout << "Edges" << endl;
  for (index = 0; index < m; index++)
  {
    cout << edges[index] << ", ";
  }
  cout << endl;

  for (index = 0; index < 4; index++)
  {
    cout << "Edge flags for NUMA nodes = " << numa_nodes_configs[index] << endl;
    for (int k = 0; k < m; k++)
    {
      cout << edge_first_compress_flag[index][k] << ", ";
    }
    cout << endl;
  }

  cout << "All same flags" << endl;
  for (index = 0; index < m; index++)
  {
    cout << all_same_flag[index] << ", ";
  }
  cout << endl;

  // produce the total space needed for all compressed lists in chars. 
  long totalSpace = sequence::plusScan(charsUsedArr, compressionStarts, n);
  compressionStarts[n] = totalSpace;
  free(degrees);
  free(charsUsedArr);
  
  uchar *finalArr = newA(uchar, totalSpace);
  cout << "total space requested is : " << totalSpace << endl;
  float avgBitsPerEdge = (float)totalSpace*8 / (float)m; 
  cout << "Average bits per edge: " << avgBitsPerEdge << endl;

  {parallel_for(long i=0; i<n; i++) {
      long o = compressionStarts[i];
    memcpy(finalArr + o, (uchar *)(edgePts[i]), compressionStarts[i+1]-o);
    offsets[i] = o;
  }}
  offsets[n] = totalSpace;
  free(iEdges);
  free(edgePts);
  free(compressionStarts);
  cout << "finished compressing, bytes used = " << totalSpace << endl;
  cout << "would have been, " << (m * 4) << endl;
  return ((uintE *)finalArr);
}

typedef pair<uintE,intE> intEPair;

/*
  Takes: 
    1. The edge array of chars to write into
    2. The current offset into this array
    3. The vertices degree
    4. The vertices vertex number
    5. The array of saved out-edges we're compressing
  Returns:
    The new offset into the edge array
*/
long sequentialCompressWeightedEdgeSet
(uchar *edgeArray, long currentOffset, uintT degree, 
 uintE vertexNum, intEPair *savedEdges) {
  if (degree > 0) {
    // Compress the first edge whole, which is signed difference coded
    //target ID
    currentOffset = compressFirstEdge(edgeArray, currentOffset, 
                                       vertexNum, savedEdges[0].first);
    //weight
    currentOffset = compressFirstEdge(edgeArray, currentOffset, 
              0,savedEdges[0].second);

    for (uintT edgeI=1; edgeI < degree; edgeI++) {
      // Store difference between cur and prev edge. 
      uintE difference = savedEdges[edgeI].first - 
                        savedEdges[edgeI - 1].first;

      //compress difference
      currentOffset = compressEdge(edgeArray, currentOffset, difference);

      //compress weight
      currentOffset = compressFirstEdge(edgeArray, currentOffset, 0, savedEdges[edgeI].second);      
    }
    // Increment nWritten after all of vertex n's neighbors are written
  }
  return currentOffset;
}

/*
  Compresses the weighted edge set in parallel. 
*/
uchar *parallelCompressWeightedEdges(intEPair *edges, uintT *offsets, long n, long m, uintE* Degrees) {
  cout << "parallel compressing, (n,m) = (" << n << "," << m << ")" << endl;
  uintE **edgePts = newA(uintE*, n);
  uintT *degrees = newA(uintT, n+1);
  long *charsUsedArr = newA(long, n);
  long *compressionStarts = newA(long, n+1);
  {parallel_for(long i=0; i<n; i++) { 
    degrees[i] = Degrees[i];
    charsUsedArr[i] = 2*(ceil((degrees[i] * 9) / 8) + 4); //to change
  }}
  degrees[n] = 0;
  sequence::plusScan(degrees,degrees, n+1);
  long toAlloc = sequence::plusScan(charsUsedArr,charsUsedArr,n);
  uintE* iEdges = newA(uintE,toAlloc);

  {parallel_for(long i=0; i<n; i++) {
    edgePts[i] = iEdges+charsUsedArr[i];
    long charsUsed = 
      sequentialCompressWeightedEdgeSet((uchar *)(iEdges+charsUsedArr[i]), 0, degrees[i+1]-degrees[i],i, edges + offsets[i]);
    charsUsedArr[i] = charsUsed;
  }}

  // produce the total space needed for all compressed lists in chars. 
  long totalSpace = sequence::plusScan(charsUsedArr, compressionStarts, n);
  compressionStarts[n] = totalSpace;
  free(degrees);
  free(charsUsedArr);

  uchar *finalArr = newA(uchar, totalSpace);
  cout << "total space requested is : " << totalSpace << endl;
  float avgBitsPerEdge = (float)totalSpace*8 / (float)m; 
  cout << "Average bits per edge: " << avgBitsPerEdge << endl;

  {parallel_for(long i=0; i<n; i++) {
      long o = compressionStarts[i];
    memcpy(finalArr + o, (uchar *)(edgePts[i]), compressionStarts[i+1]-o);
    offsets[i] = o;
  }}
  offsets[n] = totalSpace;
  free(iEdges);
  free(edgePts);
  free(compressionStarts);
  cout << "finished compressing, bytes used = " << totalSpace << endl;
  cout << "would have been, " << (m * 8) << endl;
  return finalArr;
}

#endif
