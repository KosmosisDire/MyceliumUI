#include "semantic/symbol_table.hpp"
#include "ast/ast.hpp"
#include "ast/ast_rtti.hpp"
#include "common/logger.hpp"
#include "codegen/ir_command.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace Mycelium::Scripting::Lang {

using namespace Mycelium::Scripting::Common;

SymbolTable::SymbolTable() : building_scope_level(0) {
    // Create global scope
    all_scopes.emplace_back("global", -1);
    scope_name_to_id["global"] = 0;
    next_scope_id = 1;
    
    // Start with global scope active
    active_scope_stack.push_back(0);
}

// === BUILDING PHASE API ===
void SymbolTable::enter_scope() {
    // Create an anonymous scope for building
    std::string scope_name = "scope_" + std::to_string(next_scope_id);
    enter_named_scope(scope_name);
}

void SymbolTable::enter_named_scope(const std::string& scope_name) {
    int parent_id = building_scope_level;
    all_scopes.emplace_back(scope_name, parent_id);
    scope_name_to_id[scope_name] = next_scope_id;
    building_scope_level = next_scope_id;
    next_scope_id++;
}

void SymbolTable::exit_scope() {
    if (building_scope_level > 0) {
        // Find parent scope
        building_scope_level = all_scopes[building_scope_level].parent_scope_id;
    }
}

bool SymbolTable::declare_symbol(const std::string& name, SymbolType type, const IRType& data_type, const std::string& type_name) {
    if (symbol_exists_current_scope(name)) {
        return false;
    }
    
    auto symbol = std::make_shared<Symbol>(name, type, data_type, type_name, building_scope_level);
    symbol->resolution_state = TypeResolutionState::RESOLVED;  // Explicit types are already resolved
    all_scopes[building_scope_level].symbols[name] = symbol;
    return true;
}

bool SymbolTable::declare_unresolved_symbol(const std::string& name, SymbolType type, ExpressionNode* initializer) {
    if (symbol_exists_current_scope(name)) {
        return false;
    }
    
    // Create symbol with placeholder type - will be resolved later
    auto symbol = std::make_shared<Symbol>(name, type, IRType::i32(), "unresolved", building_scope_level);
    symbol->resolution_state = TypeResolutionState::UNRESOLVED;
    symbol->initializer_expression = initializer;
    
    // Extract dependencies from initializer if present
    if (initializer) {
        symbol->dependencies = extract_dependencies(initializer);
    }
    
    all_scopes[building_scope_level].symbols[name] = symbol;
    return true;
}

// === NAVIGATION API ===
int SymbolTable::push_scope(const std::string& scope_name) {
    auto it = scope_name_to_id.find(scope_name);
    if (it != scope_name_to_id.end()) {
        active_scope_stack.push_back(it->second);
        return it->second;
    }
    return -1; // Scope not found
}

int SymbolTable::push_scope(int scope_id) {
    if (scope_id >= 0 && scope_id < all_scopes.size()) {
        active_scope_stack.push_back(scope_id);
        return scope_id;
    }
    return -1; // Invalid scope ID
}

void SymbolTable::pop_scope() {
    if (active_scope_stack.size() > 1) { // Keep at least global scope
        active_scope_stack.pop_back();
    }
}

void SymbolTable::reset_navigation() {
    active_scope_stack.clear();
    active_scope_stack.push_back(0); // Reset to global scope
}

// === QUERY API ===
std::shared_ptr<Symbol> SymbolTable::lookup_symbol(const std::string& name) {
    // Search from current scope up through parent chain
    for (int i = active_scope_stack.size() - 1; i >= 0; i--) {
        int scope_id = active_scope_stack[i];
        auto it = all_scopes[scope_id].symbols.find(name);
        if (it != all_scopes[scope_id].symbols.end()) {
            return it->second;
        }
        
        // Special handling for member function scopes - check for unqualified field access
        // If we're in a member function scope (Type::function), check the parent type scope for fields
        if (i == active_scope_stack.size() - 1) { // Only check from current scope
            const std::string& scope_name = all_scopes[scope_id].scope_name;
            if (scope_name.find("::") != std::string::npos) {
                // This is a member function scope, extract the type name
                std::string type_name = scope_name.substr(0, scope_name.find("::"));
                int type_scope_id = find_scope_by_name(type_name);
                if (type_scope_id != -1) {
                    auto field_it = all_scopes[type_scope_id].symbols.find(name);
                    if (field_it != all_scopes[type_scope_id].symbols.end() && 
                        field_it->second->type == SymbolType::VARIABLE) {
                        return field_it->second;
                    }
                }
            }
        }
    }
    return nullptr;
}

std::shared_ptr<Symbol> SymbolTable::lookup_symbol_current_scope(const std::string& name) {
    if (active_scope_stack.empty()) return nullptr;
    
    int current_scope = active_scope_stack.back();
    auto it = all_scopes[current_scope].symbols.find(name);
    if (it != all_scopes[current_scope].symbols.end()) {
        return it->second;
    }
    return nullptr;
}

std::shared_ptr<Symbol> SymbolTable::lookup_symbol_in_scope(int scope_id, const std::string& name) {
    if (scope_id < 0 || scope_id >= all_scopes.size()) return nullptr;
    
    auto it = all_scopes[scope_id].symbols.find(name);
    if (it != all_scopes[scope_id].symbols.end()) {
        return it->second;
    }
    return nullptr;
}

std::vector<std::shared_ptr<Symbol>> SymbolTable::get_all_symbols_in_scope(int scope_id) {
    std::vector<std::shared_ptr<Symbol>> symbols;
    
    if (scope_id < 0 || scope_id >= all_scopes.size()) {
        return symbols; // Return empty vector for invalid scope_id
    }
    
    // Iterate through all symbols in the scope and collect them
    for (const auto& [name, symbol] : all_scopes[scope_id].symbols) {
        symbols.push_back(symbol);
    }
    
    return symbols;
}

