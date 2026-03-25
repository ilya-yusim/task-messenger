/**
 * @file skills/registry/SkillKey.hpp
 * @brief Deterministic skill ID derivation from namespaced string names.
 *
 * Eliminates the need for a centralized SkillIds enum. Independent skill
 * authors choose a namespaced name and the numeric ID is derived automatically.
 */
#pragma once

#include <cstdint>
#include <string_view>

namespace TaskMessenger::Skills {

namespace SkillKey {

    /**
     * @brief Derive a deterministic 32-bit skill ID from a canonical name.
     *
     * Uses FNV-1a 32-bit hash. The same name always produces the same ID.
     * Namespaced names (e.g., "alice.MonteCarloSample") prevent practical collisions.
     *
     * @param name Fully-qualified skill name (e.g., "builtin.StringReversal").
     * @return Deterministic 32-bit skill ID.
     *
     * Convention: names must contain a dot separator (namespace.SkillName).
     * Builtin skills use the "builtin." prefix.
     */
    constexpr uint32_t from_name(std::string_view name) {
        uint32_t hash = 2166136261u;  // FNV offset basis
        for (char c : name) {
            hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
            hash *= 16777619u;  // FNV prime
        }
        return hash;
    }

    /**
     * @brief Validate that a skill name follows the namespace convention.
     * @param name The skill name to validate.
     * @return true if the name contains at least one dot and non-empty segments.
     */
    constexpr bool is_valid_name(std::string_view name) {
        if (name.empty()) return false;
        auto dot = name.find('.');
        if (dot == std::string_view::npos) return false;
        if (dot == 0 || dot == name.size() - 1) return false;
        return true;
    }

} // namespace SkillKey

} // namespace TaskMessenger::Skills
