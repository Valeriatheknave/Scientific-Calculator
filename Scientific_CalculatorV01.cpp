#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#if defined(_WIN32)
    #include <io.h>
    #define CALC_STDIN_IS_TTY() (_isatty(_fileno(stdin)) != 0)
#else
    #include <unistd.h>
    #define CALC_STDIN_IS_TTY() (isatty(fileno(stdin)) != 0)
#endif

namespace calc {

constexpr const char* kVersion = "2.0.0";
constexpr double kPi = 3.14159265358979323846;
constexpr std::size_t kNoPos = std::string::npos;
constexpr int kMaxNestingDepth = 256;     
constexpr std::size_t kMaxHistory = 500;
constexpr int kDefaultPrecision = 12;

class CalcError : public std::runtime_error {
public:
    explicit CalcError(const std::string& message, std::size_t position = kNoPos)
        : std::runtime_error(message), position_(position) {}

    std::size_t position() const noexcept { return position_; }

private:
    std::size_t position_;
};

inline bool isDigit(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }
inline bool isAlpha(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
inline bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }
inline bool isSpace(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }

std::string toLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string trim(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && isSpace(text[first])) ++first;
    while (last > first && isSpace(text[last - 1])) --last;
    return std::string(text.substr(first, last - first));
}

bool isInteger(double v) { return std::isfinite(v) && std::floor(v) == v; }

std::string formatNumber(double value, int precision) {
    if (value == 0.0) value = 0.0;  // turns -0 into +0
    std::ostringstream out;
    out << std::setprecision(precision) << value;
    return out.str();
}

enum class TokenKind { Number, Identifier, Operator, LParen, RParen, Comma, Assign, End };

struct Token {
    Token(TokenKind k, std::size_t p) : kind(k), pos(p) {}

    TokenKind kind;
    std::size_t pos;       
    double number = 0.0;   
    char op = '\0';        
    std::string text;      
};

std::vector<Token> tokenize(std::string_view src) {
    std::vector<Token> tokens;
    const std::size_t n = src.size();
    std::size_t i = 0;

    while (i < n) {
        const char c = src[i];
        const std::size_t start = i;

        if (isSpace(c)) {
            ++i;
            continue;
        }

       
        if (isDigit(c) || (c == '.' && i + 1 < n && isDigit(src[i + 1]))) {
            while (i < n && isDigit(src[i])) ++i;
            if (i < n && src[i] == '.') {
                ++i;
                while (i < n && isDigit(src[i])) ++i;
            }
            
            if (i < n && (src[i] == 'e' || src[i] == 'E')) {
                std::size_t j = i + 1;
                if (j < n && (src[j] == '+' || src[j] == '-')) ++j;
                if (j < n && isDigit(src[j])) {
                    i = j;
                    while (i < n && isDigit(src[i])) ++i;
                }
            }
            if (i < n && src[i] == '.') {
                throw CalcError("Malformed number", start);
            }

            const std::string text(src.substr(start, i - start));
            errno = 0;
            const double value = std::strtod(text.c_str(), nullptr);
            if (errno == ERANGE && std::isinf(value)) {
                throw CalcError("Number is too large", start);
            }

            Token t{TokenKind::Number, start};
            t.number = value;
            tokens.push_back(t);
            continue;
        }

        if (isAlpha(c) || c == '_') {
            while (i < n && (isAlnum(src[i]) || src[i] == '_')) ++i;
            Token t{TokenKind::Identifier, start};
            t.text = toLower(src.substr(start, i - start));
            tokens.push_back(t);
            continue;
        }

        if (c == '*' && i + 1 < n && src[i + 1] == '*') {
            Token t{TokenKind::Operator, start};
            t.op = '^';
            tokens.push_back(t);
            i += 2;
            continue;
        }

        switch (c) {
            case '+': case '-': case '*': case '/':
            case '%': case '^': case '!': {
                Token t{TokenKind::Operator, start};
                t.op = c;
                tokens.push_back(t);
                break;
            }
            case '(': tokens.push_back({TokenKind::LParen, start}); break;
            case ')': tokens.push_back({TokenKind::RParen, start}); break;
            case ',': tokens.push_back({TokenKind::Comma, start}); break;
            case '=': tokens.push_back({TokenKind::Assign, start}); break;
            default:
                if (static_cast<unsigned char>(c) < 0x80 && std::isprint(static_cast<unsigned char>(c))) {
                    throw CalcError(std::string("Unexpected character '") + c + "'", start);
                }
                throw CalcError("Unexpected non-ASCII character", start);
        }
        ++i;
    }

    tokens.push_back({TokenKind::End, n});
    return tokens;
}

