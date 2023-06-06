# Please run this script at the shell directory in build

import yaml
import argparse
import subprocess
import os
import re
import time

parser = argparse.ArgumentParser(description='Fuzz testing')
parser.add_argument("file_name", type=str, nargs='*', help="Unit test file name")
parser.add_argument("--group", type=str, nargs='?', help="Group name")
parser.add_argument("--test", type=str, nargs='?', help="Test name")
parser.add_argument("-n", type=int, nargs='?', help="Number of times a single variable is mutated and tested")
parser.add_argument('--all', action='store_true')
parser.add_argument("--coverage", action='store_true', help="Generate Coverage report using lcov")
args = parser.parse_args()

all_tests = [
            'acl_auto_configure_fuzz_test',
            ]

# First level: unit test file name => table of attributes
# Second level: attribute name => value
# Attributes: 
# - Total runs
# - Successful runs
# - Failed runs
# - ASAN errors
# - Hangs
# - Test errors
results_dictionary = dict()

TOTAL_RUNS = "Total runs"
SUCCESSFUL_RUNS = "Successful runs"
FAILED_RUNS = "Failed runs"
ABORTED_RUNS = "Aborted runs"
ASSERTION_FAILURES = "Assertion failures"
ASAN_ERRORS = "ASAN errors"
HANGS = "Hangs"
TEST_ERRORS = "Test errors"

SUCCESS_TEST_PATTERN = "OK .* tests, .* ran, .* checks, .* ignored, .* filtered out, .* ms"
FAILED_TEST_PATTERN = "Errors .* tests, .* ran, .* checks, .* ignored, .* filtered out, .* ms"
ASAN_ERROR_PATTERN = "SUMMARY: AddressSanitizer"
FUZZ_TEST_ERROR_PATTERN = "Fuzz test error"
ASSERTION_FAILURE_PATTERN = "acl_fuzz_test:.*Assertion"

# Give it 60 seconds to finish a single unit test
TIMEOUT = 60
TIMEOUT_COMMAND = "timeout " + str(TIMEOUT)
TIMEOUT_MESSAGE = "TIMEOUT! " + str(TIMEOUT) + " seconds have passed! "

if args.all and args.file_name != []:
    print("You should not have file_name arguments when using --all flag")
    exit(1)

def initialize_attribute_table():
    return {
        TOTAL_RUNS: 0,
        SUCCESSFUL_RUNS: 0,
        FAILED_RUNS: 0,
        ABORTED_RUNS : 0,
        ASSERTION_FAILURES: 0,
        ASAN_ERRORS: 0,
        HANGS: 0,
        TEST_ERRORS: 0,
    }

def parse_output(output):
    # Errors (1 failures, 230 tests, 1 ran, 5 checks, 0 ignored, 229 filtered out, 3 ms)
    return

def encode_if_condition(pattern, condition):
    if condition:
        return pattern.encode()
    return pattern

def fuzz_test_main(test_file_name, group, test, var, original_value, all_outputs):
    # Initialize results
    if test_file_name not in results_dictionary:
        results_dictionary[test_file_name] = initialize_attribute_table()

    # Mutate variable
    fuzz_var_str = "Fuzzing variable: " + group + "--" + test + "--" + var
    print("      " + fuzz_var_str)
    mutation_command = ["python", "mutator.py", test_file_name, group, test, var]
    # mutated_value = subprocess.check_output(mutation_command) 
    original_value_message = "Original value: \n" + original_value
    subprocess.check_output(mutation_command) 
    print("      Mutation finished! ")
    with open("temp.txt", "rb") as file:
        mutated_value = file.read()
        mutated_value_message = "Mutated value: "
    os.chdir("../test")
    # Run test with timeout
    start = time.time()
    end = start
    time_taken = end - start
    run_test_command = TIMEOUT_COMMAND + " ./acl_fuzz_test -v -g " + group + " -n " + test + " "
    print("      Test command: " + run_test_command)
    # Save output
    try:
        output = subprocess.check_output(run_test_command.split(), stderr=subprocess.STDOUT)
    except Exception as e:
        output = str(e.output)
    end = time.time()
    time_taken = end - start
    os.chdir("../script")
    print("      Test finished! ")
    # print(output)
    all_outputs.append(fuzz_var_str)
    all_outputs.append(original_value_message)
    all_outputs.append(mutated_value_message)
    all_outputs.append(mutated_value)
    all_outputs.append("\n")
    encoded = type(output) != str
    all_outputs.append(output)
    all_outputs.append("\n")
    timeout = False
    if time_taken >= TIMEOUT:
        timeout = True
        all_outputs.append(TIMEOUT_MESSAGE)
    
    # Update results
    # Total run
    results_dictionary[test_file_name][TOTAL_RUNS] = results_dictionary[test_file_name][TOTAL_RUNS] + 1
    # If successful test message found
    if re.search(encode_if_condition(SUCCESS_TEST_PATTERN, encoded), output):
        # If failed test message found
        if re.search(encode_if_condition(FAILED_TEST_PATTERN, encoded), output):
            # Test error + 1
            results_dictionary[test_file_name][TEST_ERRORS] = results_dictionary[test_file_name][TEST_ERRORS] + 1
            test_error = True
        # If failed test message not found
        else:
            # Successful test + 1
            results_dictionary[test_file_name][SUCCESSFUL_RUNS] = results_dictionary[test_file_name][SUCCESSFUL_RUNS] + 1
    # If successful test message not found
    else:
        # If failed test message found 
        if re.search(encode_if_condition(FAILED_TEST_PATTERN, encoded), output):
            # Failed test + 1 
            results_dictionary[test_file_name][FAILED_RUNS] = results_dictionary[test_file_name][FAILED_RUNS] + 1
        # If failed test message not found
        else:
            # If timeout
            if timeout:
                # Hang + 1
                results_dictionary[test_file_name][HANGS] = results_dictionary[test_file_name][HANGS] + 1
            # If not timeout
            else:
                # If assertion message found
                if re.search(encode_if_condition(ASSERTION_FAILURE_PATTERN, encoded), output):
                    # Assertion failures + 1
                    results_dictionary[test_file_name][ASSERTION_FAILURES] = results_dictionary[test_file_name][ASSERTION_FAILURES] + 1
                # If assertion message not found
                else:
                    # Aborted run + 1
                    results_dictionary[test_file_name][ABORTED_RUNS] = results_dictionary[test_file_name][ABORTED_RUNS] + 1
    # ASAN errors
    if re.search(encode_if_condition(ASAN_ERROR_PATTERN, encoded), output):
        results_dictionary[test_file_name][ASAN_ERRORS] = results_dictionary[test_file_name][ASAN_ERRORS] + 1
    # Test errors
    if re.search(encode_if_condition(FUZZ_TEST_ERROR_PATTERN, encoded), output) and not test_error:
        results_dictionary[test_file_name][TEST_ERRORS] = results_dictionary[test_file_name][TEST_ERRORS] + 1

