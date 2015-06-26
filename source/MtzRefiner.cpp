/*
 * MtzRefiner.cpp
 *
 *  Created on: 4 Jan 2015
 *      Author: helenginn
 */

#include "MtzRefiner.h"
#include <vector>
#include "lbfgs_cluster.h"
#include "misc.h"
#include "Vector.h"
#include <boost/thread/thread.hpp>
#include "GraphDrawer.h"
#include "Image.h"
#include <fstream>
#include "AmbiguityBreaker.h"

#include "FileParser.h"
#include "Index.h"
#include "parameters.h"
#include "FileReader.h"
#include "PanelParser.h"
#include "Panel.h"
#include "Logger.h"
#include "XManager.h"

bool MtzRefiner::hasPanelParser;
int MtzRefiner::imageLimit;
int MtzRefiner::cycleNum;



MtzRefiner::MtzRefiner()
{
    // TODO Auto-generated constructor stub
    reference = NULL;
    panelParser = NULL;
    
    hasPanelParser = false;
    int logInt = FileParser::getKey("VERBOSITY_LEVEL", 0);
    Logger::mainLogger->changePriorityLevel((LogLevel)logInt);
    
    imageLimit = FileParser::getKey("IMAGE_LIMIT", 0);
}

void print_parameters()
{
    std::cout << "N: ===== Default parameters =====" << std::endl;
    
    std::cout << "N: Initial parameters for MTZ grid search:" << std::endl;
    std::cout << "N: Bandwidth: " << INITIAL_BANDWIDTH << std::endl;
    std::cout << "N: Mosaicity: " << INITIAL_MOSAICITY << std::endl;
    std::cout << "N: Spot size: " << INITIAL_SPOT_SIZE << std::endl;
    std::cout << "N: Exponent: " << INITIAL_EXPONENT << std::endl;
    std::cout << std::endl;
    
    std::cout << "N: Parameters refined during grid search: ";
    std::cout << (OPTIMISED_ROT ? "rotations; " : "");
    std::cout << (OPTIMISED_BANDWIDTH ? "bandwidth; " : "");
    std::cout << (OPTIMISED_WAVELENGTH ? "wavelength; " : "");
    std::cout << (OPTIMISED_EXPONENT ? "exponent; " : "");
    std::cout << (OPTIMISED_MOSAICITY ? "mosaicity; " : "");
    std::cout << (OPTIMISED_SPOT_SIZE ? "spot size; " : "");
    std::cout << std::endl << std::endl;
    
    std::cout << "N: Grid search steps: ";
    std::cout << "N: Bandwidth: " << BANDWIDTH_STEP << std::endl;
    std::cout << "N: Rotation: " << ROT_STEP << std::endl;
    std::cout << "N: Spot size: " << SPOT_STEP << std::endl;
    std::cout << "N: Exponent: " << EXPONENT_STEP << std::endl;
    std::cout << "N: Wavelength: " << MEAN_STEP << std::endl;
    std::cout << std::endl;
    
    std::cout << "N: Grid search tolerances: ";
    std::cout << "N: Bandwidth: " << BANDWIDTH_TOLERANCE << std::endl;
    std::cout << "N: Rotation: " << ROT_TOLERANCE << std::endl;
    std::cout << "N: Spot size: " << SPOT_STEP << std::endl;
    std::cout << "N: Exponent: " << SPOT_SIZE_TOLERANCE << std::endl;
    std::cout << "N: Wavelength: " << MEAN_TOLERANCE << std::endl;
    std::cout << std::endl;
    
    std::cout << "N: Maximum resolution used for most grid searches: "
    << MAX_OPTIMISATION_RESOLUTION << std::endl;
    std::cout << "N: Maximum resolution used for spot size: "
    << MAX_SPOT_SIZE_OPT_RESOLUTION << std::endl;
    std::cout << "N: Further optimisation is "
    << (FURTHER_OPTIMISATION ? "" : "not ") << "used." << std::endl;
    
    std::cout << "N: Default detector distance: " << DEFAULT_DETECTOR_DISTANCE
    << std::endl;
    std::cout << "N: Default wavelength: " << DEFAULT_WAVELENGTH << std::endl;
    std::cout << "N: Intensity threshold: " << INTENSITY_THRESHOLD << std::endl;
    
    std::cout << std::endl << "N: Polarisation correction: "
    << (POLARISATION_CORRECTION ? "on" : "off") << std::endl;
    std::cout << "N: Polarisation factor (horizontal): "
    << HORIZONTAL_POLARISATION_FACTOR << std::endl;
    std::cout << std::endl << "N: Minimum miller count for rejection: "
    << MIN_MILLER_COUNT << std::endl;
    std::cout << "N: Rejecting miller indices: "
    << (REJECTING_MILLERS ? "on" : "off") << std::endl;
}

// MARK: Refinement

void MtzRefiner::cycleThreadWrapper(MtzRefiner *object, int offset)
{
    object->cycleThread(offset);
}

void MtzRefiner::cycleThread(int offset)
{
    int img_num = (int)mtzManagers.size();
    int j = 0;
    
    bool initialGridSearch = FileParser::getKey("INITIAL_GRID_SEARCH", false);
    
    int maxThreads = FileParser::getMaxThreads();
    
    for (int i = offset; i < img_num; i += maxThreads)
    {
        j++;
        
        std::ostringstream logged;
        
        MtzPtr image = mtzManagers[i];
        
        if (!image->isRejected())
        {
            logged << "Refining image " << i << " " << image->getFilename() << std::endl;
            Logger::mainLogger->addStream(&logged);
            if (initialGridSearch && cycleNum == 0)
                image->findSteps();
            
            for (int i = 0; i < 5; i++)
                image->gridSearch();
            
            if (image->getScoreType() == ScoreTypeStandardDeviation)
            {
                image->setDefaultScoreType(DEFAULT_SCORE_TYPE);
      //          image->gridSearch(true);
            }
            
            image->writeToDat();
        }
    }
}

void MtzRefiner::cycle()
{
    MtzManager::setReference(reference);
    
    time_t startcputime;
    time(&startcputime);
    
    boost::thread_group threads;
    
    int maxThreads = FileParser::getMaxThreads();
    
    for (int i = 0; i < maxThreads; i++)
    {
        boost::thread *thr = new boost::thread(cycleThreadWrapper, this, i);
        threads.add_thread(thr);
    }
    
    threads.join_all();
    
    time_t endcputime;
    time(&endcputime);
    
    clock_t difference = endcputime - startcputime;
    double seconds = difference;
    
    int finalSeconds = (int) seconds % 60;
    int minutes = seconds / 60;
    
    std::cout << "N: Refinement cycle: " << minutes << " minutes, "
    << finalSeconds << " seconds." << std::endl;
}

