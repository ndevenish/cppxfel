//
//  GeometryRefiner.cpp
//  cppxfel
//
//  Created by Helen Ginn on 28/12/2016.
//  Copyright (c) 2016 Division of Structural Biology Oxford. All rights reserved.
//

#include "GeometryRefiner.h"
#include "GeometryParser.h"
#include "Detector.h"
#include "RefinementStepSearch.h"
#include "RefinementGridSearch.h"
#include "NelderMead.h"
#include "IndexManager.h"
#include "misc.h"
#include "UnitCellLattice.h"

PseudoScoreType pseudoScoreTypeForGeometryType(GeometryScoreType type)
{
    switch (type)
    {
        case GeometryScoreTypeInterpanel:
            return PseudoScoreTypeInterPanel;
        case GeometryScoreTypeBeamCentre:
            return PseudoScoreTypeBeamCentre;
        case GeometryScoreTypeIntrapanel:
            return PseudoScoreTypeIntraPanel;
        default:
            return PseudoScoreTypeInvalid;
    }
}

std::string stringForScoreType(GeometryScoreType type)
{
    std::string typeString = "";
    
    switch (type) {
        case GeometryScoreTypeBeamCentre:
            typeString = "beam_centre";
            break;
        case GeometryScoreTypeInterpanel:
            typeString = "interpanel";
            break;
        case GeometryScoreTypeIntrapanel:
            typeString = "intrapanel";
            break;
        case GeometryScoreTypeInterMiller:
            typeString = "intra_miller";
            break;
        case GeometryScoreTypeIntraMiller:
            typeString = "inter_miller";
            break;
        default:
            break;
    }

    return typeString;
}

RefinementGridSearchPtr GeometryRefiner::makeGridRefiner(DetectorPtr detector, GeometryScoreType type)
{
    RefinementGridSearchPtr strategy = RefinementGridSearchPtr(new RefinementGridSearch());
    strategy->setGridLength(31);
    strategy->setVerbose(false);

	if (type != GeometryScoreTypePeakSearch)
	{
		IndexManagerPtr aManager = IndexManagerPtr(new IndexManager(images));
		aManager->setActiveDetector(detector);
		aManager->setPseudoScoreType(pseudoScoreTypeForGeometryType(type));
		strategy->setEvaluationFunction(IndexManager::pseudoScore, &*aManager);
		strategy->setFinishFunction(IndexManager::staticPseudoAnglePDB);
	}
	else
	{
		strategy->setEvaluationFunction(Detector::peakScoreWrapper, &*detector);
	}

    return strategy;
}

RefinementStrategyPtr GeometryRefiner::makeRefiner(DetectorPtr detector, GeometryScoreType type)
{
    RefinementStrategyPtr strategy = RefinementStrategy::userChosenStrategy();
    
    std::string typeString = stringForScoreType(type);
    
    strategy->setJobName("Refining " + detector->getTag() + " (" + typeString + ")");

    strategy->setVerbose(false);
    
    IndexManagerPtr aManager = IndexManagerPtr(new IndexManager(images));
	PseudoScoreType pseudoType = pseudoScoreTypeForGeometryType(type);

	// Reuse an old manager if the score type is correct
	if (detector->getIndexManager() && detector->getIndexManager()->getPseudoScoreType() == pseudoType)
	{
		aManager = detector->getIndexManager();
	}

    aManager->setActiveDetector(detector);
    aManager->setCycleNum(refinementEvent);
	aManager->lockVectors();

    switch (type)
    {
        case GeometryScoreTypeInterMiller:
            strategy->setEvaluationFunction(Detector::millerScoreWrapper, &*detector);
			break;
        case GeometryScoreTypeIntraMiller:
            strategy->setEvaluationFunction(Detector::millerStdevScoreWrapper, &*detector);
			break;
        case GeometryScoreTypeInterpanel:
            aManager->setPseudoScoreType(PseudoScoreTypeInterPanel);
            strategy->setEvaluationFunction(IndexManager::pseudoScore, &*aManager);
		//	strategy->setFinishFunction(IndexManager::staticPseudoAnglePDB);
            break;
        case GeometryScoreTypeBeamCentre:
            aManager->setPseudoScoreType(PseudoScoreTypeBeamCentre);
            strategy->setEvaluationFunction(IndexManager::pseudoScore, &*aManager);
		//	strategy->setFinishFunction(IndexManager::staticPseudoAnglePDB);
            break;
        case GeometryScoreTypeIntrapanel:
            aManager->setPseudoScoreType(PseudoScoreTypeIntraPanel);
            strategy->setEvaluationFunction(IndexManager::pseudoScore, &*aManager);
		//	strategy->setFinishFunction(IndexManager::staticPseudoAnglePDB);
			break;
        default:
            break;
    }
    
    return strategy;
}