enum class AngleMode { Degrees, Radians };

class Calculator {
public:
    struct Result {
        double value;
        std::string assignedTo;
    };

    Calculator() { registerFunctions(); }

    Result evaluate(std::string_view line);

    AngleMode angleMode() const { return angleMode_; }
    void setAngleMode(AngleMode mode) { angleMode_ = mode; }

    const std::map<std::string, double>& variables() const { return variables_; }
    const std::map<std::string, double>& constants() const { return constants_; }

    bool isReserved(const std::string& name) const {
        return constants_.count(name) != 0 || functions_.count(name) != 0 ||
               reservedWords_.count(name) != 0;
    }

    void reserveWord(const std::string& word) { reservedWords_.insert(word); }

private:
    friend class Parser;

    struct Function {
        std::size_t minArgs;
        std::size_t maxArgs;
        std::function<double(const std::vector<double>&)> impl;
    };

    void registerFunctions();
    double sinAngle(double x) const;
    double cosAngle(double x) const;
    double tanAngle(double x) const;
    double cotAngle(double x) const;
    double fromRadians(double r) const {
        return angleMode_ == AngleMode::Degrees ? r * 180.0 / kPi : r;
    }

    std::map<std::string, double> constants_ = {
        {"pi", kPi},
        {"tau", 2.0 * kPi},
        {"e", 2.71828182845904523536},
        {"phi", 1.61803398874989484820},
    };
    std::map<std::string, double> variables_ = {{"ans", 0.0}};
    std::unordered_map<std::string, Function> functions_;
    std::unordered_set<std::string> reservedWords_;
    AngleMode angleMode_ = AngleMode::Degrees;
};

namespace {

double reduceDegrees(double deg, double period) {
    double r = std::fmod(deg, period);
    if (r < 0) r += period;
    return r;
}

double snapRadianNoise(double input, double result) {
    const double scale = std::abs(input);
    if (scale >= 1.0 && std::abs(result) < 1e-15 * scale) return 0.0;
    return result;
}

}

double Calculator::sinAngle(double x) const {
    if (angleMode_ == AngleMode::Radians) {
        return snapRadianNoise(x, std::sin(x));
    }
    const double r = reduceDegrees(x, 360.0);
    if (r == 0.0 || r == 180.0) return 0.0;
    if (r == 90.0) return 1.0;
    if (r == 270.0) return -1.0;
    if (r == 30.0 || r == 150.0) return 0.5;
    if (r == 210.0 || r == 330.0) return -0.5;
    return std::sin(r * kPi / 180.0);
}

double Calculator::cosAngle(double x) const {
    if (angleMode_ == AngleMode::Radians) {
        return snapRadianNoise(x, std::cos(x));
    }
    return sinAngle(x + 90.0);
}

double Calculator::tanAngle(double x) const {
    if (angleMode_ == AngleMode::Radians) {
        const double r = std::remainder(x, kPi);
        const double c = std::cos(r);
        if (std::abs(c) < 1e-15) {
            throw CalcError("tan() is undefined for this angle");
        }
        return snapRadianNoise(r, std::tan(r));
    }
    const double r = reduceDegrees(x, 180.0);
    if (r == 90.0) throw CalcError("tan() is undefined for this angle");
    if (r == 0.0) return 0.0;
    if (r == 45.0) return 1.0;
    if (r == 135.0) return -1.0;
    return std::tan(r * kPi / 180.0);
}

double Calculator::cotAngle(double x) const {
    if (angleMode_ == AngleMode::Radians) {
        const double r = std::remainder(x, kPi);
        const double s = std::sin(r);
        if (std::abs(s) < 1e-15) {
            throw CalcError("cot() is undefined for this angle");
        }
        return snapRadianNoise(r, std::cos(r) / s);
    }
    const double r = reduceDegrees(x, 180.0);
    if (r == 0.0) throw CalcError("cot() is undefined for this angle");
    if (r == 90.0) return 0.0;
    if (r == 45.0) return 1.0;
    if (r == 135.0) return -1.0;
    return 1.0 / std::tan(r * kPi / 180.0);
}

