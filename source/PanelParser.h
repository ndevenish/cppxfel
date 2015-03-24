/*
 * PanelParser.h
 *
 *  Created on: 13 Jan 2015
 *      Author: helenginn
 */

#ifndef PANELPARSER_H_
#define PANELPARSER_H_

#include "FileParser.h"

class PanelParser : public FileParser
{
private:
	std::vector<PanelPtr> panels;

public:
	virtual void parse();

	void addPanel(std::string rest);
	PanelParser(std::string filename);
	virtual ~PanelParser();
};

#endif /* PANELPARSER_H_ */