GeometryRefiner::GeometryRefiner()
{
    refinementEvent = 0;
    cycleNum = 0;
    lastIntraScore = 0;
    lastInterScore = 0;
    lastInterAngleScore = 0;
    lastIntraAngleScore = 0;
    _changed = true;
}


void GeometryRefiner::startingGraphs()
{
    logged << "Generating starting graphs..." << std::endl;
    sendLog();
    
    std::vector<DetectorPtr> allDetectors;
    Detector::getMaster()->getAllSubDetectors(allDetectors, false);
    
    for (int i = 0; i < allDetectors.size(); i++)
    {
        DetectorPtr detector = allDetectors[i];
        IndexManagerPtr aManager = IndexManagerPtr(new IndexManager(images));
        aManager->setActiveDetector(detector);
        aManager->setCycleNum(0);
        aManager->lockVectors();
        
        if (detector->isRefinable(GeometryScoreTypeIntrapanel))
        {
            aManager->setPseudoScoreType(PseudoScoreTypeIntraPanel);
            IndexManager::pseudoAngleScore(&*aManager);
            aManager->plotGoodVectors();
        }
        
        if (detector->isRefinable(GeometryScoreTypeInterpanel))
        {
            aManager->clearGoodVectors();
            aManager->setPseudoScoreType(PseudoScoreTypeInterPanel);
            IndexManager::pseudoAngleScore(&*aManager);
            aManager->plotGoodVectors();
        }
    }
}

void GeometryRefiner::reportProgress()
{
    if (!_changed)
    {
        return;
    }
    
    _changed = false;
    
    std::string filename = "special_image_" + i_to_str(refinementEvent) + ".png";
    Detector::drawSpecialImage(filename);
    
    manager->setPseudoScoreType(PseudoScoreTypeIntraPanel);
    double intraScore = IndexManager::pseudoScore(&*manager);
    manager->setPseudoScoreType(PseudoScoreTypeAllInterPanel);
    double interScore = IndexManager::pseudoScore(&*manager);

    double intraIncrease = 100;
    double interIncrease = 100;
    double mScore = Detector::getMaster()->millerScore(true, false);
    Detector::getMaster()->reportMillerScores(refinementEvent);

	intraIncrease = (intraScore / lastIntraScore - 1) * 100;
	interIncrease = (interScore / lastInterScore - 1) * 100;

    lastInterScore = interScore;
    lastIntraScore = intraScore;

    
    logged << "N: Progress score (event " << refinementEvent << ", intra-panel-dist): " << intraScore
    << " (" << (intraIncrease > 0 ? "+" : "") << intraIncrease << "% from last round) " << std::endl;
    logged << "N: Progress score (event " << refinementEvent << ", inter-panel-dist): " << interScore
    << " (" << (interIncrease > 0 ? "+" : "") << interIncrease << "% from last round)" << std::endl;

    if (mScore != 0)
    {
        logged << "N: Progress score (event " << refinementEvent << ", miller mean): " << mScore << std::endl;
    }
    sendLog();
    
    manager->powderPattern("geom_refinement_event_" + i_to_str(refinementEvent) + ".csv", false);
    GeometryParser geomParser = GeometryParser("whatever", GeometryFormatCppxfel);
    geomParser.writeToFile("new_" + i_to_str(refinementEvent) + ".cppxfel_geom", refinementEvent);
    refinementEvent++;
    
    sendLog();
}

