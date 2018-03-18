//------------------------------------------------------------------------------
// ScriptSession.h
// High-level interface to the compiler tools to evaluate snippets of code.
//
// File is under the MIT license; see LICENSE for details.
//------------------------------------------------------------------------------
#pragma once

#include "compilation/Compilation.h"
#include "parsing/SyntaxTree.h"

namespace slang {

/// A helper class that allows evaluating arbitrary snippets of SystemVerilog
/// source code and maintaining state across multiple eval calls.
class ScriptSession {
public:
    ScriptSession() : scope(compilation.createScriptScope()) {}

    ConstantValue eval(const std::string& text) {
        syntaxTrees.emplace_back(SyntaxTree::fromText(string_view(text)));

        const auto& node = syntaxTrees.back()->root();
        switch (node.kind) {
            case SyntaxKind::ParameterDeclarationStatement:
            case SyntaxKind::FunctionDeclaration:
            case SyntaxKind::TaskDeclaration:
            case SyntaxKind::InterfaceDeclaration:
            case SyntaxKind::ModuleDeclaration:
            case SyntaxKind::HierarchyInstantiation:
                scope.addMembers(node);
                return nullptr;
            case SyntaxKind::DataDeclaration: {
                SmallVectorSized<const VariableSymbol*, 2> symbols;
                VariableSymbol::fromSyntax(compilation, node.as<DataDeclarationSyntax>(), symbols);

                for (auto symbol : symbols) {
                    scope.addMember(*symbol);

                    ConstantValue initial;
                    if (symbol->initializer)
                        initial = symbol->initializer->eval(evalContext);
                    else
                        initial = SVInt(symbol->type->getBitWidth(), 0, symbol->type->isSigned());

                    evalContext.createLocal(symbol, initial);
                }
                return nullptr;
            }
            default:
                if (isExpression(node.kind))
                    return evalExpression(node.as<ExpressionSyntax>());
                else if (isStatement(node.kind))
                    return evalStatement(node.as<StatementSyntax>());
                else
                    // TODO: not supported yet
                    THROW_UNREACHABLE;
        }
    }

    ConstantValue evalExpression(const ExpressionSyntax& expr) {
        const auto& bound = Expression::bind(compilation, expr, BindContext(scope, LookupLocation::max));
        return bound.eval(evalContext);
    }

    ConstantValue evalStatement(const StatementSyntax&) {
        // TODO:
        return nullptr;
    }

    std::string reportDiagnostics() {
        // TODO: clean this up
        if (syntaxTrees.empty())
            return "";

        diagnostics.sort(syntaxTrees[0]->sourceManager());
        return DiagnosticWriter(syntaxTrees[0]->sourceManager()).report(diagnostics);
    }

private:
    std::vector<std::shared_ptr<SyntaxTree>> syntaxTrees;
    BumpAllocator alloc;
    Diagnostics diagnostics;
    Compilation compilation;
    CompilationUnitSymbol& scope;
    EvalContext evalContext;
};

}
