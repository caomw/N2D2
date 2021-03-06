/*
    (C) Copyright 2015 CEA LIST. All Rights Reserved.
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

#include "Cell/ROIPoolingCell_Frame.hpp"

N2D2::Registrar<N2D2::ROIPoolingCell>
N2D2::ROIPoolingCell_Frame::mRegistrar("Frame",
                                       N2D2::ROIPoolingCell_Frame::create);

N2D2::ROIPoolingCell_Frame::ROIPoolingCell_Frame(const std::string& name,
                                                 StimuliProvider& sp,
                                                 unsigned int outputsWidth,
                                                 unsigned int outputsHeight,
                                                 unsigned int nbOutputs,
                                                 ROIPooling pooling)
    : Cell(name, nbOutputs),
      ROIPoolingCell(name, sp, outputsWidth, outputsHeight, nbOutputs, pooling),
      Cell_Frame(name, nbOutputs)
{
    // ctor
    mInputs.matchingDimB(false);
    mDiffOutputs.matchingDimB(false);
}

void N2D2::ROIPoolingCell_Frame::initialize()
{
    if (mInputs.size() < 2) {
        throw std::runtime_error("At least two inputs are required for"
                                 " ROIPoolingCell " + mName);
    }

    if (mInputs[0].dimX() * mInputs[0].dimY() * mInputs[0].dimZ() != 4) {
        throw std::runtime_error("The first input must have a XYZ size of 4 for"
                                 " ROIPoolingCell " + mName);
    }

    const unsigned int inputBatch = mInputs[1].dimB();

    for (unsigned int k = 1, size = mInputs.size(); k < size; ++k) {
        if (mInputs[k].size() == 0) {
            throw std::runtime_error("Zero-sized input for ROIPoolingCell "
                                      + mName);
        }

        if (mInputs[k].dimB() != inputBatch) {
            throw std::runtime_error("Input batch size must match for"
                                     " ROIPoolingCell" + mName);
        }

        if (mArgMax.size() == (k - 1)) {
            mArgMax.push_back(new Tensor4d<PoolCell_Frame_Kernels::ArgMax>(
                mOutputs.dimX(),
                mOutputs.dimY(),
                mOutputs.dimZ(),
                mOutputs.dimB()));
        }
    }
}

void N2D2::ROIPoolingCell_Frame::propagate(bool /*inference*/)
{
    mInputs.synchronizeDToH();

    const float alpha = 1.0f;
    float beta = 0.0f;

    const Tensor4d<Float_T>& proposals = mInputs[0];

    for (unsigned int k = 1, size = mInputs.size(); k < size; ++k) {
        if (k > 1)
            beta = 1.0;

        if (mPooling == Max) {
#if defined(_OPENMP) && _OPENMP >= 200805
#pragma omp parallel for collapse(2) if (size > 16)
#else
#pragma omp parallel for if (mOutputs.dimB() > 4 && size > 16)
#endif
            for (int batchPos = 0; batchPos < (int)mOutputs.dimB(); ++batchPos)
            {
                for (unsigned int output = 0; output < mOutputs.dimZ();
                    ++output)
                {
                    const unsigned int inputBatch = batchPos / proposals.dimB();
                    const double xRatio = mStimuliProvider.getSizeX()
                        / (double)mInputs[k].dimX();
                    const double yRatio = mStimuliProvider.getSizeY()
                        / (double)mInputs[k].dimY();

                    Float_T x = proposals(0, batchPos) / xRatio;
                    Float_T y = proposals(1, batchPos) / yRatio;
                    Float_T w = proposals(2, batchPos) / xRatio;
                    Float_T h = proposals(3, batchPos) / yRatio;

                    assert(w >= 0);
                    assert(h >= 0);

                    // Crop ROI to image boundaries
                    if (x < 0) {
                        w+= x;
                        x = 0;
                    }
                    if (y < 0) {
                        h+= y;
                        y = 0;
                    }
                    if (x + w > (int)mInputs[k].dimX())
                        w = mInputs[k].dimX() - x;
                    if (y + h > (int)mInputs[k].dimY())
                        h = mInputs[k].dimY() - y;

                    const Float_T poolWidth = w / mOutputs.dimX();
                    const Float_T poolHeight = h / mOutputs.dimY();

                    assert(poolWidth >= 0);
                    assert(poolHeight >= 0);

                    for (unsigned int oy = 0; oy < mOutputs.dimY(); ++oy) {
                        for (unsigned int ox = 0; ox < mOutputs.dimX(); ++ox) {
                            const unsigned int sxMin = (unsigned int)(x
                                                    + ox * poolWidth);
                            const unsigned int sxMax = (unsigned int)(x
                                                    + (ox + 1) * poolWidth);
                            const unsigned int syMin = (unsigned int)(y
                                                    + oy * poolHeight);
                            const unsigned int syMax = (unsigned int)(y
                                                    + (oy + 1) * poolHeight);

                            // For each output, compute the pool value
                            Float_T poolValue = 0.0;

                            unsigned int ixMax = 0;
                            unsigned int iyMax = 0;
                            bool valid = false;

                            for (unsigned int sy = syMin; sy < syMax; ++sy) {
                                for (unsigned int sx = sxMin; sx < sxMax; ++sx)
                                {
                                    const Float_T value = mInputs[k](sx,
                                                            sy,
                                                            output,
                                                            inputBatch);

                                    if (!valid || value > poolValue) {
                                        poolValue = value;
                                        valid = true;

                                        ixMax = sx;
                                        iyMax = sy;
                                    }
                                }
                            }

                            mArgMax[k-1](ox, oy, output, batchPos)
                                = PoolCell_Frame_Kernels::ArgMax(ixMax,
                                                                 iyMax,
                                                                 output,
                                                                 valid);

                            mOutputs(ox, oy, output, batchPos)
                                = alpha * poolValue
                                  + beta * mOutputs(ox, oy, output, batchPos);
                        }
                    }
                }
            }
        }
        else {
#if defined(_OPENMP) && _OPENMP >= 200805
#pragma omp parallel for collapse(2) if (size > 16)
#else
#pragma omp parallel for if (mOutputs.dimB() > 4 && size > 16)
#endif
            for (int batchPos = 0; batchPos < (int)mOutputs.dimB(); ++batchPos)
            {
                for (unsigned int output = 0; output < mOutputs.dimZ();
                    ++output)
                {
                    const unsigned int inputBatch = batchPos / proposals.dimB();
                    const double xRatio = mStimuliProvider.getSizeX()
                        / (double)mInputs[k].dimX();
                    const double yRatio = mStimuliProvider.getSizeY()
                        / (double)mInputs[k].dimY();

                    Float_T x = proposals(0, batchPos) / xRatio;
                    Float_T y = proposals(1, batchPos) / yRatio;
                    Float_T w = proposals(2, batchPos) / xRatio;
                    Float_T h = proposals(3, batchPos) / yRatio;

                    // Crop ROI to image boundaries
                    if (x < 0) {
                        w+= x;
                        x = 0;
                    }
                    if (y < 0) {
                        h+= y;
                        y = 0;
                    }
                    if (x + w > (int)mInputs[k].dimX())
                        w = mInputs[k].dimX() - x;
                    if (y + h > (int)mInputs[k].dimY())
                        h = mInputs[k].dimY() - y;

                    const Float_T poolWidth = w / mOutputs.dimX();
                    const Float_T poolHeight = h / mOutputs.dimY();

                    for (unsigned int oy = 0; oy < mOutputs.dimY(); ++oy) {
                        for (unsigned int ox = 0; ox < mOutputs.dimX(); ++ox) {
                            const unsigned int sxMin = (unsigned int)(x
                                                    + ox * poolWidth);
                            const unsigned int sxMax = (unsigned int)(x
                                                    + (ox + 1) * poolWidth);
                            const unsigned int syMin = (unsigned int)(y
                                                    + oy * poolHeight);
                            const unsigned int syMax = (unsigned int)(y
                                                    + (oy + 1) * poolHeight);

                            // For each output, compute the pool value
                            Float_T poolValue = 0.0;
                            unsigned int poolCount = 0;

                            for (unsigned int sy = syMin; sy < syMax; ++sy) {
                                for (unsigned int sx = sxMin; sx < sxMax; ++sx)
                                {
                                    poolValue += mInputs[k](sx,
                                                            sy,
                                                            output,
                                                            inputBatch);
                                }
                            }

                            poolCount += (sxMax - sxMin)*(syMax - syMin);

                            mOutputs(ox, oy, output, batchPos)
                                = alpha * ((poolCount > 0) ?
                                              (poolValue / poolCount) : 0.0)
                                  + beta * mOutputs(ox, oy, output, batchPos);
                        }
                    }
                }
            }
        }
    }

    Cell_Frame::propagate();
    mDiffInputs.clearValid();
}

