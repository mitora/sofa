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
*                              SOFA :: Framework                              *
*                                                                             *
* Authors: M. Adam, J. Allard, B. Andre, P-J. Bensoussan, S. Cotin, C. Duriez,*
* H. Delingette, F. Falipou, F. Faure, S. Fonteneau, L. Heigeas, C. Mendoza,  *
* M. Nesme, P. Neumann, J-P. de la Plata Alcade, F. Poyer and F. Roy          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef FRAME_DeformationGradientTYPES_H
#define FRAME_DeformationGradientTYPES_H

#include <sofa/defaulttype/Vec.h>
#include <sofa/defaulttype/Mat.h>
#include <sofa/defaulttype/Quat.h>
#include <sofa/defaulttype/MapMapSparseMatrix.h>
#include <sofa/core/objectmodel/BaseContext.h>
#include <sofa/helper/PolarDecompose.h>
#ifdef SOFA_SMP
#include <sofa/defaulttype/SharedTypes.h>
#endif /* SOFA_SMP */
#include <sofa/helper/vector.h>
#include <sofa/helper/rmath.h>
#include <iostream>
#include <sofa/defaulttype/VecTypes.h>

namespace sofa
{

namespace defaulttype
{

using std::endl;
using helper::vector;

template<int _spatial_dimensions, int _material_dimensions, int _order, typename _Real>
class DeformationGradient;


template<class DeformationGradientType, bool _iscorotational>
class CStrain;



template<class DeformationGradientType, bool _iscorotational>
class CStrain
{
public:
    CStrain() { }

    static const bool iscorotational = _iscorotational;

    typedef typename DeformationGradientType::Coord Coord;
    typedef typename DeformationGradientType::Deriv Deriv;
    typedef typename DeformationGradientType::Real Real;

    static const unsigned material_dimensions = DeformationGradientType::material_dimensions;
    typedef Mat<material_dimensions,material_dimensions, Real> MaterialFrame;

    enum {order = DeformationGradientType::order }; // order = 1 -> deformationgradient // order = 2 -> deformationgradient + spatial derivatives

    static const unsigned strain_size = material_dimensions * (1+material_dimensions) / 2; ///< independent entries in the strain tensor
    typedef Vec<strain_size,Real> StrainVec;    ///< Strain in vector form
    typedef Mat<strain_size,strain_size,Real> StrStr;

    static const unsigned strain_order = order==1? 0 : ( (order==2 && iscorotational)? 1 : ( (order==2 && !iscorotational)? 2 : 0 )) ;
    static const unsigned NumStrainVec = strain_order==0? 1 : ( strain_order==1? 1 + material_dimensions : ( strain_order==2? 1 + material_dimensions*(material_dimensions+3)/2 : 0 )) ;
    typedef Vec<NumStrainVec,StrainVec> Strain;  ///< Strain and its gradient, in vector form
    typedef Strain Stress;
    typedef Strain StrainDeriv;  ///< Strain change and its gradient, in vector form
    typedef Stress StressDeriv;  ///< Strain change and its gradient, in vector form

    static const unsigned strainenergy_order = 2*strain_order; 	///< twice the order of strain
    static const unsigned strainenergy_size = strainenergy_order==0? 1 : (
            strainenergy_order==2? 1 + material_dimensions*(material_dimensions+3)/2 : (
                    (strainenergy_order==4 && material_dimensions==1)? 5 : (
                            (strainenergy_order==4 && material_dimensions==2)? 15 : (
                                    (strainenergy_order==4 && material_dimensions==3)? 35 : 1 ))));
    typedef Vec<strainenergy_size,Real> StrainEnergyVec;



    static StrainVec getStrainVec(  const MaterialFrame& f ) // convert assymetric matrix to voigt notation  exx=Fxx, eyy=Fyy, ezz=Fzz, exy=(Fxy+Fyx)/2 eyz=(Fyz+Fzy)/2, ezx=(Fxz+Fzx)/2,
    {
//                cerr<<"static StrainVec getStrainVec, f = "<< f << endl;
        StrainVec s;
        unsigned ei=0;
        for(unsigned j=0; j<material_dimensions; j++)
        {
            for( unsigned k=0; k<material_dimensions-j; k++ )
            {
                s[ei] = (f[k][k+j]+f[k+j][k])*(Real)0.5;  // first diagonal, then second diagonalâ€¦
                //if(0==j)  s[ei] *= 0.5;
                ei++;
            }
        }
        return s;
    }

    static MaterialFrame getFrame( const StrainVec& s  ) // voigt notation to symmetric matrix (F+F^T)/2  Fxx=exx, Fxy=Fyx=exy/2, etc.
    {
        MaterialFrame f;
        unsigned ei=0;
        for(unsigned j=0; j<material_dimensions; j++)
        {
            for( unsigned k=0; k<material_dimensions-j; k++ )
            {
                f[k][k+j] = f[k+j][k] = s[ei] ;  // first diagonal, then second diagonalâ€¦
                if(0!=j) {f[k][k+j] *= 0.5; f[k+j][k] *= 0.5;}
                ei++;
            }
        }
        return f;
    }



    static void apply( const Coord& F, Strain& strain, MaterialFrame* rotation=NULL)
    // Apply : strain = f(F) convert deformation gradient to strain
    {
        if(iscorotational) // cauchy strain (order 0 or 1) : e= [grad(R^T u)+grad(R^T u)^T ]/2 = [R^T F + F^T R ]/2 - I
        {
            MaterialFrame strainmat;
            helper::polar_decomp(F.getMaterialFrame(), *rotation, strainmat); // decompose F=RD

            // order 0: e = [R^T F + F^T R ]/2 - I = D - I
            for(unsigned j=0; j<material_dimensions; j++) strainmat[j][j]-=1.;
            strain[0] = getStrainVec( strainmat );
            if(strain_order==0) return;

            // order 1 : de =  [R^T dF + dF^T R ]/2
            for(unsigned i=1; i<NumStrainVec; i++)
                strain[i] = getStrainVec( rotation->multTranspose( F.getMaterialFrameGradient()[i-1] ) );
        }
        else // green-lagrange strain (order 0 or 2) : E= [F^T.F - I ]/2
        {
            MaterialFrame strainmat=F.getMaterialFrame().multTranspose( F.getMaterialFrame() );
            // order 0: E = [F^T.F - I ]/2
            for(unsigned j=0; j<material_dimensions; j++) strainmat[j][j]-=1.;
            strainmat*=(Real)0.5;
            strain[0] = getStrainVec( strainmat );
            if(strain_order==0) return;

            // order 1: Ei = [Fi^T.F +  F^T.Fi ]/2
            unsigned ei=1;
            for(unsigned i=1; i<=material_dimensions; i++)
            {
                strainmat = F.getMaterialFrame().multTranspose( F.getMaterialFrameGradient()[i-1] );
                strain[ei] = getStrainVec( strainmat ); ei++;
            }

            // order 2: Eij = [Fi^T.Fj +  Fj^T.Fi ]/2
            for(unsigned i=0; i<material_dimensions; i++)
                for(unsigned j=i; j<material_dimensions; j++)
                {
                    strainmat = F.getMaterialFrameGradient()[i].multTranspose( F.getMaterialFrameGradient()[j] );
                    strain[ei] = getStrainVec( strainmat ); ei++;
                }
        }
    }

