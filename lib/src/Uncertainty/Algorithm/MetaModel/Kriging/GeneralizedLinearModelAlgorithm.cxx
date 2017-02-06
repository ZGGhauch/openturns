//                                               -*- C++ -*-
/**
 *  @brief The class builds generalized linear models
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

#include "openturns/GeneralizedLinearModelAlgorithm.hxx"
#include "openturns/PersistentObjectFactory.hxx"
#include "openturns/HMatrixFactory.hxx"
#include "openturns/ProductCovarianceModel.hxx"
#include "openturns/TensorizedCovarianceModel.hxx"
#include "openturns/Log.hxx"
#include "openturns/SpecFunc.hxx"
#include "openturns/LinearFunction.hxx"
#include "openturns/CenteredFiniteDifferenceHessian.hxx"
#include "openturns/NonCenteredFiniteDifferenceGradient.hxx"
#include "openturns/TNC.hxx"
#include "openturns/NLopt.hxx"
#include "openturns/SymbolicFunction.hxx"
#include "openturns/IdentityFunction.hxx"
#include "openturns/ComposedFunction.hxx"


BEGIN_NAMESPACE_OPENTURNS

CLASSNAMEINIT(GeneralizedLinearModelAlgorithm);

static const Factory<GeneralizedLinearModelAlgorithm> Factory_GeneralizedLinearModelAlgorithm;

/* Default constructor */
GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm()
  : MetaModelAlgorithm()
  , inputSample_(0, 1) // 1 is to be consistent with the default covariance model
  , normalizedInputSample_(0, 1) // same
  , inputTransformation_()
  , normalize_(false)
  , outputSample_(0, 1) // same
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(false)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(true)
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // Set the default covariance to adapt the active parameters of the covariance model
  setCovarianceModel(CovarianceModel());
  initializeDefaultOptimizationSolver();
}

/* Parameters constructor */
GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm(const NumericalSample & inputSample,
    const NumericalSample & outputSample,
    const CovarianceModel & covarianceModel,
    const Bool normalize,
    const Bool keepCholeskyFactor)
  : MetaModelAlgorithm()
  , inputSample_(0, 0)
  , normalizedInputSample_(0, 0)
  , inputTransformation_()
  , normalize_(normalize)
  , outputSample_(0, 0)
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(keepCholeskyFactor)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-OptimizeParameters"))
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // set data & covariance model
  setData(inputSample, outputSample);
  // If no basis then we suppose output sample centered
  checkYCentered(outputSample);
  setCovarianceModel(covarianceModel);

  // Build a normalization function if needed
  if (normalize_)
  {
    const UnsignedInteger dimension = inputSample_.getDimension();
    const NumericalPoint mean(inputSample_.computeMean());
    const NumericalPoint stdev(inputSample_.computeStandardDeviationPerComponent());
    SquareMatrix linear(dimension);
    for (UnsignedInteger j = 0; j < dimension; ++ j)
    {
      linear(j, j) = 1.0;
      if (std::abs(stdev[j]) > SpecFunc::MinNumericalScalar) linear(j, j) /= stdev[j];
    }
    const NumericalPoint zero(dimension);
    setInputTransformation(LinearFunction(mean, zero, linear));
  }
  initializeMethod();
  initializeDefaultOptimizationSolver();
}

GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm(const NumericalSample & inputSample,
    const NumericalSample & outputSample,
    const CovarianceModel & covarianceModel,
    const Basis & basis,
    const Bool normalize,
    const Bool keepCholeskyFactor)
  : MetaModelAlgorithm()
  , inputSample_()
  , normalizedInputSample_(0, inputSample.getDimension())
  , inputTransformation_()
  , normalize_(normalize)
  , outputSample_()
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(keepCholeskyFactor)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-OptimizeParameters"))
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // set data & covariance model
  setData(inputSample, outputSample);
  setCovarianceModel(covarianceModel);

  if (basis.getSize() > 0)
  {
    if (basis[0].getOutputDimension() > 1) LOGWARN(OSS() << "Expected a basis of scalar functions, but first function has dimension " << basis[0].getOutputDimension() << ". Only the first output component will be taken into account.");
    if (outputSample.getDimension() > 1) LOGWARN(OSS() << "The basis of functions will be applied to all output marginals" );
    // Set basis
    basisCollection_ = BasisCollection(outputSample.getDimension(), basis);
  }
  else
  {
    // If no basis then we suppose output sample centered
    checkYCentered(outputSample);
  }

  // Build a normalization function if needed
  if (normalize_)
  {
    const UnsignedInteger dimension = inputSample_.getDimension();
    const NumericalPoint mean(inputSample_.computeMean());
    const NumericalPoint stdev(inputSample_.computeStandardDeviationPerComponent());
    SquareMatrix linear(dimension);
    for (UnsignedInteger j = 0; j < dimension; ++ j)
    {
      linear(j, j) = 1.0;
      if (std::abs(stdev[j]) > SpecFunc::NumericalScalarEpsilon) linear(j, j) /= stdev[j];
    }
    const NumericalPoint zero(dimension);
    setInputTransformation(LinearFunction(mean, zero, linear));
  }
  initializeMethod();
  initializeDefaultOptimizationSolver();
}

/* Parameters constructor */
GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm(const NumericalSample & inputSample,
    const NumericalMathFunction & inputTransformation,
    const NumericalSample & outputSample,
    const CovarianceModel & covarianceModel,
    const Basis & basis,
    const Bool keepCholeskyFactor)
  : MetaModelAlgorithm()
  , inputSample_()
  , normalizedInputSample_(0, inputSample.getDimension())
  , inputTransformation_()
  , normalize_(true)
  , outputSample_()
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(keepCholeskyFactor)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-OptimizeParameters"))
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // set data & covariance model
  setData(inputSample, outputSample);
  setCovarianceModel(covarianceModel);

  // basis setter
  if (basis.getSize() > 0)
  {
    if (basis[0].getOutputDimension() > 1) LOGWARN(OSS() << "Expected a basis of scalar functions, but first function has dimension" << basis[0].getOutputDimension() << ". Only the first output component will be taken into account.");
    if (outputSample.getDimension() > 1) LOGWARN(OSS() << "The basis of functions will be applied to all output marginals" );
    // Set basis
    basisCollection_ = BasisCollection(outputSample.getDimension(), basis);
  }
  else
  {
    // If no basis then we suppose output sample centered
    checkYCentered(outputSample);
  }

  // Set the isoprobabilistic transformation
  setInputTransformation(inputTransformation);
  initializeMethod();
  initializeDefaultOptimizationSolver();
}