void MtzRefiner::initialMerge()
{
    MtzManager *originalMerge = NULL;
    /*
    Lbfgs_Cluster *lbfgs = new Lbfgs_Cluster();
    lbfgs->initialise_cluster_lbfgs(mtzManagers, &originalMerge);
    reference = originalMerge;
    delete lbfgs;
    
    reference->writeToFile("initialMerge.mtz");
    */
     AmbiguityBreaker breaker = AmbiguityBreaker(mtzManagers);
     breaker.run();
     originalMerge = breaker.getMergedMtz();
     reference = originalMerge;
    
    reference->writeToFile("initialMerge.mtz");
}

void MtzRefiner::refine()
{
    MtzManager *originalMerge = NULL;
    
    readMatricesAndMtzs();
    
    if (!loadInitialMtz())
    {
        initialMerge();
    }
    
    bool denormalise = FileParser::getKey("DENORMALISE_PARTIALITY", false);
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        if (denormalise)
            mtzManagers[i]->denormaliseMillers();
    }
    
    originalMerge = reference;
    
    std::cout << "N: Total images loaded: " << mtzManagers.size() << std::endl;
    
    originalMerge->writeToFile("originalMerge.mtz");
    
    MtzManager::currentManager = originalMerge;
    MtzManager::setReference(reference);
    double correl = originalMerge->correlation(true);
    std::cout << "Merged correlation = " << correl << std::endl;
    
    MtzManager::setLowRes(0);
    MtzManager::setHighRes(0);
    
    refineCycle();
}

void MtzRefiner::refineCycle(bool once)
{
    int i = 0;
    bool finished = false;
    
    int minimumCycles = FileParser::getKey("MINIMUM_CYCLES", 6);
    int maximumCycles = FileParser::getKey("MAXIMUM_CYCLES", 50);
    
    bool stop = FileParser::getKey("STOP_REFINEMENT", true);
    double correlationThreshold = FileParser::getKey("CORRELATION_THRESHOLD",
                                                     CORRELATION_THRESHOLD);
    double initialCorrelationThreshold = FileParser::getKey(
                                                            "INITIAL_CORRELATION_THRESHOLD", INITIAL_CORRELATION_THRESHOLD);
    int thresholdSwap = FileParser::getKey("THRESHOLD_SWAP",
                                           THRESHOLD_SWAP);
    bool exclusion = FileParser::getKey("EXCLUDE_OWN_REFLECTIONS", false);
    
    double resolution = FileParser::getKey("MAX_RESOLUTION_ALL",
                                           MAX_OPTIMISATION_RESOLUTION);
    int scalingInt = FileParser::getKey("SCALING_STRATEGY",
                                        (int) SCALING_STRATEGY);
    ScalingType scaling = (ScalingType) scalingInt;
    
    while (!finished)
    {
        cycleNum = i;
        cycle();
        
        std::cout << "Grouping final MTZs" << std::endl;
        MtzGrouper *grouper = new MtzGrouper();
        MtzManager::setReference(reference);
        if (i >= 0)
            grouper->setScalingType(scaling);
        if (once)
            grouper->setScalingType(ScalingTypeAverage);
        grouper->setWeighting(WeightTypePartialitySigma);
        grouper->setExpectedResolution(resolution);
        grouper->setMtzManagers(mtzManagers);
        grouper->setExcludeWorst(true);
        if (i < thresholdSwap)
            grouper->setCorrelationThreshold(initialCorrelationThreshold);
        else
            grouper->setCorrelationThreshold(correlationThreshold);
        
        MtzManager *mergedMtz = NULL;
        MtzManager *unmergedMtz = NULL;
        grouper->merge(&mergedMtz, &unmergedMtz, i);
        
        MtzManager::currentManager = mergedMtz;
        MtzManager::setReference(reference);
        
        std::cout << "Reflections: " << mergedMtz->reflectionCount() << std::endl;
        if (!once)
        {
            double correl = mergedMtz->correlation(true);
            std::cout << "N: Merged correlation = " << correl << std::endl;
        }
        mergedMtz->description();
        
        double scale = 1000 / mergedMtz->averageIntensity();
        mergedMtz->applyScaleFactor(scale);
        
        std::cout << "Here" << std::endl;
        
        std::string filename = "allMerge" + i_to_str(i) + ".mtz";
        mergedMtz->writeToFile(filename.c_str(), true);
        
        MtzManager *reloadMerged = new MtzManager();
        
        reloadMerged->setFilename(filename.c_str());
        reloadMerged->loadReflections(1);
        reloadMerged->description();
        
        MtzManager::currentManager = reloadMerged;
        double reloaded = reloadMerged->correlation(true);
        std::cout << "Reloaded correlation = " << reloaded << std::endl;
        
        if (reloaded > 0.999 && i >= minimumCycles && stop)
            finished = true;
        
        if (once)
            finished = true;
        
        if (i == maximumCycles - 1 && stop)
            finished = true;
        
        delete grouper;
        if (!once)
            delete reference;
        reference = exclusion ? unmergedMtz : mergedMtz;
        i++;
    }
}


// MARK: Symmetry-related reflection refinement

void MtzRefiner::refineSymmetry()
{
    readMatricesAndMtzs();
    
    std::cout << "N: Total images loaded: " << mtzManagers.size() << std::endl;
    
    std::cout << "Refining images using symmetry: " << std::endl;
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        int symmCount = mtzManagers[i]->symmetryRelatedReflectionCount();
        int paramCount = mtzManagers[i]->refinedParameterCount();
        
        if (symmCount < paramCount * 8 && paramCount > 30)
        {
            mtzManagers[i]->setRejected(true);
            continue;
        }
        
        std::cout << mtzManagers[i]->getFilename() << "\t" << symmCount << "\t" << paramCount << std::endl;
        
        mtzManagers[i]->setDefaultScoreType(ScoreTypeSymmetry);
    }
    
    refineCycle(true);
    
    MtzManager::setReference(reference);
    
    int defaultScoreInt = FileParser::getKey("DEFAULT_TARGET_FUNCTION",
                                             (int) DEFAULT_SCORE_TYPE);
    ScoreType desiredScoreType = (ScoreType) defaultScoreInt;
    
    double spotSum = 0;
    double mosaicitySum = 0;
    int count = 0;
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        double spotSize = fabs(mtzManagers[i]->getSpotSize());
        double mosaicity = fabs(mtzManagers[i]->getMosaicity());
        
        if (mtzManagers[i]->isRejected() == false)
        {
            spotSum += spotSize;
            mosaicitySum += mosaicity;
            count++;
        }
    }
    
    spotSum /= count;
    mosaicitySum /= count;
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        if (mtzManagers[i]->isRejected())
        {
            mtzManagers[i]->setSpotSize(spotSum);
            mtzManagers[i]->setMosaicity(mosaicitySum);
            mtzManagers[i]->setRejected(false);
        }
        
        mtzManagers[i]->setOptimisingOrientation(true);
        mtzManagers[i]->setOptimisingWavelength(true);
        
        mtzManagers[i]->setDefaultScoreType(desiredScoreType);
    }
}