bool SymbolTable::symbol_exists(const std::string& name) {
    return lookup_symbol(name) != nullptr;
}

bool SymbolTable::symbol_exists_current_scope(const std::string& name) {
    return lookup_symbol_current_scope(name) != nullptr;
}

// === SCOPE MANAGEMENT ===
int SymbolTable::find_scope_by_name(const std::string& scope_name) {
    auto it = scope_name_to_id.find(scope_name);
    return (it != scope_name_to_id.end()) ? it->second : -1;
}

int SymbolTable::get_current_scope_id() const {
    return active_scope_stack.empty() ? -1 : active_scope_stack.back();
}

std::string SymbolTable::get_current_scope_name() const {
    int scope_id = get_current_scope_id();
    return (scope_id >= 0 && scope_id < all_scopes.size()) ? all_scopes[scope_id].scope_name : "";
}

void SymbolTable::clear() {
    all_scopes.clear();
    scope_name_to_id.clear();
    active_scope_stack.clear();
    building_scope_level = 0;
    next_scope_id = 0;
    
    // Recreate global scope
    all_scopes.emplace_back("global", -1);
    scope_name_to_id["global"] = 0;
    next_scope_id = 1;
    active_scope_stack.push_back(0);
}

void SymbolTable::print_symbol_table() const {
    LOG_INFO("Total scopes: " + std::to_string(all_scopes.size()), LogCategory::SEMANTIC);
    
    for (size_t scope_id = 0; scope_id < all_scopes.size(); ++scope_id) {
        const auto& scope = all_scopes[scope_id];
        LOG_SEPARATOR('-', 60, LogCategory::SEMANTIC);
        std::string scope_info = "Scope " + std::to_string(scope_id) + ": \"" + scope.scope_name + "\"";
        if (scope.parent_scope_id >= 0) {
            scope_info += " (parent: " + std::to_string(scope.parent_scope_id) + ")";
        }

        LOG_INFO(scope_info, LogCategory::SEMANTIC);
        
        if (scope.symbols.empty()) {
            LOG_INFO("  (empty)", LogCategory::SEMANTIC);
        } else {
            // Create formatted header
            std::ostringstream header;
            header << Colors::DIM
                   << std::setw(20) << "Name" 
                   << std::setw(12) << "Type" 
                   << std::setw(15) << "Data Type" 
                   << Colors::RESET;
            LOG_INFO(header.str(), LogCategory::SEMANTIC);
            
            for (const auto& [name, symbol] : scope.symbols) {
                std::string type_str;
                switch (symbol->type) {
                    case SymbolType::VARIABLE: type_str = "VARIABLE"; break;
                    case SymbolType::FUNCTION: type_str = "FUNCTION"; break;
                    case SymbolType::CLASS: type_str = "CLASS"; break;
                    case SymbolType::PARAMETER: type_str = "PARAMETER"; break;
                    case SymbolType::ENUM: type_str = "ENUM"; break;
                }
                
                std::ostringstream row;
                row << std::setw(20) << symbol->name
                    << std::setw(12) << type_str
                    << std::setw(15) << symbol->type_name;
                LOG_INFO(row.str(), LogCategory::SEMANTIC);
            }
        }
    }
}

void SymbolTable::print_navigation_state() const {
    LOG_SUBHEADER("Navigation State", LogCategory::SEMANTIC);
    
    std::string scope_stack = "Active scope stack: ";
    for (size_t i = 0; i < active_scope_stack.size(); ++i) {
        if (i > 0) scope_stack += " -> ";
        int scope_id = active_scope_stack[i];
        scope_stack += std::to_string(scope_id) + "(\"" + all_scopes[scope_id].scope_name + "\")";
    }
    LOG_INFO(scope_stack, LogCategory::SEMANTIC);
    
    LOG_INFO("Current scope: " + get_current_scope_name() + " (ID: " + std::to_string(get_current_scope_id()) + ")", LogCategory::SEMANTIC);
    LOG_SEPARATOR('-', 30, LogCategory::SEMANTIC);
}

// === PRIVATE HELPER FUNCTIONS ===
IRType SymbolTable::string_to_ir_type(const std::string& type_str) {
    // Check for array types first (e.g., "i32[]")
    if (type_str.length() > 2 && type_str.substr(type_str.length() - 2) == "[]") {
        // For now, treat arrays as pointers to the element type
        // Later we can implement proper array types
        std::string element_type = type_str.substr(0, type_str.length() - 2);
        // Arrays are represented as pointers in LLVM
        return IRType::ptr();
    }
    
    if (type_str == "i32") {
        return IRType::i32();
    } else if (type_str == "i64") {
        return IRType::i64();
    } else if (type_str == "i8") {
        return IRType::i8();
    } else if (type_str == "i16") {
        return IRType::i16();
    } else if (type_str == "bool") {
        return IRType::bool_();
    } else if (type_str == "f32") {
        return IRType::f32();
    } else if (type_str == "f64") {
        return IRType::f64();
    } else if (type_str == "void") {
        return IRType::void_();
    } else if (type_str == "ptr") {
        return IRType::ptr();
    } else if (type_str == "string") {
        // Strings are typically represented as pointers in LLVM
        return IRType::ptr();
    } else {
        // Check if it's a custom type in the symbol table
        auto symbol = lookup_symbol(type_str);
        if (symbol) {
            switch (symbol->type) {
                case SymbolType::CLASS: {
                    // For class types, create a proper struct type with layout
                    // Find the class scope to build the layout
                    int struct_scope_id = find_scope_by_name(type_str);
                    if (struct_scope_id != -1) {
                        // Create the struct layout
                        auto layout = std::make_shared<StructLayout>();
                        layout->name = type_str;
                        
                        // Get all symbols in the struct scope
                        auto field_symbols = get_all_symbols_in_scope(struct_scope_id);
                        
                        // Process all variable symbols (struct fields)
                        for (const auto& field_symbol : field_symbols) {
                            if (field_symbol && field_symbol->type == SymbolType::VARIABLE) {
                                StructLayout::Field field;
                                field.name = field_symbol->name;
                                field.type = field_symbol->data_type;
                                field.offset = 0; // Will be calculated by calculate_layout()
                                
                                layout->fields.push_back(field);
                            }
                        }
                        
                        // Calculate field offsets and total size
                        layout->calculate_layout();
                        
                        // Create and return struct type with layout
                        return IRType::struct_(layout);
                    } else {
                        LOG_ERROR("Cannot find scope for class type: " + type_str, LogCategory::SEMANTIC);
                        return IRType::ptr(); // Fallback to pointer
                    }
                }
                case SymbolType::ENUM:
                    // Enums are treated as integers for now
                    return IRType::i32();
                default:
                    break;
            }
        }
        
        // Unknown type - this is an error
        LOG_ERROR("Unknown type in string_to_ir_type: '" + type_str + "'", LogCategory::SEMANTIC);
        throw std::runtime_error("Unknown type: " + type_str);
    }
}

