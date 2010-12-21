/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 beta 4      *
*                (c) 2006-2009 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Modules                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef FRAME_COROTATIONALFORCEFIELD_INL
#define FRAME_COROTATIONALFORCEFIELD_INL


#include <sofa/core/behavior/ForceField.inl>
#include "CorotationalForceField.h"
#include "DeformationGradientTypes.h"
#include <sofa/core/objectmodel/BaseContext.h>
#include <sofa/core/behavior/ForceField.inl>
/*
NB fjourdes: I don t get why this include is required to stop
warning C4661 occurences while compiling CorotationalForceField.cpp
*/
#include <sofa/core/Mapping.inl>
/*
*/
#include <iostream>
using std::cerr;
using std::endl;

namespace sofa
{

namespace component
{

namespace forcefield
{


using namespace helper;


template <class DataTypes>
CorotationalForceField<DataTypes>::CorotationalForceField(core::behavior::MechanicalState<DataTypes> *mm )
    : Inherit1(mm)
{}

template <class DataTypes>
CorotationalForceField<DataTypes>::~CorotationalForceField()
{}


template <class DataTypes>
void CorotationalForceField<DataTypes>::init()
{
    Inherit1::init();
    core::objectmodel::BaseContext* context = this->getContext();
    sampleData = context->get<SampleData>();
    if( sampleData==NULL )
    {
        cerr<<"CorotationalForceField<DataTypes>::init(), material not found"<< endl;
    }



    material = context->get<Material>();
    if( material==NULL )
    {
        cerr<<"CorotationalForceField<DataTypes>::init(), material not found"<< endl;
    }


    ReadAccessor<Data<VecCoord> > out (*this->getMState()->read(core::ConstVecCoordId::restPosition()));

    this->integFactors.resize(out.size());


    for(unsigned int i=0; i<out.size(); i++) // treat each sample
    {
        typename DataTypes::SpatialCoord point;
        DataTypes::get(point[0],point[1],point[2], out[i]) ;
        if(material)
        {
            vector<Real> moments;
            material->computeVolumeIntegrationFactors(i,point,StrainType::strain_order,moments);  // lumpMoments
            for(unsigned int j=0; j<moments.size() && j<this->integFactors[i].size() ; j++)
                this->integFactors[i][j]=moments[j];
        }
        else this->integFactors[i][0]=1; // default value for the volume when model vertices are used as gauss points
    }

    //for(unsigned int i=0;i<out.size();i++) std::cout<<"IntegVector["<<i<<"]="<<integFactors[i]<<std::endl;
}


template <class DataTypes>
void CorotationalForceField<DataTypes>::addForce(DataVecDeriv& _f , const DataVecCoord& _x , const DataVecDeriv& _v , const core::MechanicalParams* /*mparams*/)
{
    ReadAccessor<DataVecCoord> x(_x);
    ReadAccessor<DataVecDeriv> v(_v);
    WriteAccessor<DataVecDeriv> f(_f);
    ReadAccessor<Data<VecMaterialCoord> > out (sampleData->f_materialPoints);
    stressStrainMatrices.resize(x.size());
    rotation.resize(x.size());
    strain.resize(x.size());
    strainRate.resize(x.size());
    stress.resize(x.size());

    // compute strains and strain rates
    for(unsigned i=0; i<x.size(); i++)
    {
        StrainType::apply(x[i], strain[i],&rotation[i]);
        StrainType::mult(v[i], strainRate[i],&rotation[i]);
        if( this->f_printLog.getValue() )
        {
            cerr<<"CorotationalForceField<DataTypes>::addForce, deformation gradient = " << x[i] << endl;
            cerr<<"CorotationalForceField<DataTypes>::addForce, strain = " << strain[i] << endl;
        }
    }
    material->computeStress( stress, &stressStrainMatrices, strain, strainRate, out.ref() );

    // integrate and compute force
    for(unsigned i=0; i<x.size(); i++)
    {
        StrainType::addMultTranspose(f[i], x[i], stress[i], this->integFactors[i], &rotation[i]);
        if( this->f_printLog.getValue() )
        {
            cerr<<"CorotationalForceField<DataTypes>::addForce, stress = " << stress[i] << endl;
            cerr<<"CorotationalForceField<DataTypes>::addForce, stress deformation gradient form= " << f[i] << endl;
        }
    }
}

template <class DataTypes>
void CorotationalForceField<DataTypes>::addDForce(DataVecDeriv& _df , const DataVecDeriv&  _dx , const core::MechanicalParams* mparams)
{
    ReadAccessor<DataVecDeriv> dx(_dx);
    WriteAccessor<DataVecDeriv> df(_df);
    strainChange.resize(dx.size());
    stressChange.resize(dx.size());
    ReadAccessor<Data<VecMaterialCoord> > out (sampleData->f_materialPoints);

    // compute strains changes
    for(unsigned i=0; i<dx.size(); i++)
    {
        StrainType::mult(dx[i], strainRate[i],&rotation[i]);
        if( this->f_printLog.getValue() )
        {
            cerr<<"CorotationalForceField<DataTypes>::addDForce, deformation gradient change = " << dx[i] << endl;
            cerr<<"CorotationalForceField<DataTypes>::addDForce, strain change = " << strainRate[i] << endl;
            cerr<<"CorotationalForceField<DataTypes>::addDForce, stress deformation gradient change before accumulating = " << df[i] << endl;
        }
    }

    // compute stress changes
    material->computeStressChange( stressChange, strainRate, out.ref() );

    // apply factor
    Real kFactor = (Real)mparams->kFactor();
    for(unsigned i=0; i<dx.size(); i++)
    {
        if( this->f_printLog.getValue() )
        {
            cerr<<"CorotationalForceField<DataTypes>::addDForce, stress change = " << stressChange[i] << endl;
        }
        StrainType::mult(stressChange[i], kFactor);
    }

    // integrate and compute force
    for(unsigned i=0; i<dx.size(); i++)
    {
        StrainType::addMultTranspose(df[i], dx[i], stressChange[i], this->integFactors[i], &rotation[i]);
        if( this->f_printLog.getValue() )
        {
            cerr<<"CorotationalForceField<DataTypes>::addDForce, stress deformation gradient change after accumulating "<< kFactor<<"* df = " << df[i] << endl;
        }
    }
}

}
}
} // namespace sofa

#endif