// MARK: Loading data

bool MtzRefiner::loadInitialMtz(bool force)
{
    bool hasInitialMtz = FileParser::hasKey("INITIAL_MTZ");
    
    if (reference != NULL && !force)
        return true;
    
    std::ostringstream logged;
    
    logged << "Initial orientation matrix has "
    << (hasInitialMtz ? "" : "not ") << "been provided." << std::endl;
    
    Logger::mainLogger->addStream(&logged);
    
    if (hasInitialMtz)
    {
        
        std::string referenceFile = FileParser::getKey("INITIAL_MTZ",
                                                       std::string(""));
        
        reference = new MtzManager();
        
        reference->setFilename(referenceFile.c_str());
        reference->loadReflections(1);
        reference->setSigmaToUnity();
    }
    
    MtzManager::setReference(reference);
    
    return hasInitialMtz;
}


int MtzRefiner::imageMax(size_t lineCount)
{
    int end = (int)lineCount;
    
    if (imageLimit != 0)
        end = imageLimit < lineCount ? imageLimit : (int)lineCount;
    
    return end;
}

void MtzRefiner::applyParametersToImages()
{
    vector<double> unitCell = FileParser::getKey("UNIT_CELL", vector<double>());
    
    if (unitCell.size() == 0)
    {
        std::cout
        << "Please provide unit cell dimensions under keyword UNIT_CELL"
        << std::endl;
        exit(1);
    }
    
    int spg_num = FileParser::getKey("SPACE_GROUP", -1);
    
    if (spg_num == -1)
    {
        std::cout << "Please set space group number under keyword SPACE_GROUP"
        << std::endl;
    }
    
    double overPredSpotSize = FileParser::getKey("OVER_PRED_RLP_SIZE",
                                                 OVER_PRED_SPOT_SIZE);
    double overPredBandwidth = FileParser::getKey("OVER_PRED_BANDWIDTH",
                                                  OVER_PRED_BANDWIDTH);
    overPredBandwidth /= 2;
    
    // @TODO add orientation tolerance as flexible
    double orientationTolerance = FileParser::getKey(
                                                     "INDEXING_ORIENTATION_TOLERANCE", INDEXING_ORIENTATION_TOLERANCE);
    
    double orientationStep = FileParser::getKey("INITIAL_ORIENTATION_STEP", INITIAL_ORIENTATION_STEP);
    
    // @TODO intensityThreshold should be flexible
    double intensityThreshold = FileParser::getKey("INTENSITY_THRESHOLD",
                                                   INTENSITY_THRESHOLD);
    
    double metrologySearchSize = FileParser::getKey("METROLOGY_SEARCH_SIZE",
                                                    METROLOGY_SEARCH_SIZE);
    
    double maxIntegratedResolution = FileParser::getKey(
                                                        "MAX_INTEGRATED_RESOLUTION", MAX_INTEGRATED_RESOLUTION);
    
    bool fixUnitCell = FileParser::getKey("FIX_UNIT_CELL", true);
    
    for (int i = 0; i < images.size(); i++)
    {
        Image *newImage = images[i];
        
        newImage->setPinPoint(true);
        
        CCP4SPG *spg = ccp4spg_load_by_standard_num(spg_num);
        
        newImage->setSpaceGroup(spg);
        newImage->setMaxResolution(maxIntegratedResolution);
        newImage->setSearchSize(metrologySearchSize);
        newImage->setIntensityThreshold(intensityThreshold);
        if (fixUnitCell)
            newImage->setUnitCell(unitCell);
        
        if (fixUnitCell)
            for (int j = 0; j < newImage->indexerCount(); j++)
            {
                newImage->getIndexer(j)->getMatrix()->changeOrientationMatrixDimensions(unitCell[0], unitCell[1], unitCell[2], unitCell[3], unitCell[4], unitCell[5]);
            }
        
        newImage->setInitialStep(orientationStep);
      
        newImage->setTestSpotSize(overPredSpotSize);
        newImage->setTestBandwidth(overPredBandwidth);
    }
}

