/*
 * Miller.cpp
 *
 *  Created on: 27 Aug 2014
 *      Author: helenginn
 */

#include "Miller.h"
#include "MtzManager.h"

#include "Reflection.h"
#include "definitions.h"
#include "Vector.h"
#include <cmath>
#include "parameters.h"
#include "Shoebox.h"
#include <memory>
#include "FileParser.h"
#include "Beam.h"
#include "IOMRefiner.h"
#include "Image.h"
#include "Spot.h"
#include "FreeMillerLibrary.h"
#include "polyfit.hpp"
#include "Detector.h"

bool Miller::normalised = true;
bool Miller::correctingPolarisation = false;
double Miller::polarisationFactor = false;
int Miller::maxSlices = 100;
short int Miller::slices = 8;
float Miller::trickyRes = 8.0;
bool Miller::setupStatic = false;
PartialityModel Miller::model = PartialityModelScaled;
int Miller::peakSize = 0;
bool Miller::correctedPartiality = false;
bool Miller::absoluteIntensity = false;
double Miller::intensityThreshold = 0;
bool Miller::individualWavelength = false;
std::mutex Miller::millerMutex;

void Miller::setupStaticVariables()
{
    millerMutex.lock();
    
    if (setupStatic)
    {
        millerMutex.unlock();
        return;
    }
    
    model = FileParser::getKey("BINARY_PARTIALITY", false) ? PartialityModelBinary : PartialityModelScaled;
    
    individualWavelength = FileParser::getKey("MILLER_INDIVIDUAL_WAVELENGTHS", false);
    normalised = FileParser::getKey("NORMALISE_PARTIALITIES", true);
    correctingPolarisation = FileParser::getKey("POLARISATION_CORRECTION", false);
    polarisationFactor = FileParser::getKey("POLARISATION_FACTOR", 0.0);
    slices = FileParser::getKey("PARTIALITY_SLICES", 8);
    trickyRes = FileParser::getKey("CAREFUL_RESOLUTION", 8.0);
    maxSlices = FileParser::getKey("MAX_SLICES", 100);
    peakSize = FileParser::getKey("FOCUS_ON_PEAK_SIZE", 0);
    correctedPartiality = FileParser::getKey("CORRECTED_PARTIALITY_MODEL", false);
    absoluteIntensity = FileParser::getKey("ABSOLUTE_INTENSITY", false);
    intensityThreshold = FileParser::getKey("INTENSITY_THRESHOLD", INTENSITY_THRESHOLD);
    
    setupStatic = true;
    
    millerMutex.unlock();
}

double Miller::integrate_sphere_gaussian(double p, double q)
{
    double mean = 0.5;
    double sigma = 0.25;
    double exponent = 1.5;
    
    double partiality_add = super_gaussian(q, mean, sigma, exponent);
    double partiality_remove = super_gaussian(p, mean, sigma, exponent);
    
    double proportion = (partiality_add + partiality_remove) / 2;
    proportion *= (q - p);
    
    
    return proportion;
}

double Miller::integrate_sphere_uniform(double p, double q)
{
    double partiality_add = 4 * q * (1 - q);
    double partiality_remove = 4 * p * (1 - p);
    
    double proportion = (partiality_add + partiality_remove) / 2;
    proportion *= (q - p);
    
    return proportion;
}

double Miller::integrate_sphere(double p, double q)
{
    if (rlpModel == RlpModelGaussian)
        return integrate_sphere_gaussian(p, q);
    if (rlpModel == RlpModelUniform)
        return integrate_sphere_uniform(p, q);
    
    else return integrate_sphere_uniform(p, q);
}

double Miller::superGaussian(double bandwidth, double mean,
                             double sigma, double exponent)
{
    if (mtzParent == NULL || !mtzParent->setupGaussianTable())
    {
        return super_gaussian(bandwidth, mean, sigma, exponent);
    }
    else
    {
        if (bandwidth != bandwidth || mean != mean)
            return 0;
        
        double standardisedX = fabs((bandwidth - mean) / sigma);
        
        if (standardisedX > MAX_SUPER_GAUSSIAN)
            return 0;
        
        if (!std::isfinite(standardisedX) || standardisedX != standardisedX)
            return 0;
        
        const double step = SUPER_GAUSSIAN_STEP;
        
        int lookupInt = standardisedX / step;
        return mtzParent->superGaussianTable[lookupInt];
    }
}

double Miller::integrate_special_beam_slice(double pBandwidth, double qBandwidth)
{
    return beam->integralBetweenEwaldWavelengths(pBandwidth, qBandwidth);
}

double Miller::integrate_beam_slice(double pBandwidth, double qBandwidth, double mean,
                                    double sigma, double exponent)
{
    if (mean != mean || sigma != sigma)
        return 0;
    
    double pValue = superGaussian(pBandwidth, mean, sigma, exponent);
    double qValue = superGaussian(qBandwidth, mean, sigma, exponent);
    
    double width = fabs(pBandwidth - qBandwidth) / sigma;
    
    double area = (pValue + qValue) / 2 * width;
    
    return area;
}

