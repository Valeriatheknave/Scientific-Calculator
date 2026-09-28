# calc

A command-line scientific calculator in a single C++17 file, with no dependencies beyond the standard library. It runs as an interactive prompt, evaluates expressions passed as arguments, or reads them from a pipe.

```
$ calc
Calculator 2.0.0  -  type 'help' for instructions, 'quit' to exit.
Angle mode: degrees
> 2pi
= 6.28318530718
> r = 3
r = 3
> pi * r^2
= 28.2743338823
> sin(30)
= 0.5
```

## Build

The source needs a C++17 compiler.

```
g++ -std=c++17 -O2 -o calc Scientific_Calculator.cpp
```

It builds the same way with `clang++`. On Windows, use MSVC with `/std:c++17`, or MinGW. The code handles the `isatty` difference itself.

## Usage

```
calc [options]                   interactive mode
calc [options] EXPR [EXPR...]    evaluate each expression and exit
... | calc [options]             read one expression per line from stdin
```

| Option | Description |
|---|---|
| `-r`, `--rad` | Use radians for trigonometry (default is degrees) |
| `-p N`, `--precision N` | Significant digits to display, 1 to 17 (default 12) |
| `-t`, `--test` | Run the built-in self-tests |
| `-h`, `--help` | Show usage |
| `-v`, `--version` | Show version |
| `--` | Treat everything after it as expressions |

Anything that isn't a recognised option is treated as an expression, so `calc -5+3` works.

Quote expressions in your shell so `*`, `(`, `)` and `!` reach the program intact:

```
$ calc "2 + 2" "sqrt(16)" "5!"
4
4
120
```

Interactive mode starts only when there are no expression arguments and stdin is a terminal. Otherwise results are printed bare, without the `= ` prefix.

## Syntax

### Operators

From highest to lowest precedence:

| Operator | Meaning | Example |
|---|---|---|
| `( )` | Grouping | |
| `!` | Factorial (postfix) | `5!` = 120 |
| `^`, `**` | Power, right-associative | `2^3^2` = 512 |
| `+` `-` (unary) | Sign | `-2^2` = -4 |
| `*` `/` `%` | Multiply, divide, remainder | `10 % 3` = 1 |
| `+` `-` | Add, subtract | |

Because unary minus binds looser than `^`, `-2^2` is `-(2^2)`. Likewise `-3!` is `-(3!)`.

### Implicit multiplication

A number, name or closing parenthesis followed directly by a name or opening parenthesis is multiplied: `2pi`, `3(4+1)`, `(1+2)(3+4)`, `2sin(30)`.

Two things to watch:

- It sits at the same level as `*` and `/`, evaluated left to right. `1/2pi` is `(1/2)*pi`, not `1/(2*pi)`.
- A number followed by a number is an error (`2 3`), and `2e` means `2 * e`, while `2e3` is scientific notation and gives 2000.

### Functions

| Group | Functions |
|---|---|
| Trigonometry | `sin` `cos` `tan` `cot` |
| Inverse trig | `asin` `acos` `atan` `atan2(y, x)` |
| Hyperbolic | `sinh` `cosh` `tanh` |
| Roots and powers | `sqrt` `cbrt` `exp` `hypot(a, b)` |
| Logarithms | `ln` `log2` `log(x)` (base 10) `log(x, base)` |
| Rounding | `abs` `sign` `floor` `ceil` `round` `trunc` |
| Aggregates | `min(a, b, ...)` `max(a, b, ...)` |
| Conversion | `deg(x)` converts radians to degrees, `rad(x)` converts degrees to radians |

`sin`, `cos`, `tan` and `cot` take their argument in the current angle mode. `asin`, `acos`, `atan` and `atan2` return their result in it. `deg()` and `rad()` always convert, regardless of mode.

### Constants

`pi`, `tau` (2π), `e`, `phi` (golden ratio)

### Variables

```
> x = 4
x = 4
> x^2 + 1
= 17
> ans + 1
= 18
```

`ans` holds the result of the last successful calculation and is updated automatically. It can't be assigned to. Names are case-insensitive and may contain letters, digits and underscores. You can't reuse the name of a constant, a function or a command.

## Interactive commands

| Command | Action |
|---|---|
| `help` | Show the syntax reference |
| `history` | Show past calculations (last 500 kept) |
| `clear` | Clear the history |
| `vars` | List variables and constants |
| `deg`, `rad` | Switch angle mode |
| `mode` | Show the current angle mode |
| `precision [n]` | Show or set displayed significant digits (1 to 17) |
| `quit`, `exit` | Leave |

Blank lines and lines starting with `#` are ignored, which is handy for piped scripts. These commands are recognised in piped input as well as at the prompt.

## Error handling

Errors point at the offending position:

```
> 2 +
     ^
Error: Unexpected end of expression
```

The calculator reports an error rather than returning `inf` or `nan` for: division or modulo by zero, `0` to a negative power, a negative base with a fractional exponent, `sqrt` of a negative number, logs of non-positive numbers, `tan(90)` and other undefined trig values, out-of-range `asin`/`acos`, factorial of a non-integer or negative number, factorial above 170, results that overflow a double, and nesting deeper than 256 levels.

In non-interactive use, error messages go to stderr.

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Success |
| 1 | At least one expression failed to evaluate (argument or piped mode), or a self-test failed |
| 2 | Bad command-line option (missing or invalid `--precision` value) |

In argument mode every expression is evaluated even if an earlier one fails. Interactive sessions always exit 0.

## Numeric behaviour

- All arithmetic is done in `double`. Output uses default C++ formatting with the chosen number of significant digits, so very large or small values print in scientific notation (`1e+11`), and trailing zeros are dropped.
- In degree mode, common angles return exact results: `sin(30)` is exactly `0.5`, `cos(90)` is exactly `0`, and `tan(45)` is exactly `1`. Angles are reduced modulo 360 (or 180 for `tan` and `cot`) first, so `cos(-360)` and `sin(720)` are also exact.
- In radian mode, results that come out as floating-point noise near zero (below 1e-15 of the argument's magnitude) are snapped to `0`. `tan` and `cot` reduce their argument modulo π first, which keeps them accurate for large arguments.
- Negative zero is printed as `0`.

## Self-tests

```
$ calc --test
```

Runs a set of built-in checks covering operator precedence, implicit multiplication, trig in both angle modes, and a list of expressions that must produce errors. It prints `passed/total` and exits 0 only if everything passed.

## How it works

Everything lives in the `calc` namespace of one source file:

- `tokenize` turns the input string into tokens (numbers, identifiers, operators, parentheses, commas, `=`), each carrying its position for error reporting.
- `Parser` is a recursive-descent parser that evaluates as it parses, so there is no syntax tree. Nesting is capped at 256 levels to protect the stack.
- `Calculator` owns the function table, constants, variables and angle mode. Functions are registered in a table with minimum and maximum argument counts.
- `Repl` handles the prompt, commands, history and output formatting, and decides between interactive and piped behaviour.

To add a function, register it in `Calculator::registerFunctions()`. To add a command, extend `Repl::handleCommand()` and add the word to the reserved list in the `Repl` constructor so it can't be used as a variable name.

## Limits

| Limit | Value |
|---|---|
| Nesting depth | 256 |
| History entries | 500 |
| Displayed precision | 1 to 17 digits |
| Largest factorial | 170! |
| Input characters | ASCII only |
