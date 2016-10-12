/*
 * TimeStepCalculation.h
 *
 *  Created on: May 30, 2016
 *      Author: fehn
 */

#ifndef INCLUDE_TIMESTEPCALCULATION_H_
#define INCLUDE_TIMESTEPCALCULATION_H_

#define CFL_BASED_ON_MINIMUM_COMPONENT


double calculate_const_time_step(double const       dt,
                                 unsigned int const n_refine_time)
{
  double time_step = dt/std::pow(2.,n_refine_time);

  return time_step;
}

template<int dim>
double calculate_min_cell_diameter(Triangulation<dim> const &triangulation)
{
  typename Triangulation<dim>::active_cell_iterator cell = triangulation.begin_active(), endc = triangulation.end();

  double diameter = 0.0, min_cell_diameter = std::numeric_limits<double>::max();
  for (; cell!=endc; ++cell)
  {
    if (cell->is_locally_owned())
    {
      diameter = cell->minimum_vertex_distance();
      if (diameter < min_cell_diameter)
        min_cell_diameter = diameter;
    }
  }
  const double global_min_cell_diameter = -Utilities::MPI::max(-min_cell_diameter, MPI_COMM_WORLD);

  return global_min_cell_diameter;
}

template<int dim>
double calculate_max_velocity(Triangulation<dim> const              &triangulation,
                              std_cxx11::shared_ptr<Function<dim> > velocity,
                              double const                          time)
{
  typename Triangulation<dim>::active_cell_iterator cell = triangulation.begin_active(), endc = triangulation.end();

  double U = 0.0, max_U = std::numeric_limits<double>::min();

  Tensor<1,dim,double> vel;
  velocity->set_time(time);

  for (; cell!=endc; ++cell)
  {
    if (cell->is_locally_owned())
    {
      // calculate maximum velocity
      Point<dim> point = cell->center();

      for(unsigned int d=0;d<dim;++d)
        vel[d] = velocity->value(point,d);

      U = vel.norm();
      if (U > max_U)
        max_U = U;
    }
  }
  const double global_max_U = Utilities::MPI::max(max_U, MPI_COMM_WORLD);

  return global_max_U;
}

double calculate_const_time_step_cfl(double const       cfl,
                                     double const       max_velocity,
                                     double const       global_min_cell_diameter,
                                     unsigned int const fe_degree,
                                     double const       exponent_fe_degree = 2.0)
{
  // cfl/p^{exponent_fe_degree} = || U || * time_step / h
  double time_step = cfl/pow(fe_degree,exponent_fe_degree) * global_min_cell_diameter / max_velocity;

  return time_step;
}

double calculate_const_time_step_diff(double const       diffusion_number,
                                      double const       diffusivity,
                                      double const       global_min_cell_diameter,
                                      unsigned int const fe_degree,
                                      double const       exponent_fe_degree = 3.0)
{
  // diffusion_number/p^{exponent_fe_degree} = diffusivity * time_step / h²
  double time_step = diffusion_number/pow(fe_degree,exponent_fe_degree) * pow(global_min_cell_diameter,2.0) / diffusivity;

  return time_step;
}

/*
 *  calculates time step such that the spatial and temporal error are of the
 *  same order of magnitude
 *  spatial error: e = C_h * h^{p+1}
 *  temporal error: e = C_dt * dt^{n} (n: order of time integration method)
 */
double calculate_time_step_max_efficiency(double const       c_eff,
                                          double const       global_min_cell_diameter,
                                          unsigned int const fe_degree,
                                          unsigned int const order_time_integration,
                                          unsigned int const n_refine_time)
{

  double exponent = (double)(fe_degree+1)/order_time_integration;
  double time_step = c_eff * std::pow(global_min_cell_diameter,exponent)/std::pow(2.,n_refine_time);

  return time_step;
}

template<int dim, int fe_degree, typename value_type>
double calculate_adaptive_time_step_cfl(MatrixFree<dim,value_type> const                &data,
                                        unsigned int const                              dof_index,
                                        unsigned int const                              quad_index,
                                        parallel::distributed::Vector<value_type> const &velocity,
                                        double const                                    cfl,
                                        double const                                    last_time_step,
                                        bool const                                      use_limiter = true,
                                        double const                                    exponent_fe_degree = 1.5)
{
  FEEvaluation<dim,fe_degree,fe_degree+1,dim,value_type> fe_eval(data,dof_index,quad_index);

  value_type new_time_step = std::numeric_limits<value_type>::max();
  value_type cfl_p = cfl/pow(fe_degree,exponent_fe_degree);

  // loop over cells of processor
  for (unsigned int cell=0; cell<data.n_macro_cells(); ++cell)
  {
    VectorizedArray<value_type> delta_t_cell =
        make_vectorized_array<value_type>(std::numeric_limits<value_type>::max());
    Tensor<2,dim,VectorizedArray<value_type> > invJ;
    Tensor<1,dim,VectorizedArray<value_type> > u_x;
    Tensor<1,dim,VectorizedArray<value_type> > ut_xi;

    fe_eval.reinit(cell);
    fe_eval.read_dof_values(velocity);
    fe_eval.evaluate(true,false);

    // loop over quadrature points
    for (unsigned int q = 0; q < fe_eval.n_q_points; ++q)
    {
      u_x = fe_eval.get_value(q);
      invJ = fe_eval.inverse_jacobian(q);
      invJ = transpose(invJ);
      ut_xi = invJ*u_x;

#ifdef CFL_BASED_ON_MINIMUM_COMPONENT
      for (unsigned int d = 0; d < dim; ++d)
        delta_t_cell = std::min(delta_t_cell,cfl_p/(std::abs(ut_xi[d])));
#else
      delta_t_cell = std::min(delta_t_cell,cfl_p/ut_xi.norm());
#endif
    }

    // loop over vectorized array
    value_type dt = std::numeric_limits<value_type>::max();
    for (unsigned int v = 0; v < VectorizedArray<value_type>::n_array_elements; ++v)
    {
      dt = std::min(dt,delta_t_cell[v]);
    }

    new_time_step = std::min(new_time_step,dt);
  }

  // find minimum over all processors
  new_time_step = Utilities::MPI::min(new_time_step, MPI_COMM_WORLD);

  // limit the maximum increase/decrease of the time step size
  if(use_limiter == true)
  {
    double fac = 1.2; //TODO
    if (new_time_step >= fac*last_time_step)
    {
      new_time_step = fac*last_time_step;
    }

    else if (new_time_step <= last_time_step/fac)
    {
      new_time_step = last_time_step/fac;
    }
  }

  return new_time_step;
}


#endif /* INCLUDE_TIMESTEPCALCULATION_H_ */
