/**
 * @file PiPoGraph.h
 * @author Joseph Larralde
 *
 * @brief PiPo dataflow graph class that parses a graph description string and instantiates
 * the corresponding graph of PiPos (made of PiPoSequence and PiPoParallel instances)
 *
 * PiPoGraph is built around the classes PiPoSequence, PiPoParallel and PiPoOp.
 *
 * A PiPoGraph is itself also a PiPo module, i.e. data processing works the same
 * as for a simple operator :
 * - the PiPoGraph is prepared for processing by calling streamAttributes() on it.
 * - data is piped into the PiPoGraph with the frames() method.
 *
 * @ingroup pipoapi
 *
 * @copyright
 * Copyright (C) 2017 by ISMM IRCAM - Centre Pompidou, Paris, France.
 * All rights reserved.
 *
 * License (BSD 3-clause)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _PIPO_GRAPH_
#define _PIPO_GRAPH_

#include <string>
#include <vector>
#include <iostream>

#include "PiPoOp.h"
#include "PiPoSequence.h"
#include "PiPoParallel.h"

// NB : this is a work in progress
// TODO: define error return codes for parsing

class PiPoGraph : public PiPo
{
  typedef enum PiPoGraphTypeE
  {
    undefined = -1, leaf = 0, sequence, parallel
  } PiPoGraphType;

private:
  bool topLevel;
  std::string representation;
  PiPoGraphType graphType;

  // parallel graphs if graphType is parallel
  // sequence of graphs if graphType is sequence (not sequence of PiPos)
  // empty if graphType is leaf
  std::vector<PiPoGraph> subGraphs;

  // use op if we are a leaf to parse instanceName and to hold attributes
  PiPoOp op;

  PiPo *pipo;
  std::vector<std::string *> attrNames;
  std::vector<std::string *> attrDescrs;
  PiPoModuleFactory *moduleFactory;

public:
  PiPoGraph(PiPo::Parent *parent, PiPoModuleFactory *moduleFactory, bool topLevel = true) :
  PiPo(parent)
  {
    this->pipo = nullptr;
    this->parent = parent;
    this->moduleFactory = moduleFactory;
    this->topLevel = topLevel;
  }

  ~PiPoGraph()
  {
    if (this->topLevel)
    {
      for (unsigned int i = 0; i < attrNames.size(); ++i)
        delete attrNames[i];

      for (unsigned int i = 0; i < attrDescrs.size(); ++i)
        delete attrDescrs[i];
    }

    this->clear();
  }

  void clear()
  {
    for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
      this->subGraphs[i].clear();

    if (this->graphType != leaf && this->pipo != nullptr)
    {
      // delete parallels and sequences instantiated with new
      delete this->pipo;
      this->pipo = nullptr;
    }
    else
    {
      // let op clear itself
      this->op.clear();
    }
  }

  bool create(std::string graphStr) {
    if (parse(graphStr) && instantiate() && wire()) {
      copyPiPoAttributes();
      return true;
    }

    this->clear();
    return false;
  }

private:
  //======================== PARSE GRAPH EXPRESSION ==========================//

  bool parse(std::string graphStr) {

    //=================== BASIC SYNTAX RULES CHECKING ========================//

    //========== check if we have the right number of '<' and '>' ============//

    int cnt = 0;

    for (unsigned int i = 0; i < graphStr.length(); ++i)
      if (graphStr[i] == '<')
        cnt++;
      else if (graphStr[i] == '>')
        cnt--;

    if (cnt != 0)
      return false;

    // TODO : add more syntax checking here ?

    //======= determine the type of graph (leaf, sequence or parallel) =======//

    unsigned int trims = 0;
    while (graphStr[0] == '<' && graphStr[graphStr.length() - 1] == '>')
    {
      graphStr = graphStr.substr(1, graphStr.length() - 2);
      trims++;
    }

    this->representation = graphStr;

    // by default we are a sequence
    this->graphType = sequence;

    // if we have surrounding "<...>" and first-level commas, we are a parallel
    if (trims > 0)
    {
      int subLevelsCnt = 0;

      for (unsigned int i = 0; i < graphStr.length(); ++i)
        if (graphStr[i] == '<')
        {
          subLevelsCnt++;
        }
        else if (graphStr[i] == '>')
        {
          subLevelsCnt--;
        }
        else if (subLevelsCnt == 0 && graphStr[i] == ',')
        {
          this->graphType = parallel;
          break;
        }
    }

    // if we don't have any sequential / parallelism symbol,
    // and we are not a single top-level pipo
    // (because we need to be inside a PiPoGraph at the top-level),
    // then we are a leaf :
    if (graphStr.find("<") >= std::string::npos &&
        graphStr.find(">") >= std::string::npos &&
        graphStr.find(",") >= std::string::npos &&
        graphStr.find(":") >= std::string::npos &&
        !this->topLevel)
      this->graphType = leaf;

    //====== now fill (or not) subGraphs vector according to graph type ======//

    switch (this->graphType)
    {

      //========================== leaf graph, we are a single PiPo, trim spaces
      case leaf:
      {
        // trim spaces - TODO: trim tabs and other spaces ?
        this->representation.erase(
          std::remove(this->representation.begin(), this->representation.end(), ' '),
          this->representation.end()
        );

        size_t pos = 0;
        this->op.parse(this->representation.c_str(), pos);

        // leaf string representation cannot be empty
        return (graphStr.length() > 0);
      }
      break;

      //================= sequence of graphs, parse according to ':', <' and '>'
      case sequence:
      {
        int subLevelsCnt = 0;
        unsigned int lastStartIndex = 0;
        std::vector< std::pair<unsigned int, unsigned int> > subStrings;

        if (graphStr[0] == '<')
          subLevelsCnt++;

        for (unsigned int i = 1; i < graphStr.length(); ++i)
        {
          if (graphStr[i] == ':' && subLevelsCnt == 0)
          {
            subStrings.push_back(std::pair<unsigned int, unsigned int>(
              lastStartIndex, i - lastStartIndex
            ));
            lastStartIndex = i + 1;
          }
          else if (graphStr[i] == '<')
          {
            if (subLevelsCnt == 0)
            {
              subStrings.push_back(std::pair<unsigned int, unsigned int>(
                lastStartIndex, i - lastStartIndex
              ));
              lastStartIndex = i;
            }
            subLevelsCnt++;
          }
          else if (graphStr[i] == '>')
          {
            subLevelsCnt--;
            if (subLevelsCnt == 0)
            {
              subStrings.push_back(std::pair<unsigned int, unsigned int>(
                lastStartIndex, i - lastStartIndex + 1
              ));
              lastStartIndex = i + 1;
            }
          }
          else if (graphStr[i] == ',' && subLevelsCnt == 0)
            // cannot have first-level commas in a sequence
            return false;
        }

        if (graphStr[graphStr.length() - 1] != '>')
        {
          subStrings.push_back(std::pair<unsigned int, unsigned int>(
            lastStartIndex, graphStr.length() - lastStartIndex
          ));
        }

        for (unsigned int i = 0; i < subStrings.size(); ++i)
        {
          this->subGraphs.push_back(PiPoGraph(this->parent, this->moduleFactory, false));
          PiPoGraph &g = this->subGraphs[this->subGraphs.size() - 1];

          if (!g.parse(graphStr.substr(subStrings[i].first, subStrings[i].second)))
            return false;
        }

        return (subLevelsCnt == 0);
      }
      break;

      //============================= parallel graphs, parse according to commas
      case parallel:
      {
        int subLevelsCnt = 0;
        std::vector<unsigned int> commaIndices;

        commaIndices.push_back(0);

        for (unsigned int i = 0; i < graphStr.length(); ++i)
          if (graphStr[i] == '<')
            subLevelsCnt++;
          else if (graphStr[i] == '>')
            subLevelsCnt--;
          else if (graphStr[i] == ',' && i < graphStr.length() - 1 && subLevelsCnt == 0)
            commaIndices.push_back(i + 1);

        commaIndices.push_back(static_cast<unsigned int>(graphStr.length()) + 1);

        for (unsigned int i = 0; i < commaIndices.size() - 1; ++i)
        {
          this->subGraphs.push_back(PiPoGraph(this->parent, this->moduleFactory, false));
          PiPoGraph &g = this->subGraphs[this->subGraphs.size() - 1];
          unsigned int blockStart = commaIndices[i];
          unsigned int blockLength = commaIndices[i + 1] - blockStart - 1;

          if (!g.parse(graphStr.substr(blockStart, blockLength)))
            return false;
        }

        return (subLevelsCnt == 0);
      }
      break;

      //===================================================== this never happens
      default:
      break;
    }

    return false;
  }

  //================ ONCE EXPRESSION PARSED, INSTANTIATE OPs =================//

  bool instantiate()
  {
    if (this->graphType == leaf)
    {
      if (!this->op.instantiate(this->parent, this->moduleFactory))
        return false;
      else
        this->pipo = this->op.getPiPo();
    }
    else if (this->graphType == sequence)
    {
      for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
        if (!this->subGraphs[i].instantiate())
          return false;

      this->pipo = new PiPoSequence(this->parent);
    }
    else if (this->graphType == parallel)
    {
      for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
        if (!this->subGraphs[i].instantiate())
          return false;

      this->pipo = new PiPoParallel(this->parent);
    }

    return true;
  }

  bool wire()
  {

    for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
      this->subGraphs[i].wire();

    if (this->graphType == sequence)
        for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
          static_cast<PiPoSequence *>(this->pipo)->add(this->subGraphs[i].getPiPo());

    else if (this->graphType == parallel)
        for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
          static_cast<PiPoParallel *>(this->pipo)->add(this->subGraphs[i].getPiPo());

    return true;
  }

  // TODO: add an option to get PiPoAttributes only from named modules ?
  void copyPiPoAttributes()
  {
    for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
    {
      PiPoGraph &subGraph = this->subGraphs[i];

      subGraph.copyPiPoAttributes();
      PiPo *pipo = subGraph.getPiPo();
      unsigned int numAttrs = pipo->getNumAttrs();

      if (subGraph.getGraphType() == leaf)
      {
        std::string instanceName = subGraph.getInstanceName();

        for (unsigned int iAttr = 0; iAttr < numAttrs; ++iAttr)
        {
          PiPo::Attr *attr = pipo->getAttr(iAttr);

          std::string *attrName = new std::string(instanceName);
          *attrName += ".";
          *attrName += attr->getName();

          std::string *attrDescr = new std::string(attr->getDescr());
          *attrDescr += " (";
          *attrDescr += instanceName;
          *attrDescr += ")";

          attrNames.push_back(attrName);
          attrDescrs.push_back(attrDescr);

          PiPo *p = this->topLevel ? this : this->pipo;
          p->addAttr(p, attrNames[attrNames.size() - 1]->c_str(), attrDescrs[attrDescrs.size() - 1]->c_str(), attr);
        }
      }

      if (subGraph.getGraphType() == sequence || subGraph.getGraphType() == parallel)
      {
        attrNames.insert(attrNames.end(), subGraph.attrNames.begin(), subGraph.attrNames.end());
        attrDescrs.insert(attrDescrs.end(), subGraph.attrDescrs.begin(), subGraph.attrDescrs.end());

        for (unsigned int iAttr = 0; iAttr < numAttrs; ++iAttr)
        {
          PiPo::Attr *attr = pipo->getAttr(iAttr);
          PiPo *p = this->topLevel ? this : this->pipo;
          p->addAttr(p, subGraph.attrNames[iAttr]->c_str(), subGraph.attrDescrs[iAttr]->c_str(), attr);
        }
      }
    }
  }

  std::string getInstanceName()
  {
    return (this->graphType == leaf) ? std::string(this->op.getInstanceName()) : "";
  }

  PiPoGraphType getGraphType()
  {
    return this->graphType;
  }

public:
  PiPo *getPiPo()
  {
    return this->pipo;
  }

  //=============== OVERRIDING ALL METHODS FROM THE BASE CLASS ===============//

  void setParent(PiPo::Parent *parent) override
  {
    this->pipo->setParent(parent);

    for (unsigned int i = 0; i < this->subGraphs.size(); ++i)
      this->subGraphs[i].setParent(parent);
  }

  PiPo *getReceiver(unsigned int index = 0) override
  {
    return this->pipo->getReceiver(index);
  }

  void setReceiver(PiPo *receiver, bool add = false) override
  {
    this->pipo->setReceiver(receiver);
  }

  int reset() override
  {
    return this->pipo->reset();
  }

  int segment(double time, bool start) override
  {
    return this->pipo->segment(time, start);
  }

  int finalize(double inputEnd) override
  {
    return this->pipo->finalize(inputEnd);
  }

  int streamAttributes(bool hasTimeTags, double rate, double offset,
                               unsigned int width, unsigned int height,
                               const char **labels, bool hasVarSize,
                               double domain, unsigned int maxFrames) override
  {
    return this->pipo->streamAttributes(hasTimeTags, rate, offset,
                                           width, height, labels, hasVarSize,
                                           domain, maxFrames);
  }

  int frames (double time, double weight, PiPoValue *values,
                      unsigned int size, unsigned int num) override
  {
    return this->pipo->frames(time, weight, values, size, num);
  }

  // void print() {
  //   std::cout << this->representation << " " << this->graphType << std::endl;
  //   for (unsigned int i = 0; i < this->subGraphs.size(); ++i) {
  //     std::cout << " ";
  //     this->subGraphs[i].print();
  //   }
  // }
};

#endif /* _PIPO_GRAPH_ */
