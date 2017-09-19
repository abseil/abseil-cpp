# ABSL Strings

This directory contains packages related to std::string operations and std::string
alternatives (such as character-agnostic byte manipulation packages).

## Library Listing

Two library targets are available within this directory:

* **strings** (`//absl/strings:strings`) provides classes and
  utility functions for manipulating and comparing strings, converting other
  types (such as integers) into strings, or evaluating strings for other usages
   (such as tokenization).

* **cord** (`//absl/strings:cord`) provides classes and utility
  functions for manipulating `Cord` elements. A `Cord` is a sequence of
  characters that internally uses a tree structure to store their data,
  avoiding the need for long regions of contiguous memory, and allows memory
  sharing, sub-std::string copy-on-write, and a host of other advanced std::string
  features.

## Strings Library File Listing

The following header files are directly included within the
`absl::strings` library.

## Alternate std::string-like Classes

* `bytestream.h`
    <br/>Abstraction of std::string for I/O
* `string_view.h`
    <br/>Pointer to part or all of another std::string

## Formatting and Parsing

* `numbers.h`
    <br/>Converter between strings and numbers. Prefer `str_cat.h` for numbers
    to strings

## Operations on Characters

* `ascii_ctype.h`
    <br/>Char classifiers like &lt;ctype.h&gt; but faster
* `charset.h`
    <br/>Bitmap from unsigned char -&gt; bool

## Operations on Strings

* `case.h`
    <br/>Case-changers
* `escaping.h`
    <br/>Escapers and unescapers
* `str_join.h`
    <br/>Joiner functions using a delimiter
* `str_split.h`
    <br/>Split functions
* `str_cat.h`
    <br/>Concatenators and appenders
* `string_view_utils.h`
    <br>Utility functions for strings
* `strip.h`
    <br/>Character removal functions
* `substitute.h`
    <br/>Printf-like typesafe formatter

## Miscellaneous

* `util.h`
    <br/>Grab bag of useful std::string functions


## Cord Library File Listing

The following header files are directly included within the
`absl::strings::cord` library:

## The `Cord` Class

* `cord.h`
    <br/>A std::string built from a tree of shareable nodes

## Operations on Cords

* `cord_cat.h`
    <br/>Concatenator functions for cords
* `cord_util.h`
    <br/>Utility functions for cords