/* Parameters constructor */
GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm(const NumericalSample & inputSample,
    const NumericalSample & outputSample,
    const CovarianceModel & covarianceModel,
    const BasisCollection & basisCollection,
    const Bool normalize,
    const Bool keepCholeskyFactor)
  : MetaModelAlgorithm()
  , inputSample_(inputSample)
  , normalizedInputSample_(0, inputSample.getDimension())
  , inputTransformation_()
  , normalize_(normalize)
  , outputSample_(outputSample)
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(keepCholeskyFactor)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-OptimizeParameters"))
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // set data & covariance model
  setData(inputSample, outputSample);
  setCovarianceModel(covarianceModel);

  // Set basis collection
  if (basisCollection.getSize() > 0) setBasisCollection(basisCollection);

  // Build a normalization function if needed
  if (normalize_)
  {
    const UnsignedInteger dimension = inputSample_.getDimension();
    const NumericalPoint mean(inputSample_.computeMean());
    const NumericalPoint stdev(inputSample_.computeStandardDeviationPerComponent());
    SquareMatrix linear(dimension);
    for (UnsignedInteger j = 0; j < dimension; ++ j)
    {
      linear(j, j) = 1.0;
      if (std::abs(stdev[j]) > SpecFunc::MinNumericalScalar) linear(j, j) /= stdev[j];
    }
    const NumericalPoint zero(dimension);
    setInputTransformation(LinearFunction(mean, zero, linear));
  }
  initializeMethod();
  initializeDefaultOptimizationSolver();
}

