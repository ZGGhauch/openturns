//                                               -*- C++ -*-
/**
 *  @brief Implementation of the Faure sequence
 *
 *  Copyright 2005-2016 Airbus-EDF-IMACS-Phimeca
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

#ifndef OPENTURNS_FAURESEQUENCE_HXX
#define OPENTURNS_FAURESEQUENCE_HXX

#include "openturns/LowDiscrepancySequenceImplementation.hxx"

BEGIN_NAMESPACE_OPENTURNS
/**
 * @class FaureSequence
 */

class OT_API FaureSequence :
  public LowDiscrepancySequenceImplementation
{
  CLASSNAME;

public:

  /** Constructor with parameters */
  explicit FaureSequence(const UnsignedInteger dimension = 1);

  /** Virtual constructor */
  virtual FaureSequence * clone() const;

  /** Initialize the sequence */
  void initialize(const UnsignedInteger dimension);

  /** Generate a quasi-random vector of numbers uniformly distributed over [0, 1[ */
  using LowDiscrepancySequenceImplementation::generate;
  NumericalPoint generate();

  /** String converter */
  String __repr__() const;

  /** Method save() stores the object through the StorageManager */
  void save(Advocate & adv) const;

  /** Method load() reloads the object from the StorageManager */
  void load(Advocate & adv);

private:
  /** Compute the initial binomial coefficients table associated with the modulus */
  void computeInitialBinomialCoefficients();

  /** Update the binomial table by adding one column to the triangular array */
  void updateBinomialCoefficients();

  /** Binomial for the generation of the sequence */
  Unsigned64BitsIntegerPersistentCollection coefficients_;

  /** Modulus of the sequence, i.e. least prime number greater or equal to the dimension */
  Unsigned64BitsInteger modulus_;

  /** Inverse of the modulus */
  NumericalScalar modulusInverse_;

  /** Current seed into the sequence */
  Unsigned64BitsInteger seed_;

  /** Next value of the seed that needs a coefficient update */
  Unsigned64BitsInteger seedBound_;

  /** Number of digits of the seed in base the modulus */
  UnsignedInteger logSeed_;

}; /* class FaureSequence */

END_NAMESPACE_OPENTURNS

#endif /* OPENTURNS_FAURESEQUENCE_HXX */