// === TYPE RESOLUTION API ===
bool SymbolTable::resolve_all_types() {
    LOG_DEBUG("Starting type resolution for all unresolved symbols", LogCategory::SEMANTIC);
    
    bool progress = true;
    int max_iterations = 10;  // Prevent infinite loops
    int iteration = 0;
    
    while (progress && iteration < max_iterations) {
        progress = false;
        iteration++;
        
        LOG_DEBUG("Type resolution iteration " + std::to_string(iteration), LogCategory::SEMANTIC);
        
        // Try to resolve all unresolved symbols in all scopes
        for (auto& scope : all_scopes) {
            for (auto& [name, symbol] : scope.symbols) {
                if (symbol->resolution_state == TypeResolutionState::UNRESOLVED) {
                    LOG_DEBUG("Attempting to resolve symbol: " + name, LogCategory::SEMANTIC);
                    if (resolve_symbol_type(name)) {
                        progress = true;
                        LOG_DEBUG("Successfully resolved symbol: " + name, LogCategory::SEMANTIC);
                    }
                }
            }
        }
    }
    
    // Check if any symbols remain unresolved
    bool all_resolved = true;
    for (const auto& scope : all_scopes) {
        for (const auto& [name, symbol] : scope.symbols) {
            if (symbol->resolution_state == TypeResolutionState::UNRESOLVED) {
                LOG_ERROR("Failed to resolve type for symbol: " + name, LogCategory::SEMANTIC);
                all_resolved = false;
            }
        }
    }
    
    if (iteration >= max_iterations) {
        LOG_ERROR("Type resolution exceeded maximum iterations - possible circular dependencies", LogCategory::SEMANTIC);
        return false;
    }
    
    LOG_DEBUG("Type resolution completed successfully", LogCategory::SEMANTIC);
    return all_resolved;
}

bool SymbolTable::resolve_symbol_type(const std::string& name) {
    // Find the symbol in any scope
    std::shared_ptr<Symbol> symbol = nullptr;
    int symbol_scope_id = -1;
    
    for (int scope_id = 0; scope_id < all_scopes.size(); scope_id++) {
        auto it = all_scopes[scope_id].symbols.find(name);
        if (it != all_scopes[scope_id].symbols.end()) {
            symbol = it->second;
            symbol_scope_id = scope_id;
            break;
        }
    }
    
    if (!symbol) {
        LOG_ERROR("Cannot resolve type for unknown symbol: " + name, LogCategory::SEMANTIC);
        return false;
    }
    
    if (symbol->resolution_state == TypeResolutionState::RESOLVED) {
        return true;  // Already resolved
    }
    
    if (symbol->resolution_state == TypeResolutionState::RESOLVING) {
        LOG_ERROR("Circular dependency detected while resolving symbol: " + name, LogCategory::SEMANTIC);
        return false;  // Circular dependency
    }
    
    symbol->resolution_state = TypeResolutionState::RESOLVING;
    
    // Resolve dependencies first - need to search in the symbol's scope context
    for (const std::string& dep : symbol->dependencies) {
        if (!resolve_symbol_type_in_context(dep, symbol_scope_id)) {
            LOG_ERROR("Failed to resolve dependency '" + dep + "' for symbol '" + name + "'", LogCategory::SEMANTIC);
            symbol->resolution_state = TypeResolutionState::UNRESOLVED;
            return false;
        }
    }
    
    // Infer type from initializer expression
    if (symbol->initializer_expression) {
        std::string inferred_type = infer_type_from_expression_in_context(symbol->initializer_expression, symbol_scope_id);
        if (inferred_type != "unresolved") {
            try {
                symbol->data_type = string_to_ir_type(inferred_type);
                symbol->type_name = inferred_type;
                symbol->resolution_state = TypeResolutionState::RESOLVED;
                LOG_DEBUG("Resolved symbol '" + name + "' to type '" + inferred_type + "'", LogCategory::SEMANTIC);
                return true;
            } catch (const std::runtime_error& e) {
                LOG_ERROR("Error converting inferred type '" + inferred_type + "' to IR type for symbol '" + name + "': " + e.what(), LogCategory::SEMANTIC);
                symbol->resolution_state = TypeResolutionState::UNRESOLVED;
                return false;
            }
        }
    }
    
    LOG_ERROR("Cannot infer type for symbol: " + name, LogCategory::SEMANTIC);
    symbol->resolution_state = TypeResolutionState::UNRESOLVED;
    return false;
}

