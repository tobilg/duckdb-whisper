#pragma once

#include "duckdb.hpp"
#include "duckdb/main/extension/extension_loader.hpp"
#include "duckdb/parser/parsed_data/create_function_info.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

// Catalog metadata exposed through duckdb_functions().
// A single description without parameter_types applies to every overload of a function, and each overload takes
// as many entries from parameter_names as it has arguments. All whisper overload sets only add optional trailing
// arguments, so parameter_names lists the names of the longest overload.
inline FunctionDescription MakeFunctionDescription(vector<string> parameter_names, string description,
                                                   vector<string> examples, vector<string> categories) {
	FunctionDescription result;
	result.parameter_names = std::move(parameter_names);
	result.description = std::move(description);
	result.examples = std::move(examples);
	result.categories = std::move(categories);
	return result;
}

// Same conflict behavior as the bare ExtensionLoader::RegisterFunction overloads
template <class FUNCTION>
void RegisterScalarFunction(ExtensionLoader &loader, FUNCTION function, FunctionDescription description) {
	CreateScalarFunctionInfo info(std::move(function));
	info.descriptions.push_back(std::move(description));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

template <class FUNCTION>
void RegisterTableFunction(ExtensionLoader &loader, FUNCTION function, FunctionDescription description) {
	CreateTableFunctionInfo info(std::move(function));
	info.descriptions.push_back(std::move(description));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

} // namespace duckdb
