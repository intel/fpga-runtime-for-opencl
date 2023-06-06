import yaml
import argparse
import subprocess
import os

parser = argparse.ArgumentParser(description='Mutate single variable')
parser.add_argument("file_name", type=str, help="Unit test file name")
parser.add_argument("group_name", type=str, help="Unit test group name")
parser.add_argument("test_name", type=str, help="Specific unit test name")
parser.add_argument("variable_name", type=str, help="Variable name")
args = parser.parse_args()

# Fetch data from original_inputs
original_file_path = "../original_inputs/" + args.file_name + ".yml"

with open(original_file_path, 'r') as file:
    inputs = yaml.safe_load(file)

value = inputs[args.group_name][args.test_name][args.variable_name]

# Mutation
# Pipe the output to a txt file, we can not save this output as a variable in python3 because 
# as it might be converted to a binary
if os.path.exists("temp.txt"):
    os.remove("temp.txt")
subprocess.check_output("echo {0} | radamsa > temp.txt".format(value), shell=True)
# Remove last newline character
subprocess.check_output("perl -pi -e 'chomp if eof' temp.txt", shell=True)
mutated_key = args.group_name + "--" + args.test_name + "--" + args.variable_name

# Output to mutated_inputs
mutated_inputs_path = "../mutated_inputs"
mutated_inputs_file_path = mutated_inputs_path + "/" + args.file_name + ".yml"
if not os.path.exists(mutated_inputs_path):
    os.makedirs(mutated_inputs_path)
tab = "  " 
col = ":"
nl = "\n"
if os.path.exists(mutated_inputs_file_path):
    os.remove(mutated_inputs_file_path)
for group in inputs:
    with open(mutated_inputs_file_path, 'a+') as file:
        file.write(group + col + nl)
    for test in inputs[group]:
        with open(mutated_inputs_file_path, 'a+') as file:
            file.write(tab + test + col + nl)
        for var in inputs[group][test]:
            with open(mutated_inputs_file_path, 'a+') as file:
                file.write(tab + tab + var + col + " \"")
            # file.write(inputs[group][test][var])
            current_key = group + "--" + test + "--" + var
            if current_key == mutated_key:
                command = "cat temp.txt >> " + mutated_inputs_file_path
                subprocess.check_output(command, shell=True)
            else:
                with open(mutated_inputs_file_path, 'a+') as file:
                    file.write(inputs[group][test][var])
            with open(mutated_inputs_file_path, 'a+') as file:
                file.write("\"" + nl)