bool SymbolTable::resolve_symbol_type_in_context(const std::string& name, int context_scope_id) {
    // Simply call the main resolve method since we already handle finding symbols in any scope
    return resolve_symbol_type(name);
}

std::shared_ptr<Symbol> SymbolTable::lookup_symbol_in_context(const std::string& name, int context_scope_id) {
    // Search from the given scope up through parent chain
    int current_scope_id = context_scope_id;
    
    while (current_scope_id >= 0 && current_scope_id < all_scopes.size()) {
        auto it = all_scopes[current_scope_id].symbols.find(name);
        if (it != all_scopes[current_scope_id].symbols.end()) {
            return it->second;
        }
        // Move to parent scope
        current_scope_id = all_scopes[current_scope_id].parent_scope_id;
    }
    
    return nullptr;
}

std::string SymbolTable::infer_type_from_expression_in_context(ExpressionNode* expr, int context_scope_id) {
    if (!expr) return "void";
    
    if (auto* literal = expr->as<LiteralExpressionNode>()) {
        switch (literal->kind) {
            case LiteralKind::Integer: return "i32";
            case LiteralKind::Boolean: return "bool";
            case LiteralKind::String: return "string";
            case LiteralKind::Float: return "f32";
            default: return "unresolved";
        }
    }
    
    if (auto* binary = expr->as<BinaryExpressionNode>()) {
        switch (binary->opKind) {
            // Comparison and logical operators return bool
            case BinaryOperatorKind::LessThan:
            case BinaryOperatorKind::LessThanOrEqual:
            case BinaryOperatorKind::GreaterThan:
            case BinaryOperatorKind::GreaterThanOrEqual:
            case BinaryOperatorKind::Equals:
            case BinaryOperatorKind::NotEquals:
            case BinaryOperatorKind::LogicalAnd:
            case BinaryOperatorKind::LogicalOr:
                return "bool";
            
            // Arithmetic operators return the type of operands
            default:
                if (auto* left_expr = ast_cast_or_error<ExpressionNode>(binary->left)) {
                    std::string left_type = infer_type_from_expression_in_context(left_expr, context_scope_id);
                    if (left_type != "unresolved") {
                        return left_type;
                    }
                }
                if (auto* right_expr = ast_cast_or_error<ExpressionNode>(binary->right)) {
                    std::string right_type = infer_type_from_expression_in_context(right_expr, context_scope_id);
                    if (right_type != "unresolved") {
                        return right_type;
                    }
                }
                return "unresolved";
        }
    }
    
    if (auto* unary = expr->as<UnaryExpressionNode>()) {
        switch (unary->opKind) {
            case UnaryOperatorKind::Not:
                return "bool";
            case UnaryOperatorKind::Minus:
            case UnaryOperatorKind::Plus:
                if (auto* operand_expr = ast_cast_or_error<ExpressionNode>(unary->operand)) {
                    return infer_type_from_expression_in_context(operand_expr, context_scope_id);
                }
                return "unresolved";
            default:
                return "unresolved";
        }
    }
    
    if (auto* identifier = expr->as<IdentifierExpressionNode>()) {
        std::string var_name = std::string(identifier->identifier->name);
        auto symbol = lookup_symbol_in_context(var_name, context_scope_id);
        if (symbol && symbol->resolution_state == TypeResolutionState::RESOLVED) {
            return symbol->type_name;
        }
        return "unresolved";
    }
    
    if (auto* call = expr->as<CallExpressionNode>()) {
        if (auto* target_ident = call->target->as<IdentifierExpressionNode>()) {
            // Regular function call: func()
            std::string func_name = std::string(target_ident->identifier->name);
            auto symbol = lookup_symbol_in_context(func_name, context_scope_id);
            if (symbol && symbol->type == SymbolType::FUNCTION && symbol->resolution_state == TypeResolutionState::RESOLVED) {
                return symbol->type_name;
            }
        } else if (auto* member_access = call->target->as<MemberAccessExpressionNode>()) {
            // Member function call: obj.method()
            std::string target_type = infer_type_from_expression_in_context(member_access->target, context_scope_id);
            if (target_type != "unresolved") {
                // Find the type scope for the target
                int type_scope_id = find_scope_by_name(target_type);
                if (type_scope_id != -1) {
                    // Look up the member function in the type scope
                    std::string method_name = std::string(member_access->member->name);
                    auto method_symbol = lookup_symbol_in_scope(type_scope_id, method_name);
                    if (method_symbol && method_symbol->type == SymbolType::FUNCTION && method_symbol->resolution_state == TypeResolutionState::RESOLVED) {
                        return method_symbol->type_name;
                    }
                }
            }
        }
        return "unresolved";
    }
    
    if (auto* assignment = expr->as<AssignmentExpressionNode>()) {
        if (auto* source_expr = ast_cast_or_error<ExpressionNode>(assignment->source)) {
            return infer_type_from_expression_in_context(source_expr, context_scope_id);
        }
        return "unresolved";
    }
    
    if (auto* new_expr = expr->as<NewExpressionNode>()) {
        if (new_expr->type) {
            // Get the type name from the TypeNameNode
            if (new_expr->type->identifier) {
                std::string type_name = std::string(new_expr->type->identifier->name);
                // Check if it's a known type in the symbol table
                auto symbol = lookup_symbol_in_context(type_name, context_scope_id);
                if (symbol && (symbol->type == SymbolType::CLASS || symbol->type == SymbolType::ENUM)) {
                    return type_name;
                }
            }
        }
        return "unresolved";
    }
    
    if (auto* member_access = expr->as<MemberAccessExpressionNode>()) {
        // Get target type (e.g., "Player" for p.b where p is Player)
        std::string target_type = infer_type_from_expression_in_context(member_access->target, context_scope_id);
        if (target_type == "unresolved") return "unresolved";
        
        // Find struct scope for the target type
        int struct_scope_id = find_scope_by_name(target_type);
        if (struct_scope_id == -1) return "unresolved";
        
        // Look up field in struct scope
        std::string field_name = std::string(member_access->member->name);
        auto field_symbol = lookup_symbol_in_scope(struct_scope_id, field_name);
        if (field_symbol && field_symbol->resolution_state == TypeResolutionState::RESOLVED) {
            return field_symbol->type_name;  // Returns field type (e.g., "i32" for Player.b)
        }
        return "unresolved";
    }
    
    // Default for unknown expressions
    return "unresolved";
}