    static void mult( const Deriv& F, StrainDeriv& strain , const MaterialFrame* rotation=NULL)
    // ApplyJ : dstrain = J(dF)  convert deformation gradient change to strain change
    {
        if(iscorotational)
        {
            // order 0: e = [R^T Fr + Fr^T R ]/2
            if(rotation!=NULL) strain[0] = getStrainVec( rotation->multTranspose(F.getMaterialFrame()) );
            else strain[0] = getStrainVec( F.getMaterialFrame() );

            if(strain_order==0) return;
            // order 1 : de =  [R^T dFr + dFr^T R ]/2
            if(rotation!=NULL) { for(unsigned i=1; i<NumStrainVec; i++)  strain[i] = getStrainVec( rotation->multTranspose( F.getMaterialFrameGradient()[i-1] ) ); }
            else { for(unsigned i=1; i<NumStrainVec; i++)  strain[i] = getStrainVec( F.getMaterialFrameGradient()[i-1] ); }
        }
        else // green-lagrange strain (order 0 or 2) : E= [F^T.F - I ]/2
        {
            // to do
        }
    }

    static void addMultTranspose( Deriv& dF , const Deriv& F, const StrainDeriv& s, const StrainEnergyVec& integ, const MaterialFrame* rotation=NULL)
    // for each dof Fi, add Fi+= sum_p  (dE/dFi dp )^T dE dp = [(dE /dFi )^T dE ]^(order 2) . sum_p  dp^(order 2)
    {
        if(iscorotational) // cauchy strain (order 0 or 1) : e= [grad(R^T u)+grad(R^T u)^T ]/2 = [R^T F + F^T R ]/2 - I
        {
            // order 0: dF -= R.dE * vol
            MaterialFrame s0 = *rotation * getFrame( s[0] );
            dF.getMaterialFrame() -= s0 *integ[0];

            if(strain_order==0) return;

            Vec<material_dimensions,MaterialFrame> si;
            for(unsigned i=0; i<material_dimensions; i++) si[i]=*rotation * getFrame( s[i+1] );
            unsigned ci=1;

            // order 1: dF -= R.dEi * sum dpi
            //			dFi -= R.dE * sum dpi

            for(unsigned i=0; i<material_dimensions; i++)
            {
                dF.getMaterialFrame() -= si[i]*integ[ci];
                dF.getMaterialFrameGradient()[i] -= s0*integ[ci];
                ci++;
            }

            // order 2: dFi -= R.dEj * sum dpidpj
            //			dFj -= R.dEi * sum dpidpj

            for(unsigned i=0; i<material_dimensions; i++)
            {
                for(unsigned j=i; i<material_dimensions; i++)
                {
                    dF.getMaterialFrameGradient()[i] -= si[j]*integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= si[i]*integ[ci];
                    ci++;
                }
            }

        }
        else // green-lagrange strain (order 0 or 2)
        {
            // order 0: dF -= (F.dE) * vol
            MaterialFrame s0=getFrame( s[0] );
            MaterialFrame F0s0=F.getMaterialFrame()*s0;
            dF.getMaterialFrame() -= F0s0*integ[0];
            if(strain_order==0) return;

            // compute Fi.dEj
            unsigned i,j,k;

            Vec<NumStrainVec,MaterialFrame> si;
            for(i=0; i<NumStrainVec; i++) si[i]=getFrame( s[i] );
            Mat<material_dimensions,NumStrainVec,MaterialFrame> Fisj;

            Fisj[0][0]=F0s0;
            for(j=1; j<NumStrainVec; j++)
                Fisj[0][j]=F.getMaterialFrame()*si[j];
            for(i=1; i<material_dimensions; i++)
                for(j=0; j<NumStrainVec; j++)
                    Fisj[i][j]=F.getMaterialFrameGradient()[i-1]*si[j];

            unsigned ci=1+material_dimensions;
            Mat<material_dimensions,material_dimensions,unsigned> indexij; // index of terms in i.j
            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                    indexij[i][j]=indexij[j][i]=ci;

            // order 1: dF -= (F.dEi +  Fi.dE) * sum dpi
            //			dFi-= (F.dE) * sum dpi

            ci=1;
            for(i=0; i<material_dimensions; i++)
            {
                dF.getMaterialFrame() -= (Fisj[0][i+1] + Fisj[i+1][0])*integ[ci];
                dF.getMaterialFrameGradient()[i] -= Fisj[0][0]*integ[ci];
                ci++;
            }

            // order 2: dF -= (F.dEij +  Fi.dEj +  Fj.dEi) * sum dpidpj
            //			dFi-= (Fj.dE + F.dEj ) * sum dpidpj
            //			dFj-= (Fi.dE + F.dEi) * sum dpidpj

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                {
                    dF.getMaterialFrame() -= ( Fisj[0][ci] + Fisj[i+1][j+1] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrame() -= ( Fisj[j+1][i+1] ) *integ[ci];
                    dF.getMaterialFrameGradient()[i] -= (Fisj[0][j+1] + Fisj[j+1][0])*integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= (Fisj[0][i+1] + Fisj[i+1][0])*integ[ci];
                    ci++;
                }

            // order 3: term ijk (only in 3D)	dF -= (Fi.dEjk +  Fj.dEik +  Fk.dEij) * sum dpidpjdpk
            //									dFi-= (F.dEjk + Fj.dEk + Fk.dEj ) * sum dpidpjdpk
            //									dFj-= (F.dEik + Fi.dEk + Fk.dEi) * sum dpidpjdpk
            //									dFk-= (F.dEji + Fj.dEi + Fi.dEj) * sum dpidpjdpk

            if(material_dimensions==3)
            {
                dF.getMaterialFrame() -= ( Fisj[1][indexij[1][2]] + Fisj[2][indexij[0][2]] + Fisj[3][indexij[0][1]] ) *integ[ci];
                dF.getMaterialFrameGradient()[0] -= ( Fisj[0][ci] + Fisj[2][3] + Fisj[3][2] ) *integ[ci];
                dF.getMaterialFrameGradient()[1] -= ( Fisj[0][ci] + Fisj[1][3] + Fisj[3][1] ) *integ[ci];
                dF.getMaterialFrameGradient()[2] -= ( Fisj[0][ci] + Fisj[2][1] + Fisj[1][2] ) *integ[ci];
                ci++;
            }

            // order 3: dF -= (Fj.dEii +  Fi.dEij ) * sum dpi^2dpj
            //			dFi-= (Fj.dEi + F.dEij + Fi.dEj ) * sum dpi^2dpj
            //			dFj-= (Fi.dEi + Fi.dEij ) * sum dpi^2dpj


            for(i=0; i<material_dimensions; i++)
                for(j=0; j<material_dimensions; j++)
                {
                    dF.getMaterialFrame() -= ( Fisj[i+1][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrame() -= ( Fisj[j+1][indexij[i][i]] ) *integ[ci];
                    dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][i+1] + Fisj[0][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][j+1] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][i+1] + Fisj[0][indexij[i][i]] ) *integ[ci];
                    ci++;
                }

            // order 4: dFi-= (Fi.dEjj + Fj.dEij ) * sum dpi^2dpj^2
            //			dFj-= (Fj.dEii + Fi.dEij ) * sum dpi^2dpj^2

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                {
                    dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= ( Fisj[j+1][indexij[i][i]] + Fisj[i+1][indexij[i][j]] ) *integ[ci];
                    ci++;
                }

            // order 4: term i^2jk (only in 3D)		dFi-= (Fi.dEjk + Fj.dEij + Fk.dEij ) * sum dpi^2dpjdpk
            //										dFj-= (Fi.dEik + Fk.dEii ) * sum dpi^2dpjdpk
            //										dFk-= (Fi.dEij + Fj.dEii ) * sum dpi^2dpjdpk

            if(material_dimensions==3)
            {
                i=0; j=1; k=2;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;

                i=1; j=0; k=2;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;

                i=2; j=1; k=1;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;
            }

            // order 4: dFi-= (Fj.dEii + Fi.dEij ) * sum dpi^3dpj
            //			dFj-= (Fi.dEii ) * sum dpi^3dpj

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                    if(i!=j)
                    {
                        dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][indexij[i][i]] + Fisj[i+1][indexij[i][j]] ) *integ[ci];
                        dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][i]] ) *integ[ci];
                        ci++;
                    }
        }
    }


    static void addMultTranspose( Deriv& dF , const Coord& F, const StrainDeriv& s, const StrainEnergyVec& integ, const MaterialFrame* rotation=NULL)
    // for each dof Fi, add Fi+= sum_p  (dE/dFi dp )^T dE dp = [(dE /dFi )^T dE ]^(order 2) . sum_p  dp^(order 2)
    {
        if(iscorotational) // cauchy strain (order 0 or 1) : e= [grad(R^T u)+grad(R^T u)^T ]/2 = [R^T F + F^T R ]/2 - I
        {
            // order 0: dF -= R.dE * vol
            MaterialFrame s0 = *rotation * getFrame( s[0] );
            dF.getMaterialFrame() -= s0 *integ[0];

            if(strain_order==0) return;

            Vec<material_dimensions,MaterialFrame> si;
            for(unsigned i=0; i<material_dimensions; i++) si[i]=*rotation * getFrame( s[i+1] );
            unsigned ci=1;

            // order 1: dF -= R.dEi * sum dpi
            //			dFi -= R.dE * sum dpi

            for(unsigned i=0; i<material_dimensions; i++)
            {
                dF.getMaterialFrame() -= si[i]*integ[ci];
                dF.getMaterialFrameGradient()[i] -= s0*integ[ci];
                ci++;
            }

            // order 2: dFi -= R.dEj * sum dpidpj
            //			dFj -= R.dEi * sum dpidpj

            for(unsigned i=0; i<material_dimensions; i++)
            {
                for(unsigned j=i; i<material_dimensions; i++)
                {
                    dF.getMaterialFrameGradient()[i] -= si[j]*integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= si[i]*integ[ci];
                    ci++;
                }
            }

        }
        else // green-lagrange strain (order 0 or 2)
        {
            // order 0: dF -= (F.dE) * vol
            MaterialFrame s0=getFrame( s[0] );
            MaterialFrame F0s0=F.getMaterialFrame()*s0;
            dF.getMaterialFrame() -= F0s0*integ[0];
            if(strain_order==0) return;

            // compute Fi.dEj
            unsigned i,j,k;

            Vec<NumStrainVec,MaterialFrame> si;
            for(i=0; i<NumStrainVec; i++) si[i]=getFrame( s[i] );
            Mat<material_dimensions,NumStrainVec,MaterialFrame> Fisj;

            Fisj[0][0]=F0s0;
            for(j=1; j<NumStrainVec; j++)
                Fisj[0][j]=F.getMaterialFrame()*si[j];
            for(i=1; i<material_dimensions; i++)
                for(j=0; j<NumStrainVec; j++)
                    Fisj[i][j]=F.getMaterialFrameGradient()[i-1]*si[j];

            unsigned ci=1+material_dimensions;
            Mat<material_dimensions,material_dimensions,unsigned> indexij; // index of terms in i.j
            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                    indexij[i][j]=indexij[j][i]=ci;

            // order 1: dF -= (F.dEi +  Fi.dE) * sum dpi
            //			dFi-= (F.dE) * sum dpi

            ci=1;
            for(i=0; i<material_dimensions; i++)
            {
                dF.getMaterialFrame() -= (Fisj[0][i+1] + Fisj[i+1][0])*integ[ci];
                dF.getMaterialFrameGradient()[i] -= Fisj[0][0]*integ[ci];
                ci++;
            }

            // order 2: dF -= (F.dEij +  Fi.dEj +  Fj.dEi) * sum dpidpj
            //			dFi-= (Fj.dE + F.dEj ) * sum dpidpj
            //			dFj-= (Fi.dE + F.dEi) * sum dpidpj

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                {
                    dF.getMaterialFrame() -= ( Fisj[0][ci] + Fisj[i+1][j+1] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrame() -= ( Fisj[j+1][i+1] ) *integ[ci];
                    dF.getMaterialFrameGradient()[i] -= (Fisj[0][j+1] + Fisj[j+1][0])*integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= (Fisj[0][i+1] + Fisj[i+1][0])*integ[ci];
                    ci++;
                }

            // order 3: term ijk (only in 3D)	dF -= (Fi.dEjk +  Fj.dEik +  Fk.dEij) * sum dpidpjdpk
            //									dFi-= (F.dEjk + Fj.dEk + Fk.dEj ) * sum dpidpjdpk
            //									dFj-= (F.dEik + Fi.dEk + Fk.dEi) * sum dpidpjdpk
            //									dFk-= (F.dEji + Fj.dEi + Fi.dEj) * sum dpidpjdpk

            if(material_dimensions==3)
            {
                dF.getMaterialFrame() -= ( Fisj[1][indexij[1][2]] + Fisj[2][indexij[0][2]] + Fisj[3][indexij[0][1]] ) *integ[ci];
                dF.getMaterialFrameGradient()[0] -= ( Fisj[0][ci] + Fisj[2][3] + Fisj[3][2] ) *integ[ci];
                dF.getMaterialFrameGradient()[1] -= ( Fisj[0][ci] + Fisj[1][3] + Fisj[3][1] ) *integ[ci];
                dF.getMaterialFrameGradient()[2] -= ( Fisj[0][ci] + Fisj[2][1] + Fisj[1][2] ) *integ[ci];
                ci++;
            }

            // order 3: dF -= (Fj.dEii +  Fi.dEij ) * sum dpi^2dpj
            //			dFi-= (Fj.dEi + F.dEij + Fi.dEj ) * sum dpi^2dpj
            //			dFj-= (Fi.dEi + Fi.dEij ) * sum dpi^2dpj


            for(i=0; i<material_dimensions; i++)
                for(j=0; j<material_dimensions; j++)
                {
                    dF.getMaterialFrame() -= ( Fisj[i+1][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrame() -= ( Fisj[j+1][indexij[i][i]] ) *integ[ci];
                    dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][i+1] + Fisj[0][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][j+1] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][i+1] + Fisj[0][indexij[i][i]] ) *integ[ci];
                    ci++;
                }

            // order 4: dFi-= (Fi.dEjj + Fj.dEij ) * sum dpi^2dpj^2
            //			dFj-= (Fj.dEii + Fi.dEij ) * sum dpi^2dpj^2

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                {
                    dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][indexij[i][j]] ) *integ[ci];
                    if(i!=j) dF.getMaterialFrameGradient()[j] -= ( Fisj[j+1][indexij[i][i]] + Fisj[i+1][indexij[i][j]] ) *integ[ci];
                    ci++;
                }

            // order 4: term i^2jk (only in 3D)		dFi-= (Fi.dEjk + Fj.dEij + Fk.dEij ) * sum dpi^2dpjdpk
            //										dFj-= (Fi.dEik + Fk.dEii ) * sum dpi^2dpjdpk
            //										dFk-= (Fi.dEij + Fj.dEii ) * sum dpi^2dpjdpk

            if(material_dimensions==3)
            {
                i=0; j=1; k=2;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;

                i=1; j=0; k=2;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;

                i=2; j=1; k=1;
                dF.getMaterialFrameGradient()[i] -= ( Fisj[i+1][indexij[j][k]] + Fisj[j+1][indexij[i][k]] + Fisj[k+1][indexij[i][j]] ) *integ[ci];
                dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][k]] + Fisj[k+1][indexij[i][i]] ) *integ[ci];
                dF.getMaterialFrameGradient()[k] -= ( Fisj[i+1][indexij[i][j]] + Fisj[j+1][indexij[i][i]]  ) *integ[ci];
                ci++;
            }

            // order 4: dFi-= (Fj.dEii + Fi.dEij ) * sum dpi^3dpj
            //			dFj-= (Fi.dEii ) * sum dpi^3dpj

            for(i=0; i<material_dimensions; i++)
                for(j=i; j<material_dimensions; j++)
                    if(i!=j)
                    {
                        dF.getMaterialFrameGradient()[i] -= ( Fisj[j+1][indexij[i][i]] + Fisj[i+1][indexij[i][j]] ) *integ[ci];
                        dF.getMaterialFrameGradient()[j] -= ( Fisj[i+1][indexij[i][i]] ) *integ[ci];
                        ci++;
                    }
        }
    }



    static void mult( Strain& s, Real r )
    {
        for(unsigned i=0; i<s.size(); i++)
            s[i] *= r;
    }

    static Strain mult( const Strain& s, const Mat<strain_size,strain_size,Real>& H )
    // compute H.s -> returns a vector of the order of the input strain
    {
        Strain ret;
        for(unsigned i=0; i<s.size(); i++)
            ret[i] = H*s[i];
        return ret;
    }

};






