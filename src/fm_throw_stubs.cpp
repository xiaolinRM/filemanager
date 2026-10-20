/**
 * vim: set ts=4 sw=4 tw=99 noet :
 * =============================================================================
 * File Manager Extension
 * Copyright (C) 2026 File Manager Contributors.  All rights reserved.
 * =============================================================================
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License, version 3.0, as published by the
 * Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * -----------------------------------------------------------------------------
 * Linux compatibility shim.
 *
 * The extension is compiled with -fno-exceptions. libstdc++'s string/vector
 * implementation still references the std::__throw_* helpers from
 * functexcept.o. Pulling that object in also pulls the complete C++ exception
 * runtime and unwinder, which in turn references glibc symbols that only exist
 * on recent distributions (pthread_once@GLIBC_2.34 and
 * __libc_single_threaded@GLIBC_2.32).
 *
 * Defining the helpers here satisfies those references. Since exceptions are
 * disabled, the only correct behaviour on such an internal error (allocation
 * failure or an invalid string/vector index) is to abort the server process.
 * The definitions are hidden symbols, so they do not interpose on libstdc++
 * used by other libraries in the process.
 */

#if defined(__linux__)

extern "C" void abort(void);

namespace std
{

void __throw_bad_exception()
{
	abort();
}

void __throw_bad_alloc()
{
	abort();
}

void __throw_bad_array_new_length()
{
	abort();
}

void __throw_bad_cast()
{
	abort();
}

void __throw_bad_typeid()
{
	abort();
}

void __throw_logic_error(const char *)
{
	abort();
}

void __throw_domain_error(const char *)
{
	abort();
}

void __throw_invalid_argument(const char *)
{
	abort();
}

void __throw_length_error(const char *)
{
	abort();
}

void __throw_out_of_range(const char *)
{
	abort();
}

void __throw_out_of_range_fmt(const char *, ...)
{
	abort();
}

void __throw_runtime_error(const char *)
{
	abort();
}

void __throw_range_error(const char *)
{
	abort();
}

void __throw_overflow_error(const char *)
{
	abort();
}

void __throw_underflow_error(const char *)
{
	abort();
}

} // namespace std

namespace __gnu_cxx
{

void __throw_concurrence_lock_error()
{
	abort();
}

void __throw_concurrence_unlock_error()
{
	abort();
}

void __throw_insufficient_space(const char *, const char *)
{
	abort();
}

} // namespace __gnu_cxx

#endif // __linux__
