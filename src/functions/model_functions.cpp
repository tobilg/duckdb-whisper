#include "duckdb.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

#include "duckdb/common/exception.hpp"

#include "model_manager.hpp"
#include "whisper_config.hpp"

namespace duckdb {

// ============================================================================
// whisper_list_models() - Table function listing all available models
// ============================================================================

struct ListModelsState : public GlobalTableFunctionState {
	std::vector<ModelInfo> models;
	idx_t current_idx;

	ListModelsState() : current_idx(0) {
	}

	idx_t MaxThreads() const override {
		return 1;
	}
};

static unique_ptr<FunctionData> ListModelsBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
	return_types.push_back(LogicalType::VARCHAR); // name
	names.push_back("name");

	return_types.push_back(LogicalType::BOOLEAN); // is_downloaded
	names.push_back("is_downloaded");

	return_types.push_back(LogicalType::BIGINT); // file_size
	names.push_back("file_size");

	return_types.push_back(LogicalType::VARCHAR); // file_path
	names.push_back("file_path");

	return_types.push_back(LogicalType::VARCHAR); // description
	names.push_back("description");

	return nullptr;
}

static unique_ptr<GlobalTableFunctionState> ListModelsInit(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<ListModelsState>();
	auto config = WhisperConfigManager::GetConfig(context);
	state->models = ModelManager::ListModels(config.model_path);
	return std::move(state);
}

static void ListModelsExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<ListModelsState>();

	idx_t output_idx = 0;
	while (state.current_idx < state.models.size() && output_idx < STANDARD_VECTOR_SIZE) {
		const auto &model = state.models[state.current_idx];

		output.SetValue(0, output_idx, Value(model.name));
		output.SetValue(1, output_idx, Value::BOOLEAN(model.is_downloaded));
		output.SetValue(2, output_idx, model.is_downloaded ? Value::BIGINT(model.file_size) : Value());
		output.SetValue(3, output_idx, Value(model.file_path));
		output.SetValue(4, output_idx, Value(model.description));

		state.current_idx++;
		output_idx++;
	}

	output.SetCardinality(output_idx);
}

// ============================================================================
// whisper_download_model(model_name) - Scalar function to download a model
// ============================================================================

static void WhisperDownloadModelFunction(DataChunk &args, ExpressionState &state, Vector &result) {
	auto &context = state.GetContext();
	auto config = WhisperConfigManager::GetConfig(context);

	auto &model_name_vec = args.data[0];
	idx_t count = args.size();

	UnaryExecutor::Execute<string_t, string_t>(model_name_vec, result, count, [&](string_t model_name_val) {
		std::string model_name = model_name_val.GetString();
		std::string error;

		if (!ModelManager::IsValidModelName(model_name)) {
			throw InvalidInputException("Invalid model name: " + model_name +
			                            ". Use whisper_list_models() to see available models.");
		}

		if (ModelManager::IsModelDownloaded(model_name, config.model_path)) {
			return StringVector::AddString(result, "Model '" + model_name + "' is already downloaded");
		}

		if (!ModelManager::DownloadModel(model_name, config.model_path, error)) {
			throw InvalidInputException("Failed to download model: " + error);
		}

		return StringVector::AddString(result, "Successfully downloaded model '" + model_name + "'");
	});
}

// ============================================================================
// whisper_model_info() - Table function showing current model info
// ============================================================================

struct ModelInfoState : public GlobalTableFunctionState {
	ModelInfo info;
	bool returned;

	ModelInfoState() : returned(false) {
	}

	idx_t MaxThreads() const override {
		return 1;
	}
};

static unique_ptr<FunctionData> ModelInfoBind(ClientContext &context, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names) {
	return_types.push_back(LogicalType::VARCHAR); // current_model
	names.push_back("current_model");

	return_types.push_back(LogicalType::VARCHAR); // model_path
	names.push_back("model_path");

	return_types.push_back(LogicalType::BOOLEAN); // is_downloaded
	names.push_back("is_downloaded");

	return_types.push_back(LogicalType::BIGINT); // file_size
	names.push_back("file_size");

	return_types.push_back(LogicalType::VARCHAR); // language
	names.push_back("language");

	return_types.push_back(LogicalType::INTEGER); // threads
	names.push_back("threads");

	return nullptr;
}

static unique_ptr<GlobalTableFunctionState> ModelInfoInit(ClientContext &context, TableFunctionInitInput &input) {
	auto state = make_uniq<ModelInfoState>();
	auto config = WhisperConfigManager::GetConfig(context);
	state->info = ModelManager::GetModelInfo(config.model, config.model_path);
	return std::move(state);
}

static void ModelInfoExecute(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &state = data.global_state->Cast<ModelInfoState>();

	if (state.returned) {
		output.SetCardinality(0);
		return;
	}

	auto config = WhisperConfigManager::GetConfig(context);

	output.SetValue(0, 0, Value(config.model));
	output.SetValue(1, 0, Value(state.info.file_path));
	output.SetValue(2, 0, Value::BOOLEAN(state.info.is_downloaded));
	output.SetValue(3, 0, state.info.is_downloaded ? Value::BIGINT(state.info.file_size) : Value());
	output.SetValue(4, 0, Value(config.language));
	output.SetValue(5, 0, Value::INTEGER(config.threads));

	output.SetCardinality(1);
	state.returned = true;
}

// ============================================================================
// Registration
// ============================================================================

void RegisterModelFunctions(ExtensionLoader &loader) {
	// whisper_list_models()
	TableFunction list_models("whisper_list_models", {}, ListModelsExecute, ListModelsBind, ListModelsInit);
	loader.RegisterFunction(list_models);

	// whisper_download_model(model_name)
	auto download_func = ScalarFunction("whisper_download_model", {LogicalType::VARCHAR}, LogicalType::VARCHAR,
	                                    WhisperDownloadModelFunction);
	loader.RegisterFunction(download_func);

	// whisper_model_info()
	TableFunction model_info("whisper_model_info", {}, ModelInfoExecute, ModelInfoBind, ModelInfoInit);
	loader.RegisterFunction(model_info);
}

} // namespace duckdb
