#pragma once
#include "Exception.h"
#include "Reference.h"
#include "Value.h"

#include <format>
#include <string_view>

namespace jspp::namespace_utils {

/**
 * Get or create the parent namespace object for a dot-separated path.
 *
 * @param obj The starting object (usually global or exports)
 * @param ns Dot-separated path, e.g., "foo.bar.ClassA"
 * @return If ns contains at least one dot, returns the object at the parent path
 *         (for "foo.bar.ClassA", returns the object at "foo.bar").
 *         If ns has no dot, returns obj itself (indicating the value should be
 *         mounted directly on obj).
 * @throws Exception if any segment exists but is not an object
 *
 * @example
 * auto parent = getParentNamespaceObject(global, "com.example.utils.helper");
 * // parent is the object at "com.example.utils"
 * parent.set("helper", function);  // global.com.example.utils.helper
 */
inline Local<Object> getParentNamespaceObject(Local<Object> obj, std::string_view ns);

/**
 * Extract the last segment of a dot-separated namespace string.
 *
 * @param ns Dot-separated namespace path, e.g., "com.example.utils"
 * @return The last segment, e.g., "utils"
 *
 * @example
 * getNamespaceLeafString("foo.bar.baz")  // returns "baz"
 * getNamespaceLeafString("hello")        // returns "hello"
 * getNamespaceLeafString("")             // returns ""
 * getNamespaceLeafString("a..b")         // returns "b" (empty segments ignored)
 * getNamespaceLeafString(".foo")         // returns "foo"
 */
inline constexpr std::string_view getNamespaceLeafString(std::string_view ns);

/**
 * Mount a value onto a namespace path.
 *
 * Creates any missing intermediate objects and sets the final property.
 *
 * @param obj The starting object (e.g., global, exports)
 * @param ns Dot-separated namespace path, e.g., "foo.bar.baz"
 * @param value The value to mount at the leaf
 *
 * @example
 * mountNamespace(global, "com.example.utils.helpers", helperObject);
 * // global.com.example.utils.helpers === helperObject
 *
 * mountNamespace(exports, "math.constants.PI", piValue);
 * // exports.math.constants.PI === piValue
 *
 * @throws Exception if ns is empty, leaf is empty, or any intermediate segment
 *         exists but is not an object
 */
inline void mountNamespace(Local<Object> obj, std::string_view ns, Local<Value> value);

/**
 * Check if the string contains a namespace separator (dot).
 *
 * Note: This only checks for the presence of a dot. It does not guarantee
 * the namespace is valid. Use validNamespace() for structural validation.
 */
inline constexpr bool hasNamespace(std::string_view sv) { return sv.find('.') != std::string_view::npos; }

/**
 * Validate the namespace string format.
 *
 * Rules:
 * 1. Cannot be empty.
 * 2. Cannot start or end with a dot ('.').
 * 3. Cannot contain consecutive dots ("..").
 *
 * @return true if the namespace follows the rules.
 */
inline constexpr bool validNamespace(std::string_view sv) {
    if (sv.empty()) return false;

    // Cannot start or end with '.'
    if (sv.front() == '.' || sv.back() == '.') return false;

    // Cannot contain consecutive dots ".."
    if (sv.find("..") != std::string_view::npos) return false;

    return true;
}

// impl

inline Local<Object> getParentNamespaceObject(Local<Object> obj, std::string_view ns) {
    if (ns.empty()) return obj;

    Local<Object> current = obj;

    size_t last_dot = ns.rfind('.');
    if (last_dot == std::string_view::npos) {
        return current;
    }

    std::string_view parent_path = ns.substr(0, last_dot);
    size_t           pos         = 0;

    while (pos < parent_path.size()) {
        size_t           dot     = parent_path.find('.', pos);
        std::string_view segment = parent_path.substr(pos, dot - pos);

        if (segment.empty()) [[unlikely]] {
            throw Exception("Empty namespace segment in: " + std::string(ns));
        }

        auto key = String::newString(segment);

        if (!current.has(key)) {
            auto new_obj = Object::newObject();
            current.set(key, new_obj);
            current = new_obj;
        } else {
            auto value = current.get(key);
            if (!value.isObject()) [[unlikely]] {
                throw Exception(std::format("Namespace segment '{}' is not an object in: {}", segment, ns));
            }
            current = value.asObject();
        }

        pos = (dot == std::string_view::npos) ? parent_path.size() : dot + 1;
    }

    return current;
}

inline constexpr std::string_view getNamespaceLeafString(std::string_view ns) {
    if (ns.empty()) return ns;

    size_t end = ns.size();
    size_t pos = ns.rfind('.');

    // "foo.bar.."
    while (pos != std::string_view::npos && pos == end - 1) {
        end = pos;
        pos = ns.rfind('.', end - 1);
    }

    if (pos == std::string_view::npos) {
        size_t last_non_dot = ns.find_last_not_of('.');
        return (last_non_dot == std::string_view::npos) ? "" : ns.substr(0, last_non_dot + 1);
    }

    return ns.substr(pos + 1, end - pos - 1);
}

inline void mountNamespace(Local<Object> obj, std::string_view ns, Local<Value> value) {
    if (ns.empty()) [[unlikely]] {
        throw Exception("Cannot mount value with empty namespace");
    }
    auto parent = getParentNamespaceObject(obj, ns);
    auto leaf   = getNamespaceLeafString(ns);

    if (leaf.empty()) [[unlikely]] {
        throw Exception("Invalid namespace leaf: " + std::string(ns));
    }

    auto key = String::newString(leaf);
    parent.set(key, value);
}


} // namespace jspp::namespace_utils