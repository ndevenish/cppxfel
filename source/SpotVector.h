//
//  SpotVector.h
//  cppxfel
//
//  Created by Helen Ginn on 15/10/2015.
//  Copyright (c) 2015 Division of Structural Biology Oxford. All rights reserved.
//

#ifndef __cppxfel__SpotVector__
#define __cppxfel__SpotVector__

#include <stdio.h>
#include "Spot.h"
#include "parameters.h"

class SpotVector
{
private:
    SpotPtr firstSpot;
    SpotPtr secondSpot;
    
    vec spotDiff;
public:
    
    SpotVector(SpotPtr first, SpotPtr second);

    double distance();
    double angleWithVector(SpotVectorPtr spotVector2);
    
    vec getVector()
    {
        return spotDiff;
    }
    
    SpotPtr getFirstSpot()
    {
        return firstSpot;
    }
    
    SpotPtr getSecondSpot()
    {
        return secondSpot;
    }
};

#endif /* defined(__cppxfel__SpotVector__) */