double integrate_beam(double pBandwidth, double qBandwidth, double mean,
                      double sigma)
{
    double normal_add = cdf(pBandwidth, mean, sigma);
    double normal_remove = cdf(qBandwidth, mean, sigma);
    
    double normal_slice = (normal_add - normal_remove);
    
    return normal_slice;
}

double Miller::sliced_integral(double low_wavelength, double high_wavelength,
                               double spot_size_radius, double maxP, double maxQ, double mean, double sigma,
                               double exponent, bool binary, bool withBeamObject, bool fixPredicted)
{
    double mySlices = slices;
    
    if (resolution() < 1 / trickyRes)
    {
        mySlices = maxSlices;
    }
    
    double bandwidth_span = high_wavelength - low_wavelength;
    
    double fraction_total = 1;
    double currentP = 0;
    double currentQ = (double)1 / (double)mySlices;
    
    double total_integral = 0;
    
    double total_normal = 0;
    double total_sphere = 0;
    
    if (fixPredicted)
    {
        predictedWavelength = 0;
    }
    
    for (int i = 0; i < mySlices; i++)
    {
        double pBandwidth = high_wavelength - bandwidth_span * currentP;
        double qBandwidth = high_wavelength - bandwidth_span * currentQ;
        
        double normalSlice = 0;
        
        if (!withBeamObject)
        {
            normalSlice = integrate_beam_slice(pBandwidth, qBandwidth, mean,
                                               sigma, exponent);
        }
        else
        {
            normalSlice = integrate_special_beam_slice(qBandwidth, pBandwidth);
        }
        
        double sphereSlice = 0;
        
        if (normalSlice > 0 && !binary)
        {
            sphereSlice = integrate_sphere(currentP, currentQ);
        }
        
        sphereSlice = 4 * currentQ * (1 - currentQ);
        normalSlice = superGaussian(qBandwidth, mean, sigma, exponent);
        
        double totalHeight = normalSlice * sphereSlice;
        double totalSlice = totalHeight * (currentQ - currentP);
        
        if (binary && normalSlice > 0.01)
            return 1.0;
        
        if (fixPredicted)
        {
            predictedWavelength += totalSlice * (qBandwidth + pBandwidth) / 2;
        }
        
        total_integral += totalSlice;
        total_normal += normalSlice;
        total_sphere += sphereSlice;
        
        currentP = currentQ;
        currentQ += fraction_total / mySlices;
    }
    
    if (fixPredicted)
    {
        predictedWavelength /= total_integral;
    }
    
    return total_integral;
}

vec incrementedPosition(vec low_wl_pos, vec high_wl_pos, double proportion)
{
    vec diff = copy_vector(high_wl_pos);
    take_vector_away_from_vector(low_wl_pos, &diff);
    
    double currLength = length_of_vector(diff);
    currLength *= proportion;
    
    scale_vector_to_distance(&diff, currLength);
    add_vector_to_vector(&diff, low_wl_pos);
    
    return diff;
}

double Miller::slicedIntegralWithVectors(vec low_wl_pos, vec high_wl_pos, double rlpSize, double mean, double sigma, double exponent)
{
    double p = 0;
    double q = 1;
    
    double mySlices = slices;
    
    if (resolution() < 1 / trickyRes)
    {
        mySlices = maxSlices;
    }
    
    double interval = 1 / mySlices;
    
    double total_integral = 0;
    double total_normal = 0;
    double total_sphere = 0;
    
    vec oldIncrement = low_wl_pos;
    
    predictedWavelength = 0;
    
    for (double slice = p + interval; slice < q; slice += interval)
    {
        vec newIncrement = incrementedPosition(low_wl_pos, high_wl_pos, slice);
        
        double pBandwidth = getEwaldSphereNoMatrix(oldIncrement);
        double qBandwidth = getEwaldSphereNoMatrix(newIncrement);
        
        double normalSlice = integrate_beam_slice(qBandwidth, pBandwidth, mean, sigma, exponent);
        double sphereSlice = 0;
        
        if (normalSlice > 0)
        {
            sphereSlice = integrate_sphere(slice - interval, slice);
        }
        
        double totalSlice = normalSlice * sphereSlice;
        
        predictedWavelength += totalSlice * fabs(qBandwidth - pBandwidth);
        total_integral += totalSlice;
        total_normal += normalSlice;
        total_sphere += sphereSlice;
    }
    
    if (predictedWavelength != predictedWavelength)
    {
        predictedWavelength = getImage()->getWavelength();
    }
    else
    {
        predictedWavelength /= total_integral;
    }
    
    return total_integral;
}

void Miller::recalculatePredictedWavelength()
{
    double rlpSize = FileParser::getKey("INITIAL_RLP_SIZE", 0.0001);
    double bandwidth = FileParser::getKey("INITIAL_BANDWIDTH", 0.0013);
    double exponent = FileParser::getKey("INITIAL_EXPONENT", 1.5);
    double mosaicity = 0;
    double wavelength = getImage()->getWavelength();
    
    if (getMtzParent())
    {
        rlpSize = getMtzParent()->getSpotSize();
        bandwidth = getMtzParent()->getBandwidth();
        exponent = getMtzParent()->getExponent();
        mosaicity = getMtzParent()->getMosaicity();
        wavelength = getMtzParent()->getWavelength();
    }
    
    recalculatePartiality(matrix, mosaicity, rlpSize, wavelength, bandwidth, exponent, false, true);
    
}