void MtzRefiner::readSingleImageV2(std::string *filename, vector<Image *> *newImages, vector<MtzPtr> *newMtzs, int offset)
{
    double wavelength = FileParser::getKey("INTEGRATION_WAVELENGTH", 0.0);
    double detectorDistance = FileParser::getKey("DETECTOR_DISTANCE", 0.0);
    double tolerance = FileParser::getKey("ACCEPTABLE_UNIT_CELL_TOLERANCE", 0.0);
    vector<double> givenUnitCell = FileParser::getKey("UNIT_CELL", vector<double>());
    
    bool checkingUnitCell = false;
    
    if (givenUnitCell.size() > 0 && tolerance > 0.0)
        checkingUnitCell = true;

    if (wavelength == 0 && newImages != NULL)
    {
        std::cout
        << "Please provide initial wavelength for integration under keyword INTEGRATION_WAVELENGTH"
        << std::endl;
        exit(1);
    }
    
    if (detectorDistance == 0 && newImages != NULL)
    {
        std::cout
        << "Please provide detector distance for integration under keyword DETECTOR_DISTANCE"
        << std::endl;
        exit(1);
    }
    
    const std::string contents = FileReader::get_file_contents(
                                                               filename->c_str());
    
    vector<std::string> imageList = FileReader::split(contents, "\nimage ");
    
    int maxThreads = FileParser::getMaxThreads();
    int end = imageMax(imageList.size());
    
    for (int i = offset; i < end; i += maxThreads)
    {
        if (imageList[i].length() == 0)
            continue;
        
        vector<std::string> lines = FileReader::split(imageList[i], '\n');
        
        vector<std::string> components = FileReader::split(lines[0], ' ');
        
        if (components.size() <= 1)
            continue;
        
        std::string imgName = components[1];
        
        if (newImages)
            imgName += ".img";
        else if (newMtzs)
            imgName += ".mtz";
        
        std::ostringstream logged;
        
        if (!FileReader::exists(imgName))
        {
            logged << "Skipping image " << imgName << std::endl;
            Logger::mainLogger->addStream(&logged);
            continue;
        }
        
        logged << "Loading image " << i << " (" << imgName << ")"
        << std::endl;
        Logger::mainLogger->addStream(&logged);

        double usedWavelength = wavelength;
        double usedDistance = detectorDistance;
        
        if (components.size() >= 3)
        {
            usedWavelength = atof(components[1].c_str());
            usedDistance = atof(components[2].c_str());
        }
        
        MatrixPtr unitCell;
        MatrixPtr newMatrix;
        
        Image *newImage = new Image(imgName, wavelength,
                                    detectorDistance);
        
        for (int i = 1; i < lines.size(); i++)
        {
            vector<std::string> components = FileReader::split(lines[i], ' ');
            
            if (components[0] == "matrix")
            {
                // individual matrices
                double matrix[9];
                readMatrix(matrix, lines[i]);
                
                newMatrix = MatrixPtr(new Matrix(matrix));
                
                newMatrix->rotate(0, 0, M_PI / 2);
                continue;
            }
            
            if (components[0] == "unitcell")
            {
                // individual matrices
                double matrix[9];
                readMatrix(matrix, lines[i]);
                
                unitCell = MatrixPtr(new Matrix(matrix));
            }
            
            if (components[0] == "rotation")
            {
                // individual matrices
                double matrix[9];
                readMatrix(matrix, lines[i]);
                
                MatrixPtr rotation = MatrixPtr(new Matrix(matrix));
                
                if (unitCell)
                {
                    double rightRot = 0.5 * M_PI / 180;
                    double upRot = 0.1 * M_PI / 180;
                    if (newImages)
                        rotation->rotate(0, 0, M_PI / 2);
                    rotation->rotate(rightRot, upRot, 0);
                    newMatrix = MatrixPtr(new Matrix);
                    newMatrix->setComplexMatrix(unitCell, rotation);
                    
                    unitCell = MatrixPtr();
                }
                else
                {
                    std::cout << "Warning, unmatched unitcell / rotation pair?" << std::endl;
                }
            }
        }
        if (newImages)
        {
            newImage->setUpIndexer(newMatrix);
            newImages->push_back(newImage);
        }
        
        if (newMtzs)
        {
            MtzPtr newManager = MtzPtr(new MtzManager());
            
            newManager->setFilename(imgName.c_str());
            newManager->setMatrix(newMatrix);
            newManager->loadReflections(1);
            newManager->setSigmaToUnity();
            newManager->loadParametersMap();
            
            if (newManager->reflectionCount() > 0)
            {
                if (checkingUnitCell && newManager->checkUnitCell(givenUnitCell[0], givenUnitCell[1], givenUnitCell[2], tolerance))
                {
                    newMtzs->push_back(newManager);
                }
                else if (!checkingUnitCell)
                {
                    newMtzs->push_back(newManager);
                }
                else
                {
                    logged << "Skipping file " << filename << " due to poor unit cell" << std::endl;
                }
            }
        }
    }
    
}

void MtzRefiner::readMatricesAndImages(std::string *filename, bool areImages)
{
    if (images.size() > 0)
        return;
    
    if (filename == NULL)
    {
        std::string aFilename = FileParser::getKey("ORIENTATION_MATRIX_LIST", std::string(""));
        filename = &aFilename;
        
        if (filename->length() == 0)
        {
            std::cout << "No orientation matrix list provided. Exiting now." << std::endl;
            exit(1);
        }
    }

    double version = FileParser::getKey("MATRIX_LIST_VERSION", 1.0);
    
    // thought: turn the vector concatenation into a templated function
    
    boost::thread_group threads;
    
    int maxThreads = FileParser::getMaxThreads();
    
    vector<vector<Image *> > imageSubsets;
    vector<vector<MtzPtr> > mtzSubsets;
    imageSubsets.resize(maxThreads);
    mtzSubsets.resize(maxThreads);
    
    for (int i = 0; i < maxThreads; i++)
    {
        if (version == 1.0)
        {
            boost::thread *thr = new boost::thread(singleLoadImages, filename,
                                               &imageSubsets[i], i);
            threads.add_thread(thr);
        }
        else if (version == 2.0)
        {
            vector<MtzPtr> *chosenMtzs = areImages ? NULL : &mtzSubsets[i];
            vector<Image *> *chosenImages = areImages ? &imageSubsets[i] : NULL;
            boost::thread *thr = new boost::thread(readSingleImageV2, filename,
                                                   chosenImages, chosenMtzs, i);
            threads.add_thread(thr);
        }
    }
    
    threads.join_all();
    
    
    int total = 0;
   
    for (int i = 0; i < maxThreads; i++)
    {
        if (areImages)
            total += imageSubsets[i].size();
        
        if (!areImages)
            total += mtzSubsets[i].size();
    }
    
    mtzManagers.reserve(total);
    images.reserve(total);
    int lastPos = 0;
    
    for (int i = 0; i < maxThreads; i++)
    {
        if (areImages)
        {
            images.insert(images.begin() + lastPos,
                          imageSubsets[i].begin(), imageSubsets[i].end());
            lastPos += imageSubsets[i].size();
        }
        else
        {
            mtzManagers.insert(mtzManagers.begin() + lastPos,
                               mtzSubsets[i].begin(), mtzSubsets[i].end());
            lastPos += mtzSubsets[i].size();
        }
    }
    
    if (version == 2.0 && areImages)
        applyParametersToImages();
}

