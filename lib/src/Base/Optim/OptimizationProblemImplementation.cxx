//                                               -*- C++ -*-
/**
 *  @brief OptimizationProblemImplementation allows to describe an optimization problem
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
#include <cstdlib>

#include "openturns/OptimizationProblemImplementation.hxx"
#include "openturns/ResourceMap.hxx"
#include "openturns/PersistentObjectFactory.hxx"
#include "openturns/SymmetricTensor.hxx"
#include "openturns/IdentityMatrix.hxx"
#include "openturns/QuadraticFunction.hxx"
#include "openturns/LinearFunction.hxx"

BEGIN_NAMESPACE_OPENTURNS

CLASSNAMEINIT(OptimizationProblemImplementation);

static const Factory<OptimizationProblemImplementation> Factory_OptimizationProblemImplementation;

/* Default constructor */
OptimizationProblemImplementation::OptimizationProblemImplementation()
  : PersistentObject()
  , objective_()
  , equalityConstraint_()
  , inequalityConstraint_()
  , bounds_()
  , minimization_(true)
  , dimension_(0)
{
  // Nothing to do
}

OptimizationProblemImplementation::OptimizationProblemImplementation(const Function & objective)
  : PersistentObject()
  , objective_(objective)
  , minimization_(true)
  , dimension_(objective.getInputDimension())
{
  // nothing to do
}

/*
 * @brief General multi-objective equality, inequality and bound constraints
 */
OptimizationProblemImplementation::OptimizationProblemImplementation(const Function & objective,
    const Function & equalityConstraint,
    const Function & inequalityConstraint,
    const Interval & bounds)
  : PersistentObject()
  , objective_(objective)
  , minimization_(true)
  , dimension_(objective.getInputDimension())
{
  setEqualityConstraint(equalityConstraint);
  setInequalityConstraint(inequalityConstraint);
  setBounds(bounds);
}

/* Constructor for nearest point problem */
OptimizationProblemImplementation::OptimizationProblemImplementation(const Function & levelFunction,
    Scalar levelValue)
  : PersistentObject()
  , objective_()
  , equalityConstraint_()
  , inequalityConstraint_()
  , bounds_()
  , levelValue_(levelValue)
  , minimization_(true)
  , dimension_(0)
{
  setLevelFunction(levelFunction);
}

/* Virtual constructor */
OptimizationProblemImplementation * OptimizationProblemImplementation::clone() const
{
  return new OptimizationProblemImplementation(*this);
}

/* Objective accessor */
Function OptimizationProblemImplementation::getObjective() const
{
  return objective_;
}

void OptimizationProblemImplementation::setObjective(const Function & objective)
{
  if (objective.getInputDimension() != objective_.getInputDimension())
  {
    // clear constraints, bounds
    LOGWARN(OSS() << "Clearing constraints and bounds");
    equalityConstraint_ = Function();
    inequalityConstraint_ = Function();
    bounds_ = Interval(0);
  }
  clearLevelFunction();

  objective_ = objective;
  // Update dimension_ member accordingly
  dimension_ = objective.getInputDimension();
}

Bool OptimizationProblemImplementation::hasMultipleObjective() const
{
  return objective_.getOutputDimension() > 1;
}

/* Equality constraint accessor */
Function OptimizationProblemImplementation::getEqualityConstraint() const
{
  return equalityConstraint_;
}

void OptimizationProblemImplementation::setEqualityConstraint(const Function & equalityConstraint)
{
  if ((equalityConstraint.getInputDimension() > 0) && (equalityConstraint.getInputDimension() != dimension_)) throw InvalidArgumentException(HERE) << "Error: the given equality constraints have an input dimension=" << equalityConstraint.getInputDimension() << " different from the input dimension=" << dimension_ << " of the objective.";

  clearLevelFunction();
  equalityConstraint_ = equalityConstraint;
}

Bool OptimizationProblemImplementation::hasEqualityConstraint() const
{
  return equalityConstraint_.getEvaluation()->isActualImplementation();
}

/* Inequality constraint accessor */
Function OptimizationProblemImplementation::getInequalityConstraint() const
{
  return inequalityConstraint_;
}

void OptimizationProblemImplementation::setInequalityConstraint(const Function & inequalityConstraint)
{
  if ((inequalityConstraint.getInputDimension() > 0) && (inequalityConstraint.getInputDimension() != dimension_)) throw InvalidArgumentException(HERE) << "Error: the given inequality constraints have an input dimension=" << inequalityConstraint.getInputDimension() << " different from the input dimension=" << dimension_ << " of the objective.";

  clearLevelFunction();
  inequalityConstraint_ = inequalityConstraint;
}

Bool OptimizationProblemImplementation::hasInequalityConstraint() const
{
  return inequalityConstraint_.getEvaluation()->isActualImplementation();
}

