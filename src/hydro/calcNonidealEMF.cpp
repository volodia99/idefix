// ***********************************************************************************************
// Idefix MHD astrophysical code
// Copyright(C) 2020 Geoffroy R. J. Lesur <geoffroy.lesur@univ-grenoble-alpes.fr
// and other code contributors
// Licensed under CeCILL 2.1 License, see COPYING for more information
// ***********************************************************************************************

#include "../idefix.hpp"
#include "hydro.hpp"


// Compute Corner EMFs from nonideal MHD
void Hydro::CalcNonidealEMF(real t) {
  idfx::pushRegion("Hydro::CalcNonidealEMF");

  // Corned EMFs
  IdefixArray3D<real> ex = this->emf.ex;
  IdefixArray3D<real> ey = this->emf.ey;
  IdefixArray3D<real> ez = this->emf.ez;
  IdefixArray4D<real> J = this->J;
  IdefixArray4D<real> Vs = this->Vs;
  IdefixArray4D<real> Vc = this->Vc;

  // These arrays have been previously computed in calcParabolicFlux
  IdefixArray3D<real> etaArr = this->etaOhmic;
  IdefixArray3D<real> xAmbiArr = this->xAmbipolar;

  // these two are required to ensure that the type is captured by KOKKOS_LAMBDA
  HydroModuleStatus haveResistivity = this->haveResistivity;
  HydroModuleStatus haveAmbipolar = this->haveAmbipolar;

  real etaConstant = this->etaO;
  real xAConstant = this->xA;


#if MHD == YES
  idefix_for("CalcNIEMF",
             data->beg[KDIR],data->end[KDIR]+KOFFSET,
             data->beg[JDIR],data->end[JDIR]+JOFFSET,
             data->beg[IDIR],data->end[IDIR]+IOFFSET,
    KOKKOS_LAMBDA (int k, int j, int i) {
      real Bx1, Bx2, Bx3;
      real Jx1, Jx2, Jx3;
      real eta, xA;
      // CT_EMF_ArithmeticAverage (emf, 0.25);

      if(haveResistivity == Constant)
        eta = etaConstant;
      if(haveAmbipolar == Constant)
        xA = xAConstant;

  #if DIMENSIONS == 3
      // -----------------------
      // X1 EMF Component
      // -----------------------
      Jx1 = J(IDIR,k,j,i);

      // Ohmic resistivity
      if(haveResistivity == UserDefFunction)
        eta = AVERAGE_3D_YZ(etaArr,k,j,i);
      if(haveResistivity)
        ex(k,j,i) += eta * Jx1;

      // Ambipolar diffusion
      if(haveAmbipolar == UserDefFunction)
        xA = AVERAGE_3D_YZ(xAmbiArr,k,j,i);
      if(haveAmbipolar) {
        Bx1 = AVERAGE_4D_XYZ(Vs, BX1s, k,j,i+1);
        Bx2 = AVERAGE_4D_Z(Vs, BX2s, k, j, i);
        Bx3 = AVERAGE_4D_Y(Vs, BX3s, k, j, i);

        // Jx1 is already defined above
        Jx2 = AVERAGE_4D_XY(J, JDIR, k, j, i+1);
        Jx3 = AVERAGE_4D_XZ(J, KDIR, k, j, i+1);

        real JdotB = (Jx1*Bx1 + Jx2*Bx2 + Jx3*Bx3);
        real BdotB = (Bx1*Bx1 + Bx2*Bx2 + Bx3*Bx3);

        ex(k,j,i) += xA * (BdotB*Jx1 - JdotB * Bx1);
      }

      // -----------------------
      // X2 EMF Component
      // -----------------------
      Jx2 = J(JDIR,k,j,i);

      // Ohmic resistivity
      if(haveResistivity == UserDefFunction)
        eta = AVERAGE_3D_XZ(etaArr,k,j,i);
      if(haveResistivity)
        ey(k,j,i) += eta * Jx2;

      // Ambipolar diffusion
      if(haveAmbipolar == UserDefFunction)
        xA = AVERAGE_3D_XZ(xAmbiArr,k,j,i);
      if(haveAmbipolar) {
        Bx1 = AVERAGE_4D_Z(Vs, BX1s, k, j, i);
        Bx2 = AVERAGE_4D_XYZ(Vs, BX2s, k, j+1, i);
        Bx3 = AVERAGE_4D_X(Vs, BX3s, k, j, i);

        // Jx2 is already defined above
        Jx1 = AVERAGE_4D_XY(J, IDIR, k, j+1, i);
        Jx3 = AVERAGE_4D_YZ(J, KDIR, k, j+1, i);

        real JdotB = (Jx1*Bx1 + Jx2*Bx2 + Jx3*Bx3);
        real BdotB = (Bx1*Bx1 + Bx2*Bx2 + Bx3*Bx3);

        ey(k,j,i) += xA * (BdotB*Jx2 - JdotB * Bx2);
      }
  #endif
      // -----------------------
      // X3 EMF Component
      // -----------------------
      Jx3 = J(KDIR,k,j,i);

      // Ohmic resistivity
      if(haveResistivity == UserDefFunction)
        eta = AVERAGE_3D_XY(etaArr,k,j,i);
      if(haveResistivity)
        ez(k,j,i) += eta * Jx3;

      // Ambipolar diffusion
      if(haveAmbipolar == UserDefFunction)
        xA = AVERAGE_3D_XY(xAmbiArr,k,j,i);
      if(haveAmbipolar) {
        Bx1 = AVERAGE_4D_Y(Vs, BX1s, k, j, i);
  #if DIMENSIONS >= 2
        Bx2 = AVERAGE_4D_X(Vs, BX2s, k, j, i);
  #else
        Bx2 = AVERAGE_4D_XY(Vc, BX2, k, j, i);
  #endif

  #if DIMENSIONS == 3
        Bx3 = AVERAGE_4D_XYZ(Vs, BX3s, k+1, j, i);
  #else
        Bx3 = AVERAGE_4D_XY(Vc, BX3, k, j, i);
  #endif

        // Jx3 is already defined above
  #if DIMENSIONS == 3
        Jx1 = AVERAGE_4D_XZ(J, IDIR, k+1, j, i);
        Jx2 = AVERAGE_4D_YZ(J, JDIR, k+1, j, i);
  #else
        Jx1 = AVERAGE_4D_X(J, IDIR, k, j, i);
        Jx2 = AVERAGE_4D_Y(J, JDIR, k, j, i);
  #endif
        real JdotB = (Jx1*Bx1 + Jx2*Bx2 + Jx3*Bx3);
        real BdotB = (Bx1*Bx1 + Bx2*Bx2 + Bx3*Bx3);

        ez(k,j,i) += xA * (BdotB * Jx3 - JdotB * Bx3);
      }
    }
  );
#endif

  idfx::popRegion();
}
