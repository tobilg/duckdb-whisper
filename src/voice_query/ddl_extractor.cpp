#ifdef WHISPER_ENABLE_VOICE_QUERY

#include "duckdb.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database_manager.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include <string>
#include <sstream>

namespace duckdb {

std::string ExtractDatabaseDDL(ClientContext &context) {
	// Use catalog API directly instead of running a query (avoids deadlock in bind phase)
	std::ostringstream ddl_stream;
	bool first = true;

	try {
		// Get all attached databases
		auto &db_manager = DatabaseManager::Get(context);
		auto databases = db_manager.GetDatabases(context);

		for (auto &db : databases) {
			// Skip system database
			if (db->IsSystem()) {
				continue;
			}

			auto &catalog = db->GetCatalog();
			auto schemas = catalog.GetSchemas(context);

			for (auto &schema_ref : schemas) {
				auto &schema = schema_ref.get();

				// Skip internal schemas
				if (schema.name == "pg_catalog" || schema.name == "information_schema") {
					continue;
				}

				// Scan all tables in this schema
				schema.Scan(context, CatalogType::TABLE_ENTRY, [&](CatalogEntry &entry) {
					if (entry.type == CatalogType::TABLE_ENTRY) {
						auto &table = entry.Cast<TableCatalogEntry>();
						// Skip internal tables
						if (table.internal) {
							return;
						}

						std::string sql = table.ToSQL();
						if (!sql.empty()) {
							if (!first) {
								ddl_stream << "; ";
							}
							ddl_stream << sql;
							first = false;
						}
					}
				});
			}
		}
	} catch (...) {
		// Return empty string on error
		return "";
	}

	return ddl_stream.str();
}

} // namespace duckdb

#endif // WHISPER_ENABLE_VOICE_QUERY
