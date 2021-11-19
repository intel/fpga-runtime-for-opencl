#!/usr/bin/env python3
# Copyright (C) 2021 Intel Corporation
# SPDX-License-Identifier: BSD-3-Clause

# Example usage: ./coverage_diff.py child_build_dir parent_build_dir
import xml.etree.ElementTree as ET
import sys
import argparse
import glob


def want_file(fn):
    return fn.startswith("./src")

def txt_table(matrix):
    s = [[str(e) for e in row] for row in matrix]
    lens = [max(map(len, col))+1 for col in zip(*s)]
    fmt = '\t'.join('{{:{}}}'.format(x) for x in lens)
    table = [fmt.format(*row) for row in s]
    return '\n'.join(table)

def parse_coverage(child_data, parent_data):
    """
    child_data: xml file string of coverage scan result for child build
    parent_data: xml file string of coverage scan result for parent build
    output: {filename: {parent_cov: float, child_cov: float, delta_cov: float, new_file: bool}}
    """
    coverage = {}
    # Parse parent coverage
    root = ET.fromstring(parent_data)
    for elm in root.findall(".//File[@FullPath]"):
        fn = elm.attrib["FullPath"]
        if not want_file(fn):
            continue
        percent_cov = float(elm.find('PercentCoverage').text)
        if fn not in coverage:
            # When file exist in parent, the file is not new
            coverage[fn] = {'child_cov': 0, 'new_file': False}
        coverage[fn]['parent_cov'] = percent_cov
    
    # Parse child coverage
    root = ET.fromstring(child_data)
    for elm in root.findall(".//File[@FullPath]"):
        fn = elm.attrib["FullPath"]
        if not want_file(fn):
            continue
        percent_cov = float(elm.find('PercentCoverage').text)
        if fn not in coverage:
            # After parsing parent, if the file stil do not exist, then the file is new
            coverage[fn] = {'parent_cov': 0, 'new_file': True}
        coverage[fn]['child_cov'] = percent_cov
    
    for fn in coverage:
        coverage[fn]['delta_cov'] = coverage[fn]['child_cov'] - coverage[fn]['parent_cov']

    return coverage    

def parse_coverage_status(coverage):
    """Return the exit code and exit message for given coverage
    coverage: {filename: {parent_cov: float, child_cov: float, delta_cov: float, new_file: bool}}
    output: {"exit_code":int, "message":str} 
    """
    for fn in coverage:
        if coverage[fn]["new_file"] and coverage[fn]["delta_cov"] <= 20:
            # New files need to have at least 20% coverage
            return {"exit_code":1, "message":"New files need at least 20% coverage."} 
        elif coverage[fn]["delta_cov"] < 0 and abs(coverage[fn]["delta_cov"]) >= min(20, coverage[fn]["parent_cov"]):
            return {"exit_code":1, "message":"Coverage decreased severely for more than 20% or a file lost coverage."} 
        elif coverage[fn]["delta_cov"] < 0:
            return {"exit_code":0, "message":"Coverage decreased slightly for less than 20%"} 
    return {"exit_code":0, "message":"Coverage non decreasing."} 

def coverage_table(coverage):
    """Return the exit code and exit message for given coverage
    coverage: {filename: {parent_cov: float, child_cov: float, delta_cov: float, new_file: bool}}
    output: None
    """
    columns = ["File Name", "Coverage (%)", "Delta Coverage"]
    separator = ['-'*len(cn) for cn in columns]
    result = [columns, separator] + [[fn, coverage[fn]["child_cov"], coverage[fn]["delta_cov"]] for fn in coverage]
    return txt_table(result)

def main():
    parser = argparse.ArgumentParser(description='Optional app description')
    parser.add_argument('child_coverage', type=str,
                        help='Child branch build directory')
    parser.add_argument('parent_coverage', type=str,
                        help='Parent branch build directory')
    args = parser.parse_args()

    child_cov_file = glob.glob(args.child_coverage + "/Testing/**/Coverage.xml", recursive = True)[0]
    parent_cov_file = glob.glob(args.parent_coverage + "/Testing/**/Coverage.xml", recursive = True)[0]

    with open(child_cov_file, 'r') as f:
        child_data = f.read()
    with open(parent_cov_file, 'r') as f:
        parent_data = f.read()

    aggregated_coverage = parse_coverage(child_data, parent_data)
    txt_coverage_table = coverage_table(aggregated_coverage)
    exit_result = parse_coverage_status(aggregated_coverage)

    print(txt_coverage_table)
    print(exit_result["message"])
    exit(exit_result["exit_code"])


if __name__ == '__main__':
    main()