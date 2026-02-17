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

} // namespace manager_opts