/** Local deformation state of a material object.

  spatial_dimensions is the number of dimensions the object is moving in.
  material_dimensions is the number of internal dimensions of the object: 1 for a wire, 2 for a hull, 3 for a volumetric object
  order is the degree of the local displacement function: 0 for a simple displacement, 1 for a displacent and a nonrigid local frame, 2 for a displacent, a nonrigid local frame and the gradient of this frame.
  */
template<int _spatial_dimensions, int _material_dimensions, int _order, typename _Real>
struct DeformationGradientTypes
{
    static const unsigned spatial_dimensions = _spatial_dimensions;
    static const unsigned material_dimensions = _material_dimensions;
    static const unsigned order = _order;  ///< 0: only a point, no gradient 1:deformation gradient, 2: deformation gradient and its gradient
    static const unsigned NumMatrices = order==0? 0 : (order==1? 1 : (order==2? 1 + material_dimensions : -1 ));
    static const unsigned VSize = spatial_dimensions +  NumMatrices * material_dimensions * material_dimensions;  // number of entries
    typedef _Real Real;
    typedef vector<Real> VecReal;

    // ------------    Types and methods defined for easier data access
    typedef Vec<material_dimensions, Real> MaterialCoord;
    typedef vector<MaterialCoord> VecMaterialCoord;
    typedef Vec<spatial_dimensions, Real> SpatialCoord;                   ///< Position or velocity of a point
    typedef Mat<material_dimensions,material_dimensions, Real> MaterialFrame;      ///< Matrix representing a deformation gradient
    typedef Vec<material_dimensions, MaterialFrame> MaterialFrameGradient;                 ///< Gradient of a deformation gradient

