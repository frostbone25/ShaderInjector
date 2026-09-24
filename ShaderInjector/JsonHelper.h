#pragma once
#ifndef JSON_HELPER_H
#define JSON_HELPER_H

//THIRD PARTY: JSON Library
#include <json.hpp>

//new customized macros because the json library does not have macros for it's ordered_json class.
//NOTE TO SELF: we use ordered_json because it's annoying when popping json text files and fields move around at random, this keeps everything consistent

#define NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE(Type, ...)                                       \
	friend void to_json(nlohmann::ordered_json& nlohmann_json_j, const Type& nlohmann_json_t)   \
	{                                                                                           \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__))                \
	}                                                                                           \
	friend void from_json(const nlohmann::ordered_json& nlohmann_json_j, Type& nlohmann_json_t) \
	{                                                                                           \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM, __VA_ARGS__))              \
	}

#define NLOHMANN_ORDERED_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Type, ...)                          \
	friend void to_json(nlohmann::ordered_json& nlohmann_json_j, const Type& nlohmann_json_t)   \
	{                                                                                           \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_TO, __VA_ARGS__))                \
	}                                                                                           \
	friend void from_json(const nlohmann::ordered_json& nlohmann_json_j, Type& nlohmann_json_t) \
	{                                                                                           \
		const Type nlohmann_json_default_obj{};                                                 \
		NLOHMANN_JSON_EXPAND(NLOHMANN_JSON_PASTE(NLOHMANN_JSON_FROM_WITH_DEFAULT, __VA_ARGS__)) \
	}

#endif