void Calculator::registerFunctions() {
    using Args = const std::vector<double>&;
    auto unary = [this](std::function<double(double)> f) {
        return Function{1, 1, [f](Args a) { return f(a[0]); }};
    };

    functions_["sin"] = unary([this](double x) { return sinAngle(x); });
    functions_["cos"] = unary([this](double x) { return cosAngle(x); });
    functions_["tan"] = unary([this](double x) { return tanAngle(x); });
    functions_["cot"] = unary([this](double x) { return cotAngle(x); });

    functions_["asin"] = unary([this](double x) {
        if (x < -1.0 || x > 1.0) throw CalcError("asin() requires a value between -1 and 1");
        return fromRadians(std::asin(x));
    });
    functions_["acos"] = unary([this](double x) {
        if (x < -1.0 || x > 1.0) throw CalcError("acos() requires a value between -1 and 1");
        return fromRadians(std::acos(x));
    });
    functions_["atan"] = unary([this](double x) { return fromRadians(std::atan(x)); });
    functions_["atan2"] = Function{2, 2, [this](Args a) {
        if (a[0] == 0.0 && a[1] == 0.0) throw CalcError("atan2(0, 0) is undefined");
        return fromRadians(std::atan2(a[0], a[1]));
    }};

    functions_["sinh"] = unary([](double x) { return std::sinh(x); });
    functions_["cosh"] = unary([](double x) { return std::cosh(x); });
    functions_["tanh"] = unary([](double x) { return std::tanh(x); });

    functions_["sqrt"] = unary([](double x) {
        if (x < 0) throw CalcError("sqrt() is undefined for negative numbers");
        return std::sqrt(x);
    });
    functions_["cbrt"] = unary([](double x) { return std::cbrt(x); });
    functions_["exp"] = unary([](double x) { return std::exp(x); });
    functions_["ln"] = unary([](double x) {
        if (x <= 0) throw CalcError("ln() requires a positive number");
        return std::log(x);
    });
    functions_["log2"] = unary([](double x) {
        if (x <= 0) throw CalcError("log2() requires a positive number");
        return std::log2(x);
    });
    functions_["log"] = Function{1, 2, [](Args a) {
        if (a[0] <= 0) throw CalcError("log() requires a positive number");
        if (a.size() == 1) return std::log10(a[0]);
        if (a[1] <= 0 || a[1] == 1.0) throw CalcError("log() base must be positive and not 1");
        return std::log(a[0]) / std::log(a[1]);
    }};
    functions_["hypot"] = Function{2, 2, [](Args a) { return std::hypot(a[0], a[1]); }};

    functions_["abs"] = unary([](double x) { return std::abs(x); });
    functions_["floor"] = unary([](double x) { return std::floor(x); });
    functions_["ceil"] = unary([](double x) { return std::ceil(x); });
    functions_["round"] = unary([](double x) { return std::round(x); });
    functions_["trunc"] = unary([](double x) { return std::trunc(x); });
    functions_["sign"] = unary([](double x) { return static_cast<double>((x > 0) - (x < 0)); });
    functions_["min"] = Function{1, kNoPos, [](Args a) {
        double m = a[0];
        for (double v : a) m = std::min(m, v);
        return m;
    }};
    functions_["max"] = Function{1, kNoPos, [](Args a) {
        double m = a[0];
        for (double v : a) m = std::max(m, v);
        return m;
    }};

    functions_["deg"] = unary([](double r) { return r * 180.0 / kPi; });
    functions_["rad"] = unary([](double d) { return d * kPi / 180.0; });
}

class Parser {
public:
    Parser(const Calculator& calc, const std::vector<Token>& tokens, std::size_t start = 0)
        : calc_(calc), tokens_(tokens), index_(start) {}

    double parseAll() {
        const double value = parseExpression();
        if (peek().kind != TokenKind::End) {
            throw CalcError(describeUnexpected(peek()), peek().pos);
        }
        return value;
    }

private:
    struct DepthGuard {
        DepthGuard(int& depth, std::size_t pos) : depth_(depth) {
            if (++depth_ > kMaxNestingDepth) {
                --depth_;
                throw CalcError("Expression is nested too deeply", pos);
            }
        }
        ~DepthGuard() { --depth_; }
        DepthGuard(const DepthGuard&) = delete;
        DepthGuard& operator=(const DepthGuard&) = delete;
        int& depth_;
    };

