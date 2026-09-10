// Copyright ZhaoZining. All Rights Reserved.

#include "Core/Json/JsonValue.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <utility>

namespace worlddata::json
{
	Value Value::Boolean(const bool value)
	{
		Value result;
		result.type = Type::Boolean;
		result.boolean = value;
		return result;
	}

	Value Value::Number(const double value)
	{
		Value result;
		result.type = Type::Number;
		result.number = value;
		return result;
	}

	Value Value::String(std::string value)
	{
		Value result;
		result.type = Type::String;
		result.string = std::move(value);
		return result;
	}

	Value Value::Array(std::vector<Value> value)
	{
		Value result;
		result.type = Type::Array;
		result.array = std::move(value);
		return result;
	}

	Value Value::Object(std::map<std::string, Value, std::less<>> value)
	{
		Value result;
		result.type = Type::Object;
		result.object = std::move(value);
		return result;
	}

	const Value* Value::Find(const std::string_view key) const
	{
		if (type != Type::Object)
		{
			return nullptr;
		}
		const auto iterator = object.find(key);
		return iterator == object.end() ? nullptr : &iterator->second;
	}

	Value* Value::Find(const std::string_view key)
	{
		return const_cast<Value*>(std::as_const(*this).Find(key));
	}

	std::string Value::StringOr(const std::string_view fallback) const
	{
		return type == Type::String ? string : std::string(fallback);
	}

	bool Value::BooleanOr(const bool fallback) const
	{
		return type == Type::Boolean ? boolean : fallback;
	}

	double Value::NumberOr(const double fallback) const
	{
		return type == Type::Number ? number : fallback;
	}

	namespace
	{
		void AppendUtf8(std::string& output, const unsigned code_point)
		{
			if (code_point <= 0x7f)
			{
				output.push_back(static_cast<char>(code_point));
			}
			else if (code_point <= 0x7ff)
			{
				output.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
				output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
			}
			else if (code_point <= 0xffff)
			{
				output.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
				output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
				output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
			}
			else
			{
				output.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
				output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
				output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
				output.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
			}
		}

		class Parser
		{
		public:
			explicit Parser(const std::string_view input) : input_(input)
			{
			}

			bool ParseDocument(Value& output, std::string& error)
			{
				SkipWhitespace();
				if (!ParseValue(output, error))
				{
					return false;
				}
				SkipWhitespace();
				if (position_ != input_.size())
				{
					error = "Unexpected trailing JSON data.";
					return false;
				}
				return true;
			}

		private:
			void SkipWhitespace()
			{
				while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\t' || input_[position_] == '\r' || input_[position_] == '\n'))
				{
					++position_;
				}
			}

			bool Consume(const char expected)
			{
				if (position_ < input_.size() && input_[position_] == expected)
				{
					++position_;
					return true;
				}
				return false;
			}

			bool ConsumeLiteral(const std::string_view literal)
			{
				if (input_.substr(position_, literal.size()) == literal)
				{
					position_ += literal.size();
					return true;
				}
				return false;
			}

			bool ParseValue(Value& output, std::string& error)
			{
				SkipWhitespace();
				if (position_ >= input_.size())
				{
					error = "Unexpected end of JSON.";
					return false;
				}
				switch (input_[position_])
				{
				case 'n':
					if (ConsumeLiteral("null"))
					{
						output = Value{};
						return true;
					}
					break;
				case 't':
					if (ConsumeLiteral("true"))
					{
						output = Value::Boolean(true);
						return true;
					}
					break;
				case 'f':
					if (ConsumeLiteral("false"))
					{
						output = Value::Boolean(false);
						return true;
					}
					break;
				case '"':
				{
					std::string text;
					if (!ParseString(text, error))
					{
						return false;
					}
					output = Value::String(std::move(text));
					return true;
				}
				case '[':
					return ParseArray(output, error);
				case '{':
					return ParseObject(output, error);
				default:
					if (input_[position_] == '-' || (input_[position_] >= '0' && input_[position_] <= '9'))
					{
						return ParseNumber(output, error);
					}
					break;
				}
				error = "Invalid JSON value.";
				return false;
			}

			bool ParseString(std::string& output, std::string& error)
			{
				if (!Consume('"'))
				{
					error = "Expected JSON string.";
					return false;
				}
				while (position_ < input_.size())
				{
					const unsigned char current = static_cast<unsigned char>(input_[position_++]);
					if (current == '"')
					{
						return true;
					}
					if (current < 0x20)
					{
						error = "Unescaped control character in JSON string.";
						return false;
					}
					if (current != '\\')
					{
						output.push_back(static_cast<char>(current));
						continue;
					}
					if (position_ >= input_.size())
					{
						error = "Incomplete JSON string escape.";
						return false;
					}
					const char escaped = input_[position_++];
					switch (escaped)
					{
					case '"':
						output.push_back('"');
						break;
					case '\\':
						output.push_back('\\');
						break;
					case '/':
						output.push_back('/');
						break;
					case 'b':
						output.push_back('\b');
						break;
					case 'f':
						output.push_back('\f');
						break;
					case 'n':
						output.push_back('\n');
						break;
					case 'r':
						output.push_back('\r');
						break;
					case 't':
						output.push_back('\t');
						break;
					case 'u':
					{
						unsigned code_point = 0;
						if (!ParseHex4(code_point))
						{
							error = "Invalid JSON unicode escape.";
							return false;
						}
						if (code_point >= 0xd800 && code_point <= 0xdbff)
						{
							if (!Consume('\\') || !Consume('u'))
							{
								error = "Missing low surrogate.";
								return false;
							}
							unsigned low = 0;
							if (!ParseHex4(low) || low < 0xdc00 || low > 0xdfff)
							{
								error = "Invalid low surrogate.";
								return false;
							}
							code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
						}
						AppendUtf8(output, code_point);
						break;
					}
					default:
						error = "Unknown JSON string escape.";
						return false;
					}
				}
				error = "Unterminated JSON string.";
				return false;
			}

