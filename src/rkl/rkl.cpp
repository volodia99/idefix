// ***********************************************************************************
// Idefix MHD astrophysical code
// Copyright(C) 2020-2021 Geoffroy R. J. Lesur <geoffroy.lesur@univ-grenoble-alpes.fr>
// and other code contributors
// Licensed under CeCILL 2.1 License, see COPYING for more information
// ***********************************************************************************

#include <cmath>

#include "rkl.hpp"
#include "dataBlock.hpp"
#include "hydro.hpp"


#ifndef RKL_ORDER
  #define RKL_ORDER       2
#endif


RKLegendre::RKLegendre() {
  // do nothing!
}


void RKLegendre::Init(Input &input, DataBlock &datain) {
  idfx::pushRegion("RKLegendre::Init");

  idfx::cout << "RKLegendre: enabled." << std::endl;
  // Save the datablock to which we are attached from now on
  this->data = &datain;

  dU = IdefixArray4D<real>("RKL_dU", NVAR,
                           data->np_tot[KDIR], data->np_tot[JDIR], data->np_tot[IDIR]);
  dU0 = IdefixArray4D<real>("RKL_dU0", NVAR,
                           data->np_tot[KDIR], data->np_tot[JDIR], data->np_tot[IDIR]);
  Uc1 = IdefixArray4D<real>("RKL_Uc1", NVAR,
                           data->np_tot[KDIR], data->np_tot[JDIR], data->np_tot[IDIR]);

  if(input.CheckEntry("RKL","cfl")>0) {
    cfl_rkl = input.GetReal("RKL","cfl",0);
  } else {
    idfx::cout << "RKLegendre: no RKL cfl given. Using 0.5 by default." << std::endl;
    cfl_rkl = 0.5;
  }
  rmax_par = 100.0;

  idfx::popRegion();
}


