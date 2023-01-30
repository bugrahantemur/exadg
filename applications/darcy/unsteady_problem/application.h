/*  ______________________________________________________________________
 *
 *  ExaDG - High-Order Discontinuous Galerkin for the Exa-Scale
 *
 *  Copyright (C) 2021 by the ExaDG authors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *  ______________________________________________________________________
 */

#ifndef APPLICATIONS_DARCY_TEST_CASES_UNSTEADY_PROBLEM_H_
#define APPLICATIONS_DARCY_TEST_CASES_UNSTEADY_PROBLEM_H_

namespace ExaDG
{
namespace Darcy
{
template<int dim>
class InflowVelocityBC : public dealii::Function<dim>
{
public:
  InflowVelocityBC(double const inflow_velocity, double const end_time)
    : dealii::Function<dim>(dim, 0.0), inflow_velocity(inflow_velocity), end_time(end_time)
  {
  }

  double
  value(dealii::Point<dim> const & /*p*/, unsigned int const component = 0) const
  {
    if(component == 0)
    {
      double const t = this->get_time();

      return inflow_velocity * (0.5 * std::cos(dealii::numbers::PI * (1.0 + t / end_time)) + 0.5);
      // return inflow_velocity * t / end_time;
    }

    return 0.0;
  }

private:
  double const inflow_velocity;
  double const end_time;
};

template<int dim>
class InversePermeabilityField : public dealii::Function<dim>
{
public:
  double
  value(dealii::Point<dim> const & /*p*/, unsigned int const component = 0) const
  {
    Assert(component < dim * dim,
           dealii::ExcMessage("Trying to access a tensor coordinate out-of-bounds."));

    return component % (dim + 1) == 0 ? 2.0e10 : 0.0;
  }
};

template<int dim>
class PorosityField : public dealii::Function<dim>
{
public:
  double
  value(dealii::Point<dim> const & p, unsigned int const component = 0) const
  {
    (void)component;
    return 0.3 * (1.0 + p[0]);
  }
};

template<int dim, typename Number>
class Application : public ApplicationBase<dim, Number>
{
public:
  Application(std::string input_file, MPI_Comm const & comm)
    : ApplicationBase<dim, Number>(input_file, comm)
  {
  }

  void
  add_parameters(dealii::ParameterHandler & prm) final
  {
    ApplicationBase<dim, Number>::add_parameters(prm);

    // clang-format off
    prm.enter_subsection("Application");
      prm.add_parameter("ApplySymmetryBC",
                        apply_symmetry_bc,
                        "Apply symmetry boundary condition.",
                        dealii::Patterns::Bool());
    prm.leave_subsection();
    // clang-format on
  }

private:
  void
  parse_parameters() final
  {
    ApplicationBase<dim, Number>::parse_parameters();
  }
  void
  set_parameters() final
  {
    // MATHEMATICAL MODEL
    this->param.math_model.problem_type    = ProblemType::Unsteady;
    this->param.math_model.right_hand_side = false;

    // PHYSICAL QUANTITIES
    this->param.physical_quantities.start_time = start_time;
    this->param.physical_quantities.end_time   = end_time;
    this->param.physical_quantities.viscosity  = viscosity;
    this->param.physical_quantities.density    = density;

    // TEMPORAL DISCRETIZATION
    this->param.temporal_disc.solver_type           = TemporalSolverType::Unsteady;
    this->param.temporal_disc.method                = TemporalDiscretizationMethod::BDFCoupled;
    this->param.temporal_disc.order_time_integrator = 3;
    this->param.temporal_disc.start_with_low_order  = true;
    this->param.temporal_disc.calculation_of_time_step_size = TimeStepCalculation::UserSpecified;
    this->param.temporal_disc.time_step_size                = 1.0e-1;

    // output of solver information
    this->param.temporal_disc.solver_info_data.interval_time_steps = 1;

    // SPATIAL DISCRETIZATION
    this->param.spatial_disc.degree_u = 3;
    this->param.spatial_disc.degree_p = DegreePressure::MixedOrder;

    this->param.spatial_disc.method                       = SpatialDiscretizationMethod::L2;
    this->param.spatial_disc.grid_data.triangulation_type = TriangulationType::Distributed;
    this->param.spatial_disc.grid_data.n_refine_global    = 3;
    this->param.spatial_disc.grid_data.mapping_degree     = this->param.spatial_disc.degree_u;


    // COUPLED DARCY SOLVER

    // linear solver
    this->param.linear_solver.method = LinearSolverMethod::FGMRES;
    this->param.linear_solver.data   = SolverData(1e4, 1.e-12, 1.e-8);

    this->param.linear_solver.preconditioner.update                  = true;
    this->param.linear_solver.preconditioner.update_every_time_steps = 1;

    // preconditioning linear solver
    this->param.linear_solver.preconditioner.type = PreconditionerCoupled::BlockTriangular;

    // preconditioner velocity/momentum block
    this->param.linear_solver.preconditioner.velocity_block.type =
      VelocityBlockPreconditioner::BlockJacobi;
    this->param.linear_solver.preconditioner.velocity_block.block_jacobi.implement_matrix_free =
      true;

    // preconditioner Schur-complement block
    this->param.linear_solver.preconditioner.schur_complement.type =
      SchurComplementPreconditioner::LaplaceOperator;

    // NUMERICAL PARAMETERS
    this->param.numerical.use_cell_based_face_loops = true;
  }

