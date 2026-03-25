#pragma once

namespace generator_opts {

/**
 * \brief Register generator-specific options (verify flags).
 * Safe to call multiple times; registration is protected by an internal flag.
 */
void register_options();

bool get_verify_enabled();
double get_verify_epsilon();
double get_verify_rel_epsilon();
bool get_verify_inject_failure();

} // namespace generator_opts
