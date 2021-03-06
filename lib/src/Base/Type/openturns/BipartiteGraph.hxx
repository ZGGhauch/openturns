//                                               -*- C++ -*-
/**
 *  @brief BipartiteGraph defines a graph with two sets of nodes (red and black)
 *         and links from one set to the other only.
 *
 *  Copyright 2005-2017 Airbus-EDF-IMACS-Phimeca
 *
 *  This library is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  along with this library.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef OPENTURNS_BIPARTITEGRAPH_HXX
#define OPENTURNS_BIPARTITEGRAPH_HXX

#include "openturns/OTprivate.hxx"
#include "openturns/Graph.hxx"
#include "openturns/Indices.hxx"

BEGIN_NAMESPACE_OPENTURNS

/**
 * @class BipartiteGraph
 *
 * A class that represents bipartite graphs
 */
class OT_API BipartiteGraph :
  public PersistentCollection<Indices>
{
  CLASSNAME;

public:
  typedef PersistentCollection<Indices> InternalType;
  typedef Collection<Indices>           IndicesCollection;

  /** Default constructor */
  BipartiteGraph()
    : InternalType(1, Indices(1))
  {
    // Nothing to do
  }

  /** Constructor that pre-allocate size elements */
  BipartiteGraph(const UnsignedInteger size) : InternalType(size)
  {
    // Nothing to do
  }

  /** Constructor from a base object */
  BipartiteGraph(const IndicesCollection & coll) : InternalType(coll)
  {
    // Nothing to do
  }

  /** Accessor to the red nodes */
  Indices getRedNodes() const;

  /** Accessor to the black nodes */
  Indices getBlackNodes() const;

  /** Draw the bipartite graph */
  Graph draw() const;

  /** Destructor */
  ~BipartiteGraph() throw() {}

#ifdef SWIG
  /** @copydoc Object::__repr__() const */
  virtual String __repr__() const;

  /** @copydoc Object::__str__() const */
  virtual String __str__(const String & offset = "") const;
#endif

}; /* class BipartiteGraph */

END_NAMESPACE_OPENTURNS

#endif /* OPENTURNS_BIPARTITEGRAPH_HXX */