/* Parameters constructor */
GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm(const NumericalSample & inputSample,
    const NumericalMathFunction & inputTransformation,
    const NumericalSample & outputSample,
    const CovarianceModel & covarianceModel,
    const BasisCollection & basisCollection,
    const Bool keepCholeskyFactor)
  : MetaModelAlgorithm()
  , inputSample_()
  , normalizedInputSample_(0, inputSample.getDimension())
  , inputTransformation_()
  , normalize_(true)
  , outputSample_()
  , covarianceModel_()
  , reducedCovarianceModel_()
  , solver_()
  , optimizationBounds_()
  , beta_(0)
  , rho_(0)
  , F_(0, 0)
  , result_()
  , basisCollection_()
  , covarianceCholeskyFactor_()
  , covarianceCholeskyFactorHMatrix_()
  , keepCholeskyFactor_(keepCholeskyFactor)
  , method_(0)
  , hasRun_(false)
  , optimizeParameters_(ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-OptimizeParameters"))
  , analyticalAmplitude_(false)
  , lastReducedLogLikelihood_(SpecFunc::LogMinNumericalScalar)
{
  // set data & covariance model
  setData(inputSample, outputSample);
  setCovarianceModel(covarianceModel);

  // Set basis collection
  if (basisCollection.getSize() > 0)  setBasisCollection(basisCollection);

  // Set the isoprobabilistic transformation
  setInputTransformation(inputTransformation);
  initializeMethod();
  initializeDefaultOptimizationSolver();
}

/* set sample  method */
void GeneralizedLinearModelAlgorithm::setData(const NumericalSample & inputSample,
    const NumericalSample & outputSample)
{
  // Check the sample sizes
  if (inputSample.getSize() != outputSample.getSize())
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm, input sample size=" << inputSample.getSize() << " does not match output sample size=" << outputSample.getSize();
  // Set samples
  inputSample_ = inputSample;
  outputSample_ = outputSample;
}

/* Covariance model accessors */
void GeneralizedLinearModelAlgorithm::setCovarianceModel(const CovarianceModel & covarianceModel)
{
  // Here we can store any modified version of the given covariance model wrt its parameters as it is mainly a parametric template
  const UnsignedInteger inputDimension = inputSample_.getDimension();
  const UnsignedInteger dimension = outputSample_.getDimension();

  // Check dimensions of the covariance model
  // There are 4 cases:
  // 1) Both the spatial dimension and the dimension of the model match the dimensions of the problem, in which case the model is used as-is
  // 2) The spatial dimension of the model is 1 and different from the spatial dimension of the problem, and the dimension of both the model and the problem are 1. The actual model is a product of the given model.
  // 3) The spatial dimension of the model and the problem match, but the dimension of the model is 1, different from the dimension of the problem. The actual model is a tensorization of the given model.
  // 4) The spatial dimension of the model is 1 and different from the spatial dimension of the problem, and the dimension of the model is 1 and different from the dimension of the problem. The actual model is a tensorization of products of the given model.
  // The other situations are invalid.

  const Bool sameDimension = dimension == covarianceModel.getDimension();
  const Bool unitModelDimension = covarianceModel.getDimension() == 1;
  const Bool sameSpatialDimension = inputDimension == covarianceModel.getSpatialDimension();
  const Bool unitModelSpatialDimension = covarianceModel.getSpatialDimension() == 1;
  // Case 1
  if (sameSpatialDimension && sameDimension)
    covarianceModel_ = covarianceModel;
  // Case 2
  else if (unitModelSpatialDimension && sameDimension && unitModelDimension)
    covarianceModel_ = ProductCovarianceModel(ProductCovarianceModel::CovarianceModelCollection(inputDimension, covarianceModel));
  // Case 3
  else if (sameSpatialDimension && unitModelDimension)
    covarianceModel_ = TensorizedCovarianceModel(TensorizedCovarianceModel::CovarianceModelCollection(dimension, covarianceModel));
  // Case 4
  else if (unitModelSpatialDimension && unitModelDimension)
    covarianceModel_ = TensorizedCovarianceModel(TensorizedCovarianceModel::CovarianceModelCollection(dimension, ProductCovarianceModel(ProductCovarianceModel::CovarianceModelCollection(inputDimension, covarianceModel))));
  else throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm, invalid dimension=" << covarianceModel.getDimension() << " or spatial dimension=" << covarianceModel.getSpatialDimension() << " for the given covariance model. A model of both spatial dimension=" << inputDimension << " and dimension=" << dimension << " is expected, or a model of spatial dimension=" << inputDimension << " and unit dimension, or a model of unit spatial dimension and dimension=" << dimension << ", or a model of unit spatial dimension and unit dimension.";
  // All the computation will be done on the reduced covariance model. We keep the initial covariance model (ie the one we just built) in order to reinitialize the reduced covariance model if some flags are changed after the creation of the algorithm.
  reducedCovarianceModel_ = covarianceModel_;
  // Now, adapt the model parameters.
  // First, check if the parameters have to be optimized. If not, remove all the active parameters.
  analyticalAmplitude_ = false;
  if (!optimizeParameters_) reducedCovarianceModel_.setActiveParameter(Indices());
  // Second, check if the amplitude parameter is unique and active
  else if (ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-UseAnalyticalAmplitudeEstimate"))
  {
    // The model has to be of dimension 1
    if (reducedCovarianceModel_.getDimension() == 1)
    {
      const Description activeParametersDescription(reducedCovarianceModel_.getParameterDescription());
      // And one of the active parameters must be called amplitude_0
      for (UnsignedInteger i = 0; i < activeParametersDescription.getSize(); ++i)
        if (activeParametersDescription[i] == "amplitude_0")
        {
          analyticalAmplitude_ = true;
          Indices newActiveParameters(reducedCovarianceModel_.getActiveParameter());
          newActiveParameters.erase(newActiveParameters.begin() + i);
          reducedCovarianceModel_.setActiveParameter(newActiveParameters);
          // Here we have to change the current value of the amplitude as it has
          // to be equal to 1 during the potential optimization step in order for
          // the analytical formula to be correct.
          // Now, the amplitude has disapear form the active parameters so it must
          // be updated using the amplitude accessor.
          reducedCovarianceModel_.setAmplitude(NumericalPoint(1, 1.0));
          break;
        }
    } // reducedCovarianceModel_.getDimension() == 1
  } // optimizeParameters_
  LOGINFO(OSS() << "final active parameters=" << reducedCovarianceModel_.getActiveParameter());
  // Define the bounds of the optimization problem
  const UnsignedInteger optimizationDimension = reducedCovarianceModel_.getParameter().getSize();
  if (optimizationDimension > 0)
  {
    const NumericalPoint lowerBound(optimizationDimension, ResourceMap::GetAsNumericalScalar( "GeneralizedLinearModelAlgorithm-DefaultOptimizationLowerBound"));
    const NumericalPoint upperBound(optimizationDimension, ResourceMap::GetAsNumericalScalar( "GeneralizedLinearModelAlgorithm-DefaultOptimizationUpperBound"));
    optimizationBounds_ = Interval(lowerBound, upperBound);
  }
  else optimizationBounds_ = Interval();
}

CovarianceModel GeneralizedLinearModelAlgorithm::getCovarianceModel() const
{
  return covarianceModel_;
}

CovarianceModel GeneralizedLinearModelAlgorithm::getReducedCovarianceModel() const
{
  return reducedCovarianceModel_;
}

/* Set basis collection method */
void GeneralizedLinearModelAlgorithm::setBasisCollection(const BasisCollection & basis)
{
  // If basis given, then its size should be the same as the output dimension (each marginal of multibasis is a basis that will be used for trend of the corresponding marginal.
  if (basis.getSize() != outputSample_.getDimension())
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm, output sample dimension=" << outputSample_.getDimension()  << " does not match multi-basis dimension=" << basis.getSize();
  // Get the output dimension of the basis
  // The first marginal may be an empty basis
  Bool continuationCondition = true;
  UnsignedInteger index = 0;
  UnsignedInteger outputDimension = 0;
  while(continuationCondition)
  {
    try
    {
      outputDimension =  basis[index][0].getOutputDimension();
      continuationCondition = false;
    }
    catch (InvalidArgumentException)
    {
      ++index;
      continuationCondition = continuationCondition && index < basis.getSize();
    }
  }
  if (outputDimension == 0)
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm, basisCollection argument contains basis with empty collection of functions";
  if (outputDimension > 1) LOGWARN(OSS() << "Expected a basis of scalar functions, but some function has dimension" << outputDimension << ". Only the first output component will be taken into account.");
  // Everything is ok, we set the basis
  basisCollection_ = basis;
}

void GeneralizedLinearModelAlgorithm::checkYCentered(const NumericalSample & Y)
{
  const NumericalScalar meanEpsilon = ResourceMap::GetAsNumericalScalar("GeneralizedLinearModelAlgorithm-MeanEpsilon");
  const NumericalPoint meanY(Y.computeMean());
  for (UnsignedInteger k = 0; k < meanY.getDimension(); ++k)
  {
    if (std::abs(meanY[k]) > meanEpsilon)
      LOGWARN(OSS() << "In GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm, basis is empty and output sample is not centered, mean=" << meanY);
  }
}

void GeneralizedLinearModelAlgorithm::initializeDefaultOptimizationSolver()
{
  const String solverName(ResourceMap::Get("GeneralizedLinearModelAlgorithm-DefaultOptimizationSolver"));
  if (solverName == "TNC")
    solver_ = TNC();
  else if (solverName == "NELDER-MEAD")
    solver_ = NLopt("LN_NELDERMEAD");
  else if (solverName == "LBFGS")
    solver_ = NLopt("LD_LBFGS");
  else
    throw InvalidArgumentException(HERE) << "Unknown optimization solver:" << solverName;
}

/* Virtual constructor */
GeneralizedLinearModelAlgorithm * GeneralizedLinearModelAlgorithm::clone() const
{
  return new GeneralizedLinearModelAlgorithm(*this);
}


/* Normalize the input sample */
void GeneralizedLinearModelAlgorithm::normalizeInputSample()
{
  // Nothing to do if the sample has alredy been normalized
  if (normalizedInputSample_.getSize() != 0) return;
  // If we don't want to normalize the data
  if (!normalize_)
  {
    LOGINFO("No need to normalize the data");
    normalizedInputSample_ = inputSample_;
    return;
  }
  LOGINFO("Data are normalized");
  normalizedInputSample_ = inputTransformation_(inputSample_);
}

/* Compute the design matrix */
void GeneralizedLinearModelAlgorithm::computeF()
{
  // Nothing to do if the design matrix has already been computed
  if (F_.getNbRows() != 0) return;
  // No early exit based on the sample/basis size as F_ must be initialized with the correct dimensions
  // With a multivariate basis of size similar to output dimension, each ith-basis should be applied to elements
  // of corresponding marginal
  const UnsignedInteger outputDimension = outputSample_.getDimension();
  const UnsignedInteger sampleSize = normalizedInputSample_.getSize();
  const UnsignedInteger basisCollectionSize = basisCollection_.getSize();
  UnsignedInteger totalSize = 0;
  for (UnsignedInteger i = 0; i < basisCollectionSize; ++ i ) totalSize += basisCollection_[i].getSize();
  // if totalSize > 0, then basisCollection_.getSize() should be equal to outputDimension
  // This is checked in GeneralizedLinearModelAlgorithm::GeneralizedLinearModelAlgorithm
  F_ = Matrix(sampleSize * outputDimension, totalSize);
  if (totalSize == 0) return;
  // Compute F
  UnsignedInteger index = 0;
  for (UnsignedInteger outputMarginal = 0; outputMarginal < outputDimension; ++ outputMarginal )
  {
    const Basis localBasis = basisCollection_[outputMarginal];
    const UnsignedInteger localBasisSize = localBasis.getSize();
    for (UnsignedInteger j = 0; j < localBasisSize; ++j, ++index )
    {
      // Here we use potential parallelism in the evaluation of the basis functions
      const NumericalSample basisSample = localBasis[j](normalizedInputSample_);
      for (UnsignedInteger i = 0; i < sampleSize; ++i) F_(outputMarginal + i * outputDimension, index) = basisSample[i][0];
    }
  }
}


/* Perform regression
1) Compute the design matrix
2) Call the parameters optimization
  a) Compute the log-likelihood with the initial parameters. It is mandatory
     even if no parameter has to be optimized as this computation has many side
     effects such as:
     * computing the trend coefficients beta
     * computing the discretized covariance matrix Cholesky factor
  b) If the amplitude can be computed analytically from the other parameters:
     * set its value to 1
     * remove it from the list of parameters
  c) If some parameters remain, perform the optimization
  d) Deduce the associated value of the amplitude by the analytical formula if possible
3) Build the result:
  a) Extract the different parts of the trend
  b) Update the covariance model if needed
 */

void GeneralizedLinearModelAlgorithm::run()
{
  // Do not run again if already computed
  if (hasRun_) return;
  LOGINFO("Normalize the data");
  normalizeInputSample();
  LOGINFO("Compute the design matrix");
  computeF();
  const UnsignedInteger outputDimension = outputSample_.getDimension();
  // optimization of likelihood function if provided
  LOGINFO("Optimize the parameter of the covariance model if needed");
  // Here we call the optimizeReducedLogLikelihood() method even if the covariance
  // model has no active parameter, because:
  // + it can be due to the fact that the amplitude is obtained through an
  //   analytical formula and this situation is taken into account in
  //   maximizeReducedLogLikelihood()
  // + even if there is actually no parameter to optimize,
  //   maximizeReducedLogLikelihood() is the entry point to
  //   computeReducedLogLikelyhood() which has side effects on covariance
  //   discretization and factorization, and it computes beta_
  NumericalScalar optimalLogLikelihood = maximizeReducedLogLikelihood();
  LOGINFO("Store the estimates");
  // Here we do the work twice:
  // 1) To get a collection of NumericalPoint for the result class
  // 2) To get same results as NumericalSample for the trend NMF
  Collection<NumericalPoint> trendCoefficients(basisCollection_.getSize());
  NumericalSample trendCoefficientsSample(beta_.getSize(), reducedCovarianceModel_.getDimension());

  UnsignedInteger cumulatedSize = 0;
  for (UnsignedInteger outputIndex = 0; outputIndex < basisCollection_.getSize(); ++ outputIndex)
  {
    const UnsignedInteger localBasisSize = basisCollection_[outputIndex].getSize();
    NumericalPoint beta_i(localBasisSize);
    for(UnsignedInteger basisElement = 0; basisElement < localBasisSize; ++ basisElement)
    {
      beta_i[basisElement] = beta_[cumulatedSize];
      trendCoefficientsSample[cumulatedSize][outputIndex] =  beta_[cumulatedSize];
      ++cumulatedSize;
    }
    trendCoefficients[outputIndex] = beta_i;
  }

  LOGINFO("Build the output meta-model");
  // The meta model is of type DualLinearCombination function
  // We should write the coefficients into a NumericalSample and build the basis into a collection
  Collection<NumericalMathFunction> allFunctionsCollection;
  for (UnsignedInteger k = 0; k < basisCollection_.getSize(); ++k)
    for (UnsignedInteger l = 0; l < basisCollection_[k].getSize(); ++l)
      allFunctionsCollection.add(basisCollection_[k].build(l));
  NumericalMathFunction metaModel;

  if (basisCollection_.getSize() > 0)
  {
    // Care ! collection should be non empty
    metaModel = NumericalMathFunction(allFunctionsCollection, trendCoefficientsSample);
  }
  else
  {
    // If no basis ==> zero function
#ifdef OPENTURNS_HAVE_MUPARSER
    metaModel = SymbolicFunction(Description::BuildDefault(covarianceModel_.getSpatialDimension(), "x"), Description(covarianceModel_.getDimension(), "0.0"));
#else
    metaModel = NumericalMathFunction(NumericalSample(1, reducedCovarianceModel_.getSpatialDimension()), NumericalSample(1, reducedCovarianceModel_.getDimension()));
#endif
  }

  // Add transformation if needed
  if (normalize_) metaModel = ComposedFunction(metaModel, inputTransformation_);

  // compute residual, relative error
  const NumericalPoint outputVariance(outputSample_.computeVariance());
  const NumericalSample mY(metaModel(inputSample_));
  const NumericalPoint squaredResiduals((outputSample_ - mY).computeRawMoment(2));

  NumericalPoint residuals(outputDimension);
  NumericalPoint relativeErrors(outputDimension);

  const UnsignedInteger size = inputSample_.getSize();
  for ( UnsignedInteger outputIndex = 0; outputIndex < outputDimension; ++ outputIndex )
  {
    residuals[outputIndex] = sqrt(squaredResiduals[outputIndex] / size);
    relativeErrors[outputIndex] = squaredResiduals[outputIndex] / outputVariance[outputIndex];
  }

  // The scaling is done there because it has to be done as soon as some optimization has been done, either numerically or through an analytical formula
  if (keepCholeskyFactor_)
  {
    if (analyticalAmplitude_)
    {
      const NumericalScalar sigma = reducedCovarianceModel_.getAmplitude()[0];
      // Case of LAPACK backend
      if (method_ == 0) covarianceCholeskyFactor_ = covarianceCholeskyFactor_ * sigma;
      else covarianceCholeskyFactorHMatrix_.scale(sigma);
    }
    result_ = GeneralizedLinearModelResult(inputSample_, outputSample_, metaModel, residuals, relativeErrors, basisCollection_, trendCoefficients, reducedCovarianceModel_, optimalLogLikelihood, covarianceCholeskyFactor_, covarianceCholeskyFactorHMatrix_);
  }
  else
    result_ = GeneralizedLinearModelResult(inputSample_, outputSample_, metaModel, residuals, relativeErrors, basisCollection_, trendCoefficients, reducedCovarianceModel_, optimalLogLikelihood);
  // If normalize, set input transformation
  if (normalize_) result_.setTransformation(inputTransformation_);
  hasRun_ = true;
}

// Maximize the log-likelihood of the Normal process model wrt the observations
// If the covariance model has no active parameter, no numerical optimization
// is done. There are two cases:
// + no parameter has to be optimized, in which case a single call to
//   computeReducedLogLikelihood() is made in order to compute beta_ and to
//   factor the covariance matrix
// + the amplitude is the only covariance parameter to be estimated and it is
//   done thanks to an analytical formula
// The method returns the optimal log-likelihood (which is equal to the optimal
// reduced log-likelihood), the corresponding parameters being directly stored
// into the covariance model
NumericalScalar GeneralizedLinearModelAlgorithm::maximizeReducedLogLikelihood()
{
  // initial guess
  NumericalPoint initialParameters(reducedCovarianceModel_.getParameter());
  Indices initialActiveParameters(reducedCovarianceModel_.getActiveParameter());
  // We use the functional form of the log-likelihood computation to benefit from the cache mechanism
  NumericalMathFunction reducedLogLikelihoodFunction(getObjectiveFunction());
  const Bool noNumericalOptimization = initialParameters.getSize() == 0;
  // Early exit if the parameters are known
  if (noNumericalOptimization)
  {
    // We only need to compute the log-likelihood function at the initial parameters in order to get the Cholesky factor and the trend coefficients
    const NumericalScalar initialReducedLogLikelihood = reducedLogLikelihoodFunction(initialParameters)[0];
    LOGINFO("No covariance parameter to optimize");
    LOGINFO(OSS() << "initial parameters=" << initialParameters << ", log-likelihood=" << initialReducedLogLikelihood);
    return initialReducedLogLikelihood;
  }
  // At this point we have an optimization problem to solve
  // Define the optimization problem
  OptimizationProblem problem;
  problem.setObjective(reducedLogLikelihoodFunction);
  problem.setMinimization(false);
  problem.setBounds(optimizationBounds_);
  solver_.setStartingPoint(initialParameters);
  solver_.setProblem(problem);
  LOGINFO(OSS(false) << "Solve problem=" << problem << " using solver=" << solver_);
  solver_.run();
  const NumericalScalar optimalLogLikelihood = solver_.getResult().getOptimalValue()[0];
  const NumericalPoint optimalParameters = solver_.getResult().getOptimalPoint();
  // Check if the optimal value corresponds to the last computed value, in order to
  // see if the by-products (Cholesky factor etc) are correct
  if (lastReducedLogLikelihood_ != optimalLogLikelihood)
  {
    LOGINFO(OSS(false) << "Need to evaluate the objective function one more time because the last computed reduced log-likelihood value=" << lastReducedLogLikelihood_ << " is different from the optimal one=" << optimalLogLikelihood);
    (void) computeReducedLogLikelihood(optimalParameters);
  }
  // Final call to reducedLogLikelihoodFunction() in order to update the amplitude
  // No additional cost since the cache mechanism is activated
  LOGDEBUG(OSS() << "Optimized parameters=" << optimalParameters << ", log-likelihood=" << optimalLogLikelihood);
  return optimalLogLikelihood;
}

NumericalPoint GeneralizedLinearModelAlgorithm::computeReducedLogLikelihood(const NumericalPoint & parameters) const
{
  // Check that the parameters have a size compatible with the covariance model
  if (parameters.getSize() != reducedCovarianceModel_.getParameter().getSize())
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::computeReducedLogLikelihood, could not compute likelihood,"
                                         << " covariance model requires an argument of size " << reducedCovarianceModel_.getParameter().getSize()
                                         << " but here we got " << parameters.getSize();
  LOGINFO(OSS(false) << "Compute reduced log-likelihood for parameters=" << parameters);
  NumericalScalar logDeterminant = 0.0;
  // If the amplitude is deduced from the other parameters, work with
  // the correlation function
  if (analyticalAmplitude_) reducedCovarianceModel_.setAmplitude(NumericalPoint(1, 1.0));
  reducedCovarianceModel_.setParameter(parameters);
  // First, compute the log-determinant of the Cholesky factor of the covariance
  // matrix. As a by-product, also compute rho.
  if (method_ == 0)
    logDeterminant = computeLapackLogDeterminantCholesky();
  else
    logDeterminant = computeHMatLogDeterminantCholesky();
  // Compute the amplitude using an analytical formula if needed
  // and update the reduced log-likelihood.
  if (analyticalAmplitude_)
  {
    LOGINFO("Analytical amplitude");
    // J(\sigma)=-\log(\sqrt{\sigma^{2N}\det{R}})-(Y-M)^tR^{-1}(Y-M)/(2\sigma^2)
    //          =-N\log(\sigma)-\log(\det{R})/2-(Y-M)^tR^{-1}(Y-M)/(2\sigma^2)
    // dJ/d\sigma=-N/\sigma+(Y-M)^tR^{-1}(Y-M)/\sigma^3=0
    // \sigma=\sqrt{(Y-M)^tR^{-1}(Y-M)/N}
    const UnsignedInteger size = inputSample_.getSize();
    const NumericalScalar sigma = std::sqrt(rho_.normSquare() / (ResourceMap::GetAsBool("GeneralizedLinearModelAlgorithm-UnbiasedVariance") ? size - beta_.getSize() : size));
    LOGDEBUG(OSS(false) << "sigma=" << sigma);
    reducedCovarianceModel_.setAmplitude(NumericalPoint(1, sigma));
    logDeterminant += 2.0 * size * std::log(sigma);
    rho_ /= sigma;
    LOGDEBUG(OSS(false) << "rho_=" << rho_);
  } // analyticalAmplitude

  LOGDEBUG(OSS(false) << "log-determinant=" << logDeterminant << ", rho=" << rho_);
  const NumericalScalar epsilon = rho_.normSquare();
  LOGDEBUG(OSS(false) << "epsilon=||rho||^2=" << epsilon);
  if (epsilon <= 0) lastReducedLogLikelihood_ = SpecFunc::LogMinNumericalScalar;
  // For the general multidimensional case, we have to compute the general log-likelihood (ie including marginal variances)
  else lastReducedLogLikelihood_ = -0.5 * (logDeterminant + epsilon);
  LOGINFO(OSS(false) << "Reduced log-likelihood=" << lastReducedLogLikelihood_);
  return NumericalPoint(1, lastReducedLogLikelihood_);
}


NumericalScalar GeneralizedLinearModelAlgorithm::computeLapackLogDeterminantCholesky() const
{
  // Using the hypothesis that parameters = scale & model writes : C(s,t) = diag(sigma) * R(s,t) * diag(sigma) with R a correlation function
  LOGINFO(OSS(false) << "Compute the LAPACK log-determinant of the Cholesky factor for covariance=" << reducedCovarianceModel_);

  LOGINFO("Discretize the covariance model");
  CovarianceMatrix C(reducedCovarianceModel_.discretize(normalizedInputSample_));
  if (noise_.getDimension() > 0)
  {
    LOGINFO("Add noise to the covariance matrix");
    for (UnsignedInteger i = 0; i < C.getDimension(); ++ i) C(i, i) += noise_[i];
  }
  LOGDEBUG(OSS(false) << "C=\n" << C);
  LOGINFO("Compute the Cholesky factor of the covariance matrix");
  Bool continuationCondition = true;
  const NumericalScalar startingScaling = ResourceMap::GetAsNumericalScalar("GeneralizedLinearModelAlgorithm-StartingScaling");
  const NumericalScalar maximalScaling = ResourceMap::GetAsNumericalScalar("GeneralizedLinearModelAlgorithm-MaximalScaling");
  NumericalScalar cumulatedScaling = 0.0;
  NumericalScalar scaling = startingScaling;
  while (continuationCondition && (cumulatedScaling < maximalScaling))
  {
    try
    {
      covarianceCholeskyFactor_ = C.computeCholesky();
      continuationCondition = false;
    }
    // If it has not yet been computed, compute it and store it
    catch (InternalException &)
    {
      cumulatedScaling += scaling ;
      // Unroll the regularization to optimize the computation
      for (UnsignedInteger i = 0; i < C.getDimension(); ++i) C(i, i) += scaling;
      scaling *= 2.0;
    }
  }
  if (scaling >= maximalScaling)
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::computeLapackLogDeterminantCholesky, could not compute the Cholesky factor."
                                         << " Scaling up to "  << cumulatedScaling << " was not enough";
  if (cumulatedScaling > 0.0)
    LOGWARN(OSS() <<  "Warning! Scaling up to "  << cumulatedScaling << " was needed in order to get an admissible covariance. ");
  LOGDEBUG(OSS(false) << "L=\n" << covarianceCholeskyFactor_);

  // y corresponds to output data
  const NumericalPoint y(outputSample_.getImplementation()->getData());
  LOGDEBUG(OSS(false) << "y=" << y);
  // rho = L^{-1}y
  LOGINFO("Solve L.rho = y");
  rho_ = covarianceCholeskyFactor_.solveLinearSystem(y);
  LOGDEBUG(OSS(false) << "rho_=L^{-1}y=" << rho_);
  // If trend to estimate
  if (basisCollection_.getSize() > 0)
  {
    // Phi = L^{-1}F
    LOGINFO("Solve L.Phi = F");
    LOGDEBUG(OSS(false) << "F_=\n" << F_);
    Matrix Phi(covarianceCholeskyFactor_.solveLinearSystem(F_));
    LOGDEBUG(OSS(false) << "Phi=\n" << Phi);
    LOGINFO("Solve min_beta||Phi.beta - rho||^2");
    beta_ = Phi.solveLinearSystem(rho_);
    LOGDEBUG(OSS(false) << "beta_=" << beta_);
    LOGINFO("Update rho");
    rho_ -= Phi * beta_;
    LOGDEBUG(OSS(false) << "rho_=L^{-1}y-L^{-1}F.beta=" << rho_);
  }
  LOGINFO("Compute log(|det(L)|)=log(sqrt(|det(C)|))");
  NumericalScalar logDetL = 0.0;
  for (UnsignedInteger i = 0; i < covarianceCholeskyFactor_.getDimension(); ++i )
  {
    const NumericalScalar lii = covarianceCholeskyFactor_(i, i);
    if (lii <= 0.0) return -SpecFunc::LogMaxNumericalScalar;
    logDetL += log(lii);
  }
  LOGDEBUG(OSS(false) << "logDetL=" << logDetL);
  return 2.0 * logDetL;
}

NumericalScalar GeneralizedLinearModelAlgorithm::computeHMatLogDeterminantCholesky() const
{
  // Using the hypothesis that parameters = scale & model writes : C(s,t) = \sigma^2 * R(s,t) with R a correlation function
  LOGINFO(OSS(false) << "Compute the HMAT log-determinant of the Cholesky factor for covariance=" << reducedCovarianceModel_);

  Bool continuationCondition = true;
  const NumericalScalar startingScaling = ResourceMap::GetAsNumericalScalar("GeneralizedLinearModelAlgorithm-StartingScaling");
  const NumericalScalar maximalScaling = ResourceMap::GetAsNumericalScalar("GeneralizedLinearModelAlgorithm-MaximalScaling");
  NumericalScalar cumulatedScaling = 0.0;
  NumericalScalar scaling = startingScaling;
  const UnsignedInteger covarianceDimension = reducedCovarianceModel_.getDimension();

  HMatrixFactory hmatrixFactory;
  HMatrixParameters hmatrixParameters;

  while (continuationCondition && (cumulatedScaling < maximalScaling))
  {
    try
    {
      covarianceCholeskyFactorHMatrix_ = hmatrixFactory.build(normalizedInputSample_, covarianceDimension, true, hmatrixParameters);
      if (covarianceDimension == 1)
      {
        CovarianceAssemblyFunction simple(reducedCovarianceModel_, normalizedInputSample_, cumulatedScaling);
        covarianceCholeskyFactorHMatrix_.assemble(simple, 'L');
      }
      else
      {
        CovarianceBlockAssemblyFunction block(reducedCovarianceModel_, normalizedInputSample_, cumulatedScaling);
        covarianceCholeskyFactorHMatrix_.assemble(block, 'L');
      }
      // Factorize
      covarianceCholeskyFactorHMatrix_.factorize("LLt");
      continuationCondition = false;
    }
    // If it has not yet been computed, compute it and store it
    catch (InternalException &)
    {
      cumulatedScaling += scaling ;
      scaling *= 2.0;
      NumericalScalar assemblyEpsilon = hmatrixParameters.getAssemblyEpsilon() / 10.0;
      hmatrixParameters.setAssemblyEpsilon(assemblyEpsilon);
      NumericalScalar recompressionEpsilon = hmatrixParameters.getRecompressionEpsilon() / 10.0;
      hmatrixParameters.setRecompressionEpsilon(recompressionEpsilon);
      LOGDEBUG(OSS() <<  "Currently, scaling up to "  << cumulatedScaling << " to get an admissible covariance. Maybe compression & recompression factors are not adapted.");
      LOGDEBUG(OSS() <<  "Currently, assembly espilon = "  << assemblyEpsilon );
      LOGDEBUG(OSS() <<  "Currently, recompression epsilon "  <<  recompressionEpsilon);
    }
  }
  if (scaling >= maximalScaling)
    throw InvalidArgumentException(HERE) << "In GeneralizedLinearModelAlgorithm::computeHMatLogLikelihood, could not compute the Cholesky factor"
                                         << " Scaling up to "  << cumulatedScaling << " was not enough";
  if (cumulatedScaling > 0.0)
    LOGWARN(OSS() <<  "Warning! Scaling up to "  << cumulatedScaling << " was needed in order to get an admissible covariance. ");

  // y corresponds to output data
  // The PersistentCollection is returned as NumericalPoint with the right memory map
  NumericalPoint y(outputSample_.getImplementation()->getData());
  // rho = L^{-1}y
  LOGINFO("Solve L.rho = y");
  rho_ = covarianceCholeskyFactorHMatrix_.solveLower(y);
  // If trend to estimate
  if (basisCollection_.getSize() > 0)
  {
    // Phi = L^{-1}F
    LOGDEBUG("Solve L.Phi = F");
    Matrix Phi(covarianceCholeskyFactorHMatrix_.solveLower(F_));
    LOGINFO("Solve min_beta||Phi.beta - rho||^2");
    beta_ = Phi.solveLinearSystem(rho_);
    rho_ -= Phi * beta_;
  }
  LOGINFO("Compute log(sqrt(|det(C)|)) = log(|det(L)|)");
  NumericalScalar logDetL = 0.0;
  NumericalPoint diagonal(covarianceCholeskyFactorHMatrix_.getDiagonal());
  for (UnsignedInteger i = 0; i < rho_.getSize(); ++i )
  {
    const NumericalScalar lii = diagonal[i];
    if (lii <= 0.0) return SpecFunc::MaxNumericalScalar;
    logDetL += log(lii);
  }
  return 2.0 * logDetL;
}

/* Optimization solver accessor */
OptimizationSolver GeneralizedLinearModelAlgorithm::getOptimizationSolver() const
{
  return solver_;
}
void GeneralizedLinearModelAlgorithm::setOptimizationSolver(const OptimizationSolver & solver)
{
  solver_ = solver;
  hasRun_ = false;
}

void GeneralizedLinearModelAlgorithm::setInputTransformation(const NumericalMathFunction & inputTransformation)
{
  if (inputTransformation.getInputDimension() != inputSample_.getDimension()) throw InvalidDimensionException(HERE)
        << "In GeneralizedLinearModelAlgorithm::setInputTransformation, input dimension of the transformation=" << inputTransformation.getInputDimension() << " should match input sample dimension=" << inputSample_.getDimension();
  if (inputTransformation.getOutputDimension() != inputSample_.getDimension()) throw InvalidDimensionException(HERE)
        << "In GeneralizedLinearModelAlgorithm::setInputTransformation, output dimension of the transformation=" << inputTransformation.getOutputDimension() << " should match output sample dimension=" << inputSample_.getDimension();
  inputTransformation_ = inputTransformation;
  // Set normalize to true
  normalize_ = true;
}

NumericalMathFunction GeneralizedLinearModelAlgorithm::getInputTransformation() const
{
  // If normlize is false, we return identity function
  if (!normalize_) return IdentityFunction(inputSample_.getDimension());
  return inputTransformation_;
}

/* Optimize parameters flag accessor */
Bool GeneralizedLinearModelAlgorithm::getOptimizeParameters() const
{
  return optimizeParameters_;
}

void GeneralizedLinearModelAlgorithm::setOptimizeParameters(const Bool optimizeParameters)
{
  if (optimizeParameters != optimizeParameters_)
  {
    optimizeParameters_ = optimizeParameters;
    // Here we have to call setCovarianceModel() as it computes reducedCovarianceModel from covarianceModel_ in a way influenced by optimizeParameters_ flag.
    setCovarianceModel(covarianceModel_);
  }
}

/* Accessor to optimization bounds */
void GeneralizedLinearModelAlgorithm::setOptimizationBounds(const Interval & optimizationBounds)
{
  if (!(optimizationBounds.getDimension() == reducedCovarianceModel_.getParameter().getSize())) throw InvalidArgumentException(HERE) << "Error: expected bounds of dimension=" << reducedCovarianceModel_.getParameter().getSize() << ", got dimension=" << optimizationBounds.getDimension();
  optimizationBounds_ = optimizationBounds;
}

Interval GeneralizedLinearModelAlgorithm::getOptimizationBounds() const
{
  return optimizationBounds_;
}

/* Observation noise accessor */
void GeneralizedLinearModelAlgorithm::setNoise(const NumericalPoint & noise)
{
  const UnsignedInteger size = inputSample_.getSize();
  if (noise.getSize() != size) throw InvalidArgumentException(HERE) << "Noise size=" << noise.getSize()  << " does not match sample size=" << size;
  for (UnsignedInteger i = 0; i < size; ++ i)
    if (!(noise[i] >= 0.0)) throw InvalidArgumentException(HERE) << "Noise must be positive";
  noise_ = noise;
}


NumericalPoint GeneralizedLinearModelAlgorithm::getNoise() const
{
  return noise_;
}


NumericalPoint GeneralizedLinearModelAlgorithm::getRho() const
{
  return rho_;
}

/* String converter */
String GeneralizedLinearModelAlgorithm::__repr__() const
{
  OSS oss;
  oss << "class=" << getClassName()
      << ", inputSample=" << inputSample_
      << ", outputSample=" << outputSample_
      << ", basis=" << basisCollection_
      << ", covarianceModel=" << covarianceModel_
      << ", reducedCovarianceModel=" << reducedCovarianceModel_
      << ", solver=" << solver_
      << ", optimizeParameters=" << optimizeParameters_
      << ", noise=" << noise_;
  return oss;
}


NumericalSample GeneralizedLinearModelAlgorithm::getInputSample() const
{
  return inputSample_;
}


NumericalSample GeneralizedLinearModelAlgorithm::getOutputSample() const
{
  return outputSample_;
}


GeneralizedLinearModelResult GeneralizedLinearModelAlgorithm::getResult()
{
  if (!hasRun_) run();
  return result_;
}


NumericalMathFunction GeneralizedLinearModelAlgorithm::getObjectiveFunction()
{
  LOGINFO("Normalizing the data (if needed)...");
  normalizeInputSample();
  LOGINFO("Compute the design matrix");
  computeF();
  NumericalMathFunction logLikelihood(ReducedLogLikelihoodEvaluation(*this));
  // Here we change the finite difference gradient for a non centered one in order to reduce the computational cost
  logLikelihood.setGradient(NonCenteredFiniteDifferenceGradient(ResourceMap::GetAsNumericalScalar( "NonCenteredFiniteDifferenceGradient-DefaultEpsilon" ), logLikelihood.getEvaluation()).clone());
  logLikelihood.enableCache();
  return logLikelihood;
}

void GeneralizedLinearModelAlgorithm::initializeMethod()
{
  if (ResourceMap::Get("GeneralizedLinearModelAlgorithm-LinearAlgebra") == "HMAT")
    method_ = 1;
}

/* Method accessor (lapack/hmat) - Protected but friend with GeneralizedLinearModelAlgorithm class */
void GeneralizedLinearModelAlgorithm::setMethod(const UnsignedInteger method)
{
  method_ = method;
}

/* Method save() stores the object through the StorageManager */
void GeneralizedLinearModelAlgorithm::save(Advocate & adv) const
{
  MetaModelAlgorithm::save(adv);
  adv.saveAttribute( "inputSample_", inputSample_ );
  adv.saveAttribute( "inputTransformation_", inputTransformation_ );
  adv.saveAttribute( "normalize_", normalize_ );
  adv.saveAttribute( "outputSample_", outputSample_ );
  adv.saveAttribute( "covarianceModel_", covarianceModel_ );
  adv.saveAttribute( "reducedCovarianceModel_", reducedCovarianceModel_ );
  adv.saveAttribute( "solver_", solver_ );
  adv.saveAttribute( "optimizationBounds_", optimizationBounds_ );
  adv.saveAttribute( "basisCollection_", basisCollection_ );
  adv.saveAttribute( "result_", result_ );
  adv.saveAttribute( "method", method_ );
  adv.saveAttribute( "keepCholeskyFactor_", keepCholeskyFactor_ );
  adv.saveAttribute( "covarianceCholeskyFactor_", covarianceCholeskyFactor_ );
  adv.saveAttribute( "optimizeParameters_", optimizeParameters_ );
  adv.saveAttribute( "noise_", noise_ );
}


/* Method load() reloads the object from the StorageManager */
void GeneralizedLinearModelAlgorithm::load(Advocate & adv)
{
  MetaModelAlgorithm::load(adv);
  adv.loadAttribute( "inputSample_", inputSample_ );
  adv.loadAttribute( "inputTransformation_", inputTransformation_ );
  adv.loadAttribute( "normalize_", normalize_ );
  adv.loadAttribute( "outputSample_", outputSample_ );
  adv.loadAttribute( "covarianceModel_", covarianceModel_ );
  adv.loadAttribute( "reducedCovarianceModel_", reducedCovarianceModel_ );
  adv.loadAttribute( "solver_", solver_ );
  adv.loadAttribute( "optimizationBounds_", optimizationBounds_ );
  adv.loadAttribute( "basisCollection_", basisCollection_ );
  adv.loadAttribute( "result_", result_ );
  adv.loadAttribute( "method", method_ );
  adv.loadAttribute( "keepCholeskyFactor_", keepCholeskyFactor_ );
  adv.loadAttribute( "covarianceCholeskyFactor_", covarianceCholeskyFactor_ );
  adv.loadAttribute( "optimizeParameters_", optimizeParameters_ );
  adv.loadAttribute( "noise_", noise_ );
}

END_NAMESPACE_OPENTURNS