void N2D2::ROIPoolingCell_Frame::backPropagate()
{
    if (mDiffOutputs.empty())
        return;

    Cell_Frame::backPropagate();

    const Float_T alpha = 1.0;

    const Tensor4d<Float_T>& proposals = mInputs[0];

    for (unsigned int k = 1, size = mInputs.size(); k < size; ++k) {
        //const Float_T beta = (mDiffOutputs[k].isValid()) ? 1.0 : 0.0;

        if (!mDiffOutputs[k].isValid()) {
            mDiffOutputs[k].fill(0.0);
            mDiffOutputs[k].setValid();
        }

        if (mPooling == Max) {
#if defined(_OPENMP) && _OPENMP >= 200805
#pragma omp parallel for collapse(2) if (size > 16)
#else
#pragma omp parallel for if (mDiffInputs.dimB() > 4 && size > 16)
#endif
            for (int batchPos = 0; batchPos < (int)mDiffInputs.dimB();
                ++batchPos)
            {
                for (unsigned int channel = 0; channel < mDiffOutputs[k].dimZ();
                     ++channel)
                {
                    const unsigned int inputBatch = batchPos / proposals.dimB();
                    //const Float_T betaBatch = (batchPos % proposals.dimB() == 0)
                    //                            ? beta : 1.0;

                    const double xRatio = mStimuliProvider.getSizeX()
                        / (double)mInputs[k].dimX();
                    const double yRatio = mStimuliProvider.getSizeY()
                        / (double)mInputs[k].dimY();

                    Float_T x = proposals(0, batchPos) / xRatio;
                    Float_T y = proposals(1, batchPos) / yRatio;
                    Float_T w = proposals(2, batchPos) / xRatio;
                    Float_T h = proposals(3, batchPos) / yRatio;

                    // Crop ROI to image boundaries
                    if (x < 0) {
                        w+= x;
                        x = 0;
                    }
                    if (y < 0) {
                        h+= y;
                        y = 0;
                    }
                    if (x + w > (int)mInputs[k].dimX())
                        w = mInputs[k].dimX() - x;
                    if (y + h > (int)mInputs[k].dimY())
                        h = mInputs[k].dimY() - y;

                    const Float_T poolWidth = w / mOutputs.dimX();
                    const Float_T poolHeight = h / mOutputs.dimY();

                    const unsigned int ixMin = (unsigned int)(x);
                    const unsigned int ixMax = (unsigned int)(x + w);
                    const unsigned int iyMin = (unsigned int)(y);
                    const unsigned int iyMax = (unsigned int)(y + h);

                    assert(ixMin <= ixMax);
                    assert(ixMax < mDiffOutputs[k].dimX());
                    assert(iyMin <= iyMax);
                    assert(iyMax < mDiffOutputs[k].dimY());

                    for (unsigned int iy = 0; iy < mDiffOutputs[k].dimY(); ++iy)
                    {
                        for (unsigned int ix = 0; ix < mDiffOutputs[k].dimX();
                            ++ix)
                        {
                            if (ix >= ixMin && ix < ixMax
                                && iy >= iyMin && iy < iyMax)
                            {
                                const unsigned int ox = (unsigned int)
                                    ((ix - ixMin + 0.5) / poolWidth);
                                const unsigned int oy = (unsigned int)
                                    ((iy - iyMin + 0.5) / poolHeight);

                                assert(ox < mOutputs.dimX());
                                assert(oy < mOutputs.dimY());

                                const PoolCell_Frame_Kernels::ArgMax inputMax
                                    = mArgMax[k-1](ox, oy, channel, batchPos);

                                if (ix == inputMax.ix
                                    && iy == inputMax.iy
                                    && inputMax.valid)
                                {
                                    #pragma omp atomic
                                    mDiffOutputs[k](ix, iy, channel, inputBatch)
                                        += alpha * mDiffInputs(ox,
                                                               oy,
                                                               channel,
                                                               batchPos);
                                }
                            }

                            //mDiffOutputs[k](ix, iy, channel, inputBatch)
                            //    = alpha * poolGradient
                            //      + betaBatch * mDiffOutputs[k](ix, iy, channel,
                            //                               inputBatch);
                        }
                    }
                }
            }
        }
        else {
            throw std::runtime_error("ROIPoolingCell_Frame::backPropagate(): "
                                     "Average pooling back-propagation not "
                                     "implemented");
        }

        //mDiffOutputs[k].setValid();
    }

    mDiffOutputs.synchronizeHToD();
}

void N2D2::ROIPoolingCell_Frame::update()
{
    // Nothing to update
}

void N2D2::ROIPoolingCell_Frame::checkGradient(double epsilon, double maxError)
{
    GradientCheck gc(epsilon, maxError);

    mInputs[0].setValid();
    gc.initialize(mInputs,
                  mOutputs,
                  mDiffInputs,
                  std::bind(&ROIPoolingCell_Frame::propagate, this, false),
                  std::bind(&ROIPoolingCell_Frame::backPropagate, this),
                  (mPooling == Max));
    mInputs[0].clearValid();

    if (!mDiffOutputs.empty()) {
        for (unsigned int in = 1; in < mInputs.size(); ++in) {
            std::stringstream name;
            name << mName + "_mDiffOutputs[" << in << "]";

            gc.check(name.str(), mInputs[in], mDiffOutputs[in]);
        }
    } else {
        std::cout << Utils::cwarning << "Empty diff. outputs for cell " << mName
                  << ", could not check the gradient!" << Utils::cdef
                  << std::endl;
    }
}

N2D2::ROIPoolingCell_Frame::~ROIPoolingCell_Frame()
{
    for (unsigned int k = 0, size = mArgMax.size(); k < size; ++k)
        delete &mArgMax[k];
}