Miller::Miller(MtzManager *parent, int _h, int _k, int _l, bool calcFree)
{
    h = _h;
    k = _k;
    l = _l;
    lastX = 0;
    lastY = 0;
    rawIntensity = 0;
    sigma = 1;
    wavelength = 0;
    partiality = -1;
    countingSigma = 0;
    polarisationCorrection = 0;
    rejectedReasons = 0;
    scale = 1;
    bFactor = 0;
    bFactorScale = 0;
    resol = 0;
    shift = std::make_pair(0, 0);
    shoebox = ShoeboxPtr();
    flipMatrix = 0;
    correctedX = 0;
    correctedY = 0;
    predictedWavelength = 0;
    recipShiftX = 0;
    recipShiftY = 0;
    
    partialCutoff = FileParser::getKey("PARTIALITY_CUTOFF",
                                       PARTIAL_CUTOFF);
    
    int rlpInt = FileParser::getKey("RLP_MODEL", 0);
    rlpModel = (RlpModel)rlpInt;
    
    if (calcFree)
    {
        free = FreeMillerLibrary::isMillerFree(this);
    }
    
    shiftedRay = new_vector(0, 0, 0);
    mtzParent = parent;
    matrix = MatrixPtr();
}

MatrixPtr Miller::getFlipMatrix()
{
    return Reflection::getFlipMatrix(flipMatrix);
}

vec Miller::hklVector(bool shouldFlip)
{
    vec newVec = new_vector(h, k, l);
    
    if (shouldFlip)
    {
        getFlipMatrix()->multiplyVector(&newVec);
    }
    
    return newVec;
}


int Miller::getH()
{
    return hklVector().h;
}

int Miller::getK()
{
    return hklVector().k;
    
}

int Miller::getL()
{
    return hklVector().l;
}

void Miller::setFlipMatrix(int i)
{
    flipMatrix = i;
}

void Miller::setParent(ReflectionPtr reflection)
{
    parentReflection = reflection;
}

void Miller::setPartialityModel(PartialityModel _model)
{
    model = _model;
}

void Miller::setData(double _intensity, double _sigma, double _partiality,
                     double _wavelength)
{
    rawIntensity = _intensity;
    sigma = _sigma;
    partiality = _partiality;
    wavelength = _wavelength;
}

bool Miller::accepted(void)
{
    if (this->model == PartialityModelNone)
        return true;
    
    if (this->partiality < partialCutoff)
    {
        return false;
    }
    
    if (this->isRejected())
        return false;
    
    if (this->rawIntensity != this->rawIntensity)
        return false;
    
    double sigma = this->getSigma();
    
    if (sigma == 0)
    {
        return false;
    }
    
    return true;
}

bool Miller::isRejected()
{
    return (rejectedReasons > 0);
}

double Miller::getBFactorScale()
{
    if (bFactor == 0)
    {
        return 1;
    }
    
    double resn = getResolution();
    
    double four_d_squared = 4 * pow(1 / resn, 2); // = 1 / (4d^2) = (sinTheta / lambda) ^ 2
    
    double factor = exp(-2 * bFactor * 1 / four_d_squared);
    
    bFactorScale = factor;
    
    return factor;
}

double Miller::scaleForScaleAndBFactor(double scaleFactor, double bFactor,
                                       double resn, double exponent_exponent)
{
    double four_d_squared = 4 * pow(resn, 2); // = 1 / (4d^2) = (sinTheta / lambda) ^ 2
    
    double right_exp = exp(-2 * bFactor * four_d_squared);
    
    double scale = scaleFactor * right_exp;
    
    return scale;
}

double Miller::intensity(bool withCutoff)
{
    if (this->accepted())
    {
        double modifier = scale;
        double bFacScale = getBFactorScale();
        
        modifier /= bFacScale;
        modifier /= correctingPolarisation ? getPolarisationCorrection() : 1;
        
        if (model != PartialityModelBinary)
        {
            modifier /= partiality;
        }
        else if (model == PartialityModelBinary)
        {
            modifier *= (partiality < partialCutoff) ? 0 : 1;
        }
        
        if (partiality == 0)
            modifier = 0;
        
        return modifier * rawIntensity;
    }
    else
    {
        std::cout << "Warning, allowing an unaccepted intensity!" << std::endl;
        return 0;
    }
}

void Miller::makeScalesPermanent()
{
    double scale = scaleForScaleAndBFactor(this->scale, this->bFactor,
                                           1 / this->resol);
    
    this->rawIntensity *= scale;
    this->sigma *= scale;
    
    this->scale = 1;
    bFactor = 0;
}


double Miller::getSigma()
{
    // bigger sigma - larger error
    
    return sigma;
}

double Miller::getPartiality()
{
    return partiality;
}