    class Deriv
    {
    protected:
        Vec<VSize,Real> v;

    public:
        Deriv() { v.clear(); }
        Deriv( const Vec<VSize,Real>& d):v(d) {}
        void clear() { v.clear(); }

        /// seen as a vector
        Vec<VSize,Real>& getVec() { return v; }
        const Vec<VSize,Real>& getVec() const { return v; }

        /// point
        SpatialCoord& getCenter() { return *reinterpret_cast<SpatialCoord*>(&v[0]); }
        const SpatialCoord& getCenter() const { return *reinterpret_cast<const SpatialCoord*>(&v[0]); }

        /// local frame (if order>=1)
        MaterialFrame& getMaterialFrame() { return *reinterpret_cast<MaterialFrame*>(&v[spatial_dimensions]); }
        const MaterialFrame& getMaterialFrame() const { return *reinterpret_cast<const MaterialFrame*>(&v[spatial_dimensions]); }

        /// gradient of the local frame (if order>=2)
        MaterialFrameGradient& getMaterialFrameGradient() { return *reinterpret_cast<MaterialFrameGradient*>(&v[spatial_dimensions+material_dimensions * material_dimensions]); }
        const MaterialFrameGradient& getMaterialFrameGradient() const { return *reinterpret_cast<const MaterialFrameGradient*>(&v[spatial_dimensions+material_dimensions * material_dimensions]); }

