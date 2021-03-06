/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
 * Copyright (c) 2015, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE EARLIER MENTIONED AUTHORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <copyright holder> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "CppUTest/TestHarness.h"
#include "CppUTest/CommandLineArguments.h"
#include "CppUTest/TestRegistry.h"

class OptionsPlugin: public TestPlugin
{
public:
	OptionsPlugin(const SimpleString& name) :
		TestPlugin(name)
	{
	}
	;
	~OptionsPlugin()
	{
	}
	;
	bool parseArguments(int ac, const char** av, int index)
	{
		return true;
	}
	;
};

TEST_GROUP(CommandLineArguments)
{ CommandLineArguments* args;
OptionsPlugin* plugin;

void setup()
{
	plugin = new OptionsPlugin("options");
}
void teardown()
{
	delete args;
	delete plugin;
}

bool newArgumentParser(int argc, const char** argv)
{
	args = new CommandLineArguments(argc, argv, plugin);
	return args->parse();
}
};

TEST(CommandLineArguments, Create)
{
}

TEST(CommandLineArguments, verboseSetMultipleParameters)
{
	const char* argv[] = { "tests.exe", "-v" };
	CHECK(newArgumentParser(2, argv));
	CHECK(args->isVerbose());
}

TEST(CommandLineArguments, repeatSet)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-r3" };
	CHECK(newArgumentParser(argc, argv));
	LONGS_EQUAL(3, args->getRepeatCount());
}

TEST(CommandLineArguments, repeatSetDifferentParameter)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-r", "4" };
	CHECK(newArgumentParser(argc, argv));
	LONGS_EQUAL(4, args->getRepeatCount());
}

TEST(CommandLineArguments, repeatSetDefaultsToTwo)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-r" };
	CHECK(newArgumentParser(argc, argv));
	LONGS_EQUAL(2, args->getRepeatCount());
}

TEST(CommandLineArguments, setGroupFilter)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-g", "group" };
	CHECK(newArgumentParser(argc, argv));
	STRCMP_EQUAL("group", args->getGroupFilter().asCharString());
}

TEST(CommandLineArguments, setGroupFilterSameParameter)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-ggroup" };
	CHECK(newArgumentParser(argc, argv));
	STRCMP_EQUAL("group", args->getGroupFilter().asCharString());
}

TEST(CommandLineArguments, setNameFilter)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-n", "name" };
	CHECK(newArgumentParser(argc, argv));
	STRCMP_EQUAL("name", args->getNameFilter().asCharString());
}

TEST(CommandLineArguments, setNameFilterSameParameter)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-nname" };
	CHECK(newArgumentParser(argc, argv));
	STRCMP_EQUAL("name", args->getNameFilter().asCharString());
}

TEST(CommandLineArguments, setNormalOutput)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-onormal" };
	CHECK(newArgumentParser(argc, argv));
	CHECK(args->isEclipseOutput());
}

TEST(CommandLineArguments, setEclsipeOutput)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-oeclipse" };
	CHECK(newArgumentParser(argc, argv));
	CHECK(args->isEclipseOutput());
}

TEST(CommandLineArguments, setNormalOutputDifferentParameter)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-o", "normal" };
	CHECK(newArgumentParser(argc, argv));
	CHECK(args->isEclipseOutput());
}

TEST(CommandLineArguments, setJUnitOutputDifferentParameter)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-o", "junit" };
	CHECK(newArgumentParser(argc, argv));
	CHECK(args->isJUnitOutput());
}

TEST(CommandLineArguments, setOutputToGarbage)
{
	int argc = 3;
	const char* argv[] = { "tests.exe", "-o", "garbage" };
	CHECK(!newArgumentParser(argc, argv));
}

TEST(CommandLineArguments, weirdParamatersPrintsUsageAndReturnsFalse)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-SomethingWeird" };
	CHECK(!newArgumentParser(argc, argv));
	STRCMP_EQUAL("usage [-v] [-t#] [-r#] [-g groupName] [-n testName] [-o{normal, junit}]\n",
			args->usage());
}

TEST(CommandLineArguments, pluginKnowsOption)
{
	int argc = 2;
	const char* argv[] = { "tests.exe", "-pPluginOption" };
	TestRegistry::getCurrentRegistry()->installPlugin(plugin);
	CHECK(newArgumentParser(argc, argv));
	TestRegistry::getCurrentRegistry()->removePluginByName("options");
}

TEST(CommandLineArguments, checkDefaultArguments)
{
	int argc = 1;
	const char* argv[] = { "tests.exe" };
	CHECK(newArgumentParser(argc, argv));
	CHECK(!args->isVerbose());
	LONGS_EQUAL(1, args->getRepeatCount());
	STRCMP_EQUAL("", args->getGroupFilter().asCharString());
	STRCMP_EQUAL("", args->getNameFilter().asCharString());
	CHECK(args->isEclipseOutput());
}