    const Token& peek() const { return tokens_[index_]; }
    const Token& advance() { return tokens_[index_++]; }

    bool peekOperator(char op) const {
        return peek().kind == TokenKind::Operator && peek().op == op;
    }

    static std::string describeUnexpected(const Token& t) {
        switch (t.kind) {
            case TokenKind::Number:     return "Unexpected number (missing operator?)";
            case TokenKind::Identifier: return "Unexpected name '" + t.text + "'";
            case TokenKind::Operator:   return std::string("Unexpected '") + t.op + "'";
            case TokenKind::LParen:     return "Unexpected '('";
            case TokenKind::RParen:     return "Unmatched ')'";
            case TokenKind::Comma:      return "Unexpected ','";
            case TokenKind::Assign:     return "Unexpected '=' (assignment must look like: name = expression)";
            case TokenKind::End:        return "Unexpected end of expression";
        }
        return "Syntax error";
    }

    static double checkFinite(double value, std::size_t pos) {
        if (std::isnan(value)) throw CalcError("Result is undefined", pos);
        if (std::isinf(value)) throw CalcError("Result is too large", pos);
        return value;
    }

    void expect(TokenKind kind, const char* message) {
        if (peek().kind != kind) {
            if (peek().kind == TokenKind::End) throw CalcError(message, peek().pos);
            throw CalcError(describeUnexpected(peek()), peek().pos);
        }
        advance();
    }

    double parseExpression() {
        double left = parseTerm();
        while (peekOperator('+') || peekOperator('-')) {
            const Token& op = advance();
            const double right = parseTerm();
            left = checkFinite(op.op == '+' ? left + right : left - right, op.pos);
        }
        return left;
    }

    double parseTerm() {
        double left = parseUnary();
        for (;;) {
            if (peekOperator('*') || peekOperator('/') || peekOperator('%')) {
                const Token& op = advance();
                const double right = parseUnary();
                if (op.op == '*') {
                    left = checkFinite(left * right, op.pos);
                } else if (op.op == '/') {
                    if (right == 0.0) throw CalcError("Division by zero", op.pos);
                    left = checkFinite(left / right, op.pos);
                } else {
                    if (right == 0.0) throw CalcError("Modulo by zero", op.pos);
                    left = checkFinite(std::fmod(left, right), op.pos);
                }
            } else if (peek().kind == TokenKind::Identifier || peek().kind == TokenKind::LParen) {
                const std::size_t pos = peek().pos;
                const double right = parsePower();
                left = checkFinite(left * right, pos);
            } else {
                break;
            }
        }
        return left;
    }

    double parseUnary() {
        DepthGuard guard(depth_, peek().pos);
        if (peekOperator('+') || peekOperator('-')) {
            const char sign = advance().op;
            const double value = parseUnary();
            return sign == '-' ? -value : value;
        }
        return parsePower();
    }

    double parsePower() {
        const double base = parsePostfix();
        if (!peekOperator('^')) return base;

        const std::size_t pos = advance().pos;
        const double exponent = parseUnary();

        if (base == 0.0 && exponent < 0.0) {
            throw CalcError("Division by zero (0 raised to a negative power)", pos);
        }
        if (base < 0.0 && !isInteger(exponent)) {
            throw CalcError("Negative base with a fractional exponent has no real result", pos);
        }
        return checkFinite(std::pow(base, exponent), pos);
    }

    double parsePostfix() {
        double value = parsePrimary();
        while (peekOperator('!')) {
            const std::size_t pos = advance().pos;
            value = factorial(value, pos);
        }
        return value;
    }

    static double factorial(double n, std::size_t pos) {
        if (!isInteger(n) || n < 0) {
            throw CalcError("Factorial requires a non-negative integer", pos);
        }
        if (n > 170) throw CalcError("Factorial result is too large", pos);
        double result = 1.0;
        for (int i = 2; i <= static_cast<int>(n); ++i) result *= i;
        return result;
    }

