// This file is part of the dosbox-automation Project.
// License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
//

#include "lua/script_validator.h"

#include <string>

#include <gtest/gtest.h>

namespace {

// -- Body validation --

TEST(ScriptValidator, AcceptsValidScript)
{
	const auto result = Lua::ScriptValidator::ValidateBody("print('hello')");
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, RejectsOversizedBody)
{
	const auto big    = std::string(257 * 1024, 'x');
	const auto result = Lua::ScriptValidator::ValidateBody(big);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 413);
}

TEST(ScriptValidator, AcceptsBodyAtExactLimit)
{
	const auto exact  = std::string(256 * 1024, 'x');
	const auto result = Lua::ScriptValidator::ValidateBody(exact);
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, RejectsEmptyBody)
{
	const auto result = Lua::ScriptValidator::ValidateBody("");
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsNullBytes)
{
	const std::string with_null = std::string("local x = 1") + '\0' +
	                              "os.execute('rm -rf /')";
	const auto result = Lua::ScriptValidator::ValidateBody(with_null);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
	EXPECT_NE(result.error.find("null"), std::string::npos);
}

TEST(ScriptValidator, RejectsBinaryContent)
{
	std::string binary(1000, '\x01');
	const auto result = Lua::ScriptValidator::ValidateBody(binary);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
	EXPECT_NE(result.error.find("binary"), std::string::npos);
}

TEST(ScriptValidator, AcceptsScriptWithWhitespace)
{
	const auto result = Lua::ScriptValidator::ValidateBody(
	        "local x = 1\n\tlocal y = 2\r\n");
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, AcceptsScriptAtBinaryThreshold)
{
	// 100 bytes total, 1 non-printable = 1% exactly. At threshold.
	std::string script(99, 'x');
	script += '\x01';
	const auto result = Lua::ScriptValidator::ValidateBody(script);
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, RejectsScriptAboveBinaryThreshold)
{
	// 100 bytes total, 2 non-printable = 2%.
	std::string script(98, 'x');
	script += "\x01\x02";
	const auto result = Lua::ScriptValidator::ValidateBody(script);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

// -- Content-Type validation --

TEST(ScriptValidator, AcceptsTextPlain)
{
	const auto result = Lua::ScriptValidator::ValidateContentType("text/plain");
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, AcceptsApplicationLua)
{
	const auto result = Lua::ScriptValidator::ValidateContentType("application/lua");
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, AcceptsTextPlainWithCharset)
{
	const auto result = Lua::ScriptValidator::ValidateContentType(
	        "text/plain; charset=utf-8");
	EXPECT_TRUE(result.ok);
}

TEST(ScriptValidator, RejectsFormUrlencoded)
{
	const auto result = Lua::ScriptValidator::ValidateContentType(
	        "application/x-www-form-urlencoded");
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 415);
}

TEST(ScriptValidator, RejectsMultipartFormData)
{
	const auto result = Lua::ScriptValidator::ValidateContentType(
	        "multipart/form-data");
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 415);
}

TEST(ScriptValidator, RejectsApplicationJson)
{
	const auto result = Lua::ScriptValidator::ValidateContentType(
	        "application/json");
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 415);
}

TEST(ScriptValidator, RejectsEmptyContentType)
{
	const auto result = Lua::ScriptValidator::ValidateContentType("");
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 415);
}

// -- Parameter validation --

TEST(ScriptValidator, AcceptsValidParams)
{
	Lua::ScriptParams params = {};
	const auto result = Lua::ScriptValidator::ValidateParams("my-script_01",
	                                                         "42",
	                                                         "true",
	                                                         params);
	EXPECT_TRUE(result.ok);
	EXPECT_EQ(params.name, "my-script_01");
	EXPECT_EQ(params.seed, 42);
	EXPECT_TRUE(params.debug);
}

TEST(ScriptValidator, AcceptsEmptyParamsAsDefaults)
{
	Lua::ScriptParams params = {};
	const auto result = Lua::ScriptValidator::ValidateParams("", "", "", params);
	EXPECT_TRUE(result.ok);
	EXPECT_EQ(params.name, "unnamed");
	EXPECT_FALSE(params.seed.has_value());
	EXPECT_FALSE(params.debug);
}

TEST(ScriptValidator, RejectsNameWithSlash)
{
	Lua::ScriptParams params = {};
	const auto result        = Lua::ScriptValidator::ValidateParams(
                "../../etc/passwd", "0", "false", params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsNameWithBackslash)
{
	Lua::ScriptParams params = {};
	const auto result = Lua::ScriptValidator::ValidateParams("foo\\bar",
	                                                         "0",
	                                                         "false",
	                                                         params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsNameWithDots)
{
	Lua::ScriptParams params = {};
	const auto result = Lua::ScriptValidator::ValidateParams("script.lua",
	                                                         "0",
	                                                         "false",
	                                                         params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsNameTooLong)
{
	Lua::ScriptParams params = {};
	const auto long_name     = std::string(65, 'a');
	const auto result = Lua::ScriptValidator::ValidateParams(long_name,
	                                                         "0",
	                                                         "false",
	                                                         params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, AcceptsNameAtExactLimit)
{
	Lua::ScriptParams params = {};
	const auto name_64       = std::string(64, 'a');
	const auto result        = Lua::ScriptValidator::ValidateParams(name_64,
                                                                 "0",
                                                                 "false",
                                                                 params);
	EXPECT_TRUE(result.ok);
	EXPECT_EQ(params.name, name_64);
}

TEST(ScriptValidator, RejectsNameWithSpecialChars)
{
	Lua::ScriptParams params = {};
	const auto result = Lua::ScriptValidator::ValidateParams("script<>name",
	                                                         "0",
	                                                         "false",
	                                                         params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsInvalidSeed)
{
	Lua::ScriptParams params = {};
	const auto result        = Lua::ScriptValidator::ValidateParams("test",
                                                                 "not-a-number",
                                                                 "false",
                                                                 params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, RejectsInvalidDebugValue)
{
	Lua::ScriptParams params = {};
	const auto result        = Lua::ScriptValidator::ValidateParams("test",
                                                                 "0",
                                                                 "maybe",
                                                                 params);
	EXPECT_FALSE(result.ok);
	EXPECT_EQ(result.http_status, 400);
}

TEST(ScriptValidator, AcceptsNegativeSeed)
{
	Lua::ScriptParams params = {};
	const auto result        = Lua::ScriptValidator::ValidateParams("test",
                                                                 "-12345",
                                                                 "false",
                                                                 params);
	EXPECT_TRUE(result.ok);
	EXPECT_EQ(params.seed, -12345);
}

} // namespace