        static const unsigned total_size = VSize;
        typedef Real value_type;




        Deriv operator +(const Deriv& a) const { return Deriv(v+a.v); }
        void operator +=(const Deriv& a) { v+=a.v; }

        Deriv operator -(const Deriv& a) const { return Deriv(v-a.v); }
        void operator -=(const Deriv& a) { v-=a.v; }


        template<typename real2>
        Deriv operator *(real2 a) const { return Deriv(v*a); }
        template<typename real2>
        void operator *=(real2 a) { v *= a; }

        template<typename real2>
        void operator /=(real2 a) { v /= a; }

        Deriv operator - () const { return Deriv(-v); }


        /// dot product, mostly used to compute residuals as sqrt(x*x)
        Real operator*(const Deriv& a) const
        {
            return v*a.v;
        }

        /// write to an output stream
        inline friend std::ostream& operator << ( std::ostream& out, const Deriv& c )
        {
            out<<c.v;
            return out;
        }
        /// read from an input stream
        inline friend std::istream& operator >> ( std::istream& in, Deriv& c )
        {
            in>>c.v;
            return in;
        }


        Real* ptr() { return v.ptr(); }
        const Real* ptr() const { return v.ptr(); }

        /// Vector size
        static unsigned size() { return VSize; }

        /// Access to i-th element.
        Real& operator[](int i)
        {
            return v[i];
        }

        /// Const access to i-th element.
        const Real& operator[](int i) const
        {
            return v[i];
        }
    };

    typedef vector<Deriv> VecDeriv;
    typedef MapMapSparseMatrix<Deriv> MatrixDeriv;


    class Coord
    {
    protected:
        Vec<VSize,Real> v;

    public:
        Coord() { v.clear(); }
        Coord( const Vec<VSize,Real>& d):v(d) {}
        void clear() { v.clear(); for( unsigned int i = 0; i < spatial_dimensions; ++i) getMaterialFrame()[i][i] = (Real)1.0;}

        /// seen as a vector
        Vec<VSize,Real>& getVec() { return v; }
        const Vec<VSize,Real>& getVec() const { return v; }

        /// point
        SpatialCoord& getCenter() { return *reinterpret_cast<SpatialCoord*>(&v[0]); }
        const SpatialCoord& getCenter() const { return *reinterpret_cast<const SpatialCoord*>(&v[0]); }

        /// local frame (if order>=1)
        MaterialFrame& getMaterialFrame() { return *reinterpret_cast<MaterialFrame*>(&v[spatial_dimensions]); }
        const MaterialFrame& getMaterialFrame() const { return *reinterpret_cast<const MaterialFrame*>(&v[spatial_dimensions]); }

        /// gradient of the local frame (if order>=2)
        MaterialFrameGradient& getMaterialFrameGradient() { return *reinterpret_cast<MaterialFrameGradient*>(&v[spatial_dimensions+material_dimensions * material_dimensions]); }
        const MaterialFrameGradient& getMaterialFrameGradient() const { return *reinterpret_cast<const MaterialFrameGradient*>(&v[spatial_dimensions+material_dimensions * material_dimensions]); }

        static const unsigned total_size = VSize;
        typedef Real value_type;




        Coord operator +(const Coord& a) const { return Coord(v+a.v); }
        void operator +=(const Coord& a) { v+=a.v; }

        Coord operator +(const Deriv& a) const { return Coord(v+a.getVec()); }
        void operator +=(const Deriv& a) { v+=a.getVec(); }

        Coord operator -(const Coord& a) const { return Coord(v-a.v); }
        void operator -=(const Coord& a) { v-=a.v; }


        template<typename real2>
        Coord operator *(real2 a) const { return Coord(v*a); }
        template<typename real2>
        void operator *=(real2 a) { v *= a; }

        template<typename real2>
        void operator /=(real2 a) { v /= a; }

        Coord operator - () const { return Coord(-v); }


        /// dot product, mostly used to compute residuals as sqrt(x*x)
        Real operator*(const Coord& a) const
        {
            return v*a.v;
        }

        /// write to an output stream
        inline friend std::ostream& operator << ( std::ostream& out, const Coord& c )
        {
            out<<c.v;
            return out;
        }
        /// read from an input stream
        inline friend std::istream& operator >> ( std::istream& in, Coord& c )
        {
            in>>c.v;
            return in;
        }


