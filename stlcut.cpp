/* Copyright 2015 Miro Hrončok <miro@hroncok.cz>
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#include <iostream>
#include <deque>
#include <vector>
#include <set>
#include <math.h>
#include <admesh/stl.h>
#include <poly2tri/poly2tri.h>

// vertex position related to the plane
enum stl_position { above, on, below };

// for better readability
typedef stl_vertex stl_vector;

// normalize given vector
stl_vector normalize(stl_vector in) {
  double size = sqrt((double)in.x*in.x+(double)in.y*in.y+(double)in.z*in.z);
  in.x = in.x/size;
  in.y = in.y/size;
  in.z = in.z/size;
  return in;
}

// dot product of 2 vectors
float dot(stl_vector a, stl_vector b) {
  return a.x*b.x + a.y*b.y + a.z*b.z;
}

// plane in the form of equation
struct stl_plane {
  float x;
  float y;
  float z;
  float d;
  stl_vector a;
  stl_vector b;
  
  stl_plane(float x, float y, float z, float d) {
    this->x = x;
    this->y = y;
    this->z = z;
    this->d = d;
    
    // save orthonormal basis
    if (x == 0 && y == 0) {
      a.x = 1; a.y = 0; a.z = 0;
      b.x = 0; b.y = 1; b.z = 0;
    } else if (y == 0 && z == 0) {
      a.x = 0; a.y = 1; a.z = 0;
      b.x = 0; b.y = 0; b.z = 1;
    } else if (x == 0 && z == 0) {
      a.x = 1; a.y = 0; a.z = 0;
      b.x = 0; b.y = 0; b.z = 1;
    } else {
      a.x = y; a.y = -x; a.z = 0;
      a = normalize(a);
      b.x = 0; b.y = z; b.z = -y;
      
      float r = dot(a,b);
      b.x -= a.x*r;
      b.y -= a.y*r;
      b.z -= a.z*r;
      b = normalize(b);
    }
  }
  
  // returns the position of the vertex related to the plane
  stl_position position(stl_vertex vertex) {
    double result = (double)x*vertex.x + (double)y*vertex.y + (double)z*vertex.z + d;
    if (result > 0) return above;
    if (result < 0) return below;
    return on;
  }
  
  // given two vertices, return the intersection point of line they form with the plane
  stl_vertex intersection(stl_vertex a, stl_vertex b) {
    stl_vector ab; // vector from A to B
    ab.x = b.x-a.x;
    ab.y = b.y-a.y;
    ab.z = b.z-a.z;
    float t = - (a.x*x + a.y*y + a.z*z + d) / (ab.x*x + ab.y*y + ab.z*z);
    
    stl_vertex result;
    result.x = a.x + ab.x*t;
    result.y = a.y + ab.y*t;
    result.z = a.z + ab.z*t;
    return result;
  }
  
  // transform 3D coordinates to 2D (on the plane)
  stl_vertex to_2D(stl_vertex vertex, stl_vertex origin) {
    stl_vector ov;
    ov.x = vertex.x-origin.x;
    ov.y = vertex.y-origin.y;
    ov.z = vertex.z-origin.z;
    
    stl_vertex result;
    result.x = dot(a,ov);
    result.y = dot(b,ov);
    result.z = 0;
    return result;
  }
  
  // transform 2D coordinates on the plane to 3D
  stl_vertex to_3D(stl_vertex vertex, stl_vertex origin) {
    stl_vertex result;
    
    result.x = origin.x + a.x*vertex.x + b.x*vertex.y;
    result.y = origin.y + a.y*vertex.x + b.y*vertex.y;
    result.z = origin.z + a.z*vertex.x + b.z*vertex.y;
    return result;
  }
};

// pair of vertices, edge of the hole we cut
struct stl_vertex_pair {
  stl_vertex x;
  stl_vertex y;
  stl_vertex_pair(stl_vertex x, stl_vertex y) {
    this->x=x;
    this->y=y;
  }
  
  // this is needed by set
  bool operator<(const stl_vertex_pair& other) const  {
    if (x.x == other.x.x) {
      if (x.y == other.x.y) {
        if (x.z == other.x.z) {
          if (y.x == other.y.x) {
            if (y.y == other.y.y) {
              return y.z < other.y.z;
            } else return y.y < other.y.y;
          } else return y.x < other.y.x;
        } else return x.z < other.x.z;
      } else return x.y < other.x.y;
    } else return x.x < other.x.x;
  }
};

// crate a partial facet of a given facet
stl_facet semifacet(stl_facet original, stl_vertex a, stl_vertex b, stl_vertex c) {
  stl_facet f;
  f.vertex[0] = a;
  f.vertex[1] = b;
  f.vertex[2] = c;
  f.normal = original.normal;
  f.extra[0] = original.extra[0];
  f.extra[1] = original.extra[1];
  return f;
}

// one of the vertices is on the plane and we cut the facet to two
void simple_cut(stl_vertex zero, stl_vertex one, stl_vertex two, stl_facet facet, stl_plane plane,
              std::deque<stl_facet> &first, std::deque<stl_facet> &second,
              std::set<stl_vertex_pair> &border) {
  stl_vertex middle = plane.intersection(one, two);
  first.push_back(semifacet(facet, middle, zero, one));
  second.push_back(semifacet(facet, middle, two, zero));
  border.insert(stl_vertex_pair(one,middle));
}

// no vertex is on the plane and we cut the facet to three
void complex_cut(stl_vertex zero, stl_vertex one, stl_vertex two, stl_facet facet, stl_plane plane,
              std::deque<stl_facet> &first, std::deque<stl_facet> &second,
              std::set<stl_vertex_pair> &border) {
  stl_vertex one_middle = plane.intersection(zero, one);
  stl_vertex two_middle = plane.intersection(zero, two);
  first.push_back(semifacet(facet, zero, one_middle, two_middle));
  second.push_back(semifacet(facet, one_middle, one, two));
  second.push_back(semifacet(facet, one_middle, two, two_middle));
  border.insert(stl_vertex_pair(one_middle,two_middle));
}

// given facet is classified and distributed to upper or lower deque
// is cut to smaller ones when necessary
// border edges ends in border set for further triangulation
void separate(stl_facet facet, stl_plane plane,
              std::deque<stl_facet> &upper, std::deque<stl_facet> &lower,
              std::set<stl_vertex_pair> &border) {
  stl_position pos[3];
  size_t aboves = 0;
  size_t belows = 0;
  size_t ons = 0;
  
  for (size_t i = 0; i < 3; i++) {
    pos[i] = plane.position(facet.vertex[i]);
    if (pos[i]==above) aboves++;
    else if (pos[i]==below) belows++;
    else ons++;
  }
    
  // All vertexes are above the plane
  if (aboves == 3) {
    upper.push_back(facet);
    return;
  }
  
  // All vertexes are below the plane
  if (belows == 3) {
    lower.push_back(facet);
    return;
  }
  
  // All vertexes are on the plane
  if (ons == 3) return;
  
  // 2 vertexes are on the plane
  if (ons == 2) {
    for (size_t i = 0; i < 3; i++) {
      if (pos[i] == above) {
        upper.push_back(facet);
        border.insert(stl_vertex_pair(facet.vertex[(i+1)%3],facet.vertex[(i+2)%3]));
      } else if (pos[i] == below) {
        lower.push_back(facet);
        border.insert(stl_vertex_pair(facet.vertex[(i+2)%3],facet.vertex[(i+1)%3]));
      }
    }
    return;
  }
  
  // 1 vertex is on the plane
  if (ons == 1) {
    stl_vertex zero, one, two;
    stl_position onepos;
    for (size_t i = 0; i < 3; i++) {
      if (pos[i] == on) {
        zero = facet.vertex[i];
        one = facet.vertex[(i+1)%3];
        onepos = pos[(i+1)%3];
        two = facet.vertex[(i+2)%3];
        break;
      }
    }
    if (aboves == 2) {
      upper.push_back(facet);
      return;
    }
    if (belows == 2) {
      lower.push_back(facet);
      return;
    }
    if (onepos == above)
      simple_cut(zero, one, two, facet, plane, upper, lower, border);
    else
      simple_cut(zero, one, two, facet, plane, lower, upper, border);
    return;
  }
  
  // no vertexes on the plane
  if  (aboves == 1) { // belows == 2
    for (size_t i = 0; i < 3; i++) {
      if (pos[i] == above) {
        complex_cut(facet.vertex[i], facet.vertex[(i+1)%3], facet.vertex[(i+2)%3], facet, plane, upper, lower, border);
        return;
      }
    }
  }
  // belows == 1, aboves == 2
  for (size_t i = 0; i < 3; i++) {
    if (pos[i] == below) {
      complex_cut(facet.vertex[i], facet.vertex[(i+1)%3], facet.vertex[(i+2)%3], facet, plane, lower, upper, border);
      return;
    }
  }
}

// exports stl file form given deque
void export_stl(std::deque<stl_facet> facets, const char* name) {
  stl_file stl_out;
  stl_out.stats.type = inmemory;
  stl_out.stats.number_of_facets = facets.size();
  stl_out.stats.original_num_facets = stl_out.stats.number_of_facets;
  stl_out.v_indices = NULL;
  stl_out.v_shared = NULL;
  stl_out.neighbors_start = NULL;
  stl_clear_error(&stl_out);
  stl_allocate(&stl_out);
  
  int first = 1;
  for (std::deque<stl_facet>::const_iterator facet = facets.begin(); facet != facets.end(); facet++) {
    stl_out.facet_start[facet - facets.begin()] = *facet;
    stl_facet_stats(&stl_out, *facet, first);
    first = 0;
  }
  
  // check nearby in 2 iterations
  // remove unconnected facets
  // fill holes
  stl_repair(&stl_out, 0, 0, 0, 0, 0, 0, 1, 2, 1, 1, 0, 0, 0, 0);
  stl_write_ascii(&stl_out, name, "stlcut");
  stl_clear_error(&stl_out);
  stl_close(&stl_out);
}

// vertex comparison with tolerance
bool is_same(stl_vertex a, stl_vertex b, float tolerance) {
  return (ABS(a.x-b.x)<tolerance && ABS(a.y-b.y)<tolerance);
}

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " file.stl" << std::endl;
    return 1;
  }
  
  // TODO remove the algorithm from main() and provide interface using 3 stl structs (in, out, out)
  
  stl_file stl_in;
  stl_open(&stl_in, argv[1]);
  stl_exit_on_error(&stl_in);
  stl_plane plane = stl_plane(0,0,1,0);
  
  std::set<stl_vertex_pair> border;
  std::deque<stl_facet> upper, lower;
  
  // separate all facets
  for (int i = 0; i < stl_in.stats.number_of_facets; i++)
    separate(stl_in.facet_start[i], plane, upper, lower, border);
  
  stl_close(&stl_in);
  
  std::deque<stl_vertex_pair> border2d;
  
  // transform the border points coordinates to 2D
  // get a tolerance for further comparison
  stl_vertex origin = (*border.begin()).x;
  float tolerance;
  for (std::set<stl_vertex_pair>::iterator i = border.begin(); i != border.end(); i++) {
    stl_vertex x = plane.to_2D((*i).x, origin);
    stl_vertex y = plane.to_2D((*i).y, origin);
    border2d.push_back(stl_vertex_pair(x,y));
    if (i == border.begin()) {
      tolerance = ABS(x.x-y.x)+ABS(x.y-y.y);
    } else {
      tolerance = STL_MIN(tolerance,ABS(x.x-y.x)+ABS(x.y-y.y));
    }
  }
  tolerance /= 4; // TODO this needs some clarification or even replacing
  
  // sort the edges to make a polygon
  std::deque<stl_vertex> polyline;
  stl_vertex_pair pair = border2d.front();
  border2d.pop_front();
  polyline.push_back(pair.x);
  polyline.push_back(pair.y);
  bool found = true;
    
  while (found) {
    found = false;
    for (std::deque<stl_vertex_pair>::iterator i = border2d.begin(); i != border2d.end(); i++) {
      if (is_same(polyline.back(), (*i).x, tolerance)) {
        polyline.push_back((*i).y);
        border2d.erase(i);
        found = true;
        break;
      }
      if (is_same(polyline.back(), (*i).y, tolerance)) {
        polyline.push_back((*i).x);
        border2d.erase(i);
        found = true;
        break;
      }
    }
    if (!found) {
      break;
      // TODO there might still be some edges left, create multiple polygons
      // TODO (hard) also recognize holes
    }
  }
  
  // poly2tri doesn't like this
  // the condition should always be true when the mesh is valid and no error happened
  if (is_same(polyline.back(), polyline.front(), tolerance)) {
    polyline.pop_back();
  }
  
  // TODO use p2t::Points right away from to_2D
  std::vector<p2t::Point*> polygon;
  for (std::deque<stl_vertex>::iterator i = polyline.begin(); i != polyline.end(); i++) {
    polygon.push_back(new p2t::Point((*i).x,(*i).y));
  }
  
  // triangulate
  p2t::CDT cdt = p2t::CDT(polygon);
  cdt.Triangulate();
  std::vector<p2t::Triangle*> triangles = cdt.GetTriangles();
  
  // for each triangle, create facet
  for (std::vector<p2t::Triangle*>::iterator i = triangles.begin(); i != triangles.end(); i++) {
    stl_vertex vertex;
    stl_facet facet;
    for (size_t j = 0; j < 3; j++) {
      p2t::Point* p = (*i)->GetPoint(j);
      vertex.x = p->x;
      vertex.y = p->y;
      vertex = plane.to_3D(vertex, origin);
      facet.vertex[j].x = vertex.x;
      facet.vertex[j].y = vertex.y;
      facet.vertex[j].z = vertex.z;
    }
    // normal goes out of the object, for lower part, it is identical to plane normal
    facet.normal.x = plane.x;
    facet.normal.y = plane.y;
    facet.normal.z = plane.z;
    lower.push_back(facet);
    
    // for the upper part, we need to invert the normal...
    facet.normal.x = -plane.x;
    facet.normal.y = -plane.y;
    facet.normal.z = -plane.z;
    // ...and reverse the order of the vertices
    // TODO check if the order of vertices from poly2tri in fact depends on orientation of the first used edge
    // .. and the order might be reversed anyway
    vertex = facet.vertex[1];
    facet.vertex[1] = facet.vertex[2];
    facet.vertex[2] = vertex;
    upper.push_back(facet);
  }
  
  export_stl(upper, "upper.stl");
  export_stl(lower, "lower.stl");
  
  return 0;
}

