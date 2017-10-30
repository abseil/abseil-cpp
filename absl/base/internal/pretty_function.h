#ifndef ABSL_BASE_INTERNAL_PRETTY_FUNCTION_H_
#define ABSL_BASE_INTERNAL_PRETTY_FUNCTION_H_

// ABSL_PRETTY_FUNCTION
//
// In C++11, __func__ gives the undecorated name of the current function.  That
// is, "main", not "int main()".  Various compilers give extra macros to get the
// decorated function name, including return type and arguments, to
// differentiate between overload sets.  ABSL_PRETTY_FUNCTION is a portable
// version of these macros which forwards to the correct macro on each compiler.
#if defined(_MSC_VER)
#define ABSL_PRETTY_FUNCTION __FUNCSIG__
#elif defined(__GNUC__)
#define ABSL_PRETTY_FUNCTION __PRETTY_FUNCTION__
#else
#error "Unsupported compiler"
#endif

#endif  // ABSL_BASE_INTERNAL_PRETTY_FUNCTION_H_