void MtzRefiner::singleLoadImages(std::string *filename, vector<Image *> *newImages, int offset)
{
    const std::string contents = FileReader::get_file_contents(
                                                               filename->c_str());
    
    vector<std::string> lines = FileReader::split(contents, '\n');
    
    int spg_num = FileParser::getKey("SPACE_GROUP", -1);
    
    if (spg_num == -1)
    {
        std::cout << "Please set space group number under keyword SPACE_GROUP"
        << std::endl;
    }
    
    double overPredSpotSize = FileParser::getKey("OVER_PRED_RLP_SIZE",
                                                 OVER_PRED_SPOT_SIZE);
    double overPredBandwidth = FileParser::getKey("OVER_PRED_BANDWIDTH",
                                                  OVER_PRED_BANDWIDTH);
    overPredBandwidth /= 2;
    
    // @TODO add orientation tolerance as flexible
    double orientationTolerance = FileParser::getKey(
                                                     "INDEXING_ORIENTATION_TOLERANCE", INDEXING_ORIENTATION_TOLERANCE);
    
    double orientationStep = FileParser::getKey("INITIAL_ORIENTATION_STEP", INITIAL_ORIENTATION_STEP);
    
    // @TODO intensityThreshold should be flexible
    double intensityThreshold = FileParser::getKey("INTENSITY_THRESHOLD",
                                                   INTENSITY_THRESHOLD);
    
    double metrologySearchSize = FileParser::getKey("METROLOGY_SEARCH_SIZE",
                                                    METROLOGY_SEARCH_SIZE);
    
    double maxIntegratedResolution = FileParser::getKey(
                                                        "MAX_INTEGRATED_RESOLUTION", MAX_INTEGRATED_RESOLUTION);
    
    double wavelength = FileParser::getKey("INTEGRATION_WAVELENGTH", 0.0);
    double detectorDistance = FileParser::getKey("DETECTOR_DISTANCE", 0.0);
    
    bool fixUnitCell = FileParser::getKey("FIX_UNIT_CELL", true);
    
    if (wavelength == 0)
    {
        std::cout
        << "Please provide initial wavelength for integration under keyword INTEGRATION_WAVELENGTH"
        << std::endl;
        exit(1);
    }
    
    if (detectorDistance == 0)
    {
        std::cout
        << "Please provide detector distance for integration under keyword DETECTOR_DISTANCE"
        << std::endl;
        exit(1);
    }
    
    vector<double> unitCell = FileParser::getKey("UNIT_CELL", vector<double>());
    
    if (unitCell.size() == 0)
    {
        std::cout
        << "Please provide unit cell dimensions under keyword UNIT_CELL"
        << std::endl;
        exit(1);
    }
    
    int maxThreads = FileParser::getMaxThreads();
    
    int end = imageMax(lines.size());
    
    int skip = FileParser::getKey("IMAGE_SKIP", 0);
    
    if (skip > lines.size())
    {
        std::cout << "Image skip beyond image count" << std::endl;
        exit(1);
    }
    
    if (skip > 0)
    {
        std::cout << "Skipping " << skip << " lines" << std::endl;
    }
    
    for (int i = offset + skip; i < end + skip; i += maxThreads)
    {
        vector<std::string> components = FileReader::split(lines[i], ' ');
        
        if (components.size() == 0)
            continue;
        
        std::string imgName = components[0] + ".img";
        if (!FileReader::exists(imgName))
        {
            continue;
        }
        
        std::cout << "Loading image " << i << " (" << imgName << ")"
        << std::endl;
        
        if (components.size() > 11)
        {
            wavelength = atof(components[10].c_str());
            detectorDistance = atof(components[11].c_str());
        }
        
        
        Image *newImage = new Image(imgName, wavelength,
                                    detectorDistance);
        
        MatrixPtr newMat = MatrixPtr();
        
        if (components.size() >= 10)
        {
            double matrix[9];
            readMatrix(matrix, lines[i]);
            newMat = MatrixPtr(new Matrix(matrix));
            if (fixUnitCell)
                newMat->changeOrientationMatrixDimensions(unitCell[0], unitCell[1], unitCell[2], unitCell[3], unitCell[4], unitCell[5]);
        }
        else
        {
            newMat = Matrix::matrixFromUnitCell(unitCell[0], unitCell[1], unitCell[2], unitCell[3], unitCell[4], unitCell[5]);
        }
        
        newImage->setUpIndexer(newMat);
        newImage->setPinPoint(true);
        
        CCP4SPG *spg = ccp4spg_load_by_standard_num(spg_num);
        
        newImage->setSpaceGroup(spg);
        newImage->setMaxResolution(maxIntegratedResolution);
        newImage->setSearchSize(metrologySearchSize);
        newImage->setIntensityThreshold(intensityThreshold);
        newImage->setUnitCell(unitCell);
        newImage->setInitialStep(orientationStep);
        
        // @TODO: masks should be addable through input file
        /*
         newImage->addMask(0, 840, 1765, 920);
         newImage->addMask(446, 1046, 1324, 1765);
         newImage->addMask(820, 450, 839, 473);
         */
        
        newImage->setTestSpotSize(overPredSpotSize);
        newImage->setTestBandwidth(overPredBandwidth);
        
        newImages->push_back(newImage);
    }
}

void MtzRefiner::readMatricesAndMtzs()
{
    std::string filename = FileParser::getKey("ORIENTATION_MATRIX_LIST",
                                              std::string(""));
    
    double version = FileParser::getKey("MATRIX_LIST_VERSION", 1.0);
    
    if (version == 2.0)
    {
        readMatricesAndImages(NULL, false);
        
        return;
    }
    
    
    if (filename == "")
    {
        std::cout
        << "Orientation matrix list has not been provided for refinement. Please provide under keyword ORIENTATION_MATRIX_LIST."
        << std::endl;
        exit(1);
    }
    
    std::ostringstream logged;
    
    if (mtzManagers.size() > 0)
    {
        logged << "Mtzs already present; not reloading from list" << std::endl;
        Logger::mainLogger->addStream(&logged);
        return;
    }
    
    logged << "Loading MTZs from list" << std::endl;
    
    const std::string contents = FileReader::get_file_contents(
                                                               filename.c_str());
    
    vector<std::string> lines = FileReader::split(contents, '\n');
    
    int maxThreads = FileParser::getMaxThreads();
    
    boost::thread_group threads;
    
    vector<vector<MtzPtr> > managerSubsets;
    managerSubsets.resize(maxThreads);
    
    for (int i = 0; i < maxThreads; i++)
    {
        boost::thread *thr = new boost::thread(singleThreadRead, lines,
                                               &managerSubsets[i], i);
        threads.add_thread(thr);
    }
    
    threads.join_all();
    
    int total = 0;
    
    for (int i = 0; i < maxThreads; i++)
    {
        total += managerSubsets[i].size();
    }
    
    mtzManagers.reserve(total);
    int lastPos = 0;
    
    for (int i = 0; i < maxThreads; i++)
    {
        mtzManagers.insert(mtzManagers.begin() + lastPos,
                           managerSubsets[i].begin(), managerSubsets[i].end());
        lastPos += managerSubsets[i].size();
    }
    
    logged << "Mtz count: " << mtzManagers.size() << std::endl;
    Logger::mainLogger->addStream(&logged);
}