        Real* ptr() { return v.ptr(); }
        const Real* ptr() const { return v.ptr(); }

        /// Vector size
        static unsigned size() { return VSize; }

        /// Access to i-th element.
        Real& operator[](int i)
        {
            return v[i];
        }

        /// Const access to i-th element.
        const Real& operator[](int i) const
        {
            return v[i];
        }

        /// Write the OpenGL transformation matrix
        void writeOpenGlMatrix ( float m[16] ) const
        {
            BOOST_STATIC_ASSERT(spatial_dimensions == 3);
            m[0] = (float)getMaterialFrame()(0,0);
            m[4] = (float)getMaterialFrame()(0,1);
            m[8] = (float)getMaterialFrame()(0,2);
            m[1] = (float)getMaterialFrame()(1,0);
            m[5] = (float)getMaterialFrame()(1,1);
            m[9] = (float)getMaterialFrame()(1,2);
            m[2] = (float)getMaterialFrame()(2,0);
            m[6] = (float)getMaterialFrame()(2,1);
            m[10] = (float)getMaterialFrame()(2,2);
            m[3] = 0;
            m[7] = 0;
            m[11] = 0;
            m[12] = ( float ) getCenter()[0];
            m[13] = ( float ) getCenter()[1];
            m[14] = ( float ) getCenter()[2];
            m[15] = 1;
        }
        /*
                    /// Write the OpenGL transformation matrix
                    void drawDeformationGradient( const float& scale) const
                    {
                        BOOST_STATIC_ASSERT(spatial_dimensions == 3);
                        glPushMatrix();
                        //drawCylinder();
                        glPopMatrix();
                    }

                    void drawCylinder(const Vector3& p1, const Vector3 &p2, float radius, const Vec<4,float> colour, int subdRadius, int subdLength)
                      {
                        Vector3 tmp = p2-p1;
                        simulation::getSimulation()->DrawUtility.setMaterial(colour);
                        // create Vectors p and q, co-planar with the cylinder's cross-sectional disk
                        Vector3 p=tmp;
                        if (fabs(p[0]) + fabs(p[1]) < 0.00001*tmp.norm())
                          p[0] += 1.0;
                        else
                          p[2] += 1.0;
                        Vector3 q;
                        q = p.cross(tmp);
                        p = tmp.cross(q);
                        // do the normalization outside the segment loop
                        p.normalize();
                        q.normalize();

                        int i2;
                        float theta, st, ct;
                        // build the cylinder from rectangular subd
                        std::vector<Vector3> points;
                        std::vector<Vec<4,int> > indices;
                        std::vector<Vector3> normals;

                        std::vector<Vector3> pointsCloseCylinder1;
                        std::vector<Vector3> normalsCloseCylinder1;
                        std::vector<Vector3> pointsCloseCylinder2;
                        std::vector<Vector3> normalsCloseCylinder2;

                        Vector3 dir=p1-p2; dir.normalize();
                        pointsCloseCylinder1.push_back(p1);
                        normalsCloseCylinder1.push_back(dir);
                        pointsCloseCylinder2.push_back(p2);
                        normalsCloseCylinder2.push_back(-dir);


                        Vector3 dtmp = tmp / (double)subdLength;
                        for( int j = 0; j < subdLength; ++j) // Length subdivision
                        {
                            for (i2=0 ; i2<=subd ; i2++)
                            {
                                // sweep out a circle
                                theta =  i2 * 2.0 * 3.14 / subd;
                                st = sin(theta);
                                ct = cos(theta);
                                // construct normal
                                tmp = p*ct+q*st;
                                // set the normal for the two subseqent points
                                normals.push_back(tmp);

                                // point on disk 1
                                Vector3 w(p1 + dtmp*j);
                                w += tmp*radius;
                                points.push_back(w);
                                pointsCloseCylinder1.push_back(w);
                                normalsCloseCylinder1.push_back(dir);

                                // point on disk 2
                                w=p1 + dtmp*(j+1);
                                w += tmp*radius;
                                points.push_back(w);
                                pointsCloseCylinder2.push_back(w);
                                normalsCloseCylinder2.push_back(-dir);
                            }
                        }
                        pointsCloseCylinder1.push_back(pointsCloseCylinder1[1]);
                        normalsCloseCylinder1.push_back(normalsCloseCylinder1[1]);
                        pointsCloseCylinder2.push_back(pointsCloseCylinder2[1]);
                        normalsCloseCylinder2.push_back(normalsCloseCylinder2[1]);

                        simulation::getSimulation()->DrawUtility.drawTriangleStrip(points, normals,colour);
                        if (radius1 > 0) simulation::getSimulation()->DrawUtility.drawTriangleFan(pointsCloseCylinder1, normalsCloseCylinder1,colour);
                        if (radius2 > 0) simulation::getSimulation()->DrawUtility.drawTriangleFan(pointsCloseCylinder2, normalsCloseCylinder2,colour);

                        simulation::getSimulation()->DrawUtility.resetMaterial(colour);
                      }
        */
    };

    typedef vector<Coord> VecCoord;

    static const char* Name();


    template<typename T>
    static void set ( Deriv& c, T x, T y, T z )
    {
        c.clear();
        c.getCenter()[0] = ( Real ) x;
        c.getCenter() [1] = ( Real ) y;
        c.getCenter() [2] = ( Real ) z;
    }

    template<typename T>
    static void get ( T& x, T& y, T& z, const Deriv& c )
    {
        x = ( T ) c.getCenter() [0];
        y = ( T ) c.getCenter() [1];
        z = ( T ) c.getCenter() [2];
    }

    template<typename T>
    static void add ( Deriv& c, T x, T y, T z )
    {
        c.getCenter() [0] += ( Real ) x;
        c.getCenter() [1] += ( Real ) y;
        c.getCenter() [2] += ( Real ) z;
    }

    template<typename T>
    static void set ( Coord& c, T x, T y, T z )
    {
        c.clear();
        c.getCenter()[0] = ( Real ) x;
        c.getCenter() [1] = ( Real ) y;
        c.getCenter() [2] = ( Real ) z;
    }

    template<typename T>
    static void get ( T& x, T& y, T& z, const Coord& c )
    {
        x = ( T ) c.getCenter() [0];
        y = ( T ) c.getCenter() [1];
        z = ( T ) c.getCenter() [2];
    }

