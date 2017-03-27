//
//  hasSymmetry.h
//  cppxfel
//
//  Created by Helen Ginn on 25/03/2017.
//  Copyright (c) 2017 Division of Structural Biology Oxford. All rights reserved.
//

#ifndef cppxfel_hasSymmetry_h
#define cppxfel_hasSymmetry_h

#include "csymlib.h"
#include "FileParser.h"
using namespace CSym;

class hasSymmetry
{
private:
	std::vector<double> _unitCell;
	CCP4SPG *_spaceGroup;

public:
	hasSymmetry()
	{
		_spaceGroup = NULL;
		_unitCell = FileParser::getKey("UNIT_CELL", vector<double>());
	}

	std::vector<double> getUnitCell()
	{
		return _unitCell;
	}

	CCP4SPG*& getSpaceGroup()
	{
		if (!_spaceGroup)
		{
			int num = FileParser::getKey("SPACE_GROUP", 0);
			_spaceGroup = ccp4spg_load_by_ccp4_num(num);
		}
		
		return _spaceGroup;
	}

	int getSpaceGroupNum()
	{
		return _spaceGroup->spg_ccp4_num;
	}

	void setSpaceGroupNum(int num)
	{
		_spaceGroup = ccp4spg_load_by_ccp4_num(num);
	}

	void setSpaceGroup(CCP4SPG *spg)
	{
		_spaceGroup = spg;
	}

	void lockUnitCellDimensions()
	{
		double spgNum = _spaceGroup->spg_num;

		if (spgNum >= 75 && spgNum <= 194)
		{
			_unitCell[1] = _unitCell[0];
		}
		if (spgNum >= 195)
		{
			_unitCell[1] = _unitCell[0];
			_unitCell[2] = _unitCell[0];
		}
	}

	template <typename Type>
	void setUnitCell(vector<Type> unitCell)
	{
		_unitCell.resize(6);
		
		for (int i = 0; i < 6; i++)
		{
			_unitCell[i] = unitCell[i];
		}
	}

	static double getUnitCellAStatic(void *object)
	{
		return static_cast<hasSymmetry *>(object)->_unitCell[0];
	}

	static void setUnitCellAStatic(void *object, double newDim)
	{
		static_cast<hasSymmetry *>(object)->_unitCell[0] = newDim;
	}

	static double getUnitCellBStatic(void *object)
	{
		return static_cast<hasSymmetry *>(object)->_unitCell[1];
	}

	static void setUnitCellBStatic(void *object, double newDim)
	{
		static_cast<hasSymmetry *>(object)->_unitCell[1] = newDim;
	}

	static double getUnitCellCStatic(void *object)
	{
		return static_cast<hasSymmetry *>(object)->_unitCell[2];
	}

	static void setUnitCellCStatic(void *object, double newDim)
	{
		static_cast<hasSymmetry *>(object)->_unitCell[2] = newDim;
	}


};


#endif