std::string SymbolTable::infer_type_from_expression(ExpressionNode* expr) {
    if (!expr) return "void";
    
    if (auto* literal = expr->as<LiteralExpressionNode>()) {
        switch (literal->kind) {
            case LiteralKind::Integer: return "i32";
            case LiteralKind::Boolean: return "bool";
            case LiteralKind::String: return "string";
            case LiteralKind::Float: return "f32";
            default: return "unresolved";
        }
    }
    
    if (auto* binary = expr->as<BinaryExpressionNode>()) {
        switch (binary->opKind) {
            // Comparison and logical operators return bool
            case BinaryOperatorKind::LessThan:
            case BinaryOperatorKind::LessThanOrEqual:
            case BinaryOperatorKind::GreaterThan:
            case BinaryOperatorKind::GreaterThanOrEqual:
            case BinaryOperatorKind::Equals:
            case BinaryOperatorKind::NotEquals:
            case BinaryOperatorKind::LogicalAnd:
            case BinaryOperatorKind::LogicalOr:
                return "bool";
            
            // Arithmetic operators return the type of operands
            default:
                if (auto* left_expr = ast_cast_or_error<ExpressionNode>(binary->left)) {
                    std::string left_type = infer_type_from_expression(left_expr);
                    if (left_type != "unresolved") {
                        return left_type;
                    }
                }
                if (auto* right_expr = ast_cast_or_error<ExpressionNode>(binary->right)) {
                    std::string right_type = infer_type_from_expression(right_expr);
                    if (right_type != "unresolved") {
                        return right_type;
                    }
                }
                return "unresolved";
        }
    }
    
    if (auto* unary = expr->as<UnaryExpressionNode>()) {
        switch (unary->opKind) {
            case UnaryOperatorKind::Not:
                return "bool";
            case UnaryOperatorKind::Minus:
            case UnaryOperatorKind::Plus:
                if (auto* operand_expr = ast_cast_or_error<ExpressionNode>(unary->operand)) {
                    return infer_type_from_expression(operand_expr);
                }
                return "unresolved";
            default:
                return "unresolved";
        }
    }
    
    if (auto* identifier = expr->as<IdentifierExpressionNode>()) {
        std::string var_name = std::string(identifier->identifier->name);
        auto symbol = lookup_symbol(var_name);
        if (symbol && symbol->resolution_state == TypeResolutionState::RESOLVED) {
            return symbol->type_name;
        }
        return "unresolved";
    }
    
    if (auto* call = expr->as<CallExpressionNode>()) {
        if (auto* target_ident = call->target->as<IdentifierExpressionNode>()) {
            // Regular function call: func()
            std::string func_name = std::string(target_ident->identifier->name);
            auto symbol = lookup_symbol(func_name);
            if (symbol && symbol->type == SymbolType::FUNCTION && symbol->resolution_state == TypeResolutionState::RESOLVED) {
                return symbol->type_name;
            }
        } else if (auto* member_access = call->target->as<MemberAccessExpressionNode>()) {
            // Member function call: obj.method()
            std::string target_type = infer_type_from_expression(member_access->target);
            if (target_type != "unresolved") {
                // Find the type scope for the target
                int type_scope_id = find_scope_by_name(target_type);
                if (type_scope_id != -1) {
                    // Look up the member function in the type scope
                    std::string method_name = std::string(member_access->member->name);
                    auto method_symbol = lookup_symbol_in_scope(type_scope_id, method_name);
                    if (method_symbol && method_symbol->type == SymbolType::FUNCTION && method_symbol->resolution_state == TypeResolutionState::RESOLVED) {
                        return method_symbol->type_name;
                    }
                }
            }
        }
        return "unresolved";
    }
    
    if (auto* assignment = expr->as<AssignmentExpressionNode>()) {
        if (auto* source_expr = ast_cast_or_error<ExpressionNode>(assignment->source)) {
            return infer_type_from_expression(source_expr);
        }
        return "unresolved";
    }
    
    if (auto* new_expr = expr->as<NewExpressionNode>()) {
        if (new_expr->type) {
            // Get the type name from the TypeNameNode
            if (new_expr->type->identifier) {
                std::string type_name = std::string(new_expr->type->identifier->name);
                // Check if it's a known type in the symbol table
                auto symbol = lookup_symbol(type_name);
                if (symbol && (symbol->type == SymbolType::CLASS || symbol->type == SymbolType::ENUM)) {
                    return type_name;
                }
            }
        }
        return "unresolved";
    }
    
    if (auto* member_access = expr->as<MemberAccessExpressionNode>()) {
        // Get target type (e.g., "Player" for p.b where p is Player)
        std::string target_type = infer_type_from_expression(member_access->target);
        if (target_type == "unresolved") return "unresolved";
        
        // Find struct scope for the target type
        int struct_scope_id = find_scope_by_name(target_type);
        if (struct_scope_id == -1) return "unresolved";
        
        // Look up field in struct scope
        std::string field_name = std::string(member_access->member->name);
        auto field_symbol = lookup_symbol_in_scope(struct_scope_id, field_name);
        if (field_symbol && field_symbol->resolution_state == TypeResolutionState::RESOLVED) {
            return field_symbol->type_name;  // Returns field type (e.g., "i32" for Player.b)
        }
        return "unresolved";
    }
    
    // Default for unknown expressions
    return "unresolved";
}

