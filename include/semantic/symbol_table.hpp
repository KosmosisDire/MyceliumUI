#pragma once

#include <string>
#include <unordered_map>
#include <memory>
#include <vector>
#include "codegen/ir_command.hpp"
#include "ast/ast.hpp"

namespace Mycelium::Scripting::Lang {

// Forward declaration
class SymbolTable;

enum class SymbolType {
    VARIABLE,
    FUNCTION,
    CLASS,
    PARAMETER,
    ENUM
};

enum class TypeResolutionState {
    UNRESOLVED,    // Type not yet determined
    RESOLVING,     // Currently being resolved (for cycle detection)
    RESOLVED       // Type fully resolved
};

struct Symbol {
    std::string name;
    SymbolType type;
    IRType data_type;
    std::string type_name;  // Original type name (e.g., "Shape", "string")
    int scope_level;
    
    // Type resolution support
    TypeResolutionState resolution_state = TypeResolutionState::UNRESOLVED;
    ExpressionNode* initializer_expression = nullptr;  // For type inference
    std::vector<std::string> dependencies;  // Variables this symbol's type depends on
    
    Symbol(const std::string& n, SymbolType t, const IRType& dt, const std::string& tn, int level)
        : name(n), type(t), data_type(dt), type_name(tn), scope_level(level) {}
};

struct Scope {
    std::unordered_map<std::string, std::shared_ptr<Symbol>> symbols;
    int parent_scope_id = -1;
    std::string scope_name;
    
    Scope(const std::string& name = "", int parent = -1) 
        : scope_name(name), parent_scope_id(parent) {}
};

class SymbolTable {
private:
    // Persistent storage of all scopes
    std::vector<Scope> all_scopes;
    std::unordered_map<std::string, int> scope_name_to_id;
    int next_scope_id = 0;
    
    // Navigation stack for traversal
    std::vector<int> active_scope_stack;
    
    // Building state (used during symbol table construction)
    int building_scope_level = 0;

public:
    SymbolTable();
    ~SymbolTable() = default;
    
    // === BUILDING PHASE API ===
    // Used during initial symbol table construction
    void enter_scope();  // For building phase
    void enter_named_scope(const std::string& scope_name);  // For building phase with name
    void exit_scope();   // For building phase
    bool declare_symbol(const std::string& name, SymbolType type, const IRType& data_type, const std::string& type_name = "");
    bool declare_unresolved_symbol(const std::string& name, SymbolType type, ExpressionNode* initializer = nullptr);
    
    // === TYPE RESOLUTION API ===
    bool resolve_all_types();  // Resolve all unresolved types
    bool resolve_symbol_type(const std::string& name);  // Resolve specific symbol type
    bool resolve_symbol_type_in_context(const std::string& name, int context_scope_id);  // Resolve in specific scope context
    std::string infer_type_from_expression(ExpressionNode* expr);  // Type inference from expression
    std::string infer_type_from_expression_in_context(ExpressionNode* expr, int context_scope_id);  // Type inference with scope context
    std::vector<std::string> extract_dependencies(ExpressionNode* expr);  // Extract variable dependencies
    std::shared_ptr<Symbol> lookup_symbol_in_context(const std::string& name, int context_scope_id);  // Lookup with scope context
    
    // === NAVIGATION API ===
    // Used during code generation/analysis phases
    int push_scope(const std::string& scope_name);  // Returns scope ID
    int push_scope(int scope_id);                   // Push by ID
    void pop_scope();                               // Pop from navigation stack
    void reset_navigation();                        // Reset to global scope
    
    // === QUERY API ===
    // Works with current navigation state
    std::shared_ptr<Symbol> lookup_symbol(const std::string& name);
    std::shared_ptr<Symbol> lookup_symbol_current_scope(const std::string& name);
    std::shared_ptr<Symbol> lookup_symbol_in_scope(int scope_id, const std::string& name);
    std::vector<std::shared_ptr<Symbol>> get_all_symbols_in_scope(int scope_id);
    
    bool symbol_exists(const std::string& name);
    bool symbol_exists_current_scope(const std::string& name);
    
    // === SCOPE MANAGEMENT ===
    int find_scope_by_name(const std::string& scope_name);
    int get_current_scope_id() const;
    int get_current_scope_level() const { return building_scope_level; }
    std::string get_current_scope_name() const;
    
    void clear();
    void print_symbol_table() const;
    void print_navigation_state() const;
    
    // === TYPE CONVERSION ===
    IRType string_to_ir_type(const std::string& type_str);
};

// Forward declaration
struct CompilationUnitNode;

void build_symbol_table(SymbolTable& table, CompilationUnitNode* ast);

} // namespace Mycelium::Scripting::Lang