    double parsePrimary() {
        const Token& t = peek();

        switch (t.kind) {
            case TokenKind::Number:
                advance();
                return t.number;

            case TokenKind::LParen: {
                advance();
                const double value = parseExpression();
                expect(TokenKind::RParen, "Missing closing parenthesis");
                return value;
            }

            case TokenKind::Identifier:
                advance();
                if (peek().kind == TokenKind::LParen) return parseCall(t);
                return lookupName(t);

            default:
                throw CalcError(describeUnexpected(t), t.pos);
        }
    }

    double lookupName(const Token& t) const {
        if (auto it = calc_.constants_.find(t.text); it != calc_.constants_.end()) {
            return it->second;
        }
        if (auto it = calc_.variables_.find(t.text); it != calc_.variables_.end()) {
            return it->second;
        }
        if (calc_.functions_.count(t.text) != 0) {
            throw CalcError("Function '" + t.text + "' needs parentheses, e.g. " + t.text + "(2)", t.pos);
        }
        throw CalcError("Unknown name '" + t.text + "'", t.pos);
    }

    double parseCall(const Token& name) {
        const auto it = calc_.functions_.find(name.text);
        if (it == calc_.functions_.end()) {
            throw CalcError("Unknown function '" + name.text + "'", name.pos);
        }
        const Calculator::Function& fn = it->second;

        advance();
        std::vector<double> args;
        if (peek().kind != TokenKind::RParen) {
            args.push_back(parseExpression());
            while (peek().kind == TokenKind::Comma) {
                advance();
                args.push_back(parseExpression());
            }
        }
        expect(TokenKind::RParen, "Missing closing parenthesis");

        if (args.size() < fn.minArgs || (fn.maxArgs != kNoPos && args.size() > fn.maxArgs)) {
            std::string expected;
            if (fn.maxArgs == kNoPos) {
                expected = "at least " + std::to_string(fn.minArgs);
            } else if (fn.minArgs == fn.maxArgs) {
                expected = std::to_string(fn.minArgs);
            } else {
                expected = std::to_string(fn.minArgs) + " or " + std::to_string(fn.maxArgs);
            }
            throw CalcError(name.text + "() expects " + expected + " argument" +
                            (expected == "1" ? "" : "s") + ", got " +
                            std::to_string(args.size()), name.pos);
        }

        double result;
        try {
            result = fn.impl(args);
        } catch (const CalcError& e) {
            if (e.position() == kNoPos) throw CalcError(e.what(), name.pos);
            throw;
        }
        return checkFinite(result, name.pos);
    }

    const Calculator& calc_;
    const std::vector<Token>& tokens_;
    std::size_t index_;
    int depth_ = 0;
};

Calculator::Result Calculator::evaluate(std::string_view line) {
    const std::vector<Token> tokens = tokenize(line);

    if (tokens.front().kind == TokenKind::End) {
        throw CalcError("Empty expression", 0);
    }


    if (tokens.size() >= 2 && tokens[0].kind == TokenKind::Identifier &&
        tokens[1].kind == TokenKind::Assign) {
        const std::string& name = tokens[0].text;
        if (name == "ans") {
            throw CalcError("'ans' is set automatically and cannot be assigned", tokens[0].pos);
        }
        if (isReserved(name)) {
            throw CalcError("'" + name + "' is a reserved name", tokens[0].pos);
        }
        const double value = Parser(*this, tokens, 2).parseAll();
        variables_[name] = value;
        variables_["ans"] = value;
        return {value, name};
    }

    const double value = Parser(*this, tokens).parseAll();
    variables_["ans"] = value;
    return {value, ""};
}

void printHelp(std::ostream& out) {
    out << R"(
Operators (highest precedence first)
    ( )            grouping
    !              factorial              5!        -> 120
    ^  **          power (right-assoc.)   2^3^2     -> 512
    + -            sign                   -2^2      -> -4
    * / %          multiply, divide, remainder
                   implicit multiply      2pi, 3(1+2), (1+2)(3+4)
    + -            add, subtract

Functions
    sin cos tan cot            trig (uses current angle mode)
    asin acos atan atan2(y,x)  inverse trig (returns current angle mode)
    sinh cosh tanh             hyperbolic
    sqrt cbrt exp hypot(a,b)   roots and powers
    ln log2 log(x) log(x,b)    logarithms (log is base 10 by default)
    abs sign floor ceil round trunc
    min(a,b,...) max(a,b,...)
    deg(x) rad(x)              convert radians->degrees, degrees->radians

Constants
    pi  tau  e  phi

Variables
    x = 5          assign a variable
    ans            the result of the last calculation

Commands
    help           show this help
    history        show calculation history
    clear          clear the history
    vars           list variables and constants
    deg | rad      switch angle mode (current mode: 'mode')
    precision [n]  show or set displayed significant digits (1-17)
    quit | exit    leave the calculator
)" << '\n';
}