double Miller::getWavelength()
{
    return wavelength;
}

vec Miller::getRay()
{
    vec hkl = getTransformedHKL();
    
    double imageWavelength = getPredictedWavelength();
    
    double tmp = hkl.k;
    hkl.k = -hkl.h;
    hkl.h = -tmp;
    hkl.l += 1 / imageWavelength;
    
    return hkl;
}

vec Miller::getTransformedHKL(MatrixPtr myMatrix)
{
    if (!myMatrix)
        myMatrix = getMatrix();
    
    vec hkl = new_vector(h, k, l);
    
    myMatrix->multiplyVector(&hkl);
    
    return hkl;
}

vec Miller::getTransformedHKL(double hRot, double kRot)
{
    MatrixPtr newMatrix = MatrixPtr();
    rotateMatrixHKL(hRot, kRot, 0, matrix, &newMatrix);
    
    vec hkl = getTransformedHKL(newMatrix);
    return hkl;
}

double Miller::recalculateWavelength()
{
    double hRot = 0;
    double kRot = 0;
    
    if (getMtzParent() != NULL)
    {
        hRot = getMtzParent()->getHRot();
        kRot = getMtzParent()->getKRot();
    }
    
    getWavelength(hRot, kRot);
    
    return getWavelength();
}

double Miller::getWavelength(double hRot, double kRot)
{
    vec hkl = getTransformedHKL(hRot, kRot);
    
    wavelength = getEwaldSphereNoMatrix(hkl);
    
    return wavelength;
}

double Miller::getWavelength(MatrixPtr transformedMatrix)
{
    vec hkl = getTransformedHKL(transformedMatrix);
    wavelength = getEwaldSphereNoMatrix(hkl);
    
    return wavelength;
}

double Miller::getWeight(bool cutoff, WeightType weighting)
{
    if (!this->accepted() && cutoff)
    {
        return 0;
    }
    
    double weight = this->getSigma();
    double partiality = this->getPartiality();
    double scale = this->getScale();
    
    weight = partiality / scale;
    
    weight *= getBFactorScale();
    
    return weight;
}

void Miller::rotateMatrixHKL(double hRot, double kRot, double lRot, MatrixPtr oldMatrix, MatrixPtr *newMatrix)
{
    (*newMatrix) = oldMatrix->copy();
    
    double hRad = hRot * M_PI / 180;
    double kRad = kRot * M_PI / 180;
    double lRad = lRot * M_PI / 180;
    
    (*newMatrix)->rotate(hRad, kRad, lRad);
}

double Miller::expectedRadius(double spotSize, double mosaicity, vec *hkl)
{
    vec usedHKL;
    
    if (hkl == NULL)
    {
        MatrixPtr newMatrix = MatrixPtr();
        rotateMatrixHKL(0, 0, 0, matrix, &newMatrix);
        
        usedHKL = new_vector(h, k, l);
        hkl = &usedHKL;
        
        newMatrix->multiplyVector(hkl);
    }
    
    
    spotSize = fabs(spotSize);
    double radMos = fabs(mosaicity) * M_PI / 180;
    
    double distanceFromOrigin = length_of_vector(*hkl);
    
    double spotSizeIncrease = fabs(radMos * distanceFromOrigin);
    
    double radius = (spotSize + spotSizeIncrease);
    
    return radius;
}

void Miller::limitingEwaldWavelengths(vec hkl, double mosaicity, double spotSize, double wavelength, double *limitLow, double *limitHigh, vec *inwards, vec *outwards)
{
    double radius = expectedRadius(spotSize, mosaicity, &hkl);
    
    vec mean_wavelength_ewald_centre = new_vector(0, 0, -1 / wavelength);
    
    vec centred_spot_position = vector_between_vectors(
                                                       mean_wavelength_ewald_centre, hkl);
    
    double length = length_of_vector(centred_spot_position);
    
    double radiusOverLength = radius / length;
    double inwardsScalar = 1 - radiusOverLength;
    double outwardsScalar = 1 + radiusOverLength;
    double newL = hkl.l + 1 / wavelength;
    
    vec move_inwards_position = new_vector(inwardsScalar * hkl.h, inwardsScalar * hkl.k, hkl.l - radiusOverLength * newL);
    vec move_outwards_position = new_vector(outwardsScalar * hkl.h, outwardsScalar * hkl.k, hkl.l + radiusOverLength * newL);
    
    if (inwards != NULL)
    {
        *inwards = move_inwards_position;
    }
    
    if (outwards != NULL)
    {
        *outwards = move_outwards_position;
    }
    
    double inwards_bandwidth = getEwaldSphereNoMatrix(move_inwards_position);
    double outwards_bandwidth = getEwaldSphereNoMatrix(move_outwards_position);
    
    *limitHigh = inwards_bandwidth;
    *limitLow = outwards_bandwidth;
}