std::vector<std::string> SymbolTable::extract_dependencies(ExpressionNode* expr) {
    std::vector<std::string> dependencies;
    
    if (!expr) return dependencies;
    
    if (auto* identifier = expr->as<IdentifierExpressionNode>()) {
        std::string var_name = std::string(identifier->identifier->name);
        dependencies.push_back(var_name);
        return dependencies;
    }
    
    if (auto* binary = expr->as<BinaryExpressionNode>()) {
        if (auto* left_expr = ast_cast_or_error<ExpressionNode>(binary->left)) {
            auto left_deps = extract_dependencies(left_expr);
            dependencies.insert(dependencies.end(), left_deps.begin(), left_deps.end());
        }
        if (auto* right_expr = ast_cast_or_error<ExpressionNode>(binary->right)) {
            auto right_deps = extract_dependencies(right_expr);
            dependencies.insert(dependencies.end(), right_deps.begin(), right_deps.end());
        }
        return dependencies;
    }
    
    if (auto* unary = expr->as<UnaryExpressionNode>()) {
        if (auto* operand_expr = ast_cast_or_error<ExpressionNode>(unary->operand)) {
            return extract_dependencies(operand_expr);
        }
        return dependencies;
    }
    
    if (auto* call = expr->as<CallExpressionNode>()) {
        // Add function name as dependency
        if (auto* target_ident = call->target->as<IdentifierExpressionNode>()) {
            // Simple function call: func()
            std::string func_name = std::string(target_ident->identifier->name);
            dependencies.push_back(func_name);
        } else if (auto* member_access = call->target->as<MemberAccessExpressionNode>()) {
            // Member function call: obj.method() - add target object dependency
            auto target_deps = extract_dependencies(member_access->target);
            dependencies.insert(dependencies.end(), target_deps.begin(), target_deps.end());
        }
        
        // Add argument dependencies
        for (int i = 0; i < call->arguments.size; i++) {
            if (auto* arg_expr = ast_cast_or_error<ExpressionNode>(call->arguments.values[i])) {
                auto arg_deps = extract_dependencies(arg_expr);
                dependencies.insert(dependencies.end(), arg_deps.begin(), arg_deps.end());
            }
        }
        return dependencies;
    }
    
    if (auto* assignment = expr->as<AssignmentExpressionNode>()) {
        if (auto* source_expr = ast_cast_or_error<ExpressionNode>(assignment->source)) {
            return extract_dependencies(source_expr);
        }
        return dependencies;
    }
    
    if (auto* new_expr = expr->as<NewExpressionNode>()) {
        // Add the type as a dependency
        if (new_expr->type && new_expr->type->identifier) {
            std::string type_name = std::string(new_expr->type->identifier->name);
            dependencies.push_back(type_name);
        }
        
        // If there's a constructor call, add argument dependencies
        if (new_expr->constructorCall) {
            for (int i = 0; i < new_expr->constructorCall->arguments.size; i++) {
                if (auto* arg_expr = ast_cast_or_error<ExpressionNode>(new_expr->constructorCall->arguments.values[i])) {
                    auto arg_deps = extract_dependencies(arg_expr);
                    dependencies.insert(dependencies.end(), arg_deps.begin(), arg_deps.end());
                }
            }
        }
        
        return dependencies;
    }
    
    if (auto* member_access = expr->as<MemberAccessExpressionNode>()) {
        // Add dependencies from the target (e.g., for p.b, add dependency on p)
        if (member_access->target) {
            auto target_deps = extract_dependencies(member_access->target);
            dependencies.insert(dependencies.end(), target_deps.begin(), target_deps.end());
        }
        
        // Note: We don't need to add the struct type as a dependency here 
        // because the target variable (like 'p') already depends on it
        
        return dependencies;
    }
    
    // For other expression types (literals, etc.), no dependencies
    return dependencies;
}

using namespace Mycelium::Scripting::Lang;

class SymbolTableBuilder {
private:
    SymbolTable& symbol_table;
    
    std::string get_type_string(TypeNameNode* type_node) {
        if (!type_node) return ""; // Return empty string to indicate no explicit type
        
        // Check derived types first before checking base TypeNameNode
        if (auto array = type_node->as<ArrayTypeNameNode>()) {
            if (!array->elementType) {
                LOG_ERROR("ArrayTypeNameNode has null elementType", LogCategory::SEMANTIC);
                return "unknown[]";
            }
            return get_type_string(array->elementType) + "[]";
        }
        if (auto qualified = type_node->as<QualifiedTypeNameNode>()) {
            return get_type_string(qualified->left) + "::" + std::string(qualified->right->name);
        }
        // Check simple TypeNameNode last since derived types inherit from it
        if (auto simple = type_node->as<TypeNameNode>()) {
            if (!simple->identifier) {
                LOG_ERROR("TypeNameNode has null identifier", LogCategory::SEMANTIC);
                return "unknown";
            }
            return std::string(simple->identifier->name);
        }
        if (auto generic = type_node->as<GenericTypeNameNode>()) {
            std::string result = get_type_string(generic->baseType) + "<";
            for (int i = 0; i < generic->arguments.size; i++) {
                if (i > 0) result += ", ";
                if (auto* type = ast_cast_or_error<TypeNameNode>(generic->arguments.values[i])) {
                    result += get_type_string(type);
                }
            }
            result += ">";
            return result;
        }
        
        // Unknown AST node type - this should not happen
        LOG_ERROR("Unknown TypeNameNode type in get_type_string", LogCategory::SEMANTIC);
        throw std::runtime_error("Unknown TypeNameNode type");
    }
    
