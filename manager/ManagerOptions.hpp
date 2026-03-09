#pragma once

#include <optional>

namespace manager_opts {

/**
 * \brief Register manager-specific options (interactive mode, etc.)
 * 
 * This function is safe to call multiple times; registration is protected
 * by an internal flag.
 */
void register_options();

/**
 * \brief Check if interactive mode is enabled.
 * \return true if --interactive flag was set, false otherwise.
 */
bool get_interactive_mode();

/**
 * \brief Check if task verification mode is enabled.
 * \return true if --verify flag was set or configured in JSON, false otherwise.
 */
bool get_verify_enabled();

/**
 * \brief Get the absolute epsilon for verification comparisons.
 * \return The configured absolute epsilon (default: 1e-9).
 */
double get_verify_epsilon();

/**
 * \brief Get the relative epsilon for verification comparisons.
 * \return The configured relative epsilon (default: 1e-6).
 */
double get_verify_rel_epsilon();

} // namespace manager_opts