void GeometryRefiner::refineGeometry()
{
    Detector::getMaster()->enableNudge();
    
    reportProgress();
    
    std::vector<DetectorPtr> detectors;
    detectors.push_back(Detector::getMaster());
    
    int maxCycles = FileParser::getKey("MAXIMUM_CYCLES", 6);
    
    std::vector<double> sweepDetectorDistance = FileParser::getKey("SWEEP_DETECTOR_DISTANCE", std::vector<double>());
    
    if (sweepDetectorDistance.size() >= 2)
    {
        
        double start = sweepDetectorDistance[0];
        double end = sweepDetectorDistance[1];
        
        if (end < start)
        {
            end = sweepDetectorDistance[0];
            start = sweepDetectorDistance[1];
        }
        
        logged << "***********************************************" << std::endl;
        logged << "Doing a detector distance sweep from " << start << " to " << end << " mm." << std::endl;
        logged << "***********************************************" << std::endl;
        sendLog();
        
        double mmPerPixel = FileParser::getKey("MM_PER_PIXEL", 0.11);

        end /= mmPerPixel;
        start /= mmPerPixel;
        
        gridSearchDetectorDistance(Detector::getMaster(), start, end);
        double newDistance = Detector::getArrangedMidPointZ(&*Detector::getMaster());
        
        logged << "**** Grid search done ****" << std::endl;
        logged << "**** New detector distance: " << newDistance * mmPerPixel << " mm. ****" << std::endl;
        sendLog();
        
        reportProgress();
    }

	bool hasMillers = Detector::getMaster()->millerCount() > 0;
	std::vector<DetectorPtr> allDetectors;
	Detector::getMaster()->getAllSubDetectors(allDetectors, true);
	printHeader(allDetectors);
	bool approximate = FileParser::getKey("GEOMETRY_IS_APPROXIMATE", false);

	if (hasMillers && approximate)
	{
		// we need to search around for the peaks
		refineDetectorStrategyWrapper(this, allDetectors, GeometryScoreTypePeakSearch, 0);
	}

	for (int i = 0; i < maxCycles; i++)
    {
        cycleNum++;
		refineUnitCell();
		intraPanelCycle();
		interPanelCycle();
    }
/*
	logged << "**** Geometry refinement complete ****" << std::endl;
	logged << "**** Now setting METROLOGY_SEARCH_SIZE to 0 for convenience. ****" << std::endl;
	sendLog();

	FileParser::setKey("METROLOGY_SEARCH_SIZE", 0);
 */
}


void GeometryRefiner::printHeader(std::vector<DetectorPtr> detectors)
{
	std::ostringstream detectorList;

	for (int j = 0; j < detectors.size(); j++)
	{
		detectorList << detectors[j]->getTag() << " ";
	}

    logged << "***************************************************" << std::endl;
    logged << "  Cycle " << cycleNum << ", event " << refinementEvent << std::endl;
    logged << "  Refining detector" << (detectors.size() == 1 ? "" : "s") << ": " << detectorList.str() << std::endl;
    logged << "***************************************************" << std::endl << std::endl;
    sendLog();
}

void GeometryRefiner::intraPanelCycle()
{
	bool hasMillers = Detector::getMaster()->millerCount() > 0;
	std::vector<DetectorPtr> detectors;
	Detector::getMaster()->getAllSubDetectors(detectors, true);
	printHeader(detectors);

	GeometryScoreType type = GeometryScoreTypeIntrapanel;
	int cycles = 4;

	if (hasMillers)
	{
		type = GeometryScoreTypeIntraMiller;
		cycles = 4;
	}

	for (int i = 0; i < cycles; i++)
	{
		refineDetectorStrategyWrapper(this, detectors, type, 0);
	}
}

void GeometryRefiner::interPanelCycle()
{
	bool hasMillers = Detector::getMaster()->millerCount() > 0;

	GeometryScoreType type = GeometryScoreTypeInterpanel;

	if (hasMillers)
	{
		type = GeometryScoreTypeInterMiller;
		std::vector<DetectorPtr> detectors;
		Detector::getMaster()->getAllSubDetectors(detectors, true);
		printHeader(detectors);
		refineDetectorStrategyWrapper(this, detectors, type, 1);

		for (int i = 0; i < 4; i++)
		{
			refineDetectorStrategyWrapper(this, detectors, type, 0);
		}
	}
	else
	{
		int i = 64;
		while (i >= 0)
		{
			std::vector<DetectorPtr> detectors;
			detectors = Detector::getMaster()->getSubDetectorsOnLevel(i);
			i--;

			if (detectors.size() == 0)
			{
				continue;
			}

			refineDetectorStrategyWrapper(this, detectors, type, 0);
		}
	}

}


