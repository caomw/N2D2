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

#ifdef CUDA

#include "Cell/ROIPoolingCell_Frame_CUDA.hpp"

N2D2::Registrar<N2D2::ROIPoolingCell>
N2D2::ROIPoolingCell_Frame_CUDA::mRegistrar("Frame_CUDA",
                                       N2D2::ROIPoolingCell_Frame_CUDA::create);

N2D2::ROIPoolingCell_Frame_CUDA::ROIPoolingCell_Frame_CUDA(const std::string& name,
                                                 StimuliProvider& sp,
                                                 unsigned int outputsWidth,
                                                 unsigned int outputsHeight,
                                                 unsigned int nbOutputs,
                                                 ROIPooling pooling)
    : Cell(name, nbOutputs),
      ROIPoolingCell(name, sp, outputsWidth, outputsHeight, nbOutputs, pooling),
      Cell_Frame_CUDA(name, nbOutputs)
{
    // ctor
    mInputs.matchingDimB(false);
    mDiffOutputs.matchingDimB(false);
}

void N2D2::ROIPoolingCell_Frame_CUDA::initialize()
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
            mArgMax.push_back(new CudaTensor4d<PoolCell_Frame_Kernels::ArgMax>(
                mOutputs.dimX(),
                mOutputs.dimY(),
                mOutputs.dimZ(),
                mOutputs.dimB()));
        }
    }
}

void N2D2::ROIPoolingCell_Frame_CUDA::propagate(bool /*inference*/)
{
    mInputs.synchronizeHBasedToD();

    const float alpha = 1.0f;
    float beta = 0.0f;

    for (unsigned int k = 1, size = mInputs.size(); k < size; ++k) {
        if (k > 1)
            beta = 1.0;

        if (mPooling == Max) {
            cudaSROIPoolingForwardMax(alpha,
                                      mInputs[0].getDevicePtr(),
                                      mInputs[0].dimB(),
                                      mStimuliProvider.getSizeY(),
                                      mStimuliProvider.getSizeX(),
                                      mInputs[k].getDevicePtr(),
                                      mInputs[k].dimZ(),
                                      mInputs[k].dimY(),
                                      mInputs[k].dimX(),
                                      mOutputs.dimB(),
                                      beta,
                                      mOutputs.getDevicePtr(),
                                      mOutputs.dimZ(),
                                      mOutputs.dimY(),
                                      mOutputs.dimX(),
                                      mArgMax[k-1].getDevicePtr());
        }
        else {
            cudaSROIPoolingForwardAverage(alpha,
                                          mInputs[0].getDevicePtr(),
                                          mInputs[0].dimB(),
                                          mStimuliProvider.getSizeY(),
                                          mStimuliProvider.getSizeX(),
                                          mInputs[k].getDevicePtr(),
                                          mInputs[k].dimZ(),
                                          mInputs[k].dimY(),
                                          mInputs[k].dimX(),
                                          mOutputs.dimB(),
                                          beta,
                                          mOutputs.getDevicePtr(),
                                          mOutputs.dimZ(),
                                          mOutputs.dimY(),
                                          mOutputs.dimX());
        }
    }

    Cell_Frame_CUDA::propagate();
    mDiffInputs.clearValid();
}

void N2D2::ROIPoolingCell_Frame_CUDA::backPropagate()
{
    if (mDiffOutputs.empty())
        return;

    Cell_Frame_CUDA::backPropagate();

    const Float_T alpha = 1.0;
    const Float_T beta = 1.0;

    for (unsigned int k = 1, size = mInputs.size(); k < size; ++k) {
        if (!mDiffOutputs[k].isValid()) {
            mDiffOutputs[k].fill(0.0);
            mDiffOutputs[k].synchronizeHToD();
            mDiffOutputs[k].setValid();
        }

        if (mPooling == Max) {
            cudaSROIPoolingBackwardMax(alpha,
                                       mInputs[0].getDevicePtr(),
                                       mInputs[0].dimB(),
                                       mStimuliProvider.getSizeY(),
                                       mStimuliProvider.getSizeX(),
                                       mDiffInputs.getDevicePtr(),
                                       mDiffInputs.dimZ(),
                                       mDiffInputs.dimY(),
                                       mDiffInputs.dimX(),
                                       mOutputs.dimB(),
                                       beta,
                                       mDiffOutputs[k].getDevicePtr(),
                                       mDiffOutputs[k].dimZ(),
                                       mDiffOutputs[k].dimY(),
                                       mDiffOutputs[k].dimX(),
                                       mArgMax[k-1].getDevicePtr());
        }
        else {
            cudaSROIPoolingBackwardAverage(alpha,
                                           mInputs[0].getDevicePtr(),
                                           mInputs[0].dimB(),
                                           mStimuliProvider.getSizeY(),
                                           mStimuliProvider.getSizeX(),
                                           mDiffInputs.getDevicePtr(),
                                           mDiffInputs.dimZ(),
                                           mDiffInputs.dimY(),
                                           mDiffInputs.dimX(),
                                           mOutputs.dimB(),
                                           beta,
                                           mDiffOutputs[k].getDevicePtr(),
                                           mDiffOutputs[k].dimZ(),
                                           mDiffOutputs[k].dimY(),
                                           mDiffOutputs[k].dimX());
        }
    }

    mDiffOutputs.synchronizeDToHBased();
}

void N2D2::ROIPoolingCell_Frame_CUDA::update()
{
    // Nothing to update
}

void N2D2::ROIPoolingCell_Frame_CUDA::checkGradient(double epsilon,
                                                    double maxError)
{
    GradientCheck gc(epsilon, maxError);

    mInputs[0].setValid();
    gc.initialize(mInputs,
                  mOutputs,
                  mDiffInputs,
                  std::bind(&ROIPoolingCell_Frame_CUDA::propagate, this, false),
                  std::bind(&ROIPoolingCell_Frame_CUDA::backPropagate, this),
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

N2D2::ROIPoolingCell_Frame_CUDA::~ROIPoolingCell_Frame_CUDA()
{
    for (unsigned int k = 0, size = mArgMax.size(); k < size; ++k)
        delete &mArgMax[k];
}

#endif