double Miller::calculatePartiality(double pB, double qB, double beamMean, double beamSigma, double beamExp)
{
    beamSigma *= beamMean;
    beamSigma /= 2;
    
    double correction_sigma = pow(M_PI, (2 / beamExp - 1));
    const double lnNum = 3.0;
    double limit = correction_sigma * pow(lnNum, 1 / beamExp);
    
    double limitP = limit * beamSigma;
    
    double diffLimit = limitP;
    double pqMin = std::min(pB - beamMean, qB - beamMean);
    double pqMax = std::max(pB - beamMean, qB - beamMean);
    
    
    
    if ((pqMin > 0 && pqMax > pqMin && diffLimit < pqMin) ||
        (pqMax < 0 && pqMin < pqMax && -diffLimit > pqMax))
    {
        // way too far from the diffraction condition
        return 0;
    }
    
    int sampling = 10;
    double bValue = -limitP;
    double integralBeam = 0;
    double bIncrement = limitP * 2 / (double)sampling;
    
    for (int i = 0; i < sampling; i++)
    {
        double evalE = superGaussian(bValue, 0, beamSigma, beamExp);
        integralBeam += evalE * bIncrement;
        
        bValue += bIncrement;
    }
    
    double pDiff = fabs(qB - pB);
    bValue = -limitP + beamMean;
    double squash = 1 / pDiff;
    double offset = (qB + pB) / 2;

    if (limitP > pDiff / 2)
    {
        bValue = std::min(pB, qB);
        bIncrement = fabs(pDiff) / (double)sampling;
    }
    
    double integralAll = 0;
    predictedWavelength = 0;
    
    for (int i = 0; i < sampling; i++)
    {
        double pValue = (bValue - offset) * squash;
        double evalP = std::max(0., 1 - 4 * pValue * pValue);
        double evalE = superGaussian(bValue, beamMean, beamSigma, beamExp);
        double slice = (evalE * evalP) * bIncrement;
        integralAll += slice;
       
        if (individualWavelength)
        {
            predictedWavelength += bValue * slice;
        }
        
        bValue += bIncrement;
    }
    
    predictedWavelength /= integralAll;
    
    integralAll /= integralBeam;
    
 //   logged <<  integralAll << std::endl;
 //   sendLog();
    
    
    return integralAll;
}

double Miller::sinTwoTheta(MatrixPtr rotatedMatrix)
{
    vec hkl = new_vector(h, k, l);
    rotatedMatrix->multiplyVector(&hkl);
    hkl.l += 1 / wavelength;
    vec zAxis = new_vector(0, 0, 1);
    double twoTheta = cosineBetweenVectors(zAxis, hkl);
    double sinTwoTheta = sqrt(1 - twoTheta * twoTheta);
    
    return sinTwoTheta;
}

double Miller::resolution()
{
    if (resol == 0)
    {
        vec newVec = getTransformedHKL(0, 0);
        
        resol = length_of_vector(newVec);
    }
    
    return resol;
}

bool Miller::crossesBeamRoughly(MatrixPtr rotatedMatrix, double mosaicity,
                                double spotSize, double wavelength, double bandwidth)
{
    vec hkl = new_vector(h, k, l);
    
    rotatedMatrix->multiplyVector(&hkl);
    
    double inwards_bandwidth = 0; double outwards_bandwidth = 0;
    
    limitingEwaldWavelengths(hkl, mosaicity, spotSize, wavelength, &outwards_bandwidth, &inwards_bandwidth);
    
    double limit = 2 * bandwidth;
    double inwardsLimit = wavelength + limit;
    double outwardsLimit = wavelength - limit;
    
    if (outwards_bandwidth > inwardsLimit || inwards_bandwidth < outwardsLimit)
    {
        partiality = 0;
        return false;
    }
    
    partiality = 1;
    return true;
}


void Miller::recalculatePartiality(MatrixPtr rotatedMatrix, double mosaicity,
                                   double spotSize, double wavelength, double bandwidth, double exponent, bool binary, bool no_norm)
{
    if (wavelength == 0)
    {
        wavelength = getImage()->getWavelength();
    }
    
    if (wavelength == 0)
    {
        return;
    }
    
    if (model == PartialityModelFixed)
    {
        return;
    }
    
    vec hkl = new_vector(h, k, l);
    rotatedMatrix->multiplyVector(&hkl);
    double dStar = length_of_vector(hkl);
    
    double rlpWavelength = getEwaldSphereNoMatrix(hkl);
    this->wavelength = rlpWavelength;
    
    MatrixPtr mat = MatrixPtr(new Matrix());
    double pB = 0;
    double qB = 0;
    
    limitingEwaldWavelengths(hkl, mosaicity, spotSize, wavelength, &pB, &qB);
    
    double integral = calculatePartiality(pB, qB, wavelength, bandwidth, exponent);
    partiality = integral;
    
    if (integral > 0)
    {
        double newH = 0;
        double newK = sqrt((4 * pow(dStar, 2) - pow(dStar, 4) * pow(wavelength, 2)) / 4);
        double newL = 0 - pow(dStar, 2) * wavelength / 2;
        
        double pB = 0;
        double qB = 0;
        
        vec newHKL = new_vector(newH, newK, newL);
    
        limitingEwaldWavelengths(newHKL, mosaicity, spotSize, wavelength, &pB, &qB);
        
        double norm = calculatePartiality(pB, qB, wavelength, bandwidth, exponent);
        
        partiality /= norm;
    }
}

