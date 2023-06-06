/*
  This header file contains all the functions that are needed for fuzz testing
*/

#ifndef FUZZ_TESTING_H
#define FUZZ_TESTING_H
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std;

#define YML ".yml"
#define TAB "  "
#define TAB_LENGTH_FOR_VAR 4

#define FUZZ_TEST_ERROR "Fuzz test error: "

// groupName-testname-varName => data
inline unordered_map<string, string> data_map;

// Basically find terminating '"' that concludes the variable value
inline bool parseVariableValue(ifstream &inputFile, string &returnVal) {
  string line;
  while (getline(inputFile, line)) {
    returnVal = returnVal + "\n" + line;
    // Terminating '"' found
    if (line[line.length() - 1] == '"') {
      // Remove double quotes
      returnVal = returnVal.substr(1, returnVal.length() - 2);
      return true;
    }
  }
  // EOF
  return false;
}

// Generate a string that combines the groupName testName and varName (i.e.
// groupName-testname-varName)
inline string generate_key(const vector<string> &names) {
  if (names.size() != 3) {
    cout << FUZZ_TEST_ERROR << "Incorrect key size" << endl;
    exit(1);
  }
  return names[0] + "--" + names[1] + "--" + names[2];
}

// A utility function that counts how many "TAB"s are in the current line
// A line should only have at most 2 tabs in the current schema
inline int countTabs(const string &line) {
  int count = 0;
  for (unsigned int i = 0; i < line.size(); i++) {
    if (line[i] == ' ') {
      count++;
    } else {
      if (count % 2 != 0) {
        cout << FUZZ_TEST_ERROR << "Yaml does not have correct indentation "
             << endl;
        exit(1);
      }
      int indent = count / 2;
      if (indent > 2) {
        cout << FUZZ_TEST_ERROR << "Yaml has too much indentation " << endl;
        exit(1);
      }
      return indent;
    }
  }
  // A line of spaces, yaml error
  cout << FUZZ_TEST_ERROR << "Yaml should not have an empty line " << endl;
  exit(1);
}

// A function that loads fuzzed data from a input yaml file to data_map
inline void preload(ifstream &inputFile) {
  string line;
  vector<string> names;
  // 0 => group, 1 => test, 2 => variable
  int lastIndent = 0;
  bool first = true;

  while (getline(inputFile, line)) {
    // Parsing yaml file manually
    int currentIndent = countTabs(line);
    if (currentIndent - 1 > lastIndent) {
      cout << FUZZ_TEST_ERROR << "Yaml does not have correct indentation "
           << endl;
      exit(1);
    }
    int diff = lastIndent - currentIndent;
    // Delete leaf if exiting
    if (diff >= 0 && !first) {
      names.resize(names.size() - diff - 1);
    }
    stringstream ss_line(line);
    string curr_name;
    ss_line >> curr_name;
    // If current indent level is at group/test
    if (currentIndent <= 1) {
      // Remove : at the end and add them to names
      names.push_back(curr_name.substr(0, curr_name.length() - 1));
    } else {
      // Else the current indent level must be at variable
      stringstream ss(line);
      string varName;
      string varValue;
      // Note: varName contains ':'
      ss >> varName;
      names.push_back(varName.substr(0, varName.length() - 1));
      // Note that varValue contains ""
      getline(ss, varValue);
      // Bypass leading space
      varValue = varValue.substr(1, varValue.size() - 1);
      string key = generate_key(names);
      if (varValue[varValue.length() - 1] == '"') {
        // Filter out double quote characters
        data_map.insert(pair<string, string>(
            key, varValue.substr(1, varValue.length() - 2)));
      } else {
        // The variable value contains newline character
        // Needs to parse multiple lines until finding the terminating '"'
        if (parseVariableValue(inputFile, varValue)) {
          data_map.insert(pair<string, string>(key, varValue));
        }
        // EOF reached but still couldn't find terminating "
        else {
          cout << FUZZ_TEST_ERROR
               << "EOF reached but still couldn't find terminating \"" << endl;
          exit(1);
        }
      }
    }
    lastIndent = currentIndent;
    first = false;
  }
}

// A top level function that preloads all fuzzed data into the data_map
inline void preload_data(string fileName) {
  cout << "\nPreloading fuzzed data starts: " << endl;
  string pathPrefix = "../mutated_inputs/";
  string path = pathPrefix + fileName + YML;
  cout << "Opening input file: " << path << endl;
  ifstream inputFile(path.c_str());
  string value;
  if (inputFile.is_open()) {
    preload(inputFile);
    cout << "Preloading fuzzed data ends " << endl;
  } else {
    cout << FUZZ_TEST_ERROR << "Unable to open fuzz test file! " << endl;
    cout << "Make sure you run the fuzz test in the test directory" << endl;
    exit(1);
  }
}

// Parameters: group name, test name, variable name
inline string load_fuzzed_value(string groupName, string testName,
                                string varName) {
  vector<string> names = {groupName, testName, varName};
  string key = generate_key(names);
  if (data_map.find(key) == data_map.end()) {
    cout << FUZZ_TEST_ERROR << "Unable to find " << key << endl;
    exit(1);
  }
  return data_map[key];
}

// Cast data to corresponding data types
template <class castType>
inline castType load_fuzzed_value_cast(string groupName, string testName,
                                       string varName) {
  try {
    // Convert to unsigned long long then convert to castType
    string value = load_fuzzed_value(groupName, testName, varName);
    int base = 10;
    if (value.size() > 2 && value.substr(0, 2) == "0x") {
      base = 16;
    }
    return (castType)stoull(value, 0, base);
  } catch (string e) {
    cout << FUZZ_TEST_ERROR << "Data type mismatch " << endl;
    cout << e << endl;
    exit(1);
  }
}

// Utility functions
inline bool check_condition(bool condition, bool &check) {
  check = condition;
  return check;
}
#endif