void GeometryRefiner::refineDetectorStrategyWrapper(GeometryRefiner *me, std::vector<DetectorPtr> detectors, GeometryScoreType type, int strategyType)
{
    int maxThreads = FileParser::getMaxThreads();
   
    {
        boost::thread_group threads;
        
        for (int i = 0; i < maxThreads; i++)
        {
            boost::thread *thr = new boost::thread(refineDetectorWrapper, me, detectors, i, type, strategyType);
            threads.add_thread(thr);
        }
        
        threads.join_all();
    }

	if (type == GeometryScoreTypePeakSearch)
	{

	}

    me->reportProgress();
    
    me->firstCycle = false;
}

void GeometryRefiner::refineDetectorWrapper(GeometryRefiner *me, std::vector<DetectorPtr> detectors, int offset, GeometryScoreType type, int strategyType)
{
    int maxThreads = FileParser::getMaxThreads();

    for (int i = offset; i < detectors.size(); i += maxThreads)
    {
        me->refineDetectorStrategy(detectors[i], type, strategyType);
    }
}

void GeometryRefiner::refineDetectorStrategy(DetectorPtr detector, GeometryScoreType type, int strategyType)
{
    {
    //    RefinementStrategyPtr strategy = makeRefiner(detector, type);
    //    IndexManager::pseudoDistanceScore(&*detector->getIndexManager(), true, "before_");
    }
    
    std::string typeString = stringForScoreType(type);
    
    if (!detector->isRefinable(type))
    {
        return;
    }

	_changed = true;

	if (type == GeometryScoreTypeInterMiller)
	{
		if (detector->millerCount())
		{
			if (strategyType == 1)
			{
				interPanelGridSearch(detector, type);
			}
			interPanelMillerSearch(detector, type);
		}
	}
	else if (type == GeometryScoreTypeIntraMiller)
	{
		intraPanelMillerSearch(detector, type);
	}
	else if (type == GeometryScoreTypeInterpanel)
	{
		interPanelGridSearch(detector, type);
		interPanelMillerSearch(detector, type);
	}
	else if (type == GeometryScoreTypeIntrapanel)
	{
		intraPanelMillerSearch(detector, type);
	}
	else if (type == GeometryScoreTypePeakSearch)
	{
		peakSearchDetector(detector);
	}

	{
	//	RefinementStrategyPtr strategy = makeRefiner(detector, type);
    //    IndexManager::pseudoDistanceScore(&*detector->getIndexManager(), true, "after_");
    }
}

void GeometryRefiner::peakSearchDetector(DetectorPtr detector)
{
	int searchSize = FileParser::getKey("METROLOGY_SEARCH_SIZE", 3);
	int maxMovement = 50;
	int gridLength = (double)maxMovement / (double)searchSize;

	RefinementGridSearchPtr strategy = makeGridRefiner(detector, GeometryScoreTypePeakSearch);
	strategy->setGridLength(gridLength * 2);

	strategy->addParameter(&*detector, Detector::getAddPixelOffsetX, Detector::setAddPixelOffsetX, searchSize, 0.1);
	strategy->addParameter(&*detector, Detector::getAddPixelOffsetY, Detector::setAddPixelOffsetY, searchSize, 0.1);
	strategy->setJobName(detector->getTag() + "_peaksearch");

	strategy->refine();
}

void GeometryRefiner::interPanelGridSearch(DetectorPtr detector, GeometryScoreType type)
{
	double nudgeStep, nudgeTiltX, nudgeTiltY, interNudge;
	detector->nudgeTiltAndStep(&nudgeTiltX, &nudgeTiltY, &nudgeStep, &interNudge);
	interNudge /= 2;

	RefinementGridSearchPtr strategy = makeGridRefiner(detector, type);
	detector->resetPoke();

	if (type == GeometryScoreTypeInterMiller)
	{
		return;
		strategy->setJobName(detector->getTag() + "_miller");
		strategy->setEvaluationFunction(Detector::millerScoreWrapper, &*detector);
		strategy->setFinishFunction(NULL);
		strategy->addParameter(&*detector, Detector::getInterNudgeX, Detector::setInterNudgeX, interNudge / nudgeStep * 3.0, 0, "internudge_x");
		strategy->addParameter(&*detector, Detector::getInterNudgeY, Detector::setInterNudgeY, interNudge / nudgeStep * 3.0, 0, "internudge_y");
		strategy->setGridLength(101);
	}
	else
	{
		strategy->setJobName(detector->getTag() + "_powder");
		strategy->addParameter(&*detector, Detector::getPokeX, Detector::setPokeX, interNudge * 1.0, 0, "poke_x");
		strategy->addParameter(&*detector, Detector::getPokeY, Detector::setPokeY, interNudge * 1.0, 0, "poke_y");
		strategy->setGridLength(31);
	}

	strategy->refine();

	detector->resetPoke();
}