double Miller::twoTheta(bool horizontal)
{
    double usedWavelength = wavelength;
    
    double rlpCoordinates[2];
    double beamCoordinates[2];
    
    vec hkl = new_vector(h, k, l);
    matrix->multiplyVector(&hkl);
    
    
    vec crystalCentre = new_vector(0, 0, -1 / usedWavelength);
    vec crystalToRlp = vector_between_vectors(crystalCentre, hkl);
    
    rlpCoordinates[0] = (horizontal ? crystalToRlp.h : crystalToRlp.k);
    rlpCoordinates[1] = crystalToRlp.l;
    
    beamCoordinates[0] = 0;
    beamCoordinates[1] = 1 / usedWavelength;
    
    //  double cosTwoTheta = dotProduct / (rlpMagnitude * beamMagnitude);
    
    double hOrK = (horizontal ? hkl.h : hkl.k);
    double effectiveResolution = 1 / sqrt(hOrK * hOrK + hkl.l * hkl.l);
    double sinTheta = usedWavelength / (2 * effectiveResolution);
    double theta = asin(sinTheta);
    
    return 2 * theta;
}

void Miller::setHorizontalPolarisationFactor(double newFactor)
{
    polarisationCorrection = 0;
    
    // not finished
}

double Miller::getPolarisationCorrection()
{
    if (!matrix)
    {
        return 1;
    }
    
    if (polarisationCorrection == 0)
    {
        // P = (1 + cos2 2θM cos2 2θ) / (1 + cos2 2θM)
        
        // calculate polarisation factor
        
        double horizontalFactor = polarisationFactor;
        double verticalFactor = 1 - polarisationFactor;
        
        double horizontalTwoTheta = twoTheta(true);
        double verticalTwoTheta = twoTheta(false);
        
        double cosSquaredHorizontal = pow(cos(horizontalTwoTheta), 2);
        double cosSquaredVertical = pow(cos(verticalTwoTheta), 2);
        
        double numerator1 = 1 * verticalFactor + cosSquaredHorizontal * horizontalFactor;
        double denominator1 = verticalFactor + horizontalFactor;
        
        double numerator2 = 1 * horizontalFactor + cosSquaredVertical * verticalFactor;
        double denominator2 = horizontalFactor + verticalFactor;
        
        double numerator = (numerator1 + numerator2);
        double denominator = (denominator1 + denominator2);
        
        polarisationCorrection = numerator / denominator;
    }
    
    return polarisationCorrection;
}

MillerPtr Miller::copy(void)
{
    MillerPtr newMiller = MillerPtr(new Miller(mtzParent));
    
    newMiller->h = h;
    newMiller->k = k;
    newMiller->l = l;
    newMiller->rawIntensity = rawIntensity;
    newMiller->model = model;
    newMiller->sigma = sigma;
    newMiller->partiality = partiality;
    newMiller->wavelength = wavelength;
    newMiller->matrix = matrix;
    newMiller->mtzParent = mtzParent;
    newMiller->countingSigma = countingSigma;
    newMiller->rejectedReasons = rejectedReasons;
    newMiller->lastX = lastX;
    newMiller->lastY = lastY;
    newMiller->shift = shift;
    
    return newMiller;
}

void Miller::applyScaleFactor(double scaleFactor)
{
    setScale(scale * scaleFactor);
}

void Miller::applyPolarisation(double wavelength)
{
    double components[] =
    { -0.0046462837103, 0.00749971430969, -0.00347981101231, 0.00531577207698,
        0.00576709258158, 0.00533160033255, 0.00633224815006,
        0.000661573996986, -0.00702906145495 };
    
    Matrix *mat = new Matrix(components);
    
    vec hkl = new_vector(h, k, l);
    mat->multiplyVector(&hkl);
    double resolution = length_of_vector(hkl);
    
    double sintheta = wavelength / (2 / resolution);
    double theta = asin(sintheta);
    
    double l = sin(0.5 * theta);
    
    if (l == l && l != 0 && std::isfinite(l))
        rawIntensity *= l + 1;
}

bool Miller::positiveFriedel(bool *positive, int *_isym)
{
    int h = getH();
    int k = getK();
    int l = getL();
    
    int _h, _k, _l;
    
    CSym::CCP4SPG *spg = getParentReflection()->getSpaceGroup();
    
    int isym = CSym::ccp4spg_put_in_asu(spg, h, k, l, &_h, &_k, &_l);
    
    *positive = ((isym % 2) == 1);
    
    return isym != 0;
}

double Miller::getPredictedWavelength()
{
    if (!individualWavelength)
    {
        return getImage()->getWavelength();
    }
    else
    {
        if (predictedWavelength != 0)
            return predictedWavelength;
        
        recalculatePredictedWavelength();
        return predictedWavelength;
    }
}

