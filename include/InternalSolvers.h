/*
 * InternalSolvers.h
 *
 *  Created on: Dec 8, 2016
 *      Author: fehn
 */

#ifndef INCLUDE_INTERNALSOLVERS_H_
#define INCLUDE_INTERNALSOLVERS_H_


namespace InternalSolvers
{
  template <typename Number, typename Number2>
  bool all_smaller (const Number a, const Number2 b)
  {
    return a<b;
  }
  template <typename Number, typename Number2>
  bool all_smaller (const VectorizedArray<Number> a, const Number2 b)
  {
    for (unsigned int i=0; i<VectorizedArray<Number>::n_array_elements; ++i)
      if (a[i] >= b)
        return false;
    return true;
  }
  template <typename Number>
  void adjust_division_by_zero (Number &)
  {}
  template <typename Number>
  void adjust_division_by_zero (VectorizedArray<Number> &x)
  {
    for (unsigned int i=0; i<VectorizedArray<Number>::n_array_elements; ++i)
      if (x[i] < 1e-30)
        x[i] = 1;
  }

  template<typename value_type>
  class SolverCG
  {
  public:
    SolverCG(unsigned int const unknowns,
             double const       abs_tol=1.e-12,
             double const       rel_tol=1.e-8,
             unsigned int const max_iter = 1e5);

    template <typename Matrix>
    void solve(Matrix const     *matrix,
               value_type       *solution,
               value_type const *rhs);

  private:
    const double ABS_TOL;
    const double REL_TOL;
    const unsigned int MAX_ITER;
    AlignedVector<value_type> storage;
    value_type *p,*r,*v;
    const unsigned int M;

    value_type l2_norm(value_type const *vector);

    void vector_init(value_type *dst);

    void equ(value_type       *dst,
             value_type const scalar,
             value_type const *in_vector);

    void equ(value_type       *dst,
             value_type const scalar1,
             value_type const *in_vector1,
             value_type const scalar2,
             value_type const *in_vector2);

    void add(value_type       *dst,
             value_type const scalar,
             value_type const *in_vector);

    value_type inner_product(value_type const *vector1,
                             value_type const *vector2);
  };

  template<typename value_type>
  SolverCG<value_type>::SolverCG(unsigned int const unknowns,
                                 double const       abs_tol,
                                 double const       rel_tol,
                                 unsigned int const max_iter)
    :
    ABS_TOL(abs_tol),
    REL_TOL(rel_tol),
    MAX_ITER(max_iter),
    M(unknowns)
  {
    storage.resize(3*M);
    p = storage.begin();
    r = storage.begin()+M;
    v = storage.begin()+2*M;
  }

  template<typename value_type>
  value_type SolverCG<value_type>::l2_norm(value_type const *vector)
  {
    return std::sqrt(inner_product(vector, vector));
  }

  template<typename value_type>
  void SolverCG<value_type>::vector_init(value_type *vector)
  {
    for(unsigned int i=0;i<M;++i)
      vector[i] = 0.0;
  }

  template<typename value_type>
  void SolverCG<value_type>::equ(value_type       *dst,
                                 value_type const scalar,
                                 value_type const *in_vector)
  {
    for(unsigned int i=0;i<M;++i)
      dst[i] = scalar*in_vector[i];
  }

  template<typename value_type>
  void SolverCG<value_type>::equ(value_type       *dst,
                                 value_type const scalar1,
                                 value_type const *in_vector1,
                                 value_type const scalar2,
                                 value_type const *in_vector2)
  {
    for(unsigned int i=0;i<M;++i)
      dst[i] = scalar1*in_vector1[i]+scalar2*in_vector2[i];
  }

  template<typename value_type>
  void SolverCG<value_type>::add(value_type       *dst,
                                 value_type const scalar,
                                 value_type const *in_vector)
  {
    for(unsigned int i=0;i<M;++i)
      dst[i] += scalar*in_vector[i];
  }

  template<typename value_type>
  value_type SolverCG<value_type>::inner_product(value_type const *vector1,
                                                 value_type const *vector2)
  {
    value_type result = value_type();
    for(unsigned int i=0;i<M;++i)
      result += vector1[i]*vector2[i];

    return result;
  }

  template<typename value_type>
  template<typename Matrix>
  void SolverCG<value_type>::solve(Matrix const     *matrix,
                                   value_type       *solution,
                                   value_type const *rhs)
  {
    value_type one;
    one = 1.0;

    // guess initial solution
    vector_init(solution);

    // apply matrix vector product: v = A*solution
    matrix->vmult(v,solution);

    // compute residual: r = rhs-A*solution
    equ(r,one,rhs,-one,v);
    value_type norm_r0 = l2_norm(r);

    // precondition
    matrix->precondition(p,r);

    // compute norm of residual
    value_type norm_r_abs = norm_r0;
    value_type norm_r_rel = one;
    value_type r_times_y = inner_product(p, r);

    unsigned int n_iter = 0;

    while(true)
    {
      // v = A*p
      matrix->vmult(v,p);

      // p_times_v = p^T*v
      value_type p_times_v = inner_product(p,v);
      adjust_division_by_zero(p_times_v);

      // alpha = (r^T*y) / (p^T*v)
      value_type alpha = (r_times_y)/(p_times_v);

      // solution <- solution + alpha*p
      add(solution,alpha,p);

      // r <- r - alpha*v
      add(r,-alpha,v);

      // calculate residual norm
      norm_r_abs = l2_norm(r);
      norm_r_rel = norm_r_abs / norm_r0;

      // increment iteration counter
      ++n_iter;

      if (all_smaller(norm_r_abs, ABS_TOL) || all_smaller(norm_r_rel, REL_TOL) || (n_iter > MAX_ITER))
      {
        break;
      }

      // precondition
      matrix->precondition(v,r);

      value_type r_times_y_new = inner_product(r,v);

      // beta = (v^T*r) / (p^T*v)
      value_type beta = r_times_y_new / r_times_y;

      // p <- r - beta*p
      equ(p,one,v,beta,p);

      r_times_y = r_times_y_new;
    }

    std::ostringstream message;
    for (unsigned int v=0; v<VectorizedArray<value_type>::n_array_elements; v++)
    {
       message << " v: " << v << "  " << norm_r_abs[v] << " ";
    }
    Assert(n_iter <= MAX_ITER, ExcMessage("No convergence of solver in " + Utilities::to_string(MAX_ITER) + "iterations. Residual was " + message.str().c_str()));
  }

  // TODO
  template<typename value_type>
  class SolverGMRES
  {
  public:


  private:
  };

}


#endif /* INCLUDE_INTERNALSOLVERS_H_ */