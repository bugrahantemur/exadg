/*
 * DGNavierStokesBase.h
 *
 *  Created on: Jun 27, 2016
 *      Author: fehn
 */

#ifndef INCLUDE_DGNAVIERSTOKESBASE_H_
#define INCLUDE_DGNAVIERSTOKESBASE_H_

#include <deal.II/numerics/vector_tools.h>

#include <deal.II/matrix_free/operators.h>

#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_values.h>
#include <deal.II/fe/fe_system.h>
#include <deal.II/fe/mapping_q.h>

#include "MatrixOperatorBase.h"

#include "FEEvaluationWrapper.h"
#include "FE_Parameters.h"

#include "InverseMassMatrix.h"
#include "NavierStokesOperators.h"
#include "NavierStokesCalculators.h"

#include "../include/BoundaryDescriptorNavierStokes.h"
#include "../include/FieldFunctionsNavierStokes.h"
#include "InputParametersNavierStokes.h"

#include "BoundaryDescriptorLaplace.h"


using namespace dealii;

//forward declarations
template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
class DGNavierStokesDualSplittingXWall;

//struct MassMatrixOperatorData;
template<int dim> struct ViscousOperatorData;
template<int dim> struct ConvectiveOperatorData;
template<int dim> struct GradientOperatorData;
template<int dim> struct DivergenceOperatorData;
template<int dim> struct BodyForceOperatorData;

template<int dim, int fe_degree, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class MassMatrixOperator;
template<int dim, int fe_degree, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class ConvectiveOperator;
template<int dim, int fe_degree, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class ViscousOperator;
template<int dim, int fe_degree, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class BodyForceOperator;
template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class GradientOperator;
template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule, typename value_type>
class DivergenceOperator;

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
class DGNavierStokesBase : public MatrixOperatorBase
{
public:
  enum class DofHandlerSelector {
    velocity = 0,
    pressure = 1,
    n_variants = pressure+1
  };

  enum class QuadratureSelector {
    velocity = 0,
    pressure = 1,
    velocity_nonlinear = 2,
    n_variants = velocity_nonlinear+1
  };

  typedef double value_type;
  static const unsigned int number_vorticity_components = (dim==2) ? 1 : dim;
  static const bool is_xwall = (xwall_quad_rule>1) ? true : false;
  static const unsigned int n_actual_q_points_vel_linear = (is_xwall) ? xwall_quad_rule : fe_degree+1;

  /*
   * nomenclature typdedef FEEvaluationWrapper:
   * FEEval_name1_name2 : name1 specifies the dof handler, name2 the quadrature formula
   * example: FEEval_Pressure_Velocity_linear: dof handler for pressure (scalar quantity),
   * quadrature formula with fe_degree_velocity+1 quadrature points
   */

  typedef FEEvaluationWrapper<dim,fe_degree,fe_degree_xwall,n_actual_q_points_vel_linear,dim,value_type,is_xwall> FEEval_Velocity_Velocity_linear;

  typedef FEFaceEvaluationWrapper<dim,fe_degree,fe_degree_xwall,n_actual_q_points_vel_linear,dim,value_type,is_xwall> FEFaceEval_Velocity_Velocity_linear;

  // constructor
  DGNavierStokesBase(parallel::distributed::Triangulation<dim> const &triangulation,
                     InputParametersNavierStokes<dim> const          &parameter)
    :
    fe_u(new FESystem<dim>(FE_DGQArbitraryNodes<dim>(QGaussLobatto<1>(fe_degree+1)),dim)),
    fe_p(QGaussLobatto<1>(fe_degree_p+1)),
    mapping(fe_degree),
    dof_handler_u(triangulation),
    dof_handler_p(triangulation),
    viscosity(parameter.viscosity),
    dof_index_first_point(0),
    param(parameter),
    inverse_mass_matrix_operator(nullptr)
  {}

  // destructor
  virtual ~DGNavierStokesBase()
  {
    data.clear();
  }

  void initialize_boundary_descriptor_laplace();

  virtual void setup (const std::vector<GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator> >
                                                                                  periodic_face_pairs,
                      std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_velocity,
                      std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_pressure,
                      std_cxx11::shared_ptr<FieldFunctionsNavierStokes<dim> >     field_functions);

  virtual void prescribe_initial_conditions(parallel::distributed::Vector<value_type> &velocity,
                                            parallel::distributed::Vector<value_type> &pressure,
                                            double const                              evaluation_time) const;


