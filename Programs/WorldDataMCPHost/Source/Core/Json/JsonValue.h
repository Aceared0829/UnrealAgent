#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace worlddata::json
{
	/** 独立 Host 支持的 JSON 值类型。 */
	enum class Type
	{
		Null,
		Boolean,
		Number,
		String,
		Array,
		Object
	};

	/**
	 * 不依赖 Unreal Engine 的轻量 JSON 值。
	 *
	 * 该类型仅承载 MCP 协议数据，不包含业务或传输状态。
	 */
	struct Value
	{
		Type type = Type::Null;
		bool boolean = false;
		double number = 0.0;
		std::string string;
		std::vector<Value> array;
		std::map<std::string, Value, std::less<>> object;

		static Value Boolean(bool value);
		static Value Number(double value);
		static Value String(std::string value);
		static Value Array(std::vector<Value> value = {});
		static Value Object(std::map<std::string, Value, std::less<>> value = {});

		const Value* Find(std::string_view key) const;
		Value* Find(std::string_view key);
		std::string StringOr(std::string_view fallback = {}) const;
		bool BooleanOr(bool fallback = false) const;
		double NumberOr(double fallback = 0.0) const;
	};

	/** 将 UTF-8 JSON 文本解析为值；失败时返回可诊断错误。 */
	bool Parse(std::string_view text, Value& out_value, std::string& out_error);
	/** 将 JSON 值序列化为紧凑 UTF-8 文本。 */
	std::string Serialize(const Value& value);
}