void RKLegendre::Cycle() {
  idfx::pushRegion("RKLegendre::Cycle");

  IdefixArray4D<real> dU = this->dU;
  IdefixArray4D<real> dU0 = this->dU0;
  IdefixArray4D<real> Uc = data->hydro.Uc;
  IdefixArray4D<real> Uc0 = data->hydro.Uc0;
  IdefixArray4D<real> Uc1 = this->Uc1;
  real time = data->t;
  real dt_hyp = data->dt;

  // Tell the datablock that we're performing the RKL cycle
  data->rklCycle = true;

  // first RKL stage
  stage = 1;

  // Apply Boundary conditions
  data->hydro.SetBoundary(time);

  // Convert current state into conservative variable
  data->hydro.ConvertPrimToCons();

  Kokkos::deep_copy(Uc0,Uc);

  // evolve RKL stage
  EvolveStage(time);

  ComputeDt();

  Kokkos::deep_copy(dU0,dU);

  // Compute number of RKL steps
  real scrh, nrkl;
#if RKL_ORDER == 1
  scrh  = dt_hyp/dt;
  // Solution of quadratic Eq.
  // 2*dt_hyp/dt_exp = s^2 + s
  nrkl = 4.0*scrh / (1.0 + std::sqrt(1.0 + 2.0*4.0*scrh));
#elif RKL_ORDER == 2
  scrh  = dt_hyp/dt;
  // Solution of quadratic Eq.
  // 4*dt_hyp/dt_exp = s^2 + s - 2
  nrkl = 4.0*(1.0 + 2.0*scrh)
          / (1.0 + sqrt(3.0*3.0 + 4.0*4.0*scrh));
#else
  //#error Invalid RKL_ORDER
#endif
  int rklstages = 1 + floor(nrkl);

  // Compute coefficients
  real w1, mu_tilde_j;
#if RKL_ORDER == 1
  w1 = 2.0/(rklstages*rklstages + rklstages);
  mu_tilde_j = w1;
#elif RKL_ORDER == 2
  real b_j, b_jm1, b_jm2, a_jm1;
  w1 = 4.0/(rklstages*rklstages + rklstages - 2.0);
  mu_tilde_j = w1/3.0;

  b_j = b_jm1 = b_jm2 = 1.0/3.0;
  a_jm1 = 1.0 - b_jm1;
#endif

#if RKL_ORDER == 1
  time = data->t + 0.5*dt_hyp*(stage*stage+stage)*w1;
#elif RKL_ORDER == 2
  time = data->t + 0.25*dt_hyp*(stage*stage+stage-2)*w1;
#endif

  idefix_for("RKL_Cycle_InitUc1",
             data->beg[KDIR],data->end[KDIR],
             data->beg[JDIR],data->end[JDIR],
             data->beg[IDIR],data->end[IDIR],
    KOKKOS_LAMBDA (int k, int j, int i) {
      for(int nv = 0 ; nv < NVAR; nv++) {
        Uc1(nv,k,j,i) = Uc(nv,k,j,i);
      }

      for(int nv = 0 ; nv < NVAR; nv++) {
        Uc(nv,k,j,i) = Uc1(nv,k,j,i) + mu_tilde_j*dt_hyp*dU0(nv,k,j,i);
      }
    }
  );

  // Convert current state into primitive variable
  data->hydro.ConvertConsToPrim();

  real mu_j, nu_j, gamma_j;
  // subStages loop
  for(stage=2; stage <= rklstages ; stage++) {
    //idfx::cout << "RKL: looping stages" << std::endl;
    // compute RKL coefficients
#if RKL_ORDER == 1
    mu_j       = (2.0*stage -1.0)/stage;
    mu_tilde_j = w1*mu_j;
    nu_j       = -(stage -1.0)/stage;
#elif RKL_ORDER == 2
    mu_j       = (2.0*stage -1.0)/stage * b_j/b_jm1;
    mu_tilde_j = w1*mu_j;
    gamma_j    = -a_jm1*mu_tilde_j;
    nu_j       = -(stage -1.0)*b_j/(stage*b_jm2);

    b_jm2 = b_jm1;
    b_jm1 = b_j;
    a_jm1 = 1.0 - b_jm1;
    b_j   = 0.5*(stage*stage+3.0*stage)/(stage*stage+3.0*stage+2.0);
#endif

    // Apply Boundary conditions
    data->hydro.SetBoundary(time);

    // evolve RKL stage
    EvolveStage(time);

    // update Uc
    idefix_for("RKL_Cycle_UpdateUc",
             data->beg[KDIR],data->end[KDIR],
             data->beg[JDIR],data->end[JDIR],
             data->beg[IDIR],data->end[IDIR],
      KOKKOS_LAMBDA (int k, int j, int i) {
        for(int nv = 0 ; nv < COMPONENTS; nv++) {
          real Y                  = mu_j*Uc(MX1+nv,k,j,i) + nu_j*Uc1(MX1+nv,k,j,i);
          Uc1(MX1+nv,k,j,i) = Uc(MX1+nv,k,j,i);
#if RKL_ORDER == 1
          Uc(MX1+nv,k,j,i) = Y + dt_hyp*mu_tilde_j*dU(MX1+nv,k,j,i);
#elif RKL_ORDER == 2
          Uc(MX1+nv,k,j,i) = Y + (1.0 - mu_j - nu_j)*Uc0(MX1+nv,k,j,i)
                                + dt_hyp*mu_tilde_j*dU(MX1+nv,k,j,i)
                                + gamma_j*dt_hyp*dU0(MX1+nv,k,j,i);
#endif
        }
      }
    );

    // Convert current state into primitive variable
    data->hydro.ConvertConsToPrim();

    // increment time
#if RKL_ORDER == 1
    time = data->t + 0.5*dt_hyp*(stage*stage+stage)*w1;
#elif RKL_ORDER == 2
    time = data->t + 0.25*dt_hyp*(stage*stage+stage-2)*w1;
#endif
  }

  // Tell the datablock that we're done
  data->rklCycle = false;
  idfx::popRegion();
}


void RKLegendre::ResetFlux() {
  IdefixArray4D<real> Flux = data->hydro.FluxRiemann;
  idefix_for("InitRKLStage_dU_Flux",
             0,NVAR,
             0,data->np_tot[KDIR],
             0,data->np_tot[JDIR],
             0,data->np_tot[IDIR],
    KOKKOS_LAMBDA (int nv, int k, int j, int i) {
      Flux(nv,k,j,i) = ZERO_F;
    }
  );
}

