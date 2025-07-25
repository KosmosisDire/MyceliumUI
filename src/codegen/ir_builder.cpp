#include "codegen/ir_builder.hpp"
#include "common/logger.hpp"
#include <iostream>

namespace Mycelium::Scripting::Lang {

IRBuilder::IRBuilder() : next_id_(1), ignore_writes_(false) {
}

ValueRef IRBuilder::emit(Op op, IRType type, const std::vector<ValueRef>& args) {
    if (ignore_writes_) {
        // In analysis mode, just return a fake value
        return ValueRef(-next_id_++, type);
    }
    
    ValueRef result = (type.kind == IRType::Kind::Void) ? 
        ValueRef::invalid() : ValueRef(next_id_++, type);
    
    Command cmd(op, result, args);
    commands_.push_back(cmd);
    
    return result;
}

ValueRef IRBuilder::emit_with_data(Op op, IRType type, const std::vector<ValueRef>& args,
                                   const std::variant<std::monostate, int64_t, bool, double, std::string, ICmpPredicate>& data) {
    if (ignore_writes_) {
        // In analysis mode, just return a fake value
        return ValueRef(-next_id_++, type);
    }
    
    ValueRef result = (type.kind == IRType::Kind::Void) ? 
        ValueRef::invalid() : ValueRef(next_id_++, type);
    
    Command cmd(op, result, args);
    cmd.data = data;
    commands_.push_back(cmd);
    
    return result;
}

// Constants
ValueRef IRBuilder::const_i32(int32_t value) {
    return emit_with_data(Op::Const, IRType::i32(), {}, static_cast<int64_t>(value));
}

ValueRef IRBuilder::const_i64(int64_t value) {
    return emit_with_data(Op::Const, IRType::i64(), {}, value);
}

ValueRef IRBuilder::const_bool(bool value) {
    return emit_with_data(Op::Const, IRType::bool_(), {}, value);
}

ValueRef IRBuilder::const_f32(float value) {
    return emit_with_data(Op::Const, IRType::f32(), {}, static_cast<double>(value));
}

ValueRef IRBuilder::const_f64(double value) {
    return emit_with_data(Op::Const, IRType::f64(), {}, value);
}

ValueRef IRBuilder::const_null(IRType ptr_type) {
    // Verify it's a pointer type
    if (ptr_type.kind != IRType::Kind::Ptr) {
        std::cerr << "const_null requires a pointer type\n";
        return ValueRef::invalid();
    }
    // Use 0 as the null pointer value
    return emit_with_data(Op::Const, ptr_type, {}, static_cast<int64_t>(0));
}

// Binary operations
ValueRef IRBuilder::add(ValueRef lhs, ValueRef rhs) {
    // Basic type checking
    if (lhs.type != rhs.type) {
        std::cerr << "Type mismatch in add operation\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Add, lhs.type, {lhs, rhs});
}

ValueRef IRBuilder::sub(ValueRef lhs, ValueRef rhs) {
    if (lhs.type != rhs.type) {
        std::cerr << "Type mismatch in sub operation\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Sub, lhs.type, {lhs, rhs});
}

ValueRef IRBuilder::mul(ValueRef lhs, ValueRef rhs) {
    if (lhs.type != rhs.type) {
        std::cerr << "Type mismatch in mul operation\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Mul, lhs.type, {lhs, rhs});
}

ValueRef IRBuilder::div(ValueRef lhs, ValueRef rhs) {
    if (lhs.type != rhs.type) {
        std::cerr << "Type mismatch in div operation\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Div, lhs.type, {lhs, rhs});
}

// Comparison operations
ValueRef IRBuilder::icmp(ICmpPredicate predicate, ValueRef lhs, ValueRef rhs) {
    if (lhs.type != rhs.type) {
        std::cerr << "Type mismatch in icmp operation\n";
        return ValueRef::invalid();
    }
    
    return emit_with_data(Op::ICmp, IRType::bool_(), {lhs, rhs}, predicate);
}

// Logical operations
ValueRef IRBuilder::logical_and(ValueRef lhs, ValueRef rhs) {
    if (lhs.type.kind != IRType::Kind::Bool || rhs.type.kind != IRType::Kind::Bool) {
        std::cerr << "Logical AND requires boolean operands\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::And, IRType::bool_(), {lhs, rhs});
}

ValueRef IRBuilder::logical_or(ValueRef lhs, ValueRef rhs) {
    if (lhs.type.kind != IRType::Kind::Bool || rhs.type.kind != IRType::Kind::Bool) {
        std::cerr << "Logical OR requires boolean operands\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Or, IRType::bool_(), {lhs, rhs});
}

ValueRef IRBuilder::logical_not(ValueRef operand) {
    if (operand.type.kind != IRType::Kind::Bool) {
        std::cerr << "Logical NOT requires boolean operand, got: " << operand.type.to_string() << "\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Not, IRType::bool_(), {operand});
}

// Memory operations
ValueRef IRBuilder::alloca(IRType type) {
    return emit_with_data(Op::Alloca, IRType::ptr_to(type), {}, type.to_string());
}

void IRBuilder::store(ValueRef value, ValueRef ptr) {
    if (ptr.type.kind != IRType::Kind::Ptr) {
        std::cerr << "Store target must be a pointer\n";
        return;
    }
    
    emit(Op::Store, IRType::void_(), {value, ptr});
}

ValueRef IRBuilder::load(ValueRef ptr, IRType type) {
    if (ptr.type.kind != IRType::Kind::Ptr) {
        std::cerr << "Load source must be a pointer\n";
        return ValueRef::invalid();
    }
    
    return emit(Op::Load, type, {ptr});
}

ValueRef IRBuilder::gep(ValueRef ptr, const std::vector<int>& indices, IRType result_type) {
    if (ptr.type.kind != IRType::Kind::Ptr) {
        std::cerr << "GEP requires a pointer operand\n";
        return ValueRef::invalid();
    }
    
    // Create a string representation of indices for the data field
    std::string indices_str;
    for (size_t i = 0; i < indices.size(); ++i) {
        if (i > 0) indices_str += ",";
        indices_str += std::to_string(indices[i]);
    }
    
    return emit_with_data(Op::GEP, result_type, {ptr}, indices_str);
}

// Control flow
void IRBuilder::ret(ValueRef value) {
    emit(Op::Ret, IRType::void_(), {value});
}

void IRBuilder::ret_void() {
    emit(Op::RetVoid, IRType::void_(), {});
}

// Control flow operations
void IRBuilder::label(const std::string& name) {
    emit_with_data(Op::Label, IRType::void_(), {}, name);
}

void IRBuilder::br(const std::string& target_label) {
    emit_with_data(Op::Br, IRType::void_(), {}, target_label);
}

void IRBuilder::br_cond(ValueRef condition, const std::string& true_label, const std::string& false_label) {
    if (condition.type.kind != IRType::Kind::Bool) {
        std::cerr << "Conditional branch condition must be boolean, got: " << condition.type.to_string() << "\n";
        return;
    }
    
    // Store both labels in a single string separated by comma
    std::string labels = true_label + "," + false_label;
    emit_with_data(Op::BrCond, IRType::void_(), {condition}, labels);
}

bool IRBuilder::has_terminator() const {
    if (commands_.empty()) {
        return false;
    }
    
    // Look for the last non-label command
    for (int i = commands_.size() - 1; i >= 0; --i) {
        const Command& cmd = commands_[i];
        
        // Skip labels - they're not instructions
        if (cmd.op == Op::Label) {
            continue;
        }
        
        // Check if this is a terminator
        return cmd.op == Op::Ret || 
               cmd.op == Op::RetVoid || 
               cmd.op == Op::Br || 
               cmd.op == Op::BrCond;
    }
    
    // No non-label commands found
    return false;
}


// Function management
void IRBuilder::function_begin(const std::string& name, IRType return_type, const std::vector<IRType>& param_types) {
    // Encode function signature as "name:return_type:param1,param2,..."
    std::string signature = name + ":" + return_type.to_string();
    if (!param_types.empty()) {
        signature += ":";
        for (size_t i = 0; i < param_types.size(); ++i) {
            if (i > 0) signature += ",";
            signature += param_types[i].to_string();
        }
    }
    emit_with_data(Op::FunctionBegin, IRType::void_(), {}, signature);
}

void IRBuilder::function_end() {
    emit(Op::FunctionEnd, IRType::void_(), {});
}

ValueRef IRBuilder::call(const std::string& function_name, IRType return_type, const std::vector<ValueRef>& args) {
    return emit_with_data(Op::Call, return_type, args, function_name);
}

// For debugging
void IRBuilder::dump_commands() const {
    LOG_DEBUG("Command stream (" + std::to_string(commands_.size()) + " commands):", LogCategory::CODEGEN);
    for (size_t i = 0; i < commands_.size(); ++i) {
        const auto& cmd = commands_[i];
        std::string line = "[" + std::to_string(i) + "] " + cmd.to_string();
        LOG_DEBUG(line, LogCategory::CODEGEN);
    }
}

} // namespace Mycelium::Scripting::Lang