void GeometryRefiner::intraPanelMillerSearch(DetectorPtr detector, GeometryScoreType type)
{
    double nudgeStep, nudgeTiltX, nudgeTiltY, interNudge;
    detector->nudgeTiltAndStep(&nudgeTiltX, &nudgeTiltY, &nudgeStep, &interNudge);

	 nudgeStep /= 2;
	nudgeTiltX /= 2;
	nudgeTiltY /= 2;

    RefinementStrategyPtr strategy = makeRefiner(detector, type);
    detector->prepareInterNudges();
    strategy->setJobName(detector->getTag() + "_miller_stdev_z");
    strategy->addParameter(&*detector, Detector::getNudgeZ, Detector::setNudgeZ, nudgeStep, 0, "nudge_z");
    strategy->refine();
    strategy->clearParameters();
    detector->prepareInterNudges();
    strategy->addParameter(&*detector, Detector::getNudgeTiltX, Detector::setNudgeTiltX, nudgeTiltX, 0, "nudgetilt_x");
    strategy->addParameter(&*detector, Detector::getNudgeTiltY, Detector::setNudgeTiltY, nudgeTiltY, 0, "nudgetilt_y");
    strategy->setJobName(detector->getTag() + "_miller_stdev_tilt");
    strategy->refine();
    
    detector->prepareInterNudges();
}

void GeometryRefiner::interPanelMillerSearch(DetectorPtr detector, GeometryScoreType type)
{
    double nudgeStep, nudgeTiltX, nudgeTiltY, interNudge;
    detector->nudgeTiltAndStep(&nudgeTiltX, &nudgeTiltY, &nudgeStep, &interNudge);
    
    RefinementStrategyPtr strategy = makeRefiner(detector, type);
    strategy->setJobName(detector->getTag() + "_miller");
    detector->resetPoke();

	if (type == GeometryScoreTypeInterMiller)
	{
	//	detector->quickJumpToOrigin();

		strategy->addParameter(&*detector, Detector::getInterNudgeX, Detector::setInterNudgeX, interNudge, 0, "internudge_x");
		strategy->addParameter(&*detector, Detector::getInterNudgeY, Detector::setInterNudgeY, interNudge, 0, "internudge_y");
		strategy->addParameter(&*detector, Detector::getInterNudgeZ, Detector::setInterNudgeZ, interNudge, 0, "internudge_z");
	}
	else
	{
		strategy->setJobName(detector->getTag() + "_powder");
		strategy->addParameter(&*detector, Detector::getPokeX, Detector::setPokeX, interNudge, 0, "internudge_x");
		strategy->addParameter(&*detector, Detector::getPokeY, Detector::setPokeY, interNudge, 0, "internudge_y");
		strategy->addParameter(&*detector, Detector::getPokeZ, Detector::setPokeZ, interNudge, 0, "internudge_z");
	}

    strategy->refine();
    
    detector->resetPoke();
}

void GeometryRefiner::refineBeamCentre()
{
    DetectorPtr detector = Detector::getMaster();

	return;

    if (detector->millerCount() > 0)
    {
        return;
    }
    
    logged << "***************************************************" << std::endl;
    logged << "  Cycle " << cycleNum << ", event " << refinementEvent << std::endl;
    logged << "  Refining beam centre: " << detector->getTag() << std::endl;
    logged << "***************************************************" << std::endl << std::endl;
    sendLog();
    
    RefinementStrategyPtr gridSearch = makeRefiner(detector, GeometryScoreTypeBeamCentre);
    gridSearch->setJobName("grid_search_beam_centre_" + i_to_str(refinementEvent));
    gridSearch->setCycles(99);
    
    double nudgeStep, nudgeTiltX, nudgeTiltY, interNudge;
    detector->nudgeTiltAndStep(&nudgeTiltX, &nudgeTiltY, &nudgeStep, &interNudge);

    gridSearch->addParameter(&*detector, Detector::getInterNudgeX, Detector::setInterNudgeX, interNudge, 0.001, "poke_x");
    gridSearch->addParameter(&*detector, Detector::getInterNudgeY, Detector::setInterNudgeY, interNudge, 0.001, "poke_y");

    detector->getIndexManager()->setPseudoScoreType(PseudoScoreTypeBeamCentre);
    gridSearch->refine();
	_changed = true;
    
    reportProgress();
}

