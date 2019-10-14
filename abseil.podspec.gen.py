#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import os
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from collections import namedtuple

SPEC_TEMPLATE = """Pod::Spec.new do |s|
  s.name     = 'abseil'
  s.version  = '${version}'
  s.summary  = 'Abseil Common Libraries (C++) from Google'
  s.homepage = 'https://abseil.io'
  s.license  = 'Apache License, Version 2.0'
  s.authors  = { 'Abseil' => 'abseil-io@googlegroups.com' }
  s.source = {
    :git => 'https://github.com/abseil/abseil-cpp.git',
    :tag => '${tag}',
  }
  s.ios.deployment_target = '7.0'
  s.osx.deployment_target = '10.9'
  s.tvos.deployment_target = '10.0'
  s.watchos.deployment_target = '4.0'
"""

# Rule object representing the rule of Bazel BUILD.
Rule = namedtuple("Rule",
                  "type name package srcs hdrs deps visibility testonly")


def get_elem_value(elem, name):
    for child in elem:
        if child.attrib.get("name") == name:
            if child.tag == "string":
                return child.attrib.get("value")
            elif child.tag == "boolean":
                return child.attrib.get("value") == "true"
            elif child.tag == "list":
                return [
                    nested_child.attrib.get("value") for nested_child in child
                ]
            else:
                raise "Cannot recognize tag: " + child.tag
    return None


def normalize_paths(paths):
    return [path.lstrip("/").replace(":", "/") for path in paths]


def parse_rule(elem, package):
    return Rule(type=elem.attrib["class"],
                name=get_elem_value(elem, "name"),
                package=package,
                srcs=normalize_paths(get_elem_value(elem, "srcs") or []),
                hdrs=normalize_paths(get_elem_value(elem, "hdrs") or []),
                deps=get_elem_value(elem, "deps") or [],
                visibility=get_elem_value(elem, "visibility") or [],
                testonly=get_elem_value(elem, "testonly") or False)


def read_build(package):
    result = subprocess.check_output(
        ["bazel", "query", package + ":all", "--output", "xml"])
    root = ET.fromstring(result)
    return [
        parse_rule(elem, package) for elem in root
        if elem.tag == "rule" and elem.attrib["class"].startswith("cc_")
    ]


def collect_rules(root_path):
    rules = []
    for cur, _, _ in os.walk(root_path):
        build_path = os.path.join(cur, "BUILD.bazel")
        if os.path.exists(build_path):
            rules.extend(read_build("//" + cur))
    return rules


def get_spec_var(depth):
    return "s" if depth == 0 else "s{}".format(depth)


def get_spec_name(label):
    assert label.startswith("//absl/"), "{} doesn't start with //absl/".format(
        label)
    return "abseil/" + label[7:]


def write_podspec_map(f, cur_map, depth):
    for key, value in sorted(cur_map.items()):
        indent = "  " * (depth + 1)
        f.write("{indent}{var0}.subspec '{key}' do |{var1}|\n".format(
            indent=indent,
            key=key,
            var0=get_spec_var(depth),
            var1=get_spec_var(depth + 1)))
        if isinstance(value, dict):
            write_podspec_map(f, value, depth + 1)
        else:
            write_podspec_rule(f, value, depth + 1)
        f.write("{indent}end\n".format(indent=indent))


def write_list(f, leading, values):
    f.write("{}'{}'\n".format(leading, values[0]))
    for value in values[1:]:
        f.write("{}'{}'\n".format(" " * len(leading), value))


def write_podspec_rule(f, rule, depth):
    indent = "  " * (depth + 1)
    spec_var = get_spec_var(depth)
    if rule.hdrs:
        write_list(
            f, "{indent}{var}.public_header_files = ".format(
                indent=indent,
                var=spec_var,
            ), sorted(rule.hdrs))
    if rule.srcs:
        write_list(
            f, "{indent}{var}.source_files = ".format(indent=indent,
                                                      var=spec_var),
            sorted(rule.srcs))
    for dep in sorted(rule.deps):
        name = get_spec_name(dep.replace(":", "/"))
        f.write("{indent}{var}.dependency '{dep}'\n".format(indent=indent,
                                                            var=spec_var,
                                                            dep=name))


def build_rule_directory(rules):
    rule_dir = {}
    for rule in rules:
        cur = rule_dir
        for frag in get_spec_name(rule.package).split("/"):
            cur = cur.setdefault(frag, {})
        cur[rule.name] = rule
    return rule_dir


def write_podspec(f, rules, args):
    rule_dir = build_rule_directory(rules)["abseil"]
    spec = re.sub(r"\$\{(\w+)\}", lambda x: args[x.group(1)], SPEC_TEMPLATE)
    f.write(spec)
    write_podspec_map(f, rule_dir, 0)
    f.write("end\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generates abseil.podspec from BUILD.bazel")
    parser.add_argument("-v",
                        "--version",
                        help="The version of podspec",
                        required=True)
    parser.add_argument("-t",
                        "--tag",
                        default=None,
                        help="The name of git tag (default: version)")
    args = parser.parse_args()
    if args.tag is None:
        args.tag = args.version
    rules = filter(lambda r: r.type == "cc_library" and not r.testonly,
                   collect_rules("absl"))
    with open("abseil.podspec", "wt") as f:
        write_podspec(f, rules, vars(parser.parse_args()))


if __name__ == "__main__":
    main()
