#include "duckdb/parser/expression/columnref_expression.hpp"
#include "duckdb/parser/expression/constant_expression.hpp"
#include "duckdb/parser/expression_map.hpp"
#include "duckdb/parser/query_node/select_node.hpp"
#include "duckdb/parser/query_node/set_operation_node.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/expression/bound_columnref_expression.hpp"
#include "duckdb/planner/query_node/bound_set_operation_node.hpp"
#include "duckdb/planner/expression_binder/order_binder.hpp"

using namespace duckdb;
using namespace std;

static void GatherAliases(QueryNode &node, unordered_map<string, idx_t> &aliases,
                          expression_map_t<idx_t> &expressions) {
	if (node.type == QueryNodeType::SET_OPERATION_NODE) {
		// setop, recurse
		auto &setop = (SetOperationNode &)node;
		GatherAliases(*setop.left, aliases, expressions);
		GatherAliases(*setop.right, aliases, expressions);
	} else {
		// query node
		assert(node.type == QueryNodeType::SELECT_NODE);
		auto &select = (SelectNode &)node;
		// fill the alias lists
		for (idx_t i = 0; i < select.select_list.size(); i++) {
			auto &expr = select.select_list[i];
			auto name = expr->GetName();
			// first check if the alias is already in there
			auto entry = aliases.find(name);
			if (entry != aliases.end()) {
				// the alias already exists
				// check if there is a conflict
				if (entry->second != i) {
					// there is a conflict
					// we place "-1" in the aliases map at this location
					// "-1" signifies that there is an ambiguous reference
					aliases[name] = INVALID_INDEX;
				}
			} else {
				// the alias is not in there yet, just assign it
				aliases[name] = i;
			}
			// now check if the node is already in the set of expressions
			auto expr_entry = expressions.find(expr.get());
			if (expr_entry != expressions.end()) {
				// the node is in there
				// repeat the same as with the alias: if there is an ambiguity we insert "-1"
				if (expr_entry->second != i) {
					expressions[expr.get()] = INVALID_INDEX;
				}
			} else {
				// not in there yet, just place it in there
				expressions[expr.get()] = i;
			}
		}
	}
}

unique_ptr<BoundQueryNode> Binder::BindNode(SetOperationNode &statement) {
	auto result = make_unique<BoundSetOperationNode>();
	result->setop_type = statement.setop_type;

	// first recursively visit the set operations
	// both the left and right sides have an independent BindContext and Binder
	assert(statement.left);
	assert(statement.right);

	result->setop_index = GenerateTableIndex();

	if (statement.modifiers.size() > 0) {
		// handle the ORDER BY/DISTINCT clauses
		// NOTE: we handle the ORDER BY in SET OPERATIONS before binding the children
		// we do so we can perform expression comparisons BEFORE type resolution/binding

		// we recursively visit the children of this node to extract aliases and expressions that can be referenced in
		// the ORDER BY
		unordered_map<string, idx_t> alias_map;
		expression_map_t<idx_t> expression_map;
		GatherAliases(statement, alias_map, expression_map);

		// now we perform the actual resolution of the ORDER BY/DISTINCT expressions
		OrderBinder order_binder(result->setop_index, alias_map, expression_map, statement.left->GetSelectList().size());
		BindModifiers(order_binder, statement, *result);
	}

	result->left_binder = make_unique<Binder>(context, this);
	result->left = result->left_binder->BindNode(*statement.left);

	result->right_binder = make_unique<Binder>(context, this);
	result->right = result->right_binder->BindNode(*statement.right);

	result->names = result->left->names;

	// move the correlated expressions from the child binders to this binder
	MoveCorrelatedExpressions(*result->left_binder);
	MoveCorrelatedExpressions(*result->right_binder);

	// now both sides have been bound we can resolve types
	if (result->left->types.size() != result->right->types.size()) {
		throw Exception("Set operations can only apply to expressions with the "
		                "same number of result columns");
	}

	// figure out the types of the setop result by picking the max of both
	vector<TypeId> internal_types;
	for (idx_t i = 0; i < result->left->types.size(); i++) {
		auto result_type = MaxSQLType(result->left->types[i], result->right->types[i]);
		result->types.push_back(result_type);
		internal_types.push_back(GetInternalType(result_type));
	}

	// finally bind the types of the ORDER/DISTINCT clause expressions
	BindModifierTypes(*result, internal_types, result->setop_index);
	return move(result);
}