    template<typename T>
    static void add ( Coord& c, T x, T y, T z )
    {
        c.getCenter() [0] += ( Real ) x;
        c.getCenter() [1] += ( Real ) y;
        c.getCenter() [2] += ( Real ) z;
    }



    static Deriv interpolate ( const helper::vector< Deriv > & ancestors, const helper::vector< Real > & coefs )
    {
        assert ( ancestors.size() == coefs.size() );

        Deriv c;

        for ( unsigned int i = 0; i < ancestors.size(); i++ )
        {
            c += ancestors[i] * coefs[i];  // Position and deformation gradient linear interpolation.
        }

        return c;
    }

    static Coord interpolate ( const helper::vector< Coord > & ancestors, const helper::vector< Real > & coefs )
    {
        assert ( ancestors.size() == coefs.size() );

        Coord c;

        for ( unsigned int i = 0; i < ancestors.size(); i++ )
        {
            c += ancestors[i] * coefs[i];  // Position and deformation gradient linear interpolation.
        }

        return c;
    }

};





// ==========================================================================

/** Mass associated with a sampling point
*/
template<int _spatial_dimensions, int _material_dimensions, int _order, typename _Real>
struct DeformationGradientMass
{
    typedef _Real Real;
    Real mass;  ///< Currently only a scalar mass, but a matrix should be used for more precision

    // operator to cast to const Real
    operator const Real() const
    {
        return mass;
    }

    template<int S, int M, int O, typename R>
    inline friend std::ostream& operator << ( std::ostream& out, const DeformationGradientMass<S,M,O,R>& m )
    {
        out << m.mass;
        return out;
    }

    template<int S, int M, int O, typename R>
    inline friend std::istream& operator >> ( std::istream& in, DeformationGradientMass<S,M,O,R>& m )
    {
        in >> m.mass;
        return in;
    }

    void operator *= ( Real fact )
    {
        mass *= fact;
    }

    void operator /= ( Real fact )
    {
        mass /= fact;
    }
};

template<int S, int M, int O, typename R>
inline typename DeformationGradientTypes<S,M,O,R>::Deriv operator* ( const typename DeformationGradientTypes<S,M,O,R>::Deriv& d, const DeformationGradientMass<S,M,O,R>& m )
{
    typename DeformationGradientTypes<S,M,O,R>::Deriv res;
    DeformationGradientTypes<S,M,O,R>::center(res) = DeformationGradientTypes<S,M,O,R>::center(d) * m.mass;
    return res;
}

template<int S, int M, int O, typename R>
inline typename DeformationGradientTypes<S,M,O,R>::Deriv operator/ ( const typename DeformationGradientTypes<S,M,O,R>::Deriv& d, const DeformationGradientMass<S,M,O,R>& m )
{
    typename DeformationGradientTypes<S,M,O,R>::Deriv res;
    DeformationGradientTypes<S,M,O,R>::center(res) = DeformationGradientTypes<S,M,O,R>::center(d) / m.mass;
    return res;
}





// ==========================================================================
// order 1

typedef DeformationGradientTypes<3, 3, 1, double> DeformationGradient331dTypes;
typedef DeformationGradientTypes<3, 3, 1, float>  DeformationGradient331fTypes;

typedef DeformationGradientMass<3, 3, 1, double> DeformationGradient331dMass;
typedef DeformationGradientMass<3, 3, 1, float>  DeformationGradient331fMass;

/// Note: Many scenes use DeformationGradient as template for 3D double-precision rigid type. Changing it to DeformationGradient3d would break backward compatibility.
#ifdef SOFA_FLOAT
template<> inline const char* DeformationGradient331dTypes::Name() { return "DeformationGradient331d"; }

template<> inline const char* DeformationGradient331fTypes::Name() { return "DeformationGradient331"; }

#else
template<> inline const char* DeformationGradient331dTypes::Name() { return "DeformationGradient331"; }

template<> inline const char* DeformationGradient331fTypes::Name() { return "DeformationGradient331f"; }

#endif

#ifdef SOFA_FLOAT
typedef DeformationGradient331fTypes DeformationGradient331Types;
typedef DeformationGradient331fMass DeformationGradient331Mass;
#else
typedef DeformationGradient331dTypes DeformationGradient331Types;
typedef DeformationGradient331dMass DeformationGradient331Mass;
#endif

template<>
struct DataTypeInfo< DeformationGradient331fTypes::Deriv > : public FixedArrayTypeInfo< DeformationGradient331fTypes::Deriv, DeformationGradient331fTypes::Deriv::total_size >
{
    static std::string name() { std::ostringstream o; o << "DeformationGradient331<" << DataTypeName<float>::name() << ">"; return o.str(); }
};
template<>
struct DataTypeInfo< DeformationGradient331dTypes::Deriv > : public FixedArrayTypeInfo< DeformationGradient331dTypes::Deriv, DeformationGradient331dTypes::Deriv::total_size >
{
    static std::string name() { std::ostringstream o; o << "DeformationGradient331<" << DataTypeName<double>::name() << ">"; return o.str(); }
};




// The next line hides all those methods from the doxygen documentation
/// \cond TEMPLATE_OVERRIDES

template<> struct DataTypeName< defaulttype::DeformationGradient331fTypes::Coord > { static const char* name() { return "DeformationGradient331fTypes::CoordOrDeriv"; } };

//        template<> struct DataTypeName< defaulttype::DeformationGradient331fTypes::Deriv > { static const char* name() { return "DeformationGradient331fTypes::Deriv"; } };

template<> struct DataTypeName< defaulttype::DeformationGradient331dTypes::Coord > { static const char* name() { return "DeformationGradient331dTypes::CoordOrDeriv"; } };

//        template<> struct DataTypeName< defaulttype::DeformationGradient331dTypes::Deriv > { static const char* name() { return "DeformationGradient331dTypes::Deriv"; } };


template<> struct DataTypeName< defaulttype::DeformationGradient331fMass > { static const char* name() { return "DeformationGradient331fMass"; } };

template<> struct DataTypeName< defaulttype::DeformationGradient331dMass > { static const char* name() { return "DeformationGradient331dMass"; } };

/// \endcond






// ==========================================================================
// order 2


typedef DeformationGradientTypes<3, 3, 2, double> DeformationGradient332dTypes;
typedef DeformationGradientTypes<3, 3, 2, float>  DeformationGradient332fTypes;

