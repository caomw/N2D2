/*
    (C) Copyright 2014 CEA LIST. All Rights Reserved.
    Contributor(s): Olivier BICHLER (olivier.bichler@cea.fr)

    This software is governed by the CeCILL-C license under French law and
    abiding by the rules of distribution of free software.  You can  use,
    modify and/ or redistribute the software under the terms of the CeCILL-C
    license as circulated by CEA, CNRS and INRIA at the following URL
    "http://www.cecill.info".

    As a counterpart to the access to the source code and  rights to copy,
    modify and redistribute granted by the license, users are provided only
    with a limited warranty  and the software's author,  the holder of the
    economic rights,  and the successive licensors  have only  limited
    liability.

    The fact that you are presently reading this means that you have had
    knowledge of the CeCILL-C license and that you accept its terms.
*/

#include "Cell/FcCell.hpp"

const char* N2D2::FcCell::Type = "Fc";

N2D2::FcCell::FcCell(const std::string& name, unsigned int nbOutputs)
    : Cell(name, nbOutputs),
      mNoBias(this, "NoBias", true),
      mBackPropagate(this, "BackPropagate", true),
      mWeightsFiller(new NormalFiller<Float_T>(0.0, 0.05)),
      mBiasFiller(new NormalFiller<Float_T>(0.0, 0.05))
{
    // ctor
}

void N2D2::FcCell::logFreeParameters(const std::string& fileName,
                                     unsigned int output) const
{
    if (output >= mNbOutputs)
        throw std::domain_error(
            "FcCell::logFreeParameters(): output not within range.");

    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    Tensor3d<Float_T> weights(1, 1, channelsSize);

    for (unsigned int channel = 0; channel < channelsSize; ++channel)
        weights(channel) = getWeight(output, channel);

    StimuliProvider::logData(fileName, weights);
}

void N2D2::FcCell::logFreeParameters(const std::string& dirName) const
{
    Utils::createDirectories(dirName);

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        std::ostringstream fileName;
        fileName << dirName << "/cell-" << output << ".dat";

        logFreeParameters(fileName.str(), output);
    }
}

// TODO: handle mMaps
unsigned long long int N2D2::FcCell::getNbSynapses() const
{
    return mNbOutputs * (getNbChannels() * getChannelsWidth()
                         * getChannelsHeight() + !mNoBias);
}

void N2D2::FcCell::exportFreeParameters(const std::string& fileName) const
{
    std::ofstream syn(fileName.c_str());

    if (!syn.good())
        throw std::runtime_error("Could not create synaptic file: " + fileName);

    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel)
            syn << getWeight(output, channel) << " ";

        if (!mNoBias)
            syn << getBias(output) << " ";

        syn << "\n";
    }
}

void N2D2::FcCell::importFreeParameters(const std::string& fileName,
                                        bool ignoreNotExists)
{
    std::ifstream syn(fileName.c_str());

    if (!syn.good()) {
        if (ignoreNotExists) {
            std::cout << Utils::cnotice
                      << "Notice: Could not open synaptic file: " << fileName
                      << Utils::cdef << std::endl;
            return;
        } else
            throw std::runtime_error("Could not open synaptic file: "
                                     + fileName);
    }

    double weight;

    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel) {
            if (!(syn >> weight))
                throw std::runtime_error("Error while reading synaptic file: "
                                         + fileName);

            setWeight(output, channel, weight);
        }

        if (!mNoBias) {
            if (!(syn >> weight))
                throw std::runtime_error("Error while reading synaptic file: "
                                         + fileName);

            setBias(output, weight);
        }
    }

    // Discard trailing whitespaces (if not binary)
    while (std::isspace(syn.peek()))
        syn.ignore();

    if (syn.get() != std::fstream::traits_type::eof())
        throw std::runtime_error("Synaptic file size larger than expected: "
                                 + fileName);
}

