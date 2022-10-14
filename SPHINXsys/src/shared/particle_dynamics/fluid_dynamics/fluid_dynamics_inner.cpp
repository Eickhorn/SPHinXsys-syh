/**
 * @file 	fluid_dynamics.cpp
 * @author	Chi Zhang and Xiangyu Hu
 */

#include "fluid_dynamics_inner.h"
#include "fluid_dynamics_inner.hpp"

namespace SPH
{
	//=================================================================================================//
	namespace fluid_dynamics
	{
		//=================================================================================================//
		FluidInitialCondition::
			FluidInitialCondition(SPHBody &sph_body)
			: LocalDynamics(sph_body), FluidDataSimple(sph_body),
			  pos_(particles_->pos_), vel_(particles_->vel_) {}
		//=================================================================================================//
		DensitySummationInner::DensitySummationInner(BaseInnerRelation &inner_relation)
			: LocalDynamics(inner_relation.sph_body_), FluidDataInner(inner_relation),
			  rho_(particles_->rho_), rho_sum_(particles_->rho_sum_), mass_(particles_->mass_),
			  W0_(sph_body_.sph_adaptation_->getKernel()->W0(Vecd(0))),
			  rho0_(particles_->rho0_), inv_sigma0_(1.0 / particles_->sigma0_) {}
		//=================================================================================================//
		void DensitySummationInner::interaction(size_t index_i, Real dt)
		{
			/** Inner interaction. */
			Real sigma = W0_;
			const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
				sigma += inner_neighborhood.W_ij_[n];

			rho_sum_[index_i] = sigma * rho0_ * inv_sigma0_;
		}
		//=================================================================================================//
		void DensitySummationInner::update(size_t index_i, Real dt)
		{
			rho_[index_i] = ReinitializedDensity(rho_sum_[index_i], rho0_, rho_[index_i]);
		}
		//=================================================================================================//
		DensitySummationInnerVariableSmoothingLength::DensitySummationInnerVariableSmoothingLength(BaseBodyRelationInner& inner_relation) :
			DensitySummationInner(inner_relation), sph_body_(&inner_relation.sph_body_),
			h_ratio_(dynamic_cast<ParticleWithLocalRefinement&>(*inner_relation.sph_body_.sph_adaptation_).h_ratio_) {}
		//=================================================================================================//
		void DensitySummationInnerVariableSmoothingLength::setupDynamics(Real dt)
		{
			inv_sigma0_.resize(rho_.size());
		}
		//=================================================================================================//
		void DensitySummationInnerVariableSmoothingLength::interaction(size_t index_i, Real dt)
		{
			inv_sigma0_[index_i]=1.0 / sph_body_->sph_adaptation_->computeReferenceNumberDensity(Vecd(0), h_ratio_[index_i]);
			Real sigma_i = sph_body_->sph_adaptation_->getKernel()->W0(h_ratio_[index_i], Vecd(0));
			Real sigma_mass = mass_[index_i];
			Real inv_Vol_i = rho0_ / mass_[index_i];

			/** Inner interaction. */
			Neighborhood& inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n) 
				sigma_i += inner_neighborhood.W_ij_[n] * inv_Vol_i*mass_[inner_neighborhood.j_[n]] / rho0_;

			rho_sum_[index_i] = sigma_i * rho0_ *inv_sigma0_[index_i];
		}
		//=================================================================================================//
		BaseViscousAccelerationInner::BaseViscousAccelerationInner(BaseBodyRelationInner &inner_relation)
			: LocalDynamics(inner_relation.sph_body_), FluidDataInner(inner_relation),
			  rho_(particles_->rho_), vel_(particles_->vel_), acc_prior_(particles_->acc_prior_),
			  mu_(particles_->fluid_.ReferenceViscosity()),
			  smoothing_length_(sph_body_.sph_adaptation_->ReferenceSmoothingLength()) {}
		//=================================================================================================//
		void ViscousAccelerationInner::interaction(size_t index_i, Real dt)
		{
			Vecd acceleration(0), vel_derivative(0);
			const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];

