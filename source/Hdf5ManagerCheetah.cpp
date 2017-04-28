//
//  Hdf5ManagerCheetah.cpp
//  cppxfel
//
//  Created by Helen Ginn on 09/10/2016.
//  Copyright (c) 2016 Division of Structural Biology Oxford. All rights reserved.
//

#include <stdio.h>
#include "Hdf5ManagerCheetah.h"
#include "Hdf5ManagerCheetahLCLS.h"
#include "Hdf5ManagerCheetahSacla.h"
#include "FileParser.h"
#include "misc.h"

std::string Hdf5ManagerCheetah::maskAddress;
std::vector<Hdf5ManagerCheetahPtr> Hdf5ManagerCheetah::cheetahManagers;
std::mutex Hdf5ManagerCheetah::readingPaths;

void Hdf5ManagerCheetah::initialiseCheetahManagers()
{
    if (cheetahManagers.size() > 0)
        return;
    
    std::vector<std::string> hdf5FileGlobs = FileParser::getKey("HDF5_SOURCE_FILES", std::vector<std::string>());
    std::ostringstream logged;
    
    for (int i = 0; i < hdf5FileGlobs.size(); i++)
    {
        std::vector<std::string> hdf5Files = glob(hdf5FileGlobs[i]);
        
        logged << "HDF5_SOURCE_FILES entry (no. " << i << ") matches: " << std::endl;
        
        for (int j = 0; j < hdf5Files.size(); j++)
        {
            std::string aFilename = hdf5Files[j];
            Hdf5ManagerCheetahPtr cheetahPtr;
            
            int guessLCLS = (aFilename.find("cxi") != std::string::npos);
            int guessLaser = (guessLCLS) ? 0 : 1;
            
            int laserInt = FileParser::getKey("FREE_ELECTRON_LASER", guessLaser);
            FreeElectronLaserType laser = (FreeElectronLaserType)laserInt;
            
            switch (laser) {
                case FreeElectronLaserTypeLCLS:
                    cheetahPtr = Hdf5ManagerCheetahLCLS::makeManager(aFilename);
                    break;
                case FreeElectronLaserTypeSACLA:
                    cheetahPtr = Hdf5ManagerCheetahSacla::makeManager(aFilename);
                    break;
                default:
                    cheetahPtr = Hdf5ManagerCheetahSacla::makeManager(aFilename);
                    break;
            }
            
            cheetahManagers.push_back(cheetahPtr);
            
            logged << hdf5Files[j] << ", ";
        }
        
        logged << std::endl;
    }
    
    if (cheetahManagers.size())
    {
        logged << "... now managing " << cheetahManagers.size() << " hdf5 image source files." << std::endl;
    }
    if (!cheetahManagers.size())
    {
        if (hdf5FileGlobs.size())
        {
            logged << "You specified some hdf5 image source files but none could be found. Please check existence and location of files." << std::endl;
            LoggableObject::staticLogAndExit(logged);
        }
    }
    
    Logger::mainLogger->addStream(&logged);
}

Hdf5ManagerCheetahPtr Hdf5ManagerCheetah::hdf5ManagerForImage(std::string imageName)
{
    // make a map!
    for (int i = 0; i < cheetahManagers.size(); i++)
    {
        Hdf5ManagerCheetahPtr cheetahManager = cheetahManagers[i];
        
        std::string address = cheetahManager->addressForImage(imageName);


        if (address.length() > 0)
        {
            return cheetahManager;
        }
    }
    
    return Hdf5ManagerCheetahSaclaPtr();
}

void Hdf5ManagerCheetah::closeHdf5Files()
{
    for (int i = 0; i < cheetahManagers.size(); i++)
    {
        cheetahManagers[i]->closeHdf5();
    }
}

std::string Hdf5ManagerCheetah::addressForImage(std::string imageName)
{
    // maybe imageName has .img extension, so let's get rid of it
    std::string baseName = getBaseFilename(imageName);
    
    if (!imagePathMap.count(baseName))
    {
        return "";
    }
    
    int index = imagePathMap[baseName];
    return imagePaths[index];
}
