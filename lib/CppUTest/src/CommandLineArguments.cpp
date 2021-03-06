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
#include "CppUTest/PlatformSpecificFunctions.h"

CommandLineArguments::CommandLineArguments(int ac, const char** av,
		TestPlugin* plugin) :
	ac(ac), av(av), plugin_(plugin), verbose_(false), numThreads_(1), repeat_(1),
	groupFilter_(""), nameFilter_(""), outputType_(OUTPUT_ECLIPSE)
{
}

CommandLineArguments::~CommandLineArguments()
{
}

bool CommandLineArguments::parse()
{
	bool correctParameters = true;
	for (int i = 1; i < ac; i++) {
		SimpleString argument = av[i];
		if (argument == "-v") verbose_ = true;
		else if (argument.startsWith("-t")) SetNumThreads(ac, av, i);
		else if (argument.startsWith("-r")) SetRepeatCount(ac, av, i);
		else if (argument.startsWith("-g")) SetGroupFilter(ac, av, i);
		else if (argument.startsWith("-n")) SetNameFilter(ac, av, i);
		else if (argument.startsWith("-o")) correctParameters = SetOutputType(
				ac, av, i);
		else if (argument.startsWith("-p")) correctParameters
				= plugin_->parseArguments(ac, av, i);
		else correctParameters = false;

		if (correctParameters == false) {
			return false;
		}
	}
	return true;
}

const char* CommandLineArguments::usage() const
{
	return "usage [-v] [-t#] [-r#] [-g groupName] [-n testName] [-o{normal, junit}]\n";
}

bool CommandLineArguments::isVerbose() const
{
	return verbose_;
}

int CommandLineArguments::getNumThreads() const
{
	return numThreads_;
}

int CommandLineArguments::getRepeatCount() const
{
	return repeat_;
}

SimpleString CommandLineArguments::getGroupFilter() const
{
	return groupFilter_;
}

SimpleString CommandLineArguments::getNameFilter() const
{
	return nameFilter_;
}

void CommandLineArguments::SetNumThreads(int ac, const char** av, int& i)
{
	numThreads_ = 0;

	SimpleString numThreadsParameter(av[i]);
	if (numThreadsParameter.size() > 2) numThreads_ = PlatformSpecificAtoI(av[i] + 2);
	else if (i + 1 < ac) {
		numThreads_ = PlatformSpecificAtoI(av[i + 1]);
		if (numThreads_ != 0) i++;
	}

	if (numThreads_ < 1) numThreads_ = 1;

}

void CommandLineArguments::SetRepeatCount(int ac, const char** av, int& i)
{
	repeat_ = 0;

	SimpleString repeatParameter(av[i]);
	if (repeatParameter.size() > 2) repeat_ = PlatformSpecificAtoI(av[i] + 2);
	else if (i + 1 < ac) {
		repeat_ = PlatformSpecificAtoI(av[i + 1]);
		if (repeat_ != 0) i++;
	}

	if (0 == repeat_) repeat_ = 2;

}

SimpleString CommandLineArguments::getParameterField(int ac, const char** av,
		int& i)
{
	SimpleString parameter(av[i]);
	if (parameter.size() > 2) return av[i] + 2;
	else if (i + 1 < ac) return av[++i];
	return "";
}

void CommandLineArguments::SetGroupFilter(int ac, const char** av, int& i)
{
	SimpleString gf = getParameterField(ac, av, i);
	groupFilter_ = gf;
}

void CommandLineArguments::SetNameFilter(int ac, const char** av, int& i)
{
	nameFilter_ = getParameterField(ac, av, i);
}

bool CommandLineArguments::SetOutputType(int ac, const char** av, int& i)
{
	SimpleString outputType = getParameterField(ac, av, i);
	if (outputType.size() == 0) return false;

	if (outputType == "normal" || outputType == "eclipse") {
		outputType_ = OUTPUT_ECLIPSE;
		return true;
	}
	if (outputType == "junit") {
		outputType_ = OUTPUT_JUNIT;
		return true;
	}
	return false;
}

bool CommandLineArguments::isEclipseOutput() const
{
	return outputType_ == OUTPUT_ECLIPSE;
}

bool CommandLineArguments::isJUnitOutput() const
{
	return outputType_ == OUTPUT_JUNIT;
}

