//
//  SpotFinderCorrelation.cpp
//  cppxfel
//
//  Created by Helen Ginn on 29/05/2016.
//  Copyright (c) 2016 Division of Structural Biology Oxford. All rights reserved.
//

#include "SpotFinderCorrelation.h"
#include "Image.h"


void SpotFinderCorrelation::findSpecificSpots(std::vector<SpotPtr> *spots)
{
    int xDim = this->image->getXDim();
    int yDim = this->image->getYDim();
    
    int *mask = new int[xDim * yDim];
    memset(mask, 0, sizeof(int) * xDim * yDim);
    
    SpotPtr spot = SpotPtr(new Spot(image));
    
    for (int y = 0; y < yDim; y++)
    {
        for (int x = 0; x < xDim; x++)
        {
            if (mask[y * xDim + x])
            {
                continue;
            }
            
            double value = this->image->valueAt(x, y);
            
            if (value < threshold)
                continue;
            
            bool success = spot->focusOnNearbySpot(0, x, y);
            
            if (success)
            {
                spot->recentreInWindow();
                spots->push_back(spot);
                spot->addToMask(mask, xDim);
            }

            spot = SpotPtr(new Spot(image));
        }
    }
    
    delete [] mask;
}