def load_yaml(test_file_name):
    # Fetch data from original_inputs
    original_file_path = "../original_inputs/" + test_file_name + ".yml"
    with open(original_file_path, 'r') as file:
        inputs = yaml.safe_load(file)
    return inputs

def store_outputs(test_file_name, outputs):
    # Print outputs to test_outputs
    test_outputs_path = "../test_outputs"
    test_outputs_file_path = test_outputs_path + "/" + test_file_name + ".txt"

    # Output to test_outputs directory
    if not os.path.exists(test_outputs_path):
        os.makedirs(test_outputs_path)
    with open(test_outputs_file_path, 'w') as file:
        for out in outputs:
            if type(out) != str:
                file.write(out.decode("utf-8", errors='replace'))
            else:
                file.write(out)
            file.write("\n")

def fuzz_test_iterations(test_file_name, group, test, var, original_value, all_outputs, iterations=1, indents=""):
    for i in range(iterations):
        print(indents + "Iteration: " + str(i+1) + " / " + str(iterations))
        fuzz_test_main(test_file_name, group, test, var, original_value, all_outputs)

def fuzz_test(test_file_name, iterations=1):
    inputs = load_yaml(test_file_name)

    # Save all output strings
    all_outputs = []

    print("Running Fuzz tests for " + test_file_name)

    group_total = len(inputs)
    group_count = 1

    for group in inputs:
        print("Group: {} ({} / {})".format(group, group_count, group_total))
        test_total = len(inputs[group])
        test_count = 1
        for test in inputs[group]:
            print("  Test: {} ({} / {})".format(test, test_count, test_total))
            for var in inputs[group][test]:
                fuzz_test_iterations(test_file_name, group, test, var, inputs[group][test][var], all_outputs, iterations, "   ")
            test_count += 1
        group_count += 1

    store_outputs(test_file_name, all_outputs)

def fuzz_test_single(test_file_name, group, test, iterations=1):
    all_outputs = []
    inputs = load_yaml(test_file_name)
    for var in inputs[group][test]:
        fuzz_test_iterations(test_file_name, group, test, var, inputs[group][test][var], all_outputs, iterations, "")
    store_outputs(test_file_name, all_outputs)

def generate_coverage_report():
    print("Generating Coverage Report...")
    os.chdir("../test")
    clean_command = "rm -f coverage.info && rm -rf coverage_report"
    subprocess.check_output(clean_command.split())
    lcov_command = "lcov --capture --directory CMakeFiles/acl_fuzz_test.dir --output-file=coverage.info"
    subprocess.check_output(lcov_command.split())
    generate_html_command = "genhtml coverage.info --output-directory=coverage_report"
    subprocess.check_output(generate_html_command.split())
    print("Coverage Report generated at " + os.getcwd() + "/coverage_report/index.html ")
    os.chdir("../script")

def generate_results_yml():
    print("Generating results yml...")
    results_directory_path = "../results"
    if not os.path.exists(results_directory_path):
        os.makedirs(results_directory_path)
    os.chdir(results_directory_path)
    results_yml_path = "results.yml"
    with open(results_yml_path, 'w') as outfile:
        yaml.safe_dump(results_dictionary, outfile, default_flow_style=False)
    print("Results yaml file generated at " + os.getcwd() + "/" + results_yml_path)
    os.chdir("../script")

def main():
    tests = args.file_name
    iterations = 1
    if args.n:
        iterations = args.n
    if args.all:
        tests = all_tests

    # TODO: Error checking
    if args.group and args.test and len(tests) == 1:
        fuzz_test_single(tests[0], args.group, args.test, iterations)
    else:
        for test in tests:
            fuzz_test(test, iterations)
    if args.coverage:
        generate_coverage_report()
    generate_results_yml()

main()