void Miller::positionOnDetector(double *x, double *y, bool shouldSearch)
{
    
    bool even = false;
    if (shoebox)
        even = shoebox->isEven();
    
    double xSpotPred = 0;
    double ySpotPred = 0;
    DetectorPtr detector;
    vec ray = getRay();
    
    if (hasDetector())
    {
        detector = getDetector();
        vec intersection;
        detector->intersectionWithRay(ray, &intersection);
        detector->intersectionToSpotCoord(intersection, &xSpotPred, &ySpotPred);
    }
    else
    {
        detector = Detector::getMaster()->spotCoordForMiller(shared_from_this(), &xSpotPred, &ySpotPred);
    }
    
    if (!detector)
    {
        return;
    }
    
    lastX = xSpotPred;
    lastY = ySpotPred;
    
    double xVal = xSpotPred;
    double yVal = ySpotPred;
    
    bool refocus = (shouldSearch || (correctedX == 0 && correctedY == 0));
    
    if (refocus)
    {
        int search = FileParser::getKey("METROLOGY_SEARCH_SIZE", 3);
        
        if (getIOMRefiner())
        {
            search = getIOMRefiner()->getSearchSize();
        }
        
        if (search > 0)
        {
            getImage()->focusOnAverageMax(&xVal, &yVal, search, peakSize, even);
        }
        
        correctedX = xVal;
        correctedY = yVal;
    }
    else
    {
        xVal = correctedX;
        yVal = correctedY;
    }
    
    vec cross = ray;
    
    shift = std::make_pair(xVal - lastX, yVal - lastY);
    detector->rearrangeCoord(&shift);
    
    scale_vector_to_distance(&cross, 1);
    vec horiz = new_vector(1, 0, -cross.h / cross.l);
    scale_vector_to_distance(&horiz, 1);
    vec vert = cross_product_for_vectors(cross, horiz);
    
    double matComponents[9] = {horiz.h, vert.h, cross.h,
        horiz.k, vert.k, cross.k,
        horiz.l, vert.l, cross.l};
    
    MatrixPtr changeOfBasis = MatrixPtr(new Matrix(matComponents))->transpose();
    
    vec newRay = getShiftedRay();
    scale_vector_to_distance(&newRay, 1);
    changeOfBasis->multiplyVector(&newRay);
    
    take_vector_away_from_vector(new_vector(0, 0, 1), &newRay);
    newRay.l = 0;
    recipShiftX = newRay.h;
    recipShiftY = newRay.k;
    
    if (x != NULL && y != NULL)
    {
        *x = correctedX;
        *y = correctedY;
    }
    
}

vec Miller::getShiftedRay()
{
    Detector::getMaster()->findDetectorAndSpotCoordToAbsoluteVec(correctedX, correctedY, &shiftedRay);
    
    return shiftedRay;
}

void Miller::makeComplexShoebox(double wavelength, double bandwidth, double mosaicity, double rlpSize)
{
    double radius = expectedRadius(rlpSize, mosaicity, NULL);
    
    shoebox->complexShoebox(wavelength, bandwidth, radius);
}

void Miller::integrateIntensity(MatrixPtr transformedMatrix)
{
    if (!getImage())
        throw 1;
    
    std::ostringstream logged;
    
    if (!shoebox)
    {
        shoebox = ShoeboxPtr(new Shoebox(shared_from_this()));
        
        int foregroundLength = FileParser::getKey("SHOEBOX_FOREGROUND_PADDING",
                                                  SHOEBOX_FOREGROUND_PADDING);
        int neitherLength = FileParser::getKey("SHOEBOX_NEITHER_PADDING",
                                               SHOEBOX_NEITHER_PADDING);
        int backgroundLength = FileParser::getKey("SHOEBOX_BACKGROUND_PADDING",
                                                  SHOEBOX_BACKGROUND_PADDING);
        bool shoeboxEven = FileParser::getKey("SHOEBOX_MAKE_EVEN", false);
        
        logged << "Shoebox created from values " << foregroundLength << ", " << neitherLength << ", " << backgroundLength << std::endl;
        
        shoebox->simpleShoebox(foregroundLength, neitherLength, backgroundLength, shoeboxEven);
    }
    
    double x = 0;
    double y = 0;
    
    positionOnDetector(&x, &y);
    
    if (x == -INT_MAX || y == -INT_MAX)
    {
        rawIntensity = nan(" ");
        return;
    }
    
    if (is(-60, 2, -16))
    {
        logged << "Predicted x, y = " << x << ", " << y << std::endl;
        sendLog();
    }
    
    rawIntensity = getImage()->intensityAt(x, y, shoebox, &countingSigma, 0);
}

void Miller::incrementOverlapMask(double hRot, double kRot)
{
    int x = lastX;
    int y = lastY;
    
    if (!shoebox)
        return;
    
    getImage()->incrementOverlapMask(x, y, shoebox);
}


bool Miller::isOverlappedWithSpots(std::vector<SpotPtr> *spots, bool actuallyDelete)
{
    double x = correctedX;
    double y = correctedY;
    int count = 0;
    double tolerance = 2.5;
    
    for (int i = 0; i < spots->size(); i++)
    {
        double x2 = (*spots)[i]->getRawXY().first;
        double y2 = (*spots)[i]->getRawXY().second;
        
        double xDiff = fabs(x2 - x);
        double yDiff = fabs(y2 - y);
        
        if (xDiff < tolerance && yDiff < tolerance)
        {
            if (actuallyDelete)
            {
                spots->erase(spots->begin() + i);
                i--;
            }
            count++;
        }
    }
    
    return (count > 0);
}