typedef DeformationGradientMass<3, 3, 2, double> DeformationGradient332dMass;
typedef DeformationGradientMass<3, 3, 2, float>  DeformationGradient332fMass;

/// Note: Many scenes use DeformationGradient as template for 3D double-precision rigid type. Changing it to DeformationGradient3d would break backward compatibility.
#ifdef SOFA_FLOAT
template<> inline const char* DeformationGradient332dTypes::Name() { return "DeformationGradient332d"; }

template<> inline const char* DeformationGradient332fTypes::Name() { return "DeformationGradient332"; }

#else
template<> inline const char* DeformationGradient332dTypes::Name() { return "DeformationGradient332"; }

template<> inline const char* DeformationGradient332fTypes::Name() { return "DeformationGradient332f"; }

#endif

#ifdef SOFA_FLOAT
typedef DeformationGradient332fTypes DeformationGradient332Types;
typedef DeformationGradient332fMass DeformationGradient332Mass;
#else
typedef DeformationGradient332dTypes DeformationGradient332Types;
typedef DeformationGradient332dMass DeformationGradient332Mass;
#endif

template<>
struct DataTypeInfo< DeformationGradient332fTypes::Deriv > : public FixedArrayTypeInfo< DeformationGradient332fTypes::Deriv, DeformationGradient332fTypes::Deriv::total_size >
{
    static std::string name() { std::ostringstream o; o << "DeformationGradient332<" << DataTypeName<float>::name() << ">"; return o.str(); }
};
template<>
struct DataTypeInfo< DeformationGradient332dTypes::Deriv > : public FixedArrayTypeInfo< DeformationGradient332dTypes::Deriv, DeformationGradient332dTypes::Deriv::total_size >
{
    static std::string name() { std::ostringstream o; o << "DeformationGradient332<" << DataTypeName<double>::name() << ">"; return o.str(); }
};




// The next line hides all those methods from the doxygen documentation
/// \cond TEMPLATE_OVERRIDES

template<> struct DataTypeName< defaulttype::DeformationGradient332fTypes::Coord > { static const char* name() { return "DeformationGradient332fTypes::CoordOrDeriv"; } };

//        template<> struct DataTypeName< defaulttype::DeformationGradient332fTypes::Deriv > { static const char* name() { return "DeformationGradient332fTypes::Deriv"; } };

template<> struct DataTypeName< defaulttype::DeformationGradient332dTypes::Coord > { static const char* name() { return "DeformationGradient332dTypes::CoordOrDeriv"; } };

//        template<> struct DataTypeName< defaulttype::DeformationGradient332dTypes::Deriv > { static const char* name() { return "DeformationGradient332dTypes::Deriv"; } };


template<> struct DataTypeName< defaulttype::DeformationGradient332fMass > { static const char* name() { return "DeformationGradient332fMass"; } };

template<> struct DataTypeName< defaulttype::DeformationGradient332dMass > { static const char* name() { return "DeformationGradient332dMass"; } };

/// \endcond


} // namespace defaulttype

namespace core
{

namespace behavior
{

/** Return the inertia force applied to a body referenced in a moving coordinate system.
\param sv spatial velocity (omega, vorigin) of the coordinate system
\param a acceleration of the origin of the coordinate system
\param m mass of the body
\param x position of the body in the moving coordinate system
\param v velocity of the body in the moving coordinate system
This default implementation returns no inertia.
*/
template<class DeformationGradientT, class Vec, class M, class SV>
typename DeformationGradientT::Deriv inertiaForce ( const SV& /*sv*/, const Vec& /*a*/, const M& /*m*/, const typename DeformationGradientT::Coord& /*x*/, const  typename DeformationGradientT::Deriv& /*v*/ );

/// Specialization of the inertia force for defaulttype::DeformationGradient3dTypes
template <>
inline defaulttype::DeformationGradient332dTypes::Deriv inertiaForce <
defaulttype::DeformationGradient332dTypes,
            objectmodel::BaseContext::Vec3,
            defaulttype::DeformationGradientMass<3,3,2, double>,
            objectmodel::BaseContext::SpatialVector
            >
            (
                    const objectmodel::BaseContext::SpatialVector& vframe,
                    const objectmodel::BaseContext::Vec3& aframe,
                    const defaulttype::DeformationGradientMass<3,3,2, double>& mass,
                    const defaulttype::DeformationGradient332dTypes::Coord& x,
                    const defaulttype::DeformationGradient332dTypes::Deriv& v
            )
{
    defaulttype::Vec3d omega ( vframe.lineVec[0], vframe.lineVec[1], vframe.lineVec[2] );
    defaulttype::Vec3d origin = x.getCenter(), finertia;

    finertia = - ( aframe + omega.cross ( omega.cross ( origin ) + v.getCenter() * 2 ) ) * mass.mass;
    defaulttype::DeformationGradient332dTypes::Deriv result;
    result[0]=finertia[0]; result[1]=finertia[1]; result[2]=finertia[2];
    return result;
}

/// Specialization of the inertia force for defaulttype::DeformationGradient3dTypes
template <>
inline defaulttype::DeformationGradient332fTypes::Deriv inertiaForce <
defaulttype::DeformationGradient332fTypes,
            objectmodel::BaseContext::Vec3,
            defaulttype::DeformationGradientMass<3,3,2, double>,
            objectmodel::BaseContext::SpatialVector
            >
            (
                    const objectmodel::BaseContext::SpatialVector& vframe,
                    const objectmodel::BaseContext::Vec3& aframe,
                    const defaulttype::DeformationGradientMass<3,3,2, double>& mass,
                    const defaulttype::DeformationGradient332fTypes::Coord& x,
                    const defaulttype::DeformationGradient332fTypes::Deriv& v
            )
{
    const defaulttype::Vec3f omega ( (float)vframe.lineVec[0], (float)vframe.lineVec[1], (float)vframe.lineVec[2] );
    defaulttype::Vec3f origin = x.getCenter(), finertia;

    finertia = - ( aframe + omega.cross ( omega.cross ( origin ) + v.getCenter() * 2 ) ) * mass.mass;
    defaulttype::DeformationGradient332fTypes::Deriv result;
    result[0]=finertia[0]; result[1]=finertia[1]; result[2]=finertia[2];
    return result;
}


} // namespace behavoir

} // namespace core

} // namespace sofa


#endif