void MtzRefiner::singleThreadRead(vector<std::string> lines,
                                  vector<MtzPtr> *mtzManagers, int offset)
{
    int maxThreads = FileParser::getMaxThreads();
    
    int end = imageMax(lines.size());
    
    int skip = FileParser::getKey("IMAGE_SKIP", 0);
    vector<double> unitCell = FileParser::getKey("UNIT_CELL", vector<double>());
    double tolerance = FileParser::getKey("ACCEPTABLE_UNIT_CELL_TOLERANCE", 0.0);
    
    bool checkingUnitCell = false;
    
    if (unitCell.size() > 0 && tolerance > 0.0)
        checkingUnitCell = true;
    
    if (skip > lines.size())
    {
        std::cout << "Mtz skip beyond Mtz count" << std::endl;
        exit(1);
    }
    
    if (skip > 0)
    {
        std::cout << "Skipping " << skip << " lines" << std::endl;
    }
    
    for (int i = skip + offset; i < end + skip; i += maxThreads)
    {
        std::ostringstream log;
        
        vector<std::string> components = FileReader::split(lines[i], ' ');
        
        if (components.size() < 10)
            continue;
        
        std::string mtzName = components[0] + ".mtz";
        if (!FileReader::exists(mtzName))
        {
            log << "Skipping file " << mtzName << std::endl;
            continue;
        }
        
        log << "Loading file " << mtzName << std::endl;
        
        double matrix[9];
        
        readMatrix(matrix, lines[i]);
        
        MtzPtr newManager = MtzPtr(new MtzManager());
        
        newManager->setFilename(mtzName.c_str());
        newManager->setMatrix(matrix);
        newManager->loadReflections(1);
        newManager->setSigmaToUnity();
        newManager->loadParametersMap();
        
        if (newManager->reflectionCount() > 0)
        {
            if (checkingUnitCell && newManager->checkUnitCell(unitCell[0], unitCell[1], unitCell[2], tolerance))
            {
                mtzManagers->push_back(newManager);
            }
            else if (!checkingUnitCell)
            {
                mtzManagers->push_back(newManager);
            }
            else
            {
                log << "Skipping file " << mtzName << " due to poor unit cell" << std::endl;
            }
        }
        
        Logger::mainLogger->addStream(&log);
    }
}

void MtzRefiner::readMatrix(double (&matrix)[9], std::string line)
{
    vector<std::string> components = FileReader::split(line, ' ');
    
    for (int j = 1; j <= 9; j++)
    {
        std::string component = components[j];
        
        /* Locate the substring to replace. */
        int index = (int)component.find(std::string("*^"), 0);
        if (index != std::string::npos)
        {
            component.replace(index, 2, std::string("e"));
        }
        
        double matVar = atof(component.c_str());
        matrix[j - 1] = matVar;
    }
}

// MARK: Merging

void MtzRefiner::correlationAndInverse(bool shouldFlip)
{
    if (MtzManager::getReferenceManager() == NULL)
    {
        MtzManager::setReference(reference);
    }
    
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        double correl = mtzManagers[i]->correlation(true);
        double invCorrel = correl;
        
        if (MtzManager::getReferenceManager()->ambiguityCount() > 1)
        {
            mtzManagers[i]->setActiveAmbiguity(1);
            invCorrel = mtzManagers[i]->correlation(true);
            mtzManagers[i]->setActiveAmbiguity(0);
            
            if (invCorrel > correl && shouldFlip)
                mtzManagers[i]->setActiveAmbiguity(1);
        }
        double newCorrel = mtzManagers[i]->correlation(true);
        mtzManagers[i]->setRefCorrelation(newCorrel);
        
        std::cout << mtzManagers[i]->getFilename() << "\t" << correl << "\t"
        << invCorrel << std::endl;
    }
}

void MtzRefiner::merge()
{
    loadInitialMtz();
    MtzManager::setReference(this->reference);
    readMatricesAndMtzs();
    
    bool partialityRejection = FileParser::getKey("PARTIALITY_REJECTION", false);
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        mtzManagers[i]->excludeFromLogCorrelation();
        if (partialityRejection)
            mtzManagers[i]->excludePartialityOutliers();
    }
    
    bool denormalise = FileParser::getKey("DENORMALISE_PARTIALITY", false);
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        if (denormalise)
            mtzManagers[i]->denormaliseMillers();
    }

    correlationAndInverse(true);
    
    double correlationThreshold = FileParser::getKey("CORRELATION_THRESHOLD",
                                                     CORRELATION_THRESHOLD);
    
    vector<MtzPtr> idxOnly;
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        if ((mtzManagers[i]->getActiveAmbiguity() == 0))
            idxOnly.push_back(mtzManagers[i]);
    }
   
    
    bool mergeAnomalous = FileParser::getKey("MERGE_ANOMALOUS", false);
    int scalingInt = FileParser::getKey("SCALING_STRATEGY",
                                        (int) SCALING_STRATEGY);
    ScalingType scaling = (ScalingType) scalingInt;
    
    
    MtzGrouper *grouper = new MtzGrouper();
    grouper->setScalingType(scaling);
    grouper->setWeighting(WeightTypePartialitySigma);
    grouper->setMtzManagers(mtzManagers);
    
    grouper->setCutResolution(false);
    
    grouper->setCorrelationThreshold(correlationThreshold);
    
    MtzManager *mergedMtz = NULL;
    MtzManager *unmergedMtz = NULL;
    grouper->merge(&mergedMtz, NULL, -1, mergeAnomalous);
    mergedMtz->writeToFile("remerged.mtz");
    
    delete unmergedMtz;
    delete mergedMtz;
    delete grouper;
}

// MARK: Integrating images

void MtzRefiner::integrateImagesWrapper(MtzRefiner *object,
                                        vector<MtzPtr> *&mtzSubset, int offset, bool orientation)
{
    object->integrateImages(mtzSubset, offset, orientation);
}

void MtzRefiner::integrateImages(vector<MtzPtr> *&mtzSubset,
                                 int offset, bool orientation)
{
    int maxThreads = FileParser::getMaxThreads();
    
    bool refineDistances = FileParser::getKey("REFINE_DISTANCES", false);
    
    for (int i = offset; i < images.size(); i += maxThreads)
    {
        std::ostringstream logged;
        logged << "Integrating image " << i << std::endl;
        Logger::mainLogger->addStream(&logged);
        
        if (refineDistances)
            images[i]->refineDistances();

        if (orientation)
            images[i]->refineOrientations();
        
        vector<MtzPtr> mtzs = images[i]->currentMtzs();
        mtzSubset->insert(mtzSubset->end(), mtzs.begin(), mtzs.end());
        images[i]->dropImage();
    }
}