class Repl {
public:
    explicit Repl(bool interactive) : interactive_(interactive) {
        for (const char* word : {"help", "history", "clear", "vars", "deg", "rad",
                                 "mode", "precision", "quit", "exit"}) {
            calc_.reserveWord(word);
        }
    }

    Calculator& calculator() { return calc_; }

    bool setPrecision(const std::string& arg) {
        try {
            std::size_t used = 0;
            const int value = std::stoi(arg, &used);
            if (used != arg.size() || value < 1 || value > 17) return false;
            precision_ = value;
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    int run(std::istream& in) {
        if (interactive_) {
            std::cout << "Calculator " << kVersion
                      << "  -  type 'help' for instructions, 'quit' to exit.\n"
                      << "Angle mode: degrees\n";
        }

        std::string line;
        for (;;) {
            if (interactive_) std::cout << kPrompt << std::flush;
            if (!std::getline(in, line)) {       
                if (interactive_) std::cout << '\n';
                break;
            }
            if (!line.empty() && line.back() == '\r') line.pop_back();  

            const std::string trimmed = trim(line);
            if (trimmed.empty() || trimmed[0] == '#') continue;         

            if (!handleCommand(trimmed)) {
                evaluateLine(line);
            }
            if (quit_) break;
        }
        return hadError_ && !interactive_ ? 1 : 0;
    }

    bool evaluateLine(const std::string& line) {
        try {
            const Calculator::Result result = calc_.evaluate(line);
            const std::string text = formatNumber(result.value, precision_);

            if (result.assignedTo.empty()) {
                std::cout << (interactive_ ? "= " : "") << text << '\n';
            } else {
                std::cout << result.assignedTo << " = " << text << '\n';
            }
            addHistory(trim(line) + " = " + text);
            return true;
        } catch (const CalcError& e) {
            reportError(line, e.what(), e.position());
        } catch (const std::exception& e) {
            reportError(line, std::string("Internal error: ") + e.what(), kNoPos);
        }
        hadError_ = true;
        return false;
    }

private:
    static constexpr const char* kPrompt = "> ";

    void reportError(const std::string& line, const std::string& message, std::size_t pos) {
        std::ostream& out = interactive_ ? std::cout : std::cerr;
        if (pos != kNoPos) {
            if (interactive_) {
                out << std::string(std::string(kPrompt).size() + pos, ' ') << "^\n";
            } else {
                out << "  " << line << '\n' << std::string(2 + pos, ' ') << "^\n";
            }
        }
        out << "Error: " << message << '\n';
    }

    void addHistory(std::string entry) {
        history_.push_back(std::move(entry));
        if (history_.size() > kMaxHistory) history_.pop_front();
    }

    bool handleCommand(const std::string& trimmed) {
        std::istringstream words(toLower(trimmed));
        std::string cmd;
        words >> cmd;
        std::string arg;
        words >> arg;
        std::string extra;
        const bool tooManyArgs = static_cast<bool>(words >> extra);

        if (cmd == "precision") {
            if (tooManyArgs) {
                std::cout << "Usage: precision [1-17]\n";
            } else if (arg.empty()) {
                std::cout << "Precision: " << precision_ << " significant digits\n";
            } else if (setPrecision(arg)) {
                std::cout << "Precision: " << precision_ << " significant digits\n";
            } else {
                std::cout << "Error: precision must be a whole number from 1 to 17\n";
            }
            return true;
        }

        if (!arg.empty()) return false;

        if (cmd == "quit" || cmd == "exit") {
            quit_ = true;
        } else if (cmd == "help") {
            printHelp(std::cout);
        } else if (cmd == "history") {
            showHistory();
        } else if (cmd == "clear") {
            history_.clear();
            std::cout << "History cleared.\n";
        } else if (cmd == "vars") {
            showVariables();
        } else if (cmd == "deg") {
            calc_.setAngleMode(AngleMode::Degrees);
            std::cout << "Angle mode: degrees\n";
        } else if (cmd == "rad") {
            calc_.setAngleMode(AngleMode::Radians);
            std::cout << "Angle mode: radians\n";
        } else if (cmd == "mode") {
            std::cout << "Angle mode: "
                      << (calc_.angleMode() == AngleMode::Degrees ? "degrees" : "radians") << '\n';
        } else {
            return false;
        }
        return true;
    }

    void showHistory() const {
        if (history_.empty()) {
            std::cout << "History is empty.\n";
            return;
        }
        const int width = static_cast<int>(std::to_string(history_.size()).size());
        std::size_t i = 1;
        for (const std::string& entry : history_) {
            std::cout << std::setw(width) << i++ << ". " << entry << '\n';
        }
    }

    void showVariables() const {
        std::cout << "Variables:\n";
        for (const auto& [name, value] : calc_.variables()) {
            std::cout << "  " << std::left << std::setw(10) << name << std::right
                      << formatNumber(value, precision_) << '\n';
        }
        std::cout << "Constants:\n";
        for (const auto& [name, value] : calc_.constants()) {
            std::cout << "  " << std::left << std::setw(10) << name << std::right
                      << formatNumber(value, precision_) << '\n';
        }
    }

    Calculator calc_;
    std::deque<std::string> history_;
    int precision_ = kDefaultPrecision;
    bool interactive_;
    bool quit_ = false;
    bool hadError_ = false;
};

int runSelfTests() {
    struct ValueCase { const char* expr; double expected; };
    const ValueCase valueCases[] = {
        {"2 + 3 * 4", 14}, {"(2 + 3) * 4", 20}, {"2^3^2", 512}, {"-2^2", -4},
        {"(-2)^2", 4}, {"2^-1", 0.5}, {"-3!", -6}, {"5!", 120}, {"0!", 1},
        {"10 % 3", 1}, {"7 / 2", 3.5}, {"2pi", 2 * kPi}, {"3(4+1)", 15},
        {"(1+2)(3+4)", 21}, {"2e", 2 * 2.71828182845904523536}, {"2e3", 2000},
        {".5 + .5", 1}, {"1/1e-11", 1e11}, {"sin(30)", 0.5}, {"sin(180)", 0},
        {"cos(90)", 0}, {"cos(-360)", 1}, {"tan(45)", 1}, {"cot(90)", 0},
        {"asin(1)", 90}, {"sqrt(144) + 5^2", 37}, {"log(1000)", 3},
        {"log(8, 2)", 3}, {"ln(e)", 1}, {"max(1, 7, 3)", 7}, {"min(4, -2)", -2},
        {"abs(-10)", 10}, {"2 ** 10", 1024}, {"x = 4", 4}, {"x^2 + 1", 17},
        {"ans + 1", 18}, {"-0", 0}, {"+-+5", -5}, {"atan2(1, 1)", 45},
    };
    const char* errorCases[] = {
        "", "2 +", "1/0", "5 % 0", "(2 + 3", "2 + 3)", "sqrt(-1)", "log(0)",
        "tan(90)", "cot(0)", "(-8)^(1/3)", "0^-1", "2.5!", "171!", "foo(1)",
        "y + 1", "sin", "sin(1, 2)", "1.2.3", "2 3", "1e999", "10^400",
        "pi = 3", "ans = 1", "2 = 3", "sin(30) $", "min()", "asin(2)",
        "log(8, 1)",
    };

    Calculator calc;
    int failures = 0;
    int total = 0;

    for (const ValueCase& c : valueCases) {
        ++total;
        try {
            const double got = calc.evaluate(c.expr).value;
            const double tolerance = 1e-12 * std::max(1.0, std::abs(c.expected));
            if (std::abs(got - c.expected) > tolerance || (c.expected == 0 && std::signbit(got) && got != 0)) {
                std::cout << "FAIL  " << c.expr << "  expected " << c.expected << ", got " << got << '\n';
                ++failures;
            }
        } catch (const std::exception& e) {
            std::cout << "FAIL  " << c.expr << "  threw: " << e.what() << '\n';
            ++failures;
        }
    }

    for (const char* expr : errorCases) {
        ++total;
        try {
            const double got = calc.evaluate(expr).value;
            std::cout << "FAIL  \"" << expr << "\"  expected an error, got " << got << '\n';
            ++failures;
        } catch (const CalcError&) {
            // expected
        } catch (const std::exception& e) {
            std::cout << "FAIL  \"" << expr << "\"  wrong exception type: " << e.what() << '\n';
            ++failures;
        }
    }

    {
        Calculator radians;
        radians.setAngleMode(AngleMode::Radians);
        const ValueCase radianCases[] = {
            {"tan(pi/4)", 1.0}, {"cot(pi/4)", 1.0},
            {"tan(1000000*pi + pi/4)", 1.0},
            {"cot(1000000*pi + pi/4)", 1.0},
        };
        for (const ValueCase& c : radianCases) {
            ++total;
            try {
                const double got = radians.evaluate(c.expr).value;
                const double tolerance = 1e-5 * std::max(1.0, std::abs(c.expected));
                if (!std::isfinite(got) || std::abs(got - c.expected) > tolerance) {
                    std::cout << "FAIL  " << c.expr << "  expected " << c.expected
                              << ", got " << got << '\n';
                    ++failures;
                }
            } catch (const std::exception& e) {
                std::cout << "FAIL  " << c.expr << "  threw: " << e.what() << '\n';
                ++failures;
            }
        }
        for (const char* expr : {"tan(pi/2)", "cot(pi)"}) {
            ++total;
            try {
                const double got = radians.evaluate(expr).value;
                std::cout << "FAIL  \"" << expr << "\" expected an error, got " << got << '\n';
                ++failures;
            } catch (const CalcError&) {
            }
        }
    }

    ++total;
    try {
        calc.evaluate(std::string(100000, '('));
        std::cout << "FAIL  deep nesting did not error\n";
        ++failures;
    } catch (const CalcError&) {
    }

    std::cout << (total - failures) << "/" << total << " tests passed\n";
    return failures == 0 ? 0 : 1;
}

}  

namespace {

void printUsage(const char* program) {
    std::cout << "Usage:\n"
              << "  " << program << " [options]                 start interactive mode\n"
              << "  " << program << " [options] EXPR [EXPR...]  evaluate expressions and exit\n"
              << "  ... | " << program << " [options]           read expressions from stdin\n"
              << "\nOptions:\n"
              << "  -r, --rad       use radians for trigonometry (default: degrees)\n"
              << "  -p, --precision N  significant digits to display (1-17)\n"
              << "  -t, --test      run built-in self-tests\n"
              << "  -h, --help      show this message\n"
              << "  -v, --version   show version\n"
              << "  --              treat everything after this as expressions\n";
}

}  

int main(int argc, char* argv[]) {
    using namespace calc;

    const char* program = argc > 0 ? argv[0] : "calc";
    bool radians = false;
    std::string precisionArg;
    std::vector<std::string> expressions;

    bool optionsDone = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (!optionsDone) {
            if (arg == "--") { optionsDone = true; continue; }
            if (arg == "-h" || arg == "--help") { printUsage(program); return 0; }
            if (arg == "-v" || arg == "--version") { std::cout << "calc " << kVersion << '\n'; return 0; }
            if (arg == "-t" || arg == "--test") return runSelfTests();
            if (arg == "-r" || arg == "--rad") { radians = true; continue; }
            if (arg == "-p" || arg == "--precision") {
                if (i + 1 >= argc) {
                    std::cerr << "Error: " << arg << " requires a value\n";
                    return 2;
                }
                precisionArg = argv[++i];
                continue;
            }
            
        }
        expressions.push_back(arg);
    }

    const bool interactive = expressions.empty() && CALC_STDIN_IS_TTY();
    Repl repl(interactive);
    if (radians) repl.calculator().setAngleMode(AngleMode::Radians);
    if (!precisionArg.empty() && !repl.setPrecision(precisionArg)) {
        std::cerr << "Error: precision must be a whole number from 1 to 17\n";
        return 2;
    }

    if (!expressions.empty()) {
        bool ok = true;
        for (const std::string& expr : expressions) {
            ok = repl.evaluateLine(expr) && ok;
        }
        return ok ? 0 : 1;
    }

    return repl.run(std::cin);
}