    void visit_declaration(DeclarationNode* node) {
        if (!node) return;
        
        if (auto type_decl = node->as<TypeDeclarationNode>()) {
            visit_type_declaration(type_decl);
        } else if (auto interface_decl = node->as<InterfaceDeclarationNode>()) {
            visit_interface_declaration(interface_decl);
        } else if (auto enum_decl = node->as<EnumDeclarationNode>()) {
            visit_enum_declaration(enum_decl);
        } else if (auto func_decl = node->as<FunctionDeclarationNode>()) {
            visit_function_declaration(func_decl);
        } else if (auto var_decl = node->as<VariableDeclarationNode>()) {
            visit_variable_declaration(var_decl);
        } else if (auto ns_decl = node->as<NamespaceDeclarationNode>()) {
            visit_namespace_declaration(ns_decl);
        }
    }
    
    void visit_type_declaration(TypeDeclarationNode* node) {
        std::string type_name = std::string(node->name->name);
        // Check modifiers to determine if it's a ref type (class) or value type (struct)
        bool is_ref_type = false;
        for (int i = 0; i < node->modifiers.size; i++) {
            if (node->modifiers.values[i] == ModifierKind::Ref) {
                is_ref_type = true;
                break;
            }
        }
        IRType class_ir_type = IRType::ptr(); // Classes are reference types
        symbol_table.declare_symbol(type_name, SymbolType::CLASS, class_ir_type, is_ref_type ? "ref type" : "type");
        
        symbol_table.enter_named_scope(type_name);
        
        for (int i = 0; i < node->members.size; i++) {
            if (auto* decl = ast_cast_or_error<DeclarationNode>(node->members.values[i])) {
                // Check if this is a member function and handle it specially
                if (auto* func_decl = decl->as<FunctionDeclarationNode>()) {
                    visit_member_function_declaration(func_decl, type_name);
                } else {
                    visit_declaration(decl);
                }
            }
        }
        
        symbol_table.exit_scope();
    }
    
    void visit_interface_declaration(InterfaceDeclarationNode* node) {
        std::string interface_name = std::string(node->name->name);
        IRType interface_ir_type = IRType::ptr(); // Interfaces are reference types
        symbol_table.declare_symbol(interface_name, SymbolType::CLASS, interface_ir_type, "interface");
        
        symbol_table.enter_named_scope(interface_name);
        
        for (int i = 0; i < node->members.size; i++) {
            if (auto* decl = ast_cast_or_error<DeclarationNode>(node->members.values[i])) {
                visit_declaration(decl);
            }
        }
        
        symbol_table.exit_scope();
    }
    
    void visit_enum_declaration(EnumDeclarationNode* node) {
        std::string enum_name = std::string(node->name->name);
        IRType enum_ir_type = IRType::i32(); // Enums are typically integers
        symbol_table.declare_symbol(enum_name, SymbolType::ENUM, enum_ir_type, "enum");
        
        symbol_table.enter_named_scope(enum_name);
        
        // Handle enum cases
        for (int i = 0; i < node->cases.size; i++) {
            if (auto case_node = node->cases.values[i]) {
                std::string case_name = std::string(case_node->name->name);
                IRType case_ir_type = IRType::i32(); // Enum cases are integers
                symbol_table.declare_symbol(case_name, SymbolType::VARIABLE, case_ir_type, "enum case");
            }
        }
        
        // Handle enum methods
        for (int i = 0; i < node->methods.size; i++) {
            visit_function_declaration(node->methods.values[i]);
        }
        
        symbol_table.exit_scope();
    }
    
    void visit_member_function_declaration(FunctionDeclarationNode* node, const std::string& owner_type) {
        std::string func_name = std::string(node->name->name);
        std::string return_type_str = get_type_string(node->returnType);
        
        // If no explicit return type, default to void for now
        // TODO: Implement type inference from return statements
        if (return_type_str.empty()) {
            return_type_str = "void";
        }
        
        IRType return_ir_type = symbol_table.string_to_ir_type(return_type_str);
        
        // Register the member function in the current (type) scope
        symbol_table.declare_symbol(func_name, SymbolType::FUNCTION, return_ir_type, return_type_str);
        
        // Create a unique scope name for the member function to avoid conflicts
        std::string member_func_scope_name = owner_type + "::" + func_name;
        symbol_table.enter_named_scope(member_func_scope_name);
        
        LOG_DEBUG("Member function '" + func_name + "' in type '" + owner_type + "' has " + std::to_string(node->parameters.size) + " parameters", LogCategory::SEMANTIC);
        
        // Add implicit 'this' parameter for member functions
        // 'this' is a pointer to the owner type
        IRType this_type = IRType::ptr_to(symbol_table.string_to_ir_type(owner_type));
        symbol_table.declare_symbol("this", SymbolType::PARAMETER, this_type, owner_type + "*");
        
        // Process explicit parameters
        for (int i = 0; i < node->parameters.size; i++) {
            LOG_DEBUG("Parameter " + std::to_string(i) + " has type ID: " + std::to_string((int)node->parameters.values[i]->typeId), LogCategory::SEMANTIC);
            if (auto* param = ast_cast_or_error<ParameterNode>(node->parameters.values[i])) {
                std::string param_type_str = get_type_string(param->type);
                IRType param_ir_type = symbol_table.string_to_ir_type(param_type_str);
                symbol_table.declare_symbol(std::string(param->name->name), SymbolType::PARAMETER, param_ir_type, param_type_str);
            }
        }
        
        if (node->body) {
            // Process function body - member functions can access type fields without qualification
            for (int i = 0; i < node->body->statements.size; i++) {
                if (auto* stmt = ast_cast_or_error<StatementNode>(node->body->statements.values[i])) {
                    visit_statement(stmt);
                }
            }
        }
        
        symbol_table.exit_scope();
    }

