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

#ifndef INCLUDE_EXADG_DARCY_USER_INTERFACE_APPLICATION_BASE_H_
#define INCLUDE_EXADG_DARCY_USER_INTERFACE_APPLICATION_BASE_H_

// deal.II
#include <deal.II/distributed/fully_distributed_tria.h>
#include <deal.II/distributed/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/manifold_lib.h>

// ExaDG
#include <exadg/darcy/postprocessor/postprocessor.h>
#include <exadg/darcy/user_interface/field_functions.h>
#include <exadg/darcy/user_interface/parameters.h>
#include <exadg/grid/grid.h>
#include <exadg/incompressible_navier_stokes/user_interface/boundary_descriptor.h>
#include <exadg/utilities/output_parameters.h>
#include <exadg/utilities/resolution_parameters.h>

#include <utility>

namespace ExaDG
{

namespace Darcy
{
template<int dim, typename Number>
class ApplicationBase
{
public:
  virtual void
  add_parameters(dealii::ParameterHandler & prm)
  {
    output_parameters.add_parameters(prm);
  }

  ApplicationBase(std::string parameter_file, MPI_Comm const & comm)
    : mpi_comm(comm),
      pcout(std::cout, dealii::Utilities::MPI::this_mpi_process(mpi_comm) == 0),
      parameter_file(std::move(parameter_file))
  {
  }

  virtual ~ApplicationBase() = default;

  virtual void
  setup()
  {
    parse_parameters();

    set_parameters();
    param.check();
    param.print(pcout, "List of parameters:");

    // grid
    grid = std::make_shared<Grid<dim>>(param.spatial_disc.grid_data, mpi_comm);
    create_grid();
    print_grid_info(pcout, *grid);

    // boundary conditions
    boundary_descriptor = std::make_shared<IncNS::BoundaryDescriptor<dim>>();
    set_boundary_descriptor();
    verify_boundary_conditions<dim, Number>(*boundary_descriptor, *grid);

    // field functions
    field_functions = std::make_shared<FieldFunctions<dim, Number>>();
    set_field_functions();
  }

  virtual std::shared_ptr<Darcy::PostProcessor<dim, Number>>
  create_postprocessor() = 0;

  Parameters const &
  get_parameters() const
  {
    return param;
  }

  std::shared_ptr<Grid<dim> const>
  get_grid() const
  {
    return grid;
  }

  std::shared_ptr<IncNS::BoundaryDescriptor<dim> const>
  get_boundary_descriptor() const
  {
    return boundary_descriptor;
  }

  std::shared_ptr<FieldFunctions<dim, Number> const>
  get_field_functions() const
  {
    return field_functions;
  }

  // Analytical mesh motion
  virtual std::shared_ptr<dealii::Function<dim>>
  create_mesh_movement_function()
  {
    std::shared_ptr<dealii::Function<dim>> mesh_motion =
      std::make_shared<dealii::Functions::ZeroFunction<dim>>(dim);

    return mesh_motion;
  }
  
protected:
  virtual void
  parse_parameters()
  {
    dealii::ParameterHandler prm;
    this->add_parameters(prm);
    prm.parse_input(parameter_file, "", true, true);
  }

  MPI_Comm const & mpi_comm;

  dealii::ConditionalOStream pcout;

  Parameters param;

  std::shared_ptr<Grid<dim>> grid;

  std::shared_ptr<FieldFunctions<dim, Number>>    field_functions;
  std::shared_ptr<IncNS::BoundaryDescriptor<dim>> boundary_descriptor;

  std::string parameter_file;

  OutputParameters output_parameters;

private:
  virtual void
  set_parameters() = 0;

  virtual void
  create_grid() = 0;

  virtual void
  set_boundary_descriptor() = 0;

  virtual void
  set_field_functions() = 0;
};

} // namespace Darcy
} // namespace ExaDG


#endif /* INCLUDE_EXADG_DARCY_USER_INTERFACE_APPLICATION_BASE_H_ */