void N2D2::FcCell::logFreeParametersDistrib(const std::string& fileName) const
{
    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    // Append all weights
    std::vector<double> weights;
    weights.reserve(mNbOutputs * channelsSize);

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel)
            weights.push_back(getWeight(output, channel));

        if (!mNoBias)
            weights.push_back(getBias(output));
    }

    std::sort(weights.begin(), weights.end());

    // Write data file
    std::ofstream data(fileName.c_str());

    if (!data.good())
        throw std::runtime_error("Could not save weights distrib file.");

    std::copy(weights.begin(),
              weights.end(),
              std::ostream_iterator<double>(data, "\n"));
    data.close();

    const std::pair<double, double> meanStdDev = Utils::meanStdDev(weights);

    std::ostringstream label;
    label << "\"Average: " << meanStdDev.first << "\\n";
    label << "Std. dev.: " << meanStdDev.second << "\"";
    label << " at graph 0.7, graph 0.8 front";

    // Plot results
    Gnuplot gnuplot;
    gnuplot.set("grid front").set("key off");
    gnuplot << "binwidth=0.01";
    gnuplot << "bin(x,width)=width*floor(x/width+0.5)";
    gnuplot.set("boxwidth", "binwidth");
    gnuplot.set("style data boxes").set("style fill solid noborder");
    gnuplot.set("xtics", "0.2");
    gnuplot.set("mxtics", "2");
    gnuplot.set("grid", "mxtics");
    gnuplot.set("label", label.str());
    gnuplot.set("yrange", "[0:]");

    gnuplot.set("style rect fc lt -1 fs solid 0.15 noborder behind");
    gnuplot.set("obj rect from graph 0, graph 0 to -1, graph 1");
    gnuplot.set("obj rect from 1, graph 0 to graph 1, graph 1");

    const double minVal = (weights.front() < -1.0) ? weights.front() : -1.0;
    const double maxVal = (weights.back() > 1.0) ? weights.back() : 1.0;
    gnuplot.setXrange(minVal - 0.05, maxVal + 0.05);

    gnuplot.saveToFile(fileName);
    gnuplot.plot(fileName,
                 "using (bin($1,binwidth)):(1.0) smooth freq with boxes");
}

// TODO
void N2D2::FcCell::writeMap(const std::string& fileName) const
{
    std::ofstream data(fileName.c_str());

    if (!data.good())
        throw std::runtime_error("Could not save map file.");
    /*
        std::string cellName = "";
        unsigned int startChannel = 0;

        for (unsigned int channel = 0; channel <
       getNbChannels()*getChannelsWidth()*getChannelsHeight(); ++channel) {
            NodeOut* const parent =
       dynamic_cast<NodeOut*>(mInputs[channel]->getParent());
            const std::string parentName = (parent != NULL) ?
       parent->getCell().getName() : "";

            if (parentName != cellName || channel == nbChannels - 1) {
                if (channel > 0) {
                    if (channel == nbChannels - 1)
                        data << startChannel << " => " << channel << " (" <<
       (channel - startChannel + 1) << ")";
                    else
                        data << startChannel << " => " << (channel - 1) << " ("
       << (channel - startChannel) << ")";

                    if (!cellName.empty())
                        data << " # " << cellName << "\n";
                    else
                        data << "\n";
                }

                cellName = parentName;
                startChannel = channel;
            }
        }
    */
}

void N2D2::FcCell::discretizeFreeParameters(unsigned int nbLevels)
{
    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

#pragma omp parallel for if (mNbOutputs > 32)
    for (int output = 0; output < (int)mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel) {
            double weight = getWeight(output, channel);
            weight = Utils::round((nbLevels - 1) * weight) / (nbLevels - 1);

            setWeight(output, channel, weight);
        }

        if (!mNoBias) {
            double bias = getBias(output);
            bias = Utils::round((nbLevels - 1) * bias) / (nbLevels - 1);

            setBias(output, bias);
        }
    }
}

void N2D2::FcCell::randomizeFreeParameters(double stdDev)
{
    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel) {
            double weight = getWeight(output, channel);
            weight
                = Utils::clamp(Random::randNormal(weight, stdDev), -1.0, 1.0);

            setWeight(output, channel, weight);
        }

        if (!mNoBias) {
            double bias = getBias(output);
            bias = Utils::clamp(Random::randNormal(bias, stdDev), -1.0, 1.0);

            setBias(output, bias);
        }
    }
}

void N2D2::FcCell::processFreeParameters(const std::function
                                         <double(const double&)>& func)
{
    const unsigned int channelsSize = getNbChannels() * getChannelsWidth()
                                      * getChannelsHeight();

    for (unsigned int output = 0; output < mNbOutputs; ++output) {
        for (unsigned int channel = 0; channel < channelsSize; ++channel) {
            double weight = getWeight(output, channel);
            weight = func(weight);

            setWeight(output, channel, weight);
        }

        if (!mNoBias) {
            double bias = getBias(output);
            bias = func(bias);

            setBias(output, bias);
        }
    }
}

void N2D2::FcCell::getStats(Stats& stats) const
{
    const unsigned int nbSynapses = getNbSynapses();

    stats.nbNeurons += getNbOutputs() * getOutputsWidth() * getOutputsHeight();
    stats.nbNodes += getNbOutputs() * getOutputsWidth() * getOutputsHeight();
    stats.nbSynapses += nbSynapses;
    stats.nbVirtualSynapses += nbSynapses;
    stats.nbConnections += nbSynapses;
}