void MtzRefiner::integrate(bool orientation)
{
    orientation = FileParser::getKey("REFINE_ORIENTATIONS", false);
    
    loadPanels();
    
    std::string filename = FileParser::getKey("ORIENTATION_MATRIX_LIST",
                                              std::string(""));
    if (filename == "")
    {
        std::cout
        << "Filename list has not been provided for integration. Please provide under keyword ORIENTATION_MATRIX_LIST."
        << std::endl;
        exit(1);
    }
    
    readMatricesAndImages(&filename);
    
    int crystals = 0;
    
    for (int i = 0; i < images.size(); i++)
    {
        crystals += images[i]->indexerCount();
    }
    
    std::cout << images.size() << " images with " << crystals << " crystal orientations." << std::endl;
    
    mtzManagers.clear();
    
    int maxThreads = FileParser::getMaxThreads();
    
    boost::thread_group threads;
    vector<vector<MtzPtr> > managerSubsets;
    managerSubsets.resize(maxThreads);
    
    for (int i = 0; i < maxThreads; i++)
    {
        boost::thread *thr = new boost::thread(integrateImagesWrapper, this,
                                               &managerSubsets[i], i, orientation);
        threads.add_thread(thr);
    }
    
    threads.join_all();
    
    Panel::finaliseMillerArrays();
    
    int total = 0;
    
    for (int i = 0; i < maxThreads; i++)
    {
        total += managerSubsets[i].size();
    }
    
    std::cout << "N: Total images loaded: " << total << std::endl;
    
    mtzManagers.reserve(total);
    int lastPos = 0;
    
    for (int i = 0; i < maxThreads; i++)
    {
        mtzManagers.insert(mtzManagers.begin() + lastPos,
                           managerSubsets[i].begin(), managerSubsets[i].end());
        lastPos += managerSubsets[i].size();
    }
    
    writeNewOrientations(false, true);
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        mtzManagers[i]->writeToDat();
    }
    
    for (int i = 0; i < images.size(); i++)
    {
        for (int j = 0; j < images[i]->indexerCount(); j++)
        {
            std::string filename = images[i]->getFilename();
            int totalReflections = images[i]->getIndexer(j)->getTotalReflections();
            double lastScore = images[i]->getIndexer(j)->getLastScore();
            double bestHRot = 0;
            double bestKRot = 0;
            double wavelength = images[i]->getIndexer(j)->getWavelength();
            MatrixPtr matrix = images[i]->getIndexer(j)->getMatrix();
            double *lengths = new double[3];
            matrix->unitCellLengths(&lengths);
            images[i]->getIndexer(j)->getBestRots(&bestHRot, &bestKRot);
            
            std::cout << filename << "\t" << totalReflections << "\t" << lastScore << "\t"
                      << bestHRot << "\t" << bestKRot << "\t" << wavelength << "\t" << lengths[0] << "\t" << lengths[1] << "\t" << lengths[2] << std::endl;
            
            delete [] lengths;
        }
    }
}


void MtzRefiner::loadPanels()
{
    std::string panelList = FileParser::getKey("PANEL_LIST", std::string(""));
    
    panelParser = new PanelParser(panelList);
    
    if (panelList != "" && Panel::panelCount() == 0)
    {
        std::cout << "Loading panels" << std::endl;
        panelParser->parse();
        hasPanelParser = true;
    }
    else if (panelList != "")
    {
        std::cout << "No panel list provided. Continuing regardless..."
        << std::endl;
    }
    else if (Panel::panelCount() > 0)
    {
        std::cout << "Panels already present" << std::endl;
    }
}

void MtzRefiner::findSteps()
{
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        mtzManagers[i]->findSteps();
    }
}

void MtzRefiner::refineDetectorGeometry()
{
    if (hasPanelParser)
    {
        for (int i = 0; i < 5; i++)
        {
            Logger::mainLogger->addString("Refining beam center and detector distance.");
            Panel::expectedBeamCentre();
            Panel::refineDetectorDistance();
            
            Panel::clearAllMillers();
            integrate(false);
        }
    }
    else
    {
        Logger::mainLogger->addString("Individual panel information has not been set up.");
    }
    
    Panel::plotAll(PlotTypeAbsolute);
}

void MtzRefiner::loadMillersIntoPanels()
{
    loadPanels();
    
    loadInitialMtz();
    MtzManager::setReference(reference);
    
    double detectorDistance = FileParser::getKey("DETECTOR_DISTANCE", 0.0);
    double wavelength = FileParser::getKey("INTEGRATION_WAVELENGTH", 0.0);
    double mmPerPixel = FileParser::getKey("MM_PER_PIXEL", MM_PER_PIXEL);

    vector<double> beam = FileParser::getKey("BEAM_CENTRE", vector<double>());
    
    if (beam.size() == 0)
    {
        beam.push_back(BEAM_CENTRE_X);
        beam.push_back(BEAM_CENTRE_Y);
    }

    for (int i = 0; i < mtzManagers.size(); i++)
    {
        MtzPtr mtz = mtzManagers[i];
        
        double hRot = mtz->getHRot();
        double kRot = mtz->getKRot();
        
        for (int j = 0; j < mtz->reflectionCount(); j++)
        {
            for (int k = 0; k < mtz->reflection(j)->millerCount(); k++)
            {
                MillerPtr miller = mtz->reflection(j)->miller(k);
                
                double x, y;
                
                miller->calculatePosition(detectorDistance, wavelength, beam[0], beam[1], mmPerPixel, hRot, kRot, &x, &y);
                
                if (miller->accepted())
                {
                    Panel::addMillerToPanelArray(miller);
                }
            }
        }
    }
    
    Panel::finaliseMillerArrays();
}

// MARK: indexing

void MtzRefiner::writeNewOrientations(bool includeRots, bool detailed)
{
    std::ofstream newMats;
    
    std::string filename = FileParser::getKey("NEW_MATRIX_LIST", std::string("new_orientations.dat"));
    
    newMats.open(filename);
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        MtzPtr manager = mtzManagers[i];
        
        // write out matrices etc.
        std::string imgFilename = manager->filenameRoot();
        
        MatrixPtr matrix = manager->getMatrix()->copy();
        
        if (includeRots)
        {
            double hRad = manager->getHRot() * M_PI / 180;
            double kRad = manager->getKRot() * M_PI / 180;
            
            matrix->rotate(hRad, kRad, 0);
        }
        
        if (!detailed)
        {
            newMats << "img-" << imgFilename << " ";
            std::string description = matrix->description();
            newMats << description << std::endl;
        }
        else
        {
            newMats << "image img-" << imgFilename << std::endl;
            std::string description = matrix->description(true);
            newMats << description << std::endl;
        }
    }
    
    Logger::mainLogger->addString("Written to " + filename);
    
    newMats.close();
}