  MatrixFree<dim,value_type> const & get_data() const
  {
    return data;
  }

  unsigned int get_dof_index_velocity() const
  {
    return static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  }

  unsigned int get_quad_index_velocity_linear() const
  {
    return static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::velocity);
  }

  unsigned int get_dof_index_pressure() const
  {
    return static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure);
  }

  unsigned int get_quad_index_pressure() const
  {
    return static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::pressure);
  }

  Mapping<dim> const & get_mapping() const
  {
    return mapping;
  }

  FESystem<dim> const & get_fe_u() const
  {
    return *fe_u;
  }

  FE_DGQArbitraryNodes<dim> const & get_fe_p() const
  {
    return fe_p;
  }

  DoFHandler<dim> const & get_dof_handler_u() const
  {
    return dof_handler_u;
  }

  DoFHandler<dim> const & get_dof_handler_p() const
  {
    return dof_handler_p;
  }

  double get_viscosity() const
  {
    return viscosity;
  }

  MassMatrixOperatorData const & get_mass_matrix_operator_data() const
  {
    return mass_matrix_operator_data;
  }

  ViscousOperatorData<dim> const & get_viscous_operator_data() const
  {
    return viscous_operator_data;
  }

  ConvectiveOperatorData<dim> const & get_convective_operator_data() const
  {
    return convective_operator_data;
  }

  GradientOperatorData<dim> const & get_gradient_operator_data() const
  {
    return gradient_operator_data;
  }

  DivergenceOperatorData<dim> const & get_divergence_operator_data() const
  {
    return divergence_operator_data;
  }

  std_cxx11::shared_ptr<FieldFunctionsNavierStokes<dim> > const get_field_functions() const
  {
    return field_functions;
  }

  // initialization of vectors
  void initialize_vector_velocity(parallel::distributed::Vector<value_type> &src) const
  {
    this->data.initialize_dof_vector(src,
        static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity));
  }

  void initialize_vector_vorticity(parallel::distributed::Vector<value_type> &src) const
  {
    this->data.initialize_dof_vector(src,
        static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity));
  }

  void initialize_vector_pressure(parallel::distributed::Vector<value_type> &src) const
  {
    this->data.initialize_dof_vector(src,
        static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure));
  }

  // special case: pure Dirichlet boundary conditions
  // if analytical solution is available: shift pressure so that the numerical pressure solution
  // coincides the the analytical pressure solution in an arbitrary point.
  // Note that the parameter 'eval_time' is only needed for unsteady problems.
  void  shift_pressure (parallel::distributed::Vector<value_type> &pressure,
                        double const                              &eval_time = 0.0) const;

  // special case: pure Dirichlet boundary conditions
  // if no analytical solution is available: set mean value of pressure vector to zero
  void apply_zero_mean (parallel::distributed::Vector<value_type>  &dst) const;

  // vorticity
  void compute_vorticity (parallel::distributed::Vector<value_type>       &dst,
                          const parallel::distributed::Vector<value_type> &src) const;

  // divergence
  void compute_divergence (parallel::distributed::Vector<value_type>       &dst,
                           const parallel::distributed::Vector<value_type> &src) const;

  void evaluate_convective_term (parallel::distributed::Vector<value_type>       &dst,
                                 parallel::distributed::Vector<value_type> const &src,
                                 value_type const                                evaluation_time) const;

protected:
  MatrixFree<dim,value_type> data;

  std_cxx11::shared_ptr< FESystem<dim> > fe_u;
  FE_DGQArbitraryNodes<dim> fe_p;

  MappingQGeneric<dim> mapping;

  DoFHandler<dim> dof_handler_u;
  DoFHandler<dim> dof_handler_p;

  const double viscosity;

  Point<dim> first_point;
  types::global_dof_index dof_index_first_point;

  std::vector<GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator> > periodic_face_pairs;

  std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_velocity;
  std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_pressure;
  std_cxx11::shared_ptr<FieldFunctionsNavierStokes<dim> > field_functions;

  // In case of projection type Navier-Stokes solvers this variable
  // is needed to solve the pressure Poisson equation.
  // In that case, also the functions specified in BoundaryDescriptorLaplace are relevant.
  // In case of the coupled solver it is used for the block preconditioner
  // (or more precisely for the Schur-complement preconditioner and the GMG method
  // used to approximately invert the Laplace operator).
  // In that case, the functions specified in BoundaryDescriptorLaplace are irrelevant.
  std_cxx11::shared_ptr<BoundaryDescriptorLaplace<dim> > boundary_descriptor_laplace;

  InputParametersNavierStokes<dim> const &param;

  MassMatrixOperatorData mass_matrix_operator_data;
  ViscousOperatorData<dim> viscous_operator_data;
  ConvectiveOperatorData<dim> convective_operator_data;
  GradientOperatorData<dim> gradient_operator_data;
  DivergenceOperatorData<dim> divergence_operator_data;

  MassMatrixOperator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> mass_matrix_operator;
  ConvectiveOperator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> convective_operator;
  std_cxx11::shared_ptr< InverseMassMatrixOperator<dim,fe_degree,value_type> > inverse_mass_matrix_operator;
  ViscousOperator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> viscous_operator;
  BodyForceOperator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> body_force_operator;
  GradientOperator<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule, value_type> gradient_operator;
  DivergenceOperator<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule, value_type> divergence_operator;

  VorticityCalculator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> vorticity_calculator;
  DivergenceCalculator<dim, fe_degree, fe_degree_xwall, xwall_quad_rule, value_type> divergence_calculator;