				// viscous force
				vel_derivative = (vel_[index_i] - vel_[index_j]) / (inner_neighborhood.r_ij_[n] + 0.01 * smoothing_length_);
				acceleration += 2.0 * mu_ * vel_derivative * inner_neighborhood.dW_ijV_j_[n];
			}

			acc_prior_[index_i] += acceleration / rho_[index_i];
		}
		//=================================================================================================//
		void AngularConservativeViscousAccelerationInner::interaction(size_t index_i, Real dt)
		{
			Vecd acceleration(0);
			Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];
				Vecd &e_ij = inner_neighborhood.e_ij_[n];
				Real r_ij = inner_neighborhood.r_ij_[n];

				/** The following viscous force is given in Monaghan 2005 (Rep. Prog. Phys.), it seems that
				 * this formulation is more accurate than the previous one for Taylor-Green-Vortex flow. */
				Real v_r_ij = dot(vel_[index_i] - vel_[index_j], r_ij * e_ij);
				Real eta_ij = 8.0 * mu_ * v_r_ij / (r_ij * r_ij + 0.01 * smoothing_length_);
				acceleration += eta_ij * inner_neighborhood.dW_ijV_j_[n] * e_ij;
			}

			acc_prior_[index_i] += acceleration / rho_[index_i];
		}
		//=================================================================================================//
		TransportVelocityCorrectionInner::
			TransportVelocityCorrectionInner(BaseInnerRelation &inner_relation, Real coefficient)
			: LocalDynamics(inner_relation.sph_body_), FluidDataInner(inner_relation),
			  rho_(particles_->rho_), pos_(particles_->pos_),
			  surface_indicator_(particles_->surface_indicator_), p_background_(0),
			  coefficient_(coefficient) {}
		//=================================================================================================//
		void TransportVelocityCorrectionInner::setupDynamics(Real dt)
		{
			Real speed_max = particles_->speed_max_;
			Real density = particles_->fluid_.ReferenceDensity();
			p_background_ = coefficient_ * density * speed_max * speed_max;
		}
		//=================================================================================================//
		void TransportVelocityCorrectionInner::interaction(size_t index_i, Real dt)
		{
			Vecd acceleration_trans(0);
			const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];
				Vecd nablaW_ijV_j = inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];

				// acceleration for transport velocity
				acceleration_trans -= 2.0 * p_background_ * nablaW_ijV_j;
			}

			if (surface_indicator_[index_i] == 0)
				pos_[index_i] += 0.5 * acceleration_trans * dt * dt / rho_[index_i];
		}
		//=================================================================================================//
		AcousticTimeStepSize::AcousticTimeStepSize(SPHBody &sph_body, Real acousticCFL)
			: LocalDynamicsReduce<Real, ReduceMax>(sph_body, Real(0)),
			  FluidDataSimple(sph_body), fluid_(particles_->fluid_), rho_(particles_->rho_),
			  p_(particles_->p_), vel_(particles_->vel_),
			  smoothing_length_(sph_body.sph_adaptation_->ReferenceSmoothingLength()),
			  acousticCFL_(acousticCFL) {}
		//=================================================================================================//
		Real AcousticTimeStepSize::reduce(size_t index_i, Real dt)
		{
			return fluid_.getSoundSpeed(p_[index_i], rho_[index_i]) + vel_[index_i].norm();
		}
		//=================================================================================================//
		Real AcousticTimeStepSize::outputResult(Real reduced_value)
		{
			// since the particle does not change its configuration in pressure relaxation step
			// I chose a time-step size according to Eulerian method
			return acousticCFL_ * smoothing_length_ / (reduced_value + TinyReal);
		}
		//=================================================================================================//
		AcousticTimeStepSizeVariableSmoothingLength::AcousticTimeStepSizeVariableSmoothingLength(SPHBody &sph_body)
			: AcousticTimeStepSize(sph_body)
		{
			smoothing_length_ = sph_body.sph_adaptation_->ReferenceSmoothingLength() * 
				sph_body.sph_adaptation_->MinimumSpacing() / sph_body.sph_adaptation_->ReferenceSpacing();
		}
		//=================================================================================================//
		AdvectionTimeStepSizeForImplicitViscosity::
			AdvectionTimeStepSizeForImplicitViscosity(SPHBody &sph_body, Real U_max, Real advectionCFL)
			: LocalDynamicsReduce<Real, ReduceMax>(sph_body, U_max * U_max),
			  FluidDataSimple(sph_body), vel_(particles_->vel_),
			  smoothing_length_(sph_body.sph_adaptation_->ReferenceSmoothingLength()),
			  advectionCFL_(advectionCFL) {}
		//=================================================================================================//
		Real AdvectionTimeStepSizeForImplicitViscosity::reduce(size_t index_i, Real dt)
		{
			return vel_[index_i].normSqr();
		}
		//=================================================================================================//
		Real AdvectionTimeStepSizeForImplicitViscosity::outputResult(Real reduced_value)
		{
			Real speed_max = sqrt(reduced_value);
			particles_->speed_max_ = speed_max;
			return advectionCFL_ * smoothing_length_ / (speed_max + TinyReal);
		}
		//=================================================================================================//
		AdvectionTimeStepSize::AdvectionTimeStepSize(SPHBody &sph_body, Real U_max, Real advectionCFL)
			: AdvectionTimeStepSizeForImplicitViscosity(sph_body, U_max, advectionCFL),
			  fluid_(particles_->fluid_)
		{
			Real viscous_speed = fluid_.ReferenceViscosity() / fluid_.ReferenceDensity() / smoothing_length_;
			reference_ = SMAX(viscous_speed * viscous_speed, reference_);
		}
		//=================================================================================================//
		Real AdvectionTimeStepSize::reduce(size_t index_i, Real dt)
		{
			return AdvectionTimeStepSizeForImplicitViscosity::reduce(index_i, dt);
		}
		//=================================================================================================//
		AdvectionTimeStepSizeVariableSmoothingLength::AdvectionTimeStepSizeVariableSmoothingLength(SPHBody &sph_body, Real U_max)
			: AdvectionTimeStepSize(sph_body, U_max)
		{
			smoothing_length_ = sph_body.sph_adaptation_->ReferenceSmoothingLength() * 
				sph_body.sph_adaptation_->MinimumSpacing() / sph_body.sph_adaptation_->ReferenceSpacing();
		}
		//=================================================================================================//
		VorticityInner::VorticityInner(BaseBodyRelationInner &inner_relation)
			: LocalDynamics(inner_relation.sph_body_), FluidDataInner(inner_relation),
			  vel_(particles_->vel_)
		{
			particles_->registerVariable(vorticity_, "VorticityInner");
			particles_->addVariableToWrite<AngularVecd>("VorticityInner");
		}
		//=================================================================================================//
		void VorticityInner::interaction(size_t index_i, Real dt)
		{
			AngularVecd vorticity(0);
			const Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];

				vorticity += SimTK::cross(vel_[index_i] - vel_[index_j], inner_neighborhood.e_ij_[n]) *
							 inner_neighborhood.dW_ijV_j_[n];
			}

			vorticity_[index_i] = vorticity;
		}
		//=================================================================================================//
		BaseIntegration::BaseIntegration(BaseInnerRelation &inner_relation)
			: LocalDynamics(inner_relation.sph_body_), FluidDataInner(inner_relation),
			  fluid_(particles_->fluid_), rho_(particles_->rho_),
			  p_(particles_->p_), drho_dt_(particles_->drho_dt_),
			  pos_(particles_->pos_), vel_(particles_->vel_),
			  acc_(particles_->acc_), acc_prior_(particles_->acc_prior_) {}
		//=================================================================================================//
		Oldroyd_BIntegration1stHalf ::
			Oldroyd_BIntegration1stHalf(BaseInnerRelation &inner_relation)
			: Integration1stHalfDissipativeRiemann(inner_relation),
			  tau_(DynamicCast<ViscoelasticFluidParticles>(this, particles_)->tau_),
			  dtau_dt_(DynamicCast<ViscoelasticFluidParticles>(this, particles_)->dtau_dt_) {}
		//=================================================================================================//
		void Oldroyd_BIntegration1stHalf::initialization(size_t index_i, Real dt)
		{
			Integration1stHalfDissipativeRiemann::initialization(index_i, dt);

			tau_[index_i] += dtau_dt_[index_i] * dt * 0.5;
		}
		//=================================================================================================//
		void Oldroyd_BIntegration1stHalf::interaction(size_t index_i, Real dt)
		{
			Integration1stHalfDissipativeRiemann::interaction(index_i, dt);

			Vecd acceleration(0);
			Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];
				Vecd nablaW_ijV_j = inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];

				// elastic force
				acceleration += (tau_[index_i] + tau_[index_j]) * nablaW_ijV_j;
			}

			acc_[index_i] += acceleration / rho_[index_i];
		}
		//=================================================================================================//
		Oldroyd_BIntegration2ndHalf::
			Oldroyd_BIntegration2ndHalf(BaseInnerRelation &inner_relation)
			: Integration2ndHalfDissipativeRiemann(inner_relation),
			  oldroyd_b_fluid_(DynamicCast<Oldroyd_B_Fluid>(this, particles_->fluid_)),
			  tau_(DynamicCast<ViscoelasticFluidParticles>(this, particles_)->tau_),
			  dtau_dt_(DynamicCast<ViscoelasticFluidParticles>(this, particles_)->dtau_dt_)
		{
			mu_p_ = oldroyd_b_fluid_.ReferencePolymericViscosity();
			lambda_ = oldroyd_b_fluid_.getReferenceRelaxationTime();
		}
		//=================================================================================================//
		void Oldroyd_BIntegration2ndHalf::interaction(size_t index_i, Real dt)
		{
			Integration2ndHalfDissipativeRiemann::interaction(index_i, dt);

			Matd tau_i = tau_[index_i];
			Matd stress_rate(0);
			Neighborhood &inner_neighborhood = inner_configuration_[index_i];
			for (size_t n = 0; n != inner_neighborhood.current_size_; ++n)
			{
				size_t index_j = inner_neighborhood.j_[n];
				Vecd nablaW_ijV_j = inner_neighborhood.dW_ijV_j_[n] * inner_neighborhood.e_ij_[n];

				Matd velocity_gradient = -SimTK::outer((vel_[index_i] - vel_[index_j]), nablaW_ijV_j);
				stress_rate += ~velocity_gradient * tau_i + tau_i * velocity_gradient - tau_i / lambda_ +
							   (~velocity_gradient + velocity_gradient) * mu_p_ / lambda_;
			}

			dtau_dt_[index_i] = stress_rate;
		}
		//=================================================================================================//
		void Oldroyd_BIntegration2ndHalf::update(size_t index_i, Real dt)
		{
			Integration2ndHalfDissipativeRiemann::update(index_i, dt);

			tau_[index_i] += dtau_dt_[index_i] * dt * 0.5;
		}
		//=================================================================================================//
	}
	//=================================================================================================//
}
//=================================================================================================//