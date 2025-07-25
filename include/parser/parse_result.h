#pragma once
#include "ast/ast.hpp"

namespace Mycelium::Scripting::Lang
{
    template<typename T>
    struct ParseResult {
        static_assert(std::is_base_of_v<AstNode, T>, "T must derive from AstNode");

        enum class State { Success, Error, Fatal };

        union {
            T* success_node;
            ErrorNode* error_node;
        };
        State state;

        // Factory methods - no casting, type-safe by construction
        static ParseResult<T> success(T* node) {
            ParseResult result;
            result.success_node = node;
            result.state = State::Success;
            return result;
        }

        static ParseResult<T> error(Mycelium::Scripting::Lang::ErrorNode* error) {
            ParseResult result;
            result.error_node = error;
            result.state = State::Error;
            return result;
        }

        static ParseResult<T> fatal() {
            ParseResult result;
            result.error_node = nullptr;  // Use error_node member for fatal state
            result.state = State::Fatal;
            return result;
        }

        // Query methods
        bool is_success() const { return state == State::Success; }
        bool is_error() const { return state == State::Error; }
        bool is_fatal() const { return state == State::Fatal; }

        // Safe access - compiler guarantees correct types
        T* get_node() const {
            return is_success() ? success_node : nullptr;
        }

        ErrorNode* get_error() const {
            return is_error() ? error_node : nullptr;
        }

        // AST integration - static_assert ensures T derives from AstNode
        AstNode* get_ast_node() const {
            switch (state) {
            case State::Success: return success_node;  // T* → AstNode* (guaranteed safe)
            case State::Error:   return error_node;    // ErrorNode* → AstNode* (guaranteed safe)
            case State::Fatal:   return nullptr;
            }
            return nullptr;
        }
    };
}