private:
  virtual void create_dofs();

  virtual void data_reinit(typename MatrixFree<dim,value_type>::AdditionalData & additional_data);

};

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
initialize_boundary_descriptor_laplace()
{
  boundary_descriptor_laplace.reset(new BoundaryDescriptorLaplace<dim>());

  // Neumann BC Navier-Stokes -> Dirichlet BC for Laplace operator of pressure Poisson equation
  this->boundary_descriptor_laplace->dirichlet = boundary_descriptor_pressure->neumann_bc;

  // Dirichlet BC Navier-Stokes -> Neumann BC for Laplace operator of pressure Poisson equation
  // on pressure Neumann boundaries: prescribe h=0 -> set all functions to ZeroFunction
  // This is necessary for projection type Navier-Stokes solvers.
  // For the coupled solution approach, the functions
  // specified in the boundary descriptor are not evaluated.
  for (typename std::map<types::boundary_id,std_cxx11::shared_ptr<Function<dim> > >::
       const_iterator it = boundary_descriptor_pressure->dirichlet_bc.begin();
       it != boundary_descriptor_pressure->dirichlet_bc.end(); ++it)
  {
    std_cxx11::shared_ptr<Function<dim> > zero_function;
    zero_function.reset(new ZeroFunction<dim>(1));
    boundary_descriptor_laplace->neumann.insert(std::pair<types::boundary_id,std_cxx11::shared_ptr<Function<dim> > >
      (it->first,zero_function));
  }
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
setup (const std::vector<GridTools::PeriodicFacePair<typename Triangulation<dim>::cell_iterator> >
                                                                   periodic_face_pairs,
       std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_velocity_in,
       std_cxx11::shared_ptr<BoundaryDescriptorNavierStokes<dim> > boundary_descriptor_pressure_in,
       std_cxx11::shared_ptr<FieldFunctionsNavierStokes<dim> >     field_functions_in)
{
  ConditionalOStream pcout(std::cout, Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0);
  pcout << std::endl << "Setup Navier-Stokes operation ..." << std::endl;

  this->periodic_face_pairs = periodic_face_pairs;
  this->boundary_descriptor_velocity = boundary_descriptor_velocity_in;
  this->boundary_descriptor_pressure = boundary_descriptor_pressure_in;
  this->field_functions = field_functions_in;

  initialize_boundary_descriptor_laplace();

  create_dofs();

  // initialize matrix_free_data
  typename MatrixFree<dim,value_type>::AdditionalData additional_data;
  additional_data.mpi_communicator = MPI_COMM_WORLD;
  additional_data.tasks_parallel_scheme = MatrixFree<dim,value_type>::AdditionalData::partition_partition;
  additional_data.build_face_info = true;
  additional_data.mapping_update_flags = (update_gradients | update_JxW_values |
                                          update_quadrature_points | update_normal_vectors |
                                          update_values);
  additional_data.periodic_face_pairs_level_0 = periodic_face_pairs;

  data_reinit(additional_data);

  // mass matrix operator
  mass_matrix_operator_data.dof_index = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  mass_matrix_operator.initialize(data,mass_matrix_operator_data);

  // inverse mass matrix operator
  inverse_mass_matrix_operator.reset(new InverseMassMatrixOperator<dim,fe_degree,value_type>());
  inverse_mass_matrix_operator->initialize(data,
          static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity),
          static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::velocity));

  // body force operator
  BodyForceOperatorData<dim> body_force_operator_data;
  body_force_operator_data.dof_index = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  body_force_operator_data.rhs = field_functions->right_hand_side;
  body_force_operator.initialize(data,body_force_operator_data);

  // gradient operator
  gradient_operator_data.dof_index_velocity = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  gradient_operator_data.dof_index_pressure = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure);
  gradient_operator_data.integration_by_parts_of_gradP = param.gradp_integrated_by_parts;
  gradient_operator_data.use_boundary_data = param.gradp_use_boundary_data;
  gradient_operator_data.bc = boundary_descriptor_pressure;
  gradient_operator.initialize(data,gradient_operator_data);

  // divergence operator
  divergence_operator_data.dof_index_velocity = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  divergence_operator_data.dof_index_pressure = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure);
  divergence_operator_data.integration_by_parts_of_divU = param.divu_integrated_by_parts;
  divergence_operator_data.use_boundary_data = param.divu_use_boundary_data;
  divergence_operator_data.bc = boundary_descriptor_velocity;
  divergence_operator.initialize(data,divergence_operator_data);

  // convective operator
  convective_operator_data.dof_index = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  convective_operator_data.bc = boundary_descriptor_velocity;
  convective_operator.initialize(data,convective_operator_data);

  // viscous operator
  viscous_operator_data.formulation_viscous_term = param.formulation_viscous_term;
  viscous_operator_data.IP_formulation_viscous = param.IP_formulation_viscous;
  viscous_operator_data.IP_factor_viscous = param.IP_factor_viscous;
  viscous_operator_data.bc = boundary_descriptor_velocity;
  viscous_operator_data.dof_index = static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity);
  viscous_operator_data.viscosity = param.viscosity;
  viscous_operator.initialize(mapping,data,viscous_operator_data);

  // vorticity
  vorticity_calculator.initialize(data,
      static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity));
  // diveregnce
  divergence_calculator.initialize(data,
      static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity));

  dof_index_first_point = 0;
  for(unsigned int d=0;d<dim;++d)
    first_point[d] = 0.0;

  if(Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0)
  {
    typename DoFHandler<dim>::active_cell_iterator first_cell;
    typename DoFHandler<dim>::active_cell_iterator cell = dof_handler_p.begin_active(), endc = dof_handler_p.end();
    for(;cell!=endc;++cell)
    {
      if (cell->is_locally_owned())
      {
        first_cell = cell;
        break;
      }
    }
    FEValues<dim> fe_values(dof_handler_p.get_fe(),
                Quadrature<dim>(dof_handler_p.get_fe().get_unit_support_points()),
                update_quadrature_points);
    fe_values.reinit(first_cell);
    first_point = fe_values.quadrature_point(0);
    std::vector<types::global_dof_index>
    dof_indices(dof_handler_p.get_fe().dofs_per_cell);
    first_cell->get_dof_indices(dof_indices);
    dof_index_first_point = dof_indices[0];
  }
  dof_index_first_point = Utilities::MPI::sum(dof_index_first_point,MPI_COMM_WORLD);
  for(unsigned int d=0;d<dim;++d)
  {
    first_point[d] = Utilities::MPI::sum(first_point[d],MPI_COMM_WORLD);
  }

  pcout << std::endl << "... done!" << std::endl;
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
create_dofs()
{
  // enumerate degrees of freedom
  dof_handler_u.distribute_dofs(*fe_u);
  dof_handler_u.distribute_mg_dofs(*fe_u);
  dof_handler_p.distribute_dofs(fe_p);
  dof_handler_p.distribute_mg_dofs(fe_p);

  unsigned int ndofs_per_cell_velocity = Utilities::fixed_int_power<fe_degree+1,dim>::value*dim;
  unsigned int ndofs_per_cell_pressure = Utilities::fixed_int_power<fe_degree_p+1,dim>::value;

  ConditionalOStream pcout(std::cout, Utilities::MPI::this_mpi_process(MPI_COMM_WORLD) == 0);

  pcout << std::endl
        << "Discontinuous Galerkin finite element discretization:" << std::endl << std::endl;

  pcout << "Velocity:" << std::endl;
  print_parameter(pcout,"degree of 1D polynomials",fe_degree);
  print_parameter(pcout,"number of dofs per cell",ndofs_per_cell_velocity);
  print_parameter(pcout,"number of dofs (total)",dof_handler_u.n_dofs());

  pcout << "Pressure:" << std::endl;
  print_parameter(pcout,"degree of 1D polynomials",fe_degree_p);
  print_parameter(pcout,"number of dofs per cell",ndofs_per_cell_pressure);
  print_parameter(pcout,"number of dofs (total)",dof_handler_p.n_dofs());


}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
data_reinit(typename MatrixFree<dim,value_type>::AdditionalData &additional_data)
{
  std::vector<const DoFHandler<dim> * >  dof_handler_vec;

  dof_handler_vec.resize(static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::n_variants));
  dof_handler_vec[static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity)] = &dof_handler_u;
  dof_handler_vec[static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure)] = &dof_handler_p;

  std::vector<const ConstraintMatrix *> constraint_matrix_vec;
  constraint_matrix_vec.resize(static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::n_variants));
  ConstraintMatrix constraint_u, constraint_p;
  constraint_u.close();
  constraint_p.close();
  constraint_matrix_vec[static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::velocity)] = &constraint_u;
  constraint_matrix_vec[static_cast<typename std::underlying_type<DofHandlerSelector>::type >(DofHandlerSelector::pressure)] = &constraint_p;

  std::vector<Quadrature<1> > quadratures;

  // resize quadratures
  quadratures.resize(static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::n_variants));
  // velocity
  quadratures[static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::velocity)]
              = QGauss<1>(fe_degree+1);
  // pressure
  quadratures[static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::pressure)]
              = QGauss<1>(fe_degree_p+1);
  // exact integration of nonlinear convective term
  quadratures[static_cast<typename std::underlying_type<QuadratureSelector>::type >(QuadratureSelector::velocity_nonlinear)]
              = QGauss<1>(fe_degree + (fe_degree+2)/2);

  data.reinit (mapping, dof_handler_vec, constraint_matrix_vec, quadratures, additional_data);
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
prescribe_initial_conditions(parallel::distributed::Vector<value_type> &velocity,
                             parallel::distributed::Vector<value_type> &pressure,
                             double const                              evaluation_time) const
{
  this->field_functions->initial_solution_velocity->set_time(evaluation_time);
  this->field_functions->initial_solution_pressure->set_time(evaluation_time);

  VectorTools::interpolate(mapping, dof_handler_u, *(this->field_functions->initial_solution_velocity), velocity);
  VectorTools::interpolate(mapping, dof_handler_p, *(this->field_functions->initial_solution_pressure), pressure);
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
shift_pressure (parallel::distributed::Vector<value_type>  &pressure,
                double const                               &eval_time) const
{
  AssertThrow(this->param.error_data.analytical_solution_available == true,
              ExcMessage("The function shift_pressure is intended to be used only if an analytical solution is available!"));

  parallel::distributed::Vector<value_type> vec1(pressure);
  for(unsigned int i=0;i<vec1.local_size();++i)
    vec1.local_element(i) = 1.;
  this->field_functions->analytical_solution_pressure->set_time(eval_time);
  double exact = this->field_functions->analytical_solution_pressure->value(first_point);
  double current = 0.;
  if (pressure.locally_owned_elements().is_element(dof_index_first_point))
    current = pressure(dof_index_first_point);
  current = Utilities::MPI::sum(current, MPI_COMM_WORLD);
  pressure.add(exact-current,vec1);
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
apply_zero_mean (parallel::distributed::Vector<value_type>  &vector) const
{
  const value_type mean_value = vector.mean_value();
  vector.add(-mean_value);
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
compute_vorticity (parallel::distributed::Vector<value_type>       &dst,
                   const parallel::distributed::Vector<value_type> &src) const
{
  vorticity_calculator.compute_vorticity(dst,src);

  inverse_mass_matrix_operator->apply(dst,dst);
}

template<int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
compute_divergence (parallel::distributed::Vector<value_type>       &dst,
                    const parallel::distributed::Vector<value_type> &src) const
{
  divergence_calculator.compute_divergence(dst,src);

  inverse_mass_matrix_operator->apply(dst,dst);
}

template <int dim, int fe_degree, int fe_degree_p, int fe_degree_xwall, int xwall_quad_rule>
void DGNavierStokesBase<dim, fe_degree, fe_degree_p, fe_degree_xwall, xwall_quad_rule>::
evaluate_convective_term (parallel::distributed::Vector<value_type>       &dst,
                          parallel::distributed::Vector<value_type> const &src,
                          value_type const                                evaluation_time) const
{
  convective_operator.evaluate(dst,src,evaluation_time);
}


#endif /* INCLUDE_DGNAVIERSTOKESBASE_H_ */