void RKLegendre::ResetStage() {
  idfx::pushRegion("RKLegendre::ResetStage");

  IdefixArray4D<real> dU = this->dU;
  IdefixArray4D<real> Flux = data->hydro.FluxRiemann;
  idefix_for("InitRKLStage_dU_Flux",
             0,NVAR,
             0,data->np_tot[KDIR],
             0,data->np_tot[JDIR],
             0,data->np_tot[IDIR],
    KOKKOS_LAMBDA (int nv, int k, int j, int i) {
      dU(nv,k,j,i) = ZERO_F;
    }
  );

  IdefixArray3D<real> invDt = data->hydro.InvDt;
  idefix_for("InitRKLStage_invDt",
             0,data->np_tot[KDIR],
             0,data->np_tot[JDIR],
             0,data->np_tot[IDIR],
    KOKKOS_LAMBDA (int k, int j, int i) {
      invDt(k,j,i) = ZERO_F;
    }
  );

  idfx::popRegion();
}


void RKLegendre::ComputeDt() {
  idfx::pushRegion("RKLegendre::ComputeDt");

  IdefixArray3D<real> invDt = data->hydro.InvDt;

  real newinvdt = ZERO_F;
  Kokkos::parallel_reduce("RKL_Timestep_reduction",
    Kokkos::MDRangePolicy<Kokkos::Rank<3, Kokkos::Iterate::Right, Kokkos::Iterate::Right>>
    ({0,0,0},{data->end[KDIR],data->end[JDIR],data->end[IDIR]}),
    KOKKOS_LAMBDA (int k, int j, int i, real &invdt) {
      invdt = std::fmax(invDt(k,j,i), invdt);
    },
    Kokkos::Max<real>(newinvdt)
  );

#ifdef WITH_MPI
  if(idfx::psize>1) {
    MPI_SAFE_CALL(MPI_Allreduce(MPI_IN_PLACE, &newinvdt, 1, realMPI, MPI_MAX, MPI_COMM_WORLD));
  }
#endif

  dt = 1.0/newinvdt;
  dt = (cfl_rkl*dt)/2.0; // parabolic time step

  idfx::popRegion();
}


void RKLegendre::EvolveStage(real t) {
  idfx::pushRegion("RKLegendre::EvolveStage");

  ResetStage();

  for(int dir = 0 ; dir < DIMENSIONS ; dir++) {
    ResetFlux();
    // CalcParabolicFlux
    data->hydro.CalcParabolicFlux(dir, t);

    // Calc Right Hand Side
    CalcParabolicRHS(dir, t);
  }

  idfx::popRegion();
}