			bool ParseHex4(unsigned& output)
			{
				if (position_ + 4 > input_.size())
				{
					return false;
				}
				output = 0;
				for (int index = 0; index < 4; ++index)
				{
					const char value = input_[position_++];
					output <<= 4;
					if (value >= '0' && value <= '9')
						output += value - '0';
					else if (value >= 'a' && value <= 'f')
						output += value - 'a' + 10;
					else if (value >= 'A' && value <= 'F')
						output += value - 'A' + 10;
					else
						return false;
				}
				return true;
			}

			bool ParseNumber(Value& output, std::string& error)
			{
				const std::size_t start = position_;
				if (input_[position_] == '-')
					++position_;
				if (position_ >= input_.size())
				{
					error = "Incomplete JSON number.";
					return false;
				}
				if (input_[position_] == '0')
				{
					++position_;
				}
				else
				{
					if (input_[position_] < '1' || input_[position_] > '9')
					{
						error = "Invalid JSON number.";
						return false;
					}
					while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
						++position_;
				}
				if (position_ < input_.size() && input_[position_] == '.')
				{
					++position_;
					while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
						++position_;
				}
				if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E'))
				{
					++position_;
					if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-'))
						++position_;
					while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
						++position_;
				}
				const std::string number_text(input_.substr(start, position_ - start));
				char* end = nullptr;
				const double number = std::strtod(number_text.c_str(), &end);
				if (!end || *end != '\0' || !std::isfinite(number))
				{
					error = "Invalid finite JSON number.";
					return false;
				}
				output = Value::Number(number);
				return true;
			}

			bool ParseArray(Value& output, std::string& error)
			{
				Consume('[');
				std::vector<Value> values;
				SkipWhitespace();
				if (Consume(']'))
				{
					output = Value::Array();
					return true;
				}
				for (;;)
				{
					Value value;
					if (!ParseValue(value, error))
						return false;
					values.push_back(std::move(value));
					SkipWhitespace();
					if (Consume(']'))
						break;
					if (!Consume(','))
					{
						error = "Expected comma in JSON array.";
						return false;
					}
				}
				output = Value::Array(std::move(values));
				return true;
			}

			bool ParseObject(Value& output, std::string& error)
			{
				Consume('{');
				std::map<std::string, Value, std::less<>> values;
				SkipWhitespace();
				if (Consume('}'))
				{
					output = Value::Object();
					return true;
				}
				for (;;)
				{
					SkipWhitespace();
					std::string key;
					if (!ParseString(key, error))
						return false;
					SkipWhitespace();
					if (!Consume(':'))
					{
						error = "Expected colon in JSON object.";
						return false;
					}
					Value value;
					if (!ParseValue(value, error))
						return false;
					values.insert_or_assign(std::move(key), std::move(value));
					SkipWhitespace();
					if (Consume('}'))
						break;
					if (!Consume(','))
					{
						error = "Expected comma in JSON object.";
						return false;
					}
				}
				output = Value::Object(std::move(values));
				return true;
			}

			std::string_view input_;
			std::size_t position_ = 0;
		};

		void SerializeString(const std::string_view input, std::string& output)
		{
			output.push_back('"');
			for (const unsigned char value : input)
			{
				switch (value)
				{
				case '"':
					output += "\\\"";
					break;
				case '\\':
					output += "\\\\";
					break;
				case '\b':
					output += "\\b";
					break;
				case '\f':
					output += "\\f";
					break;
				case '\n':
					output += "\\n";
					break;
				case '\r':
					output += "\\r";
					break;
				case '\t':
					output += "\\t";
					break;
				default:
					if (value < 0x20)
					{
						static constexpr char Hex[] = "0123456789abcdef";
						output += "\\u00";
						output.push_back(Hex[(value >> 4) & 0xf]);
						output.push_back(Hex[value & 0xf]);
					}
					else
					{
						output.push_back(static_cast<char>(value));
					}
				}
			}
			output.push_back('"');
		}

		void SerializeValue(const Value& value, std::string& output)
		{
			switch (value.type)
			{
			case Type::Null:
				output += "null";
				break;
			case Type::Boolean:
				output += value.boolean ? "true" : "false";
				break;
			case Type::Number:
			{
				std::ostringstream stream;
				stream << std::setprecision(17) << value.number;
				output += stream.str();
				break;
			}
			case Type::String:
				SerializeString(value.string, output);
				break;
			case Type::Array:
				output.push_back('[');
				for (std::size_t index = 0; index < value.array.size(); ++index)
				{
					if (index)
						output.push_back(',');
					SerializeValue(value.array[index], output);
				}
				output.push_back(']');
				break;
			case Type::Object:
				output.push_back('{');
				{
					bool first = true;
					for (const auto& [key, item] : value.object)
					{
						if (!first)
							output.push_back(',');
						first = false;
						SerializeString(key, output);
						output.push_back(':');
						SerializeValue(item, output);
					}
				}
				output.push_back('}');
				break;
			}
		}
	}

	bool Parse(const std::string_view text, Value& out_value, std::string& out_error)
	{
		out_error.clear();
		return Parser(text).ParseDocument(out_value, out_error);
	}

	std::string Serialize(const Value& value)
	{
		std::string output;
		SerializeValue(value, output);
		return output;
	}
}