  void
  create_grid() final
  {
    double const              y_upper = H / 2;
    double const              y_lower = -H / 2;
    dealii::Point<dim>        point1(0.0, y_lower), point2(L, y_upper);
    std::vector<unsigned int> repetitions({1, 1});
    dealii::GridGenerator::subdivided_hyper_rectangle(*this->grid->triangulation,
                                                      repetitions,
                                                      point1,
                                                      point2);

    // set boundary indicator
    for(auto cell : this->grid->triangulation->cell_iterators())
    {
      for(auto const & face : cell->face_indices())
      {
        if((std::fabs(cell->face(face)->center()(0) - 0.0) < 1e-12))
          cell->face(face)->set_boundary_id(1);
        if((std::fabs(cell->face(face)->center()(0) - L) < 1e-12))
          cell->face(face)->set_boundary_id(2);
      }
    }

    this->grid->triangulation->refine_global(this->param.spatial_disc.grid_data.n_refine_global);
  }

  void
  set_boundary_descriptor() final
  {
    using pair =
      typename std::pair<dealii::types::boundary_id, std::shared_ptr<dealii::Function<dim>>>;

    // fill boundary descriptor velocity
    {
      // inflow
      {
        this->boundary_descriptor->velocity->dirichlet_bc.insert(
          pair(1, new InflowVelocityBC<dim>(this->inflow_velocity, end_time)));
      }
      // outflow
      {
        this->boundary_descriptor->velocity->neumann_bc.insert(
          pair(2, new dealii::Functions::ZeroFunction<dim>(dim)));
      }

      // slip-walls (symmetry bc)
      if(apply_symmetry_bc)
      {
        // slip boundary condition: always u*n=0
        // function will not be used -> use ZeroFunction
        this->boundary_descriptor->velocity->symmetry_bc.insert(
          pair(0, new dealii::Functions::ZeroFunction<dim>(dim)));
      }
      else
      {
        // outflow
        this->boundary_descriptor->velocity->neumann_bc.insert(
          pair(0, new dealii::Functions::ZeroFunction<dim>(dim)));
      }
    }
    // fill boundary descriptor pressure
    {
      {
        // inflow
        this->boundary_descriptor->pressure->neumann_bc.insert(1);
        // outflow
        this->boundary_descriptor->pressure->dirichlet_bc.insert(
          pair(2, new dealii::Functions::ConstantFunction<dim>(0.0, 1)));
      }

      if(apply_symmetry_bc)
      {
        // slip-walls
        this->boundary_descriptor->pressure->neumann_bc.insert(0);
      }
      else
      {
        // outflow
        this->boundary_descriptor->pressure->dirichlet_bc.insert(
          pair(0, new dealii::Functions::ConstantFunction<dim>(0.0, 1)));
      }
    }
  }

  void
  set_field_functions() final
  {
    // Initial solution
    {
      this->field_functions->initial_solution_velocity.reset(
        new dealii::Functions::ZeroFunction<dim>(dim));
      this->field_functions->initial_solution_pressure.reset(
        new dealii::Functions::ZeroFunction<dim>(1));
    }
    // RHS
    this->field_functions->right_hand_side.reset(new dealii::Functions::ZeroFunction<dim>(dim));

    // Problem coefficients
    {
      this->field_functions->inverse_permeability_field.reset(new InversePermeabilityField<dim>());
      this->field_functions->porosity_field.reset(new PorosityField<dim>());
    }
  }

  std::shared_ptr<PostProcessor<dim, Number>>
  create_postprocessor() final
  {
    Darcy::PostProcessorData<dim> pp_data;

    // write output for visualization of results
    pp_data.output_data.time_control_data.is_active = this->output_parameters.write;

    pp_data.output_data.time_control_data.start_time               = start_time;
    pp_data.output_data.time_control_data.trigger_every_time_steps = 1;

    pp_data.output_data.directory = this->output_parameters.directory + "vtu/";
    pp_data.output_data.filename  = this->output_parameters.filename;

    pp_data.output_data.write_divergence = false;

    pp_data.output_data.degree             = this->param.spatial_disc.degree_u;
    pp_data.output_data.write_higher_order = true;

    std::shared_ptr<PostProcessor<dim, Number>> pp;
    pp.reset(new Darcy::PostProcessor<dim, Number>(pp_data, this->mpi_comm));

    return pp;
  }

  bool apply_symmetry_bc = true;

  IncNS::FormulationViscousTerm const formulation_viscous_term =
    IncNS::FormulationViscousTerm::LaplaceFormulation;

  double const inflow_velocity = 1.0e-3;
  double const viscosity       = 1.81e-5;
  double const density         = 1.2;

  double const H = 3.0e-2;
  double const L = 3.0e-2;

  double const start_time = 0.0;
  double const end_time   = 1.0;
};

} // namespace Darcy
} // namespace ExaDG

#include <exadg/darcy/user_interface/implement_get_application.h>


#endif // APPLICATIONS_DARCY_TEST_CASES_UNSTEADY_PROBLEM_H_