void MtzRefiner::indexImage(int offset, vector<MtzPtr> *mtzSubset)
{
    for (int i = offset; i < images.size(); i+= MAX_THREADS)
    {
        Image *newImage = images[i];
        IndexerPtr indexer = newImage->getIndexer(0);
        
        newImage->addMask(0, 840, 1765, 920);
        newImage->addMask(446, 1046, 1324, 1765);
        newImage->addMask(820, 450, 839, 473);
        newImage->addMask(1472, 789, 1655, 829);
        
        indexer->findSpots();
        
        indexer->matchMatrixToSpots();
        
        indexer->refineRoundBeamAxis();
        vector<MtzPtr> managers = newImage->currentMtzs();
        MtzPtr manager = managers[0];
        
        manager->description();
        manager->writeToFile(manager->getFilename());
        manager->writeToDat();
        
        mtzSubset->push_back(manager);
        
        newImage->dropImage();
    }
}

void MtzRefiner::indexImageWrapper(MtzRefiner *object, int offset, vector<MtzPtr> *mtzSubset)
{
    object->indexImage(offset, mtzSubset);
}

void MtzRefiner::index()
{
    this->readMatricesAndImages();
    loadPanels();
    vector<vector<MtzPtr> > mtzSubsets;
    mtzSubsets.resize(MAX_THREADS);
    
    boost::thread_group threads;
    
    for (int i = 0; i < MAX_THREADS; i++)
    {
        boost::thread *thr = new boost::thread(indexImageWrapper, this, i, &mtzSubsets[i]);
        threads.add_thread(thr);
    }
    
    threads.join_all();
    
    std::cout << "Images size: " << images.size() << std::endl;
    
    mtzManagers.reserve(images.size());
    
    unsigned int position = 0;
    
    for (int i = 0; i < MAX_THREADS; i++)
    {
        mtzManagers.insert(mtzManagers.begin() + position, mtzSubsets[i].begin(), mtzSubsets[i].end());
        position += mtzSubsets[i].size();
    }
    
    std::cout << "Mtzs: " << mtzManagers.size() << std::endl;
    
    writeNewOrientations();
}

// MARK: Miscellaneous

void MtzRefiner::refineMetrology()
{
    loadInitialMtz();
    
    if (!Panel::hasMillers())
    {
        loadMillersIntoPanels();
    }
    
    Panel::printToFile("new_panels.txt");
    Panel::plotAll(PlotTypeAbsolute);
}

void MtzRefiner::plotDetectorGains()
{
    bool hasMillers = Panel::hasMillers();
    
    if (!hasMillers)
    {
        loadMillersIntoPanels();
    }
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        MtzManager *ref = MtzManager::getReferenceManager();
        
        double scale = mtzManagers[i]->gradientAgainstManager(*ref);
        mtzManagers[i]->applyScaleFactor(scale);
    }
    
    Panel::checkDetectorGains();
}

void MtzRefiner::xFiles()
{
    std::string filename = FileParser::getKey("ORIENTATION_MATRIX_LIST",
                                              std::string(""));
    
    if (filename == "")
    {
        std::cout << "Orientation matrix list has not been provided.";
        exit(1);
    }
    
    readXFiles(filename);
    
    loadInitialMtz();
}

void MtzRefiner::readXFiles(std::string filename)
{
    if (mtzManagers.size() > 0)
    {
        return;
    }
    
    const std::string contents = FileReader::get_file_contents(filename.c_str());
    
    vector<std::string> lines = FileReader::split(contents, '\n');
    
    for (int i = 0; i < lines.size(); i++)
    {
        vector<std::string> filenames = FileReader::split(lines[i], ' ');
        
        XManager *xManager = new XManager();
        xManager->setFilenames(filenames);
        xManager->loadReflections(PartialityModelFixed);
        xManager->loadParametersMap();
        
        MtzPtr mtz = MtzPtr();
        mtz.reset(xManager);
        
        mtzManagers.push_back(mtz);
    }
    
    std::ostringstream logged;
    logged << "Mtz count: " << mtzManagers.size() << std::endl;
    Logger::mainLogger->addStream(&logged);
}

void MtzRefiner::polarisationGraph()
{
    GraphDrawer drawer = GraphDrawer(reference);
    
#ifdef MAC
    drawer.plotPolarisation(mtzManagers);
#endif
}

MtzRefiner::~MtzRefiner()
{
    delete reference;
    delete panelParser;
    mtzManagers.clear();
    
    for (int i = 0; i < images.size(); i++)
    {
        delete images[i];
    }
    
    images.clear();

    // TODO Auto-generated destructor stub
}

void MtzRefiner::removeSigmaValues()
{
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        mtzManagers[i]->setSigmaToUnity();
    }
    
    if (MtzManager::getReferenceManager() != NULL)
        MtzManager::getReferenceManager()->setSigmaToUnity();
}


void MtzRefiner::displayIndexingHands()
{
    std::ostringstream idxLogged, invLogged;
    
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        std::ostringstream &which = mtzManagers[i]->getActiveAmbiguity() == 0 ? idxLogged : invLogged;
        
        which << mtzManagers[i]->getFilename() << std::endl;
    }
    
    Logger::mainLogger->addString("**** Indexing hands ****\n - First indexing hand");
    Logger::mainLogger->addStream(&idxLogged);
    Logger::mainLogger->addString("- Second indexing hand");
    Logger::mainLogger->addStream(&invLogged);
    
}

void MtzRefiner::applyUnrefinedPartiality()
{
    for (int i = 0; i < mtzManagers.size(); i++)
    {
        mtzManagers[i]->applyUnrefinedPartiality();
    }
}

void MtzRefiner::refineDistances()
{
    for (int i = 0; i < images.size(); i++)
    {
        for (int j = 0; j < images[i]->indexerCount(); j++)
        {
            images[i]->getIndexer(j)->refineDetectorAndWavelength(reference);
        }
    }
}

void MtzRefiner::orientationPlot()
{
#ifdef MAC
    GraphDrawer drawer = GraphDrawer(reference);
    drawer.plotOrientationStats(mtzManagers);
#endif
}