    void visit_function_declaration(FunctionDeclarationNode* node) {
        std::string func_name = std::string(node->name->name);
        std::string return_type_str = get_type_string(node->returnType);
        
        // If no explicit return type, default to void for now
        // TODO: Implement type inference from return statements
        if (return_type_str.empty()) {
            return_type_str = "void";
        }
        
        IRType return_ir_type = symbol_table.string_to_ir_type(return_type_str);
        symbol_table.declare_symbol(func_name, SymbolType::FUNCTION, return_ir_type, return_type_str);
        
        symbol_table.enter_named_scope(func_name);
        
        LOG_DEBUG("Function '" + func_name + "' has " + std::to_string(node->parameters.size) + " parameters", LogCategory::SEMANTIC);
        for (int i = 0; i < node->parameters.size; i++) {
            LOG_DEBUG("Parameter " + std::to_string(i) + " has type ID: " + std::to_string((int)node->parameters.values[i]->typeId), LogCategory::SEMANTIC);
            if (auto* param = ast_cast_or_error<ParameterNode>(node->parameters.values[i])) {
                std::string param_type_str = get_type_string(param->type);
                IRType param_ir_type = symbol_table.string_to_ir_type(param_type_str);
                symbol_table.declare_symbol(std::string(param->name->name), SymbolType::PARAMETER, param_ir_type, param_type_str);
            }
        }
        
        if (node->body) {
            // Process block contents directly without creating a new scope
            // since the function already has its own scope
            for (int i = 0; i < node->body->statements.size; i++) {
                if (auto* stmt = ast_cast_or_error<StatementNode>(node->body->statements.values[i])) {
                    visit_statement(stmt);
                }
            }
        }
        
        symbol_table.exit_scope();
    }
    
    void visit_variable_declaration(VariableDeclarationNode* node) {
        if (node->type) {
            // Explicit type declaration (e.g., "i32 x = 5;")
            std::string var_type_str = get_type_string(node->type);
            IRType var_ir_type = symbol_table.string_to_ir_type(var_type_str);
            
            // Handle multiple variable names (i32 x, y, z; or i32 a, b, c = 0;)
            for (int i = 0; i < node->names.size; i++) {
                if (node->names.values[i]) {
                    symbol_table.declare_symbol(std::string(node->names.values[i]->name), SymbolType::VARIABLE, var_ir_type, var_type_str);
                }
            }
        } else {
            // Implicit type declaration (e.g., "var x = 5;") - requires type inference
            for (int i = 0; i < node->names.size; i++) {
                if (node->names.values[i]) {
                    std::string var_name = std::string(node->names.values[i]->name);
                    symbol_table.declare_unresolved_symbol(var_name, SymbolType::VARIABLE, node->initializer);
                }
            }
        }
    }
    
    void visit_namespace_declaration(NamespaceDeclarationNode* node) {
        symbol_table.enter_scope();
        
        if (node->body) {
            visit_statement(node->body);
        }
        
        symbol_table.exit_scope();
    }
    
    void visit_statement(StatementNode* node) {
        if (!node) return;
        
        if (auto block = node->as<BlockStatementNode>()) {
            visit_block_statement(block);
        } else if (auto local_var = node->as<VariableDeclarationNode>()) {
            visit_variable_declaration(local_var);
        } else if (auto if_stmt = node->as<IfStatementNode>()) {
            visit_if_statement(if_stmt);
        } else if (auto while_stmt = node->as<WhileStatementNode>()) {
            visit_while_statement(while_stmt);
        } else if (auto for_stmt = node->as<ForStatementNode>()) {
            visit_for_statement(for_stmt);
        }
    }
    
    void visit_block_statement(BlockStatementNode* node) {
        symbol_table.enter_scope();
        
        for (int i = 0; i < node->statements.size; i++) {
            if (auto* stmt = ast_cast_or_error<StatementNode>(node->statements.values[i])) {
                visit_statement(stmt);
            }
        }
        
        symbol_table.exit_scope();
    }
    
    
    void visit_if_statement(IfStatementNode* node) {
        visit_statement(node->thenStatement);
        if (node->elseStatement) {
            visit_statement(node->elseStatement);
        }
    }
    
    void visit_while_statement(WhileStatementNode* node) {
        visit_statement(node->body);
    }
    
    void visit_for_statement(ForStatementNode* node) {
        symbol_table.enter_scope();
        
        if (node->initializer) {
            visit_statement(node->initializer);
        }
        
        visit_statement(node->body);
        
        symbol_table.exit_scope();
    }

public:
    SymbolTableBuilder(SymbolTable& table) : symbol_table(table) {}
    
    void build_from_ast(CompilationUnitNode* root) {
        if (!root) return;
        
        symbol_table.clear();
        
        for (int i = 0; i < root->statements.size; i++) {
            auto stmt = root->statements.values[i];
            // Top-level statements in a compilation unit are often declarations
            if (auto decl = ast_cast_or_error<DeclarationNode>(stmt)) {
                visit_declaration(decl);
            } else if (auto statement = ast_cast_or_error<StatementNode>(stmt)) {
                visit_statement(statement);
            }
        }
    }
};

void build_symbol_table(SymbolTable& table, CompilationUnitNode* ast) {
    SymbolTableBuilder builder(table);
    builder.build_from_ast(ast);
    
    // After building the symbol table, resolve all types
    if (!table.resolve_all_types()) {
        LOG_ERROR("Failed to resolve all types in symbol table", LogCategory::SEMANTIC);
    }
}

} // namespace Mycelium::Scripting::Lang