void RKLegendre::CalcParabolicRHS(int dir, real t) {
  idfx::pushRegion("RKLegendre::CalcParabolicRHS");

  IdefixArray4D<real> Flux = data->hydro.FluxRiemann;
  IdefixArray3D<real> A    = data->A[dir];
  IdefixArray3D<real> dV   = data->dV;
  IdefixArray1D<real> x1m  = data->xl[IDIR];
  IdefixArray1D<real> x1   = data->x[IDIR];
  IdefixArray1D<real> sm   = data->sinx2m;
  IdefixArray1D<real> rt   = data->rt;
  IdefixArray1D<real> dmu  = data->dmu;
  IdefixArray1D<real> s    = data->sinx2;
  IdefixArray1D<real> dx   = data->dx[dir];
  IdefixArray1D<real> dx2  = data->dx[JDIR];
  IdefixArray3D<real> invDt = data->hydro.InvDt;
  IdefixArray3D<real> dMax = data->hydro.dMax;
  IdefixArray4D<real> viscSrc = data->hydro.viscosity.viscSrc;
  IdefixArray4D<real> dU = this->dU;

  bool haveViscosity = data->hydro.viscosityStatus.isRKL;

  int ioffset,joffset,koffset;
  ioffset=joffset=koffset=0;
  // Determine the offset along which we do the extrapolation
  if(dir==IDIR) ioffset=1;
  if(dir==JDIR) joffset=1;
  if(dir==KDIR) koffset=1;


  idefix_for("CalcTotalFlux",
             data->beg[KDIR],data->end[KDIR]+koffset,
             data->beg[JDIR],data->end[JDIR]+joffset,
             data->beg[IDIR],data->end[IDIR]+ioffset,
    KOKKOS_LAMBDA (int k, int j, int i) {
      real Ax = A(k,j,i);

#if GEOMETRY != CARTESIAN
      if(Ax<SMALL_NUMBER)
        Ax=SMALL_NUMBER;    // Essentially to avoid singularity around poles
#endif

      for(int nv = 0 ; nv < COMPONENTS ; nv++) {
        Flux(nv+VX1,k,j,i) = Flux(nv+VX1,k,j,i) * Ax;
      }


      // Curvature terms
#if    (GEOMETRY == POLAR       && COMPONENTS >= 2) \
    || (GEOMETRY == CYLINDRICAL && COMPONENTS == 3)
      if(dir==IDIR) {
        // Conserve angular momentum, hence flux is R*Bphi
        Flux(iMPHI,k,j,i) = Flux(iMPHI,k,j,i) * FABS(x1m(i));
      }
#endif // GEOMETRY==POLAR OR CYLINDRICAL

#if GEOMETRY == SPHERICAL
      if(dir==IDIR) {
  #if COMPONENTS == 3
        Flux(iMPHI,k,j,i) = Flux(iMPHI,k,j,i) * FABS(x1m(i));
  #endif // COMPONENTS == 3
      } else if(dir==JDIR) {
  #if COMPONENTS == 3
        Flux(iMPHI,k,j,i) = Flux(iMPHI,k,j,i) * FABS(sm(j));
  #endif // COMPONENTS = 3
      }
#endif // GEOMETRY == SPHERICAL
    }
  );

  int stage = this->stage;

  idefix_for("CalcRightHandSide",
             data->beg[KDIR],data->end[KDIR],
             data->beg[JDIR],data->end[JDIR],
             data->beg[IDIR],data->end[IDIR],
    KOKKOS_LAMBDA (int k, int j, int i) {
      real rhs[COMPONENTS];

#pragma unroll
      for(int nv = 0 ; nv < COMPONENTS ; nv++) {
        rhs[nv] = -  ( Flux(nv + VX1, k+koffset, j+joffset, i+ioffset)
                     - Flux(nv + VX1, k, j, i))/dV(k,j,i);
        // Viscosity source terms
        if(haveViscosity) rhs[nv] += viscSrc(nv,k,j,i);
      }

#if GEOMETRY != CARTESIAN
      if(dir==IDIR) {
  #ifdef iMPHI
        rhs[iMPHI-1] = rhs[iMPHI-1] / x1(i);
  #endif

      } else if(dir==JDIR) {
  #if (GEOMETRY == SPHERICAL) && (COMPONENTS == 3)
        rhs[iMPHI-1] /= FABS(s(j));
  #endif // GEOMETRY
      }
      // Nothing for KDIR

#endif // GEOMETRY != CARTESIAN

      // store the field components
#pragma unroll
      for(int nv = 0 ; nv < COMPONENTS ; nv++) {
        dU(nv + VX1,k,j,i) += rhs[nv];
      }


      if (stage == 1) {
        // Compute dt from max signal speed
        const int ig = ioffset*i + joffset*j + koffset*k;
        real dl = dx(ig);
#if GEOMETRY == POLAR
        if(dir==JDIR)
          dl = dl*x1(i);

#elif GEOMETRY == SPHERICAL
        if(dir==JDIR)
          dl = dl*rt(i);
        else
          if(dir==KDIR)
            dl = dl*rt(i)*dmu(j)/dx2(j);
#endif

        invDt(k,j,i) += 0.5 * std::fmax(dMax(k+koffset,j+joffset,i+ioffset),
                                                dMax(k,j,i)) / (dl*dl);
      }
    }
  );

  idfx::popRegion();
}