bool Miller::isOverlapped()
{
    int x = lastX;
    int y = lastY;
    
    unsigned char max = getImage()->maximumOverlapMask(x, y, shoebox);
    
    return (max >= 2);
}

void Miller::setRejected(RejectReason reason, bool rejection)
{
    if (rejection)
    {
        rejectedReasons = rejectedReasons | reason;
    }
    else
    {
        // removing a flag. Flip the bits so 00001000 becomes 11110111.
        // Then & the current information with the new information
        
        unsigned int flipped = ~reason;
        rejectedReasons = rejectedReasons & flipped;
    }
}

bool Miller::isRejected(RejectReason reason)
{
    return (rejectedReasons > 0);
}

double Miller::averageRawIntensity(vector<MillerPtr> millers)
{
    double allIntensities = 0;
    int num = 0;
    
    for (int i = 0; i < millers.size(); i++)
    {
        MillerPtr miller = millers[i];
        
        double anIntensity = miller->getRawIntensity();
        
        if (anIntensity != anIntensity)
            continue;
        
        allIntensities += anIntensity;
        num++;
    }
    
    allIntensities /= num;
    
    return allIntensities;
}

double Miller::observedPartiality(double reference)
{
    return rawIntensity / reference;
}

double Miller::observedPartiality(MtzManager *reference)
{
    return getParentReflection()->observedPartiality(reference, this);
}

Miller::~Miller()
{
    
}

void Miller::recalculateBetterPartiality()
{
    MatrixPtr bestMatrix = mtzParent->getLatestMatrix();
    double rlpSize = mtzParent->getSpotSize();
    double mosaicity = mtzParent->getMosaicity();
    double limitLow = 0;
    double limitHigh = 0;
    double wavelength = beam->getNominalWavelength();
    
    vec hkl = getTransformedHKL(bestMatrix);
    
    limitingEwaldWavelengths(hkl, mosaicity, rlpSize, wavelength, &limitLow, &limitHigh);
    
    if (beam->nonZeroPartialityExpected(limitLow, limitHigh))
    {
        partiality = 0;
        return;
    }
    
    partiality = sliced_integral(limitLow, limitHigh, rlpSize, 0, 1, 0, 0, 0, false, true);
    
    if (partiality != partiality)
        partiality = 0;
    
    if (partiality == 0)
        return;
    
    // normalise
    
    double d = length_of_vector(hkl);
    
    double newH = 0;
    double newK = sqrt((4 * pow(d, 2) - pow(d, 4) * pow(wavelength, 2)) / 4);
    double newL = 0 - pow(d, 2) * wavelength / 2;
    
    vec newHKL = new_vector(newH, newK, newL);
    
    limitingEwaldWavelengths(newHKL, mosaicity, rlpSize, wavelength, &limitLow, &limitHigh);
    double normPartiality = sliced_integral(limitLow, limitHigh, rlpSize, 0, 1, 0, 0, 0, false, true);
    
    partiality /= normPartiality;
    
    if ((!std::isfinite(partiality)) || (partiality != partiality))
    {
        partiality = 0;
    }
}

RejectReason Miller::getRejectedReason()
{
    if (RejectReasonMerge & rejectedReasons)
    {
        return RejectReasonMerge;
    }
    else if (RejectReasonCorrelation & rejectedReasons)
    {
        return RejectReasonCorrelation;
    }
    else if (RejectReasonPartiality & rejectedReasons)
    {
        return RejectReasonPartiality;
    }
    else
    {
        return RejectReasonNone;
    }
}

double Miller::getRawestIntensity()
{
    return rawIntensity;
}

bool Miller::reachesThreshold()
{
    double iSigI = getRawIntensity() / getCountingSigma();
    
    if (absoluteIntensity)
    {
        return (getRawIntensity() > intensityThreshold);
    }
    
    return (iSigI > intensityThreshold);
}

void Miller::refreshMillerPositions(std::vector<MillerPtr> millers)
{
    for (int i = 0; i < millers.size(); i++)
    {
        millers[i]->positionOnDetector();
    }
}

void Miller::refreshMillerPositions(std::vector<MillerWeakPtr> millers)
{
    for (int i = 0; i < millers.size(); i++)
    {
        MillerPtr miller = millers[i].lock();
        if (miller)
        {
            miller->positionOnDetector(NULL, NULL, false);
        }
    }
}

void Miller::setDetector(DetectorPtr newD)
{
    if (lastDetector.expired())
    {
        lastDetector = newD;
    }
}

bool Miller::is(int _h, int _k, int _l)
{
    long test = Reflection::indexForReflection(_h, _k, _l, Reflection::getSpaceGroup());
    long me = Reflection::indexForReflection(h, k, l, Reflection::getSpaceGroup());
    
    return (test == me);
}
