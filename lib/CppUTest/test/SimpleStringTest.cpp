/*
 * Copyright (c) 2007, Michael Feathers, James Grenning and Bas Vodde
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
#include "CppUTest/SimpleString.h"
#include "CppUTest/PlatformSpecificFunctions.h"

TEST_GROUP(SimpleString)
{

};

	TEST(SimpleString, CreateSequence)
	{
		SimpleString expected("hellohello");
		SimpleString actual("hello", 2);

		CHECK_EQUAL(expected, actual);
	}

	TEST(SimpleString, CreateSequenceOfZero)
	{
		SimpleString expected("");
		SimpleString actual("hello", 0);

		CHECK_EQUAL(expected, actual);
	}

	TEST(SimpleString, Copy)
	{
		SimpleString s1("hello");
		SimpleString s2(s1);

		CHECK_EQUAL(s1, s2);
	}

	TEST(SimpleString, Assignment)
	{
		SimpleString s1("hello");
		SimpleString s2("goodbye");

		s2 = s1;

		CHECK_EQUAL(s1, s2);
	}

	TEST(SimpleString, Equality)
	{
		SimpleString s1("hello");
		SimpleString s2("hello");

		CHECK(s1 == s2);
	}

	TEST(SimpleString, InEquality)
	{
		SimpleString s1("hello");
		SimpleString s2("goodbye");

		CHECK(s1 != s2);
	}

	TEST(SimpleString, asCharString)
	{
		SimpleString s1("hello");

		STRCMP_EQUAL("hello", s1.asCharString());
	}

	TEST(SimpleString, Size)
	{
		SimpleString s1("hello!");

		LONGS_EQUAL(6, s1.size());
	}

	TEST(SimpleString, Addition)
	{
		SimpleString s1("hello!");
		SimpleString s2("goodbye!");
		SimpleString s3("hello!goodbye!");
		SimpleString s4;
		s4 = s1 + s2;

		CHECK_EQUAL(s3, s4);
	}

	TEST(SimpleString, Concatenation)
	{
		SimpleString s1("hello!");
		SimpleString s2("goodbye!");
		SimpleString s3("hello!goodbye!");
		SimpleString s4;
		s4 += s1;
		s4 += s2;

		CHECK_EQUAL(s3, s4);

		SimpleString s5("hello!goodbye!hello!goodbye!");
		s4 += s4;

		CHECK_EQUAL(s5, s4);
	}

	TEST(SimpleString, Contains)
	{
		SimpleString s("hello!");
		SimpleString empty("");
		SimpleString beginning("hello");
		SimpleString end("lo!");
		SimpleString mid("l");
		SimpleString notPartOfString("xxxx");

		CHECK(s.contains(empty));
		CHECK(s.contains(beginning));
		CHECK(s.contains(end));
		CHECK(s.contains(mid));
		CHECK(!s.contains(notPartOfString));

		CHECK(empty.contains(empty));
		CHECK(!empty.contains(s));
	}

	TEST(SimpleString, startsWith)
	{
		SimpleString hi("Hi you!");
		SimpleString part("Hi");
		SimpleString diff("Hrrm Hi you! ffdsfd");
		CHECK(hi.startsWith(part));
		CHECK(!part.startsWith(hi));
		CHECK(!diff.startsWith(hi));
	}

	TEST(SimpleString, split)
	{
		SimpleString hi("hello\nworld\nhow\ndo\nyou\ndo\n\n");

		SimpleStringCollection collection;
		hi.split("\n", collection);

		LONGS_EQUAL(7, collection.size());
		STRCMP_EQUAL("hello\n", collection[0].asCharString());
		STRCMP_EQUAL("world\n", collection[1].asCharString());
		STRCMP_EQUAL("how\n", collection[2].asCharString());
		STRCMP_EQUAL("do\n", collection[3].asCharString());
		STRCMP_EQUAL("you\n", collection[4].asCharString());
		STRCMP_EQUAL("do\n", collection[5].asCharString());
		STRCMP_EQUAL("\n", collection[6].asCharString());
	}

	TEST(SimpleString, splitNoTokenOnTheEnd)
	{
		SimpleString string("Bah Yah oops");
		SimpleStringCollection collection;

		string.split(" ", collection);
		LONGS_EQUAL(3, collection.size());
		STRCMP_EQUAL("Bah ", collection[0].asCharString());
		STRCMP_EQUAL("Yah ", collection[1].asCharString());
		STRCMP_EQUAL("oops", collection[2].asCharString());
	}

	TEST(SimpleString, count)
	{
		SimpleString str("ha ha ha ha");
		LONGS_EQUAL(4, str.count("ha"));
	}

	TEST(SimpleString, countTogether)
	{
		SimpleString str("hahahaha");
		LONGS_EQUAL(4, str.count("ha"));
	}

	TEST(SimpleString, endsWith)
	{
		SimpleString str("Hello World");
		CHECK(str.endsWith("World"));
		CHECK(!str.endsWith("Worl"));
		CHECK(!str.endsWith("Hello"));
		SimpleString str2("ah");
		CHECK(str2.endsWith("ah"));
		CHECK(!str2.endsWith("baah"));
		SimpleString str3("");
		CHECK(!str3.endsWith("baah"));

		SimpleString str4("ha ha ha ha");
		CHECK(str4.endsWith("ha"));
	}

	TEST(SimpleString, replaceCharWithChar)
	{
		SimpleString str("abcabcabca");
		str.replace('a', 'b');
		STRCMP_EQUAL("bbcbbcbbcb", str.asCharString());
	}

	TEST(SimpleString, replaceStringWithString)
	{
		SimpleString str("boo baa boo baa boo");
		str.replace("boo", "boohoo");
		STRCMP_EQUAL("boohoo baa boohoo baa boohoo", str.asCharString());
	}

	TEST(SimpleString, ContainsNull)
	{
		SimpleString s(0);
		CHECK(!s.contains("something"));
	}

	TEST(SimpleString, Characters)
	{
		SimpleString s(StringFrom('a'));
		SimpleString s2(StringFrom('a'));
		CHECK(s == s2);
	}

	TEST(SimpleString, Doubles)
	{
		SimpleString s(StringFrom(1.2));
		STRCMP_EQUAL("1.200000", s.asCharString());
		s = StringFrom(1.2, 2);
		STRCMP_EQUAL("1.20", s.asCharString());
	}

	TEST(SimpleString, HexStrings)
	{
		SimpleString h1 = HexStringFrom(0xffff);
		STRCMP_EQUAL("ffff", h1.asCharString());
	}

	TEST(SimpleString, StringFromFormat)
	{
		SimpleString h1 = StringFromFormat("%s %s! %d", "Hello", "World", 2009);
		STRCMP_EQUAL("Hello World! 2009", h1.asCharString());
	}

	TEST(SimpleString, StringFromFormatLarge)
	{
		const char* s =
				"ThisIsAPrettyLargeStringAndIfWeAddThisManyTimesToABufferItWillbeFull";
		SimpleString h1 = StringFromFormat("%s%s%s%s%s%s%s%s%s%s", s, s, s, s,
				s, s, s, s, s, s);
		LONGS_EQUAL(10, h1.count(s));
	}

	static int WrappedUpVSNPrintf(char* buf, int n, const char* format, ...)
	{
		va_list arguments;
		va_start(arguments, format);

		int result = PlatformSpecificVSNprintf(buf, n, format, arguments);
		va_end(arguments);
		return result;
	}

	TEST(SimpleString, PlatformSpecificSprintf_fits)
	{
		char buf[10];

		int count = WrappedUpVSNPrintf(buf, sizeof(buf), "%s", "12345");
		STRCMP_EQUAL("12345", buf);
		LONGS_EQUAL(5, count);
	}

	TEST(SimpleString, PlatformSpecificSprintf_doesNotFit)
	{
		char buf[10];

		int count = WrappedUpVSNPrintf(buf, sizeof(buf), "%s", "12345678901");
		STRCMP_EQUAL("123456789", buf);
		LONGS_EQUAL(11, count);
	}

	TEST(SimpleString, NullParameters2)
	{
		SimpleString* arr = new SimpleString[100];
		delete[] arr;
	}

	TEST(SimpleString, CollectionMultipleAllocateNoLeaksMemory)
	{
		SimpleStringCollection col;
		col.allocate(5);
		col.allocate(5);
		// CHECK no memory leak
	}

	TEST(SimpleString, CollectionReadOutOfBoundsReturnsEmptyString)
	{
		SimpleStringCollection col;
		col.allocate(3);
		STRCMP_EQUAL("", col[3].asCharString());
	}

	TEST(SimpleString, CollectionWritingToEmptyString)
	{
		SimpleStringCollection col;
		col.allocate(3);
		col[3] = SimpleString("HAH");
		STRCMP_EQUAL("", col[3].asCharString());
	}