/* Bounds accessor */
Interval OptimizationProblemImplementation::getBounds() const
{
  return bounds_;
}

void OptimizationProblemImplementation::setBounds(const Interval & bounds)
{
  if ((bounds.getDimension() > 0) && (bounds.getDimension() != dimension_)) throw InvalidArgumentException(HERE) << "Error: the given bounds are of dimension=" << bounds.getDimension() << " different from the input dimension=" << dimension_ << " of the objective.";

  bounds_ = bounds;
}

Bool OptimizationProblemImplementation::hasBounds() const
{
  return bounds_.getDimension() > 0;
}

/* Level function accessor */
Function OptimizationProblemImplementation::getLevelFunction() const
{
  return levelFunction_;
}

void OptimizationProblemImplementation::setLevelFunction(const Function & levelFunction)
{
  if (levelFunction.getOutputDimension() != 1) throw InvalidArgumentException(HERE) << "Error: level function has an output dimension=" << levelFunction.getOutputDimension() << " but only dimension 1 is supported.";

  levelFunction_ = levelFunction;
  dimension_ = levelFunction_.getInputDimension();
  // Update objective function
  const Point center(dimension_);
  const Point constant(1);
  const Matrix linear(dimension_, 1);
  const IdentityMatrix identity(dimension_);
  const SymmetricTensor quadratic(dimension_, 1, *(identity.getImplementation().get()));
  objective_ = QuadraticFunction(center, constant, linear, quadratic);
  setNearestPointConstraints();
}

Bool OptimizationProblemImplementation::hasLevelFunction() const
{
  return levelFunction_.getEvaluation()->isActualImplementation();
}

/* Level value accessor */
Scalar OptimizationProblemImplementation::getLevelValue() const
{
  return levelValue_;
}

void OptimizationProblemImplementation::setLevelValue(Scalar levelValue)
{
  levelValue_ = levelValue;
  // Update constraints
  if (hasLevelFunction()) setNearestPointConstraints();
}

void OptimizationProblemImplementation::setNearestPointConstraints()
{
  const Point center(dimension_);
  const Matrix linear(dimension_, 1);
  LinearFunction constantFunction(center, Point(1, levelValue_), linear.transpose());
  Function equalityConstraint(levelFunction_);
  equalityConstraint_ = equalityConstraint.operator - (constantFunction);
  inequalityConstraint_ = Function();
}

void OptimizationProblemImplementation::clearLevelFunction()
{
  LOGWARN(OSS() << "Clearing level function");
  levelFunction_ = Function();
  levelValue_ = 0.0;
}

/* Dimension accessor */
UnsignedInteger OptimizationProblemImplementation::getDimension() const
{
  return dimension_;
}

/* Minimization accessor */
void OptimizationProblemImplementation::setMinimization(Bool minimization)
{
  minimization_ = minimization;
}

Bool OptimizationProblemImplementation::isMinimization() const
{
  return minimization_;
}

/* String converter */
String OptimizationProblemImplementation::__repr__() const
{
  OSS oss;
  oss << "class=" << OptimizationProblemImplementation::GetClassName();
  if (hasLevelFunction())
  {
    oss << " level function=" << levelFunction_.__repr__()
        << " level value=" << levelValue_;
  }
  else
  {
    oss << " objective=" << objective_
        << " equality constraint=" << (hasEqualityConstraint() ? equalityConstraint_.__repr__() : "none")
        << " inequality constraint=" << (hasInequalityConstraint() ? inequalityConstraint_.__repr__() : "none");
  }
  oss << " bounds=" << (hasBounds() ? bounds_.__repr__() : "none")
      << " minimization=" << minimization_
      << " dimension=" << dimension_;
  return oss;
}

/* Method save() stores the object through the StorageManager */
void OptimizationProblemImplementation::save(Advocate & adv) const
{
  PersistentObject::save(adv);
  adv.saveAttribute( "objective_", objective_ );
  adv.saveAttribute( "equalityConstraint_", equalityConstraint_ );
  adv.saveAttribute( "inequalityConstraint_", inequalityConstraint_ );
  adv.saveAttribute( "bounds_", bounds_ );
  adv.saveAttribute( "minimization_", minimization_ );
  adv.saveAttribute( "dimension_", dimension_ );
}

/* Method load() reloads the object from the StorageManager */
void OptimizationProblemImplementation::load(Advocate & adv)
{
  PersistentObject::load(adv);
  adv.loadAttribute( "objective_", objective_ );
  adv.loadAttribute( "equalityConstraint_", equalityConstraint_ );
  adv.loadAttribute( "inequalityConstraint_", inequalityConstraint_ );
  adv.loadAttribute( "bounds_", bounds_ );
  adv.loadAttribute( "minimization_", minimization_ );
  adv.loadAttribute( "dimension_", dimension_ );
}

END_NAMESPACE_OPENTURNS