void GeometryRefiner::refineUnitCell()
{
    bool optimisingUnitCellA = FileParser::getKey("OPTIMISING_UNIT_CELL_A", false);
    bool optimisingUnitCellB = FileParser::getKey("OPTIMISING_UNIT_CELL_B", false);
    bool optimisingUnitCellC = FileParser::getKey("OPTIMISING_UNIT_CELL_C", false);
    bool optimisingUnitCellAlpha = FileParser::getKey("OPTIMISING_UNIT_CELL_ALPHA", false);
    bool optimisingUnitCellBeta = FileParser::getKey("OPTIMISING_UNIT_CELL_BETA", false);
    bool optimisingUnitCellGamma = FileParser::getKey("OPTIMISING_UNIT_CELL_GAMMA", false);
    
    if (!optimisingUnitCellA && !optimisingUnitCellAlpha &&
        !optimisingUnitCellB && !optimisingUnitCellBeta &&
        !optimisingUnitCellC && !optimisingUnitCellGamma)
    {
        return;
    }
    
    DetectorPtr detector = Detector::getMaster();
    
    logged << "***************************************************" << std::endl;
    logged << "  Cycle " << cycleNum << ", event " << refinementEvent << std::endl;
    logged << "  Refining unit cell: " << detector->getTag() << std::endl;
    logged << "***************************************************" << std::endl << std::endl;
    sendLog();
    
    RefinementStrategyPtr strategy = makeRefiner(detector, GeometryScoreTypeIntrapanel);
    strategy->setJobName("Refining unit cell");
    IndexManager *manager = static_cast<IndexManager *>(strategy->getEvaluationObject());

    if (optimisingUnitCellA)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellA, UnitCellLattice::setUnitCellA, 0.1, 0.00001, "unit_cell_a");
    }
    if (optimisingUnitCellB)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellB, UnitCellLattice::setUnitCellB, 0.1, 0.00001, "unit_cell_b");
    }
    if (optimisingUnitCellC)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellC, UnitCellLattice::setUnitCellC, 0.1, 0.00001, "unit_cell_c");
    }

    if (optimisingUnitCellAlpha)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellAlpha, UnitCellLattice::setUnitCellAlpha, 0.1, 0.00001, "unit_cell_alpha");
    }
    if (optimisingUnitCellBeta)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellBeta, UnitCellLattice::setUnitCellBeta, 0.1, 0.00001, "unit_cell_beta");
    }
    if (optimisingUnitCellGamma)
    {
        strategy->addParameter(&*(manager->getLattice()), UnitCellLattice::getUnitCellGamma, UnitCellLattice::setUnitCellGamma, 0.1, 0.00001, "unit_cell_gamma");
    }

	_changed = true;
    strategy->setCycles(15);
    
    strategy->refine();
    reportProgress();
}


void GeometryRefiner::gridSearchDetectorDistance(DetectorPtr detector, double start, double end)
{
    double step = 0.2;
    double middle = (end + start) / 2;
    Detector::setArrangedMidPointZ(&*detector, middle);
    double confidence = (end - start) / step / 2;
    
    RefinementGridSearchPtr strategy = RefinementGridSearchPtr(new RefinementGridSearch());
    strategy->addParameter(&*detector, Detector::getArrangedMidPointZ, Detector::setArrangedMidPointZ, step, 0.1, "nudge_z");
    
    strategy->setGridLength(confidence * 2 + 1);
    strategy->setVerbose(false);
    strategy->setJobName("Wide sweep detector " + detector->getTag());
    
    IndexManagerPtr aManager = IndexManagerPtr(new IndexManager(images));
    aManager->setActiveDetector(detector);
    aManager->setPseudoScoreType(PseudoScoreTypeIntraPanel);
    strategy->setEvaluationFunction(IndexManager::pseudoScore, &*aManager);
    
    strategy->refine();
}

void GeometryRefiner::setImages(std::vector<ImagePtr> newImages)
{
    images = newImages;
    manager = IndexManagerPtr(new IndexManager(images));    
}
