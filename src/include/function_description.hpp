#pragma once

#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
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

// Scalar functions that read audio files, record from the microphone or call HTTP endpoints must run for every row
// and never at plan time: volatile prevents constant folding (e.g. during EXPLAIN), fallible marks that they throw.
inline ScalarFunction MakeIOScalarFunction(vector<LogicalType> arguments, LogicalType return_type,
                                           scalar_function_t function) {
	ScalarFunction scalar_function(std::move(arguments), std::move(return_type), std::move(function));
	scalar_function.SetVolatile();
	scalar_function.SetFallible();
	return scalar_function;
}

// duckdb_functions() takes parameter names from each overload's signature and only falls back to
// FunctionDescription::parameter_names when the counts differ, so copy the description's names onto every overload.
template <class FUNCTION>
void SetParameterNames(FUNCTION &function, const vector<string> &names) {
	auto &signature = function.GetSignature();
	for (idx_t i = 0; i < signature.GetParameterCount() && i < names.size(); i++) {
		signature.GetParameter(i).SetName(Identifier(names[i]));
	}
}

// Same conflict behavior as the bare ExtensionLoader::RegisterFunction overloads
template <class FUNCTION_SET>
void RegisterScalarFunction(ExtensionLoader &loader, FUNCTION_SET function, FunctionDescription description) {
	CreateScalarFunctionInfo info(std::move(function));
	info.functions.ApplyToFunctions(
	    [&](ScalarFunction &overload) { SetParameterNames(overload, description.parameter_names); });
	info.descriptions.push_back(std::move(description));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

template <class FUNCTION_SET>
void RegisterTableFunction(ExtensionLoader &loader, FUNCTION_SET function, FunctionDescription description) {
	CreateTableFunctionInfo info(std::move(function));
	info.functions.ApplyToFunctions(
	    [&](TableFunction &overload) { SetParameterNames(overload, description.parameter_names); });
	info.descriptions.push_back(std::move(description));
	info.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
	loader.RegisterFunction(std::move(info));